#!/usr/bin/env python3
"""Analyse a Battlefield 1942 packet capture (.pcapng) without external deps.

Helps reverse-engineer the network protocol: lists the UDP/TCP flows, extracts
what the server's LAN broadcast reveals (server name + GUID), and can dump or
search the gameplay payloads on the game port.

Usage:
    python analyze_traffic.py <capture.pcapng>
    python analyze_traffic.py <capture.pcapng> --dump-gameplay [--port 14567]
    python analyze_traffic.py <capture.pcapng> --search-ascii Mk-VII

Requires only the Python standard library.
"""

from __future__ import annotations

import argparse
import collections
import re
import struct
import sys

# pcapng block types
BLOCK_SECTION = 0x0A0D0D0A
BLOCK_INTERFACE = 0x00000001
BLOCK_ENHANCED_PACKET = 0x00000006


def read_packets(path: str):
    """Yield (timestamp_micros, raw_ip_bytes) from a .pcapng file."""
    data = open(path, "rb").read()
    i = 0
    while i + 12 <= len(data):
        btype, blen = struct.unpack_from("<II", data, i)
        if blen < 12 or i + blen > len(data):
            break
        body = data[i + 8:i + blen - 4]
        if btype == BLOCK_ENHANCED_PACKET and len(body) >= 20:
            _iface, tsh, tsl, caplen, _origlen = struct.unpack_from("<IIIII", body, 0)
            yield tsh * (2 ** 32) + tsl, body[20:20 + caplen]
        i += blen


def udp(packet: bytes):
    """Return (src, sport, dst, dport, payload) for a UDP/IPv4-over-Ethernet packet."""
    if len(packet) < 34 or packet[12:14] != b"\x08\x00":
        return None
    off = 14
    ihl = (packet[off] & 0x0F) * 4
    if packet[off + 9] != 17 or len(packet) < off + ihl + 8:
        return None
    src = ".".join(map(str, packet[off + 12:off + 16]))
    dst = ".".join(map(str, packet[off + 16:off + 20]))
    sport, dport = struct.unpack_from(">HH", packet, off + ihl)
    return src, sport, dst, dport, packet[off + ihl + 8:]


def summarize(path: str) -> None:
    flows = collections.Counter()
    first = {}
    last = {}
    beacons = set()
    for ts, pkt in read_packets(path):
        u = udp(pkt)
        if not u:
            continue
        src, sport, dst, dport, payload = u
        key = (src, sport, dst, dport)
        flows[key] += 1
        first.setdefault(key, ts)
        last[key] = ts
        if dport == 54915 and dst.startswith("255."):
            beacons.add(bytes(payload))

    print("=== UDP flows (src:port -> dst:port) ===")
    for (src, sport, dst, dport), n in sorted(flows.items(), key=lambda kv: -kv[1]):
        dur = (last[(src, sport, dst, dport)] - first[(src, sport, dst, dport)]) / 1e6
        print(f"  {src}:{sport:<6} -> {dst}:{dport:<6}  pkts={n:<6} over {dur:.1f}s")

    print("\n=== BF1942 LAN server beacons (UDP 54915 -> 255.255.255.255) ===")
    print(f"  distinct beacon payloads: {len(beacons)}")
    for payload in list(beacons)[:1]:
        strings = [m.group().decode("latin-1") for m in re.finditer(rb"[ -~]{4,}", payload)]
        print(f"  readable strings: {strings}")
        guid = re.search(rb"\{[0-9a-fA-F-]{8,}\}", payload)
        if guid:
            print(f"  server GUID: {guid.group().decode()}")


def dump_gameplay(path: str, port: int) -> None:
    samples = collections.defaultdict(list)
    for ts, pkt in read_packets(path):
        u = udp(pkt)
        if u and u[1] == port:  # src port == game port -> server -> client
            samples[len(u[4])].append((ts, u[4]))
    for size in sorted(samples, key=lambda s: -len(samples[s]))[:8]:
        lst = samples[size]
        print(f"=== server->client size {size} (count {len(lst)}) ===")
        for ts, payload in lst[:2]:
            print(f"  [{ts/1e6:.2f}s] {payload.hex()}")


def search(path: str, port: int, text: bytes) -> None:
    for ts, pkt in read_packets(path):
        u = udp(pkt)
        if not u or (u[1] != port and u[3] != port):
            continue
        if text in u[4]:
            print(f"[{ts/1e6:.2f}s] {u[0]}:{u[1]} -> {u[2]}:{u[3]} "
                  f"len={len(u[4])} match={text!r}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="analyze_traffic.py")
    parser.add_argument("capture")
    parser.add_argument("--dump-gameplay", action="store_true")
    parser.add_argument("--port", type=int, default=14567)
    parser.add_argument("--search-ascii")
    args = parser.parse_args(argv)

    try:
        if args.dump_gameplay:
            dump_gameplay(args.capture, args.port)
        elif args.search_ascii:
            search(args.capture, args.port, args.search_ascii.encode("latin-1"))
        else:
            summarize(args.capture)
    except FileNotFoundError:
        print(f"error: no such file {args.capture}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
