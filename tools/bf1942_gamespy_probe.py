#!/usr/bin/env python3
"""Probe a Battlefield 1942 server's GameSpy/ASE query interface.

The script answers the practical question "what can I actually get out of this
server, and is there any vehicle data in it?" It

  * finds the query port (22000/23000/game port, or an explicit --scan range),
  * sends the documented ``\\status\\`` request plus other GameSpy verbs
    (``\\basic\\``, ``\\info\\``, ``\\rules\\``, ...) and queryid variants,
  * reassembles multi-datagram responses,
  * prints every key/value pair, grouped into server / teams / players,
  * highlights keys that are *not* in docs/gamespy-query.md (an unknown key is
    the only place vehicle data could hide),
  * searches keys and values for vehicle-ish terms (vehicle, pco, seat, kit,
    tank, jeep, ...), and
  * in ``--watch`` mode prints a diff for every poll so you can enter/exit a
    vehicle and see empirically whether *anything* in the status response moves.

It is deliberately dependency-free (stdlib only). Examples::

    # one shot, auto-detect the port, print everything
    ./bf1942_gamespy_probe.py 10.0.0.28

    # explicit port, raw packets, JSON result
    ./bf1942_gamespy_probe.py 10.0.0.28 --port 22000 --hex --json

    # watch for 2 minutes while entering a vehicle
    ./bf1942_gamespy_probe.py 10.0.0.28 --port 22000 --watch 120

    # save the "on foot" snapshot, later diff the "in vehicle" one against it
    ./bf1942_gamespy_probe.py 10.0.0.28 --save on_foot.json
    ./bf1942_gamespy_probe.py 10.0.0.28 --diff on_foot.json --watch 60

    # hunt for a non-default query port
    ./bf1942_gamespy_probe.py 10.0.0.28 --scan 20000-24000

Reference: docs/gamespy-query.md in this repository.
"""

from __future__ import annotations

import argparse
import json
import re
import selectors
import socket
import sys
import time
from dataclasses import dataclass, field
from typing import Iterable, Iterator

# ---------------------------------------------------------------------------
# Knowledge from docs/gamespy-query.md (used only for classification; the
# parser keeps *every* key it sees, known or not).
# ---------------------------------------------------------------------------

KNOWN_SERVER_KEYS = {
    "gamename", "gamever", "hostname", "hostport", "mapname", "gametype",
    "numplayers", "maxplayers", "tickets1", "tickets2", "roundTimeRemain",
    "status",
    # Server rule / configuration fields (also returned by \rules\).
    "active_mods", "allied_team_ratio", "allow_nose_cam",
    "auto_balance_teams", "axis_team_ratio", "bandwidth_choke_limit",
    "content_check", "cpu", "external_view", "free_camera",
    "game_start_delay", "hit_indicator", "kickback",
    "kickback_on_splash", "name_tag_distance", "name_tag_distance_scope",
    "number_of_rounds", "soldier_friendly_fire",
    "soldier_friendly_fire_on_splash", "spawn_delay", "spawn_wave_time",
    "sv_punkbuster", "ticket_ratio", "time_limit", "tk_mode",
    "unpure_mods", "vehicle_friendly_fire",
    "vehicle_friendly_fire_on_splash",
}
CONTROL_KEYS = {"final", "queryid"}

PLAYER_KEY_RE = re.compile(r"^(playername|score|kills|deaths|ping|team)_(\d+)$")
TEAMNAME_RE = re.compile(r"^teamname_(\d+)$")
INDEXED_KEY_RE = re.compile(r"^(.*)_(\d+)$")

# Keys that could carry vehicle / kit / seat state if such data ever appeared.
VEHICLE_KEY_RE = re.compile(
    r"(veh|pco|seat|kit|asset|occup|enter|exit|pawn|object|unit)", re.IGNORECASE
)
# Values that look like BF1942 vehicle names. This is a heuristic and can also
# match a player name, so hits are reported as "possible", never as proof.
VEHICLE_VALUE_RE = re.compile(
    r"\b(jeep|tank|plane|bomber|fighter|spitfire|zero|sherman|panzer|"
    r"kubelwagen|willys|apc|lcvp|higgins|barge|flak|halftrack|carrier|sub|"
    r"ship|boat|stuka|bf109|p51|il2|yak|mig|mosquito|ju88|b17|he111|"
    r"artillery|landing craft)\b",
    re.IGNORECASE,
)

DEFAULT_PORTS = [22000, 23000, 14567, 14568, 22001, 23001, 23002, 14570]
DEFAULT_VERBS = ["status", "basic", "info", "rules", "players"]
EXTRA_VERBS = ["getinfo", "getstatus", "echo", "list"]


# ---------------------------------------------------------------------------
# Query / transport
# ---------------------------------------------------------------------------


@dataclass
class ProbeResult:
    host: str
    port: int
    label: str
    request: bytes
    elapsed_ms: float
    parts: list[bytes] = field(default_factory=list)
    source: str | None = None

    @property
    def raw(self) -> bytes:
        return b"".join(self.parts)

    @property
    def responded(self) -> bool:
        return bool(self.parts)


def request_variants(verb: str) -> list[tuple[str, bytes]]:
    """Return (label, payload) variants for one GameSpy verb.

    The bare frame is the documented BF1942 form.  The queryid variants are
    accepted by many GameSpy 1 servers and make the response identifiable.
    """
    name = verb.encode("latin-1")
    return [
        ("plain", b"\\" + name + b"\\"),
        ("queryid", b"\\" + name + b"\\\\queryid\\1.1"),
        ("queryid+trailing", b"\\" + name + b"\\\\queryid\\1.1\\"),
    ]


def udp_query(host: str, port: int, payload: bytes, label: str,
              timeout: float = 2.0, max_bytes: int = 256 * 1024) -> ProbeResult:
    """Send one UDP query and reassemble the (possibly multi-datagram) answer."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(timeout)
    started = time.monotonic()
    result = ProbeResult(host=host, port=port, label=label, request=payload,
                         elapsed_ms=0.0)
    try:
        sock.sendto(payload, (host, port))
        # Allow several timeout windows for multi-packet answers, but do not
        # sit here forever if the server stays silent.
        deadline = started + max(timeout * 3.0, timeout + 1.0)
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                break
            sock.settimeout(min(timeout, remaining))
            try:
                data, addr = sock.recvfrom(65535)
            except socket.timeout:
                break
            except OSError:
                # ICMP port unreachable surfaces as ECONNREFUSED on Linux.
                break
            if not data:
                break
            if result.source is None:
                result.source = f"{addr[0]}:{addr[1]}"
            result.parts.append(data)
            joined = result.raw
            if len(joined) >= max_bytes:
                break
            if b"\\queryid\\" in joined:
                break
            if b"\\final\\" in joined:
                # Some servers split \final\ and \queryid\ into two datagrams.
                sock.settimeout(0.25)
                try:
                    more, addr = sock.recvfrom(65535)
                    if more:
                        result.parts.append(more)
                except (socket.timeout, OSError):
                    pass
                break
    finally:
        sock.close()
        result.elapsed_ms = (time.monotonic() - started) * 1000.0
    return result


def scan_ports(host: str, ports: Iterable[int], timeout: float = 0.6) -> dict[int, bytes]:
    """Send ``\\status\\`` to many ports at once; return the ones that answer."""
    sel = selectors.DefaultSelector()
    socks: dict[int, socket.socket] = {}
    first: dict[int, list[bytes]] = {}
    payload = b"\\status\\"
    for port in ports:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.setblocking(False)
        try:
            s.sendto(payload, (host, port))
        except OSError:
            s.close()
            continue
        sel.register(s, selectors.EVENT_READ, port)
        socks[port] = s

    deadline = time.monotonic() + timeout
    try:
        while socks and time.monotonic() < deadline:
            events = sel.select(max(0.0, deadline - time.monotonic()))
            for key, _mask in events:
                s: socket.socket = key.fileobj  # type: ignore[assignment]
                port: int = key.data  # type: ignore[assignment]
                try:
                    data, _addr = s.recvfrom(65535)
                except OSError:
                    data = b""
                if data:
                    first.setdefault(port, []).append(data)
                # Only the first datagram is needed for port discovery.
                try:
                    sel.unregister(s)
                except (KeyError, ValueError):
                    pass
                socks.pop(port, None)
                s.close()
    finally:
        for s in socks.values():
            try:
                sel.unregister(s)
            except (KeyError, ValueError):
                pass
            s.close()
        sel.close()
    return {p: b"".join(chunks) for p, chunks in first.items()}


# ---------------------------------------------------------------------------
# Parsing
# ---------------------------------------------------------------------------


@dataclass
class ParsedResponse:
    prefix: bytes
    pairs: list[tuple[str, str]]

    @property
    def fields(self) -> dict[str, str]:
        # Last duplicate wins, matching the C++ client's std::map behaviour.
        return dict(self.pairs)

    def get(self, key: str, default: str = "") -> str:
        return self.fields.get(key, default)

    def player_indices(self) -> list[int]:
        indices: set[int] = set()
        for key, _value in self.pairs:
            m = PLAYER_KEY_RE.match(key)
            if m:
                indices.add(int(m.group(2)))
        return sorted(indices)

    def player_rows(self) -> list[dict[str, str]]:
        rows = []
        for idx in self.player_indices():
            row: dict[str, str] = {"index": str(idx)}
            for base in ("playername", "score", "kills", "deaths", "ping", "team"):
                key = f"{base}_{idx}"
                if key in self.fields:
                    row[base] = self.fields[key]
            rows.append(row)
        return rows

    def teams(self) -> dict[str, str]:
        return {k: v for k, v in self.pairs if TEAMNAME_RE.match(k)}

    def classify(self, key: str) -> str:
        if key in CONTROL_KEYS:
            return "control"
        if key in KNOWN_SERVER_KEYS or TEAMNAME_RE.match(key):
            return "server"
        if PLAYER_KEY_RE.match(key):
            return "player"
        return "unknown"

    def unknown(self) -> list[tuple[str, str]]:
        return [(k, v) for k, v in self.pairs if self.classify(k) == "unknown"]


def parse_response(data: bytes) -> ParsedResponse:
    """Parse a ``\\key\\value\\key\\value`` stream, tolerating junk prefixes."""
    if not data:
        return ParsedResponse(b"", [])

    start = data.find(b"\\")
    if start < 0:
        return ParsedResponse(data, [])
    prefix = data[:start]
    body = data[start + 1:]

    # Split on single backslashes. Empty tokens are legal (e.g. \final\\queryid\).
    tokens = body.split(b"\\")

    def text(b: bytes) -> str:
        return b.decode("latin-1", "replace")

    pairs: list[tuple[str, str]] = []
    i = 0
    while i < len(tokens):
        key = text(tokens[i])
        if key == "" and i == len(tokens) - 1:
            break
        if i + 1 < len(tokens):
            value = text(tokens[i + 1])
        else:
            value = ""
        # A trailing empty token after the final value is normal; skip it
        # rather than emitting a phantom empty key.
        if key != "" or value != "":
            pairs.append((key, value))
        i += 2
    return ParsedResponse(prefix, pairs)


# ---------------------------------------------------------------------------
# Analysis / presentation
# ---------------------------------------------------------------------------


def vehicle_hits(parsed: ParsedResponse) -> list[str]:
    hits: list[str] = []
    for key, value in parsed.pairs:
        if VEHICLE_KEY_RE.search(key):
            hits.append(f"key {key!r} (vehicle-ish key name) = {value!r}")
        if value and VEHICLE_VALUE_RE.search(value):
            hits.append(f"value {value!r} on key {key!r} (vehicle-ish value)")
    return hits


def indexed_bases(pairs: list[tuple[str, str]]) -> dict[str, list[tuple[int, str]]]:
    """Group unknown keys by their ``<base>_<n>`` stem."""
    out: dict[str, list[tuple[int, str]]] = {}
    for key, value in pairs:
        m = INDEXED_KEY_RE.match(key)
        if m:
            out.setdefault(m.group(1), []).append((int(m.group(2)), value))
        else:
            out.setdefault(key, []).append((-1, value))
    return out


def fmt_kv(key: str, value: str, width: int = 20) -> str:
    return f"  {key:<{width}} = {value!r}"


def print_snapshot(parsed: ParsedResponse, result: ProbeResult, *,
                   show_hex: bool = False, show_all_pairs: bool = False) -> None:
    print()
    print("=" * 78)
    print(f"{result.host}:{result.port}  {result.label}  "
          f"(request {result.request!r}, {len(result.raw)} bytes, "
          f"{len(result.parts)} datagram(s), {result.elapsed_ms:.1f} ms)")
    if result.source:
        print(f"answered by {result.source}")
    if parsed.prefix:
        print(f"non-backslash prefix: {parsed.prefix!r}")

    if show_hex:
        for n, part in enumerate(result.parts):
            print(f"--- datagram {n} ({len(part)} bytes) ---")
            for off in range(0, len(part), 16):
                chunk = part[off:off + 16]
                hexpart = " ".join(f"{b:02x}" for b in chunk)
                asciipart = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
                print(f"  {off:04x}  {hexpart:<47}  {asciipart}")

    fields = parsed.fields
    server = [(k, v) for k, v in parsed.pairs
              if parsed.classify(k) == "server" and not TEAMNAME_RE.match(k)]
    teams = list(parsed.teams().items())
    control = [(k, v) for k, v in parsed.pairs if k in CONTROL_KEYS]

    if server:
        print("\n-- server --")
        for k, v in server:
            print(fmt_kv(k, v))

    if teams:
        print("\n-- teams --")
        for k, v in teams:
            print(fmt_kv(k, v))

    rows = parsed.player_rows()
    if rows:
        print(f"\n-- players ({len(rows)}) --")
        for row in rows:
            print("  [{index}] name={playername!r:<24} "
                  "score={score:<5} kills={kills:<4} deaths={deaths:<4} "
                  "ping={ping:<4} team={team}".format(
                      index=row["index"],
                      playername=row.get("playername", ""),
                      score=row.get("score", "?"),
                      kills=row.get("kills", "?"),
                      deaths=row.get("deaths", "?"),
                      ping=row.get("ping", "?"),
                      team=row.get("team", "?"),
                  ))

    unknown = parsed.unknown()
    print(f"\n-- unknown keys (not documented in gamespy-query.md): "
          f"{len(unknown)} --")
    if not unknown:
        print("  (none - the response contains only documented fields)")
    else:
        for k, v in unknown:
            print(fmt_kv(k, v, width=28))
        grouped = indexed_bases(unknown)
        for base, entries in grouped.items():
            if len(entries) > 1:
                values = ", ".join(f"_{i}={v!r}" if i >= 0 else repr(v)
                                   for i, v in sorted(entries))
                print(f"  grouped {base!r}: {values}")

    hits = vehicle_hits(parsed)
    print("\n-- vehicle-data probe --")
    if hits:
        for hit in hits:
            print(f"  POSSIBLE: {hit}")
        print("  Caveat: a player name or server name can contain these words; "
              "verify by re-running after entering a vehicle.")
    else:
        print("  no vehicle-related keys and no vehicle-like values found")
    print("  Note: BF1942's status query is documented to expose only server "
          "info and")
    print("        per-player score/kills/deaths/ping/team. Any vehicle state "
          "would have")
    print("        to appear above as an *unknown* key - watch the diff mode.")

    if control:
        print("\n-- response control --")
        for k, v in control:
            print(fmt_kv(k, v))

    if show_all_pairs:
        print(f"\n-- all {len(parsed.pairs)} key/value pairs in wire order --")
        for k, v in parsed.pairs:
            print(fmt_kv(k, v, width=28))


def snapshot_diff(before: dict[str, str], after: dict[str, str]) -> list[str]:
    lines: list[str] = []
    for key in sorted(set(after) - set(before)):
        lines.append(f"+ {key} = {after[key]!r}")
    for key in sorted(set(before) - set(after)):
        lines.append(f"- {key} (was {before[key]!r})")
    for key in sorted(set(before) & set(after)):
        if before[key] != after[key]:
            lines.append(f"~ {key}: {before[key]!r} -> {after[key]!r}")
    return lines


def result_to_json(parsed: ParsedResponse, result: ProbeResult) -> dict:
    return {
        "host": result.host,
        "port": result.port,
        "label": result.label,
        "request_hex": result.request.hex(),
        "elapsed_ms": round(result.elapsed_ms, 1),
        "source": result.source,
        "datagrams": len(result.parts),
        "raw_latin1": result.raw.decode("latin-1", "replace"),
        "fields": parsed.fields,
        "pairs": parsed.pairs,
        "players": parsed.player_rows(),
        "teams": parsed.teams(),
        "unknown": [{"key": k, "value": v} for k, v in parsed.unknown()],
        "vehicle_hits": vehicle_hits(parsed),
    }


# ---------------------------------------------------------------------------
# Modes
# ---------------------------------------------------------------------------


def parse_ports(scan: str | None) -> list[int]:
    if not scan:
        return []
    ports: list[int] = []
    for chunk in scan.split(","):
        chunk = chunk.strip()
        if not chunk:
            continue
        if "-" in chunk:
            lo_s, hi_s = chunk.split("-", 1)
            lo, hi = int(lo_s), int(hi_s)
            if lo > hi:
                lo, hi = hi, lo
            ports.extend(range(lo, hi + 1))
        else:
            ports.append(int(chunk))
    # Keep this a probe, not a port-scanner abuse tool.
    if len(ports) > 2000:
        raise SystemExit("refusing to scan more than 2000 ports; narrow --scan")
    return sorted(set(ports))


def pick_ports(host: str, explicit: int | None, scan_ports_arg: str | None,
               timeout: float) -> list[int]:
    if explicit is not None:
        return [explicit]

    candidates = list(dict.fromkeys(DEFAULT_PORTS + parse_ports(scan_ports_arg)))
    print(f"auto-detecting query port on {host} "
          f"({len(candidates)} candidate port(s): "
          f"{', '.join(str(p) for p in candidates)}) ...")
    answers = scan_ports(host, candidates, timeout=min(timeout, 0.6))
    if answers:
        print("responsive UDP port(s): " + ", ".join(str(p) for p in sorted(answers)))
        return sorted(answers)
    print("no response on the default ports. The query may be disabled, the "
          "server may use another port, or a firewall is dropping the packets.")
    print("Try: --port <port from 'GameSpy query port:' in the server console>")
    print("  or: --scan 20000-24000")
    return []


def discover_beacon(timeout: float = 5.0) -> None:
    """Listen passively for the BF1942 LAN beacon on UDP 54915."""
    print(f"listening for a BF1942 LAN beacon on UDP 54915 for {timeout:.0f}s ...")
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        sock.bind(("", 54915))
    except OSError as exc:
        print(f"cannot bind UDP 54915: {exc}")
        sock.close()
        return
    sock.settimeout(timeout)
    try:
        data, addr = sock.recvfrom(4096)
    except socket.timeout:
        print("no beacon seen (server may have LAN discovery disabled, or the "
              "broadcast does not reach this host)")
        sock.close()
        return
    finally:
        sock.close()
    name = ""
    nul = data.find(b"\x00", 1)
    if nul > 1:
        name = data[1:nul].decode("latin-1", "replace")
    guid = re.search(rb"\{[0-9a-fA-F-]{36}\}", data)
    print(f"beacon from {addr[0]}:{addr[1]}, {len(data)} bytes")
    print(f"  server name: {name!r}")
    print(f"  GUID: {guid.group(0).decode('ascii') if guid else '(not found)'}")
    print("  (the beacon carries no player, map, score or vehicle data)")


def watch(host: str, port: int, interval: float, duration: float,
          timeout: float, baseline: dict[str, str] | None,
          json_lines: bool) -> None:
    print(f"\nwatching {host}:{port} every {interval:g}s for {duration:g}s")
    if baseline is None:
        print("Enter/exit a vehicle now: every changed, added or removed key "
              "will be printed.")
    else:
        print(f"comparing against {len(baseline)} baseline key(s).")
    started = time.monotonic()
    previous = dict(baseline) if baseline is not None else None
    polls = 0
    changes_seen = 0
    first_unknown: set[str] = set()
    new_unknown_total: set[str] = set()
    first_poll = True
    try:
        while time.monotonic() - started < duration:
            result = udp_query(host, port, request_variants("status")[0][1],
                               "status (watch)", timeout=timeout)
            polls += 1
            stamp = time.strftime("%H:%M:%S")
            if not result.responded:
                print(f"[{stamp}] no response")
            else:
                parsed = parse_response(result.raw)
                current = parsed.fields
                unknown_now = {k for k, _ in parsed.unknown()}
                new_unknown = unknown_now - first_unknown
                first_unknown |= unknown_now
                if new_unknown and not first_poll:
                    new_unknown_total |= new_unknown
                    print(f"[{stamp}] NEW undocumented key(s): "
                          f"{', '.join(sorted(new_unknown))}")
                if previous is not None:
                    changes = snapshot_diff(previous, current)
                    if changes:
                        changes_seen += len(changes)
                        print(f"[{stamp}] {len(changes)} change(s), "
                              f"{len(current)} key(s):")
                        for line in changes:
                            print(f"    {line}")
                    else:
                        print(f"[{stamp}] no field changes "
                              f"({len(current)} key(s), "
                              f"{len(parsed.player_rows())} player row(s))")
                previous = current
                first_poll = False
                if json_lines:
                    # Keep the machine-readable watch stream off stdout so a
                    # preceding --json document stays valid.
                    print(json.dumps({
                        "ts": stamp,
                        "fields": current,
                        "changes": snapshot_diff(baseline or {}, current),
                    }), file=sys.stderr)
            time.sleep(max(0.0, interval))
    except KeyboardInterrupt:
        print("\nstopped by user")

    print(f"\nwatch finished: {polls} poll(s), {changes_seen} field change(s)")
    if not new_unknown_total:
        print("No previously unseen (undocumented) key appeared during the "
              "watch.")
        print("Consequence: entering/leaving a vehicle added no field to the "
              "status response.")
        print("BF1942 exposes no live vehicle state over GameSpy/ASE query.")


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Probe BF1942 GameSpy/ASE query data, including whether "
                    "any vehicle state is exposed.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="Example: %(prog)s 10.0.0.28 --watch 120",
    )
    parser.add_argument("host", help="server IP/hostname, e.g. 10.0.0.28")
    parser.add_argument("--port", type=int,
                        help="query port (skip auto-detection)")
    parser.add_argument("--timeout", type=float, default=2.0,
                        help="per-query UDP timeout in seconds (default 2.0)")
    parser.add_argument("--scan", metavar="PORTS",
                        help="extra discovery ports, e.g. 20000-24000 or "
                             "22000,23000,14567")
    parser.add_argument("--verbs", default=",".join(DEFAULT_VERBS),
                        help="comma-separated query verbs "
                             f"(default: {','.join(DEFAULT_VERBS)}; "
                             f"all: {','.join(DEFAULT_VERBS + EXTRA_VERBS)})")
    parser.add_argument("--all-variants", action="store_true",
                        help="also send queryid variants for every verb")
    parser.add_argument("--hex", action="store_true",
                        help="dump every response datagram as hex + ASCII")
    parser.add_argument("--all-pairs", action="store_true",
                        help="print all key/value pairs in wire order too")
    parser.add_argument("--json", action="store_true",
                        help="print the final result as JSON instead of text")
    parser.add_argument("--save", metavar="FILE",
                        help="save the status snapshot as JSON (baseline)")
    parser.add_argument("--diff", metavar="FILE",
                        help="compare against a snapshot saved with --save")
    parser.add_argument("--watch", nargs="?", type=float, const=60.0,
                        metavar="SECONDS",
                        help="poll status repeatedly and print field changes "
                             "(default 60 s)")
    parser.add_argument("--interval", type=float, default=2.0,
                        help="watch poll interval in seconds (default 2.0)")
    parser.add_argument("--discover", action="store_true",
                        help="also listen for the LAN beacon on UDP 54915")
    args = parser.parse_args(argv)

    if args.discover:
        discover_beacon()
        print()

    ports = pick_ports(args.host, args.port, args.scan, args.timeout)
    if not ports:
        return 1

    baseline: dict[str, str] | None = None
    if args.diff:
        try:
            with open(args.diff, "r", encoding="utf-8") as fh:
                saved = json.load(fh)
            baseline = saved.get("fields") or saved
            if not isinstance(baseline, dict):
                raise ValueError("snapshot has no 'fields' object")
        except (OSError, ValueError, json.JSONDecodeError) as exc:
            print(f"cannot read baseline {args.diff!r}: {exc}", file=sys.stderr)
            return 2

    main_parsed: ParsedResponse | None = None
    main_result: ProbeResult | None = None
    json_probes: list[dict] = []

    verbs = [v.strip() for v in args.verbs.split(",") if v.strip()]
    for port in ports:
        # Start with the documented status request; if a port does not answer
        # that, do not waste time on exotic verbs there.
        status = udp_query(args.host, port, request_variants("status")[0][1],
                           "status", timeout=args.timeout)
        if not status.responded:
            if len(ports) == 1:
                print(f"no response from {args.host}:{port} "
                      f"(query disabled, wrong port, or firewall?)")
            continue
        parsed = parse_response(status.raw)
        json_probes.append(result_to_json(parsed, status))
        if args.json:
            main_parsed, main_result = parsed, status
        else:
            print_snapshot(parsed, status, show_hex=args.hex,
                           show_all_pairs=args.all_pairs)

        # Probe the other verbs on the responsive port. Only print a section
        # when the server actually answers (many servers ignore them).
        for verb in verbs:
            if verb == "status":
                continue
            variants = request_variants(verb)
            if not args.all_variants:
                variants = variants[:1]
            for label, payload in variants:
                probe = udp_query(args.host, port, payload, f"\\{verb}\\ ({label})",
                                  timeout=min(args.timeout, 1.0))
                parsed_probe = parse_response(probe.raw)
                json_probes.append(result_to_json(parsed_probe, probe))
                if probe.responded:
                    if args.json:
                        # Collected in json_probes; keep stdout valid JSON.
                        pass
                    else:
                        print_snapshot(parsed_probe, probe,
                                       show_hex=args.hex,
                                       show_all_pairs=args.all_pairs)
                elif not args.json:
                    print(f"\n{args.host}:{port}  \\{verb}\\ ({label}): "
                          f"no response (verb unsupported or ignored)")

        if args.all_variants and not args.json:
            # Show what the queryid variants of status add, if anything.
            for label, payload in request_variants("status")[1:]:
                probe = udp_query(args.host, port, payload,
                                  f"\\status\\ ({label})", timeout=args.timeout)
                if probe.responded:
                    parsed_probe = parse_response(probe.raw)
                    json_probes.append(result_to_json(parsed_probe, probe))
                    print_snapshot(parsed_probe, probe, show_hex=args.hex,
                                   show_all_pairs=args.all_pairs)
                else:
                    print(f"\n{args.host}:{port}  \\status\\ ({label}): "
                          f"no response (BF1942 ignores queryid variants)")

        # Prefer the first responding port for watch/save.
        if main_parsed is None:
            main_parsed, main_result = parsed, status

    if args.json:
        payload = {
            "host": args.host,
            "ports": ports,
            "probes": json_probes,
        }
        if baseline is not None and main_parsed is not None:
            payload["baseline_diff"] = snapshot_diff(baseline,
                                                     main_parsed.fields)
        print(json.dumps(payload, ensure_ascii=False, indent=2))

    if main_parsed is None or main_result is None:
        print(f"\nNo BF1942 query response from {args.host} on "
              f"{', '.join(str(p) for p in ports)}.", file=sys.stderr)
        return 1

    if args.save:
        with open(args.save, "w", encoding="utf-8") as fh:
            json.dump(result_to_json(main_parsed, main_result), fh,
                      ensure_ascii=False, indent=2)
        print(f"\nsaved status snapshot to {args.save}")

    if baseline is not None and not args.json:
        changes = snapshot_diff(baseline, main_parsed.fields)
        print(f"\n-- diff against {args.diff} ({len(changes)} change(s)) --")
        if not changes:
            print("  no difference: no vehicle field appeared or changed")
        for line in changes:
            print(f"  {line}")

    if args.watch is not None:
        watch(args.host, main_result.port, args.interval, args.watch,
              args.timeout, baseline if baseline is not None
              else main_parsed.fields, json_lines=args.json)

    return 0


if __name__ == "__main__":
    sys.exit(main())
