#!/usr/bin/env python3
"""Collect Battlefield 1942 gameplay stats into a SQLite database.

Reads the XML event logs that BF1942 writes when the server setting
``game.serverEventLogging 1`` is enabled (see README.md in this folder).
The format was reverse-engineered from the strings inside BF1942.exe /
BF1942_w32ded.exe and matches this structure::

    <?xml version="1.0" encoding="iso-8859-1"?>
    <bf:log version="1.1" xmlns:bf="http://www.dice.se/xmlns/bf/1.1">
      <bf:server>
        <bf:setting name="server name">MAJORLEO</bf:setting>
        ...
      </bf:server>
      <bf:round timestamp="...">
        <bf:event name="GEStatsShots" timestamp="...">
          <bf:param type="int" name="player_id">0</bf:param>
          <bf:param type="string" name="weapon">M1Garand</bf:param>
        </bf:event>
        ...
        <bf:roundstats timestamp="...">
          <bf:teamtickets team="1">...</bf:teamtickets>
          <bf:winningteam>...</bf:winningteam>
          <bf:victorytype>...</bf:victorytype>
          <bf:playerstat playerid="0">
            <bf:statparam name="player_name">...</bf:statparam>
            <bf:statparam name="kills">...</bf:statparam>
            ...
          </bf:playerstat>
        </bf:roundstats>
      </bf:round>
    </bf:log>

Requires only the Python standard library (xml.etree + sqlite3 + zlib).
"""

from __future__ import annotations

import argparse
import json
import os
import socket
import sqlite3
import sys
import time
import zlib
import xml.etree.ElementTree as ET

BF_NS = "http://www.dice.se/xmlns/bf/1.1"

SCHEMA = """
CREATE TABLE IF NOT EXISTS servers (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    port INTEGER,
    mod_id TEXT,
    map_id TEXT,
    map TEXT,
    game_mode TEXT,
    settings_json TEXT
);

CREATE TABLE IF NOT EXISTS rounds (
    id INTEGER PRIMARY KEY,
    server_id INTEGER NOT NULL REFERENCES servers(id),
    log_file TEXT NOT NULL,
    started_at TEXT,
    ended_at TEXT,
    map TEXT,
    winning_team TEXT,
    victory_type TEXT,
    team1_tickets INTEGER,
    team2_tickets INTEGER
);

CREATE TABLE IF NOT EXISTS players (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL UNIQUE,
    is_ai INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE IF NOT EXISTS events (
    id INTEGER PRIMARY KEY,
    round_id INTEGER NOT NULL REFERENCES rounds(id),
    ts TEXT,
    name TEXT NOT NULL,
    player_id INTEGER REFERENCES players(id),
    victim_id INTEGER REFERENCES players(id),
    weapon TEXT,
    score_type TEXT,
    params_json TEXT
);

CREATE TABLE IF NOT EXISTS round_stats (
    round_id INTEGER NOT NULL REFERENCES rounds(id),
    player_id INTEGER NOT NULL REFERENCES players(id),
    team INTEGER,
    score INTEGER,
    kills INTEGER,
    deaths INTEGER,
    tks INTEGER,
    captures INTEGER,
    attacks INTEGER,
    defences INTEGER,
    objectives INTEGER,
    objective_tks INTEGER,
    PRIMARY KEY (round_id, player_id)
);

CREATE TABLE IF NOT EXISTS imported_files (
    path TEXT PRIMARY KEY,
    imported_at TEXT NOT NULL,
    events INTEGER,
    players INTEGER,
    bytes INTEGER,
    server_id INTEGER REFERENCES servers(id)
);

CREATE TABLE IF NOT EXISTS scoreboard_snapshots (
    id INTEGER PRIMARY KEY,
    server_address TEXT NOT NULL,
    query_port INTEGER,
    ts TEXT NOT NULL,
    map_name TEXT,
    numplayers INTEGER,
    maxplayers INTEGER,
    raw_json TEXT
);

CREATE TABLE IF NOT EXISTS scoreboard_players (
    snapshot_id INTEGER NOT NULL REFERENCES scoreboard_snapshots(id),
    name TEXT NOT NULL,
    score INTEGER,
    kills INTEGER,
    deaths INTEGER,
    ping INTEGER,
    team INTEGER,
    PRIMARY KEY (snapshot_id, name)
);

CREATE INDEX IF NOT EXISTS idx_events_round ON events(round_id);
CREATE INDEX IF NOT EXISTS idx_events_name ON events(name);
CREATE INDEX IF NOT EXISTS idx_events_player ON events(player_id);
CREATE INDEX IF NOT EXISTS idx_round_stats_player ON round_stats(player_id);
"""


def _tag(local: str) -> str:
    return "{%s}%s" % (BF_NS, local)


def _localname(tag_name: str) -> str:
    return tag_name.rsplit("}", 1)[-1]


def _int_or_none(value):
    if value is None:
        return None
    try:
        return int(str(value).strip())
    except (TypeError, ValueError):
        return None


def _text(value):
    if value is None:
        return None
    value = str(value).strip()
    return value or None


def _is_true(value):
    return _text(value) in ("1", "true", "yes", "TRUE", "True")


def init_db(path: str) -> sqlite3.Connection:
    conn = sqlite3.connect(path)
    conn.executescript(SCHEMA)
    conn.commit()
    return conn


def open_db(path: str) -> sqlite3.Connection:
    conn = sqlite3.connect(path)
    conn.execute("PRAGMA foreign_keys = ON")
    conn.executescript(SCHEMA)  # idempotent: ensures schema exists on a fresh db
    conn.commit()
    return conn


def decompress_log(path: str) -> bytes:
    """Return the XML bytes for a .xml or .zxml event log file."""
    with open(path, "rb") as fh:
        raw = fh.read()
    if path.lower().endswith(".zxml"):
        # .zxml files are zlib-compressed (game.serverEventLogCompression 1).
        last_error = None
        for attempt in (
            lambda b: zlib.decompress(b),
            lambda b: zlib.decompress(b, zlib.MAX_WBITS | 32),  # auto gzip/zlib
        ):
            try:
                return attempt(raw)
            except zlib.error as exc:
                last_error = exc
        raise ValueError("could not decompress %s: %s" % (path, last_error))
    return raw


def _parse_params(elem) -> dict:
    params = {}
    for param in elem.findall(_tag("param")):
        name = param.get("name")
        if name is None:
            continue
        ptype = param.get("type", "string")
        value = (param.text or "").strip()
        if ptype == "int":
            value = _int_or_none(value)
        params[name] = value
    return params


def parse_log(data: bytes) -> dict:
    """Parse event-log XML into a plain dict, faithful to the source."""
    root = ET.fromstring(data)

    server = {}
    server_elem = root.find(_tag("server"))
    if server_elem is not None:
        for setting in server_elem.findall(_tag("setting")):
            server[setting.get("name")] = (setting.text or "").strip()

    rounds = []
    for round_elem in root.findall(_tag("round")):
        round_info = {
            "started_at": round_elem.get("timestamp"),
            "events": [],
            "roundstats": None,
        }
        for child in round_elem:
            local = _localname(child.tag)
            if local == "event":
                round_info["events"].append({
                    "name": child.get("name"),
                    "ts": child.get("timestamp"),
                    "params": _parse_params(child),
                })
            elif local == "roundstats":
                stats = {
                    "ended_at": child.get("timestamp"),
                    "team_tickets": {},
                    "winning_team": None,
                    "victory_type": None,
                    "players": [],
                }
                for sc in child:
                    sl = _localname(sc.tag)
                    if sl == "teamtickets":
                        stats["team_tickets"][sc.get("team")] = _int_or_none(sc.text)
                    elif sl == "winningteam":
                        stats["winning_team"] = _text(sc.text)
                    elif sl == "victorytype":
                        stats["victory_type"] = _text(sc.text)
                    elif sl == "playerstat":
                        player = {"playerid": sc.get("playerid"), "stats": {}}
                        for sp in sc.findall(_tag("statparam")):
                            player["stats"][sp.get("name")] = (sp.text or "").strip()
                        stats["players"].append(player)
                round_info["roundstats"] = stats
        rounds.append(round_info)

    return {"server": server, "rounds": rounds}


def _build_id_map(parsed: dict) -> dict:
    """Map a per-session numeric player id to (name, is_ai)."""
    id_map = {}
    for round_info in parsed["rounds"]:
        stats = round_info["roundstats"]
        if not stats:
            continue
        for player in stats["players"]:
            pid = _int_or_none(player.get("playerid"))
            if pid is None:
                continue
            stats_map = player["stats"]
            name = _text(stats_map.get("player_name")) or "unknown_%d" % pid
            id_map[pid] = (name, _is_true(stats_map.get("is_ai")))
    return id_map


def _get_player(conn: sqlite3.Connection, name: str, is_ai: bool) -> int:
    row = conn.execute("SELECT id, is_ai FROM players WHERE name = ?", (name,)).fetchone()
    if row is not None:
        pid, stored_ai = row
        if is_ai and not stored_ai:
            conn.execute("UPDATE players SET is_ai = 1 WHERE id = ?", (pid,))
        return pid
    cur = conn.execute(
        "INSERT INTO players(name, is_ai) VALUES (?, ?)", (name, 1 if is_ai else 0)
    )
    return cur.lastrowid


def _resolve_player(conn: sqlite3.Connection, raw_id, id_map: dict):
    pid = _int_or_none(raw_id)
    if pid is None:
        return None
    if pid in id_map:
        name, is_ai = id_map[pid]
        return _get_player(conn, name, is_ai)
    return _get_player(conn, "unknown_%d" % pid, False)


def import_log_bytes(conn: sqlite3.Connection, path: str, data: bytes) -> tuple[int, int, int]:
    parsed = parse_log(data)
    server = parsed["server"]
    cur = conn.execute(
        "INSERT INTO servers(name, port, mod_id, map_id, map, game_mode, settings_json) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)",
        (
            _text(server.get("server name")) or "unknown",
            _int_or_none(server.get("port")),
            _text(server.get("modid")),
            _text(server.get("mapid")),
            _text(server.get("map")),
            _text(server.get("game mode")),
            json.dumps(server, ensure_ascii=False),
        ),
    )
    server_id = cur.lastrowid
    id_map = _build_id_map(parsed)

    n_events = 0
    n_players = 0
    for round_info in parsed["rounds"]:
        cur = conn.execute(
            "INSERT INTO rounds(server_id, log_file, started_at, map) VALUES (?, ?, ?, ?)",
            (server_id, os.path.basename(path), round_info["started_at"], _text(server.get("map"))),
        )
        round_id = cur.lastrowid

        stats = round_info["roundstats"]
        if stats:
            conn.execute(
                "UPDATE rounds SET ended_at = ?, winning_team = ?, victory_type = ?, "
                "team1_tickets = ?, team2_tickets = ? WHERE id = ?",
                (
                    stats["ended_at"],
                    stats["winning_team"],
                    stats["victory_type"],
                    stats["team_tickets"].get("1"),
                    stats["team_tickets"].get("2"),
                    round_id,
                ),
            )
            for player in stats["players"]:
                sm = player["stats"]
                name = _text(sm.get("player_name")) or ("unknown_%s" % player.get("playerid"))
                player_row = _get_player(conn, name, _is_true(sm.get("is_ai")))
                conn.execute(
                    "INSERT OR REPLACE INTO round_stats"
                    "(round_id, player_id, team, score, kills, deaths, tks, captures, "
                    "attacks, defences, objectives, objective_tks) "
                    "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
                    (
                        round_id, player_row,
                        _int_or_none(sm.get("team")), _int_or_none(sm.get("score")),
                        _int_or_none(sm.get("kills")), _int_or_none(sm.get("deaths")),
                        _int_or_none(sm.get("tks")), _int_or_none(sm.get("captures")),
                        _int_or_none(sm.get("attacks")), _int_or_none(sm.get("defences")),
                        _int_or_none(sm.get("objectives")), _int_or_none(sm.get("objectivetks")),
                    ),
                )
                n_players += 1

        for event in round_info["events"]:
            params = event["params"]
            player_row = _resolve_player(conn, params.get("player_id"), id_map)
            victim_row = _resolve_player(conn, params.get("victim_id"), id_map)
            conn.execute(
                "INSERT INTO events(round_id, ts, name, player_id, victim_id, weapon, "
                "score_type, params_json) VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
                (
                    round_id, event["ts"], event["name"], player_row, victim_row,
                    _text(params.get("weapon")), _text(params.get("score_type")),
                    json.dumps(params, ensure_ascii=False, default=str),
                ),
            )
            n_events += 1

    return server_id, n_events, n_players


def find_logs(path: str) -> list[str]:
    """Return event-log files under *path* (.xml / .zxml), newest first."""
    if os.path.isfile(path):
        return [path]
    found = []
    for root, _dirs, files in os.walk(path):
        for name in files:
            if name.lower().endswith((".xml", ".zxml")):
                found.append(os.path.join(root, name))
    return sorted(found, key=lambda p: os.path.getmtime(p), reverse=True)


def _path_key(path: str) -> str:
    return os.path.normcase(os.path.abspath(path))


def _delete_import(conn: sqlite3.Connection, key: str) -> None:
    row = conn.execute(
        "SELECT server_id FROM imported_files WHERE path = ?", (key,)
    ).fetchone()
    if row is None or row[0] is None:
        return
    server_id = row[0]
    conn.execute(
        "DELETE FROM events WHERE round_id IN (SELECT id FROM rounds WHERE server_id = ?)",
        (server_id,),
    )
    conn.execute(
        "DELETE FROM round_stats WHERE round_id IN (SELECT id FROM rounds WHERE server_id = ?)",
        (server_id,),
    )
    conn.execute("DELETE FROM rounds WHERE server_id = ?", (server_id,))
    conn.execute("DELETE FROM servers WHERE id = ?", (server_id,))
    conn.execute("DELETE FROM imported_files WHERE path = ?", (key,))


def import_file(conn: sqlite3.Connection, path: str, force: bool = False) -> tuple[int, int]:
    key = _path_key(path)
    size = os.path.getsize(path)
    row = conn.execute(
        "SELECT bytes FROM imported_files WHERE path = ?", (key,)
    ).fetchone()
    if row is not None and row[0] == size and not force:
        return 0, 0  # already imported and unchanged
    if row is not None:
        _delete_import(conn, key)  # file changed or forced: replace previous import

    data = decompress_log(path)
    server_id, n_events, n_players = import_log_bytes(conn, path, data)
    conn.execute(
        "INSERT OR REPLACE INTO imported_files"
        "(path, imported_at, events, players, bytes, server_id) VALUES (?, ?, ?, ?, ?, ?)",
        (key, time.strftime("%Y-%m-%d %H:%M:%S"), n_events, n_players, size, server_id),
    )
    conn.commit()
    return n_events, n_players


def query_stats(conn: sqlite3.Connection, player: str | None, top: int) -> None:
    params: list = []
    where = ""
    if player:
        where = " WHERE p.name = ?"
        params.append(player)
    rows = conn.execute(
        "SELECT p.name, p.is_ai, "
        "COALESCE(SUM(rs.kills), 0), COALESCE(SUM(rs.deaths), 0), "
        "COALESCE(SUM(rs.score), 0), COALESCE(SUM(rs.tks), 0), "
        "COALESCE(SUM(rs.captures), 0), COUNT(rs.round_id) "
        "FROM players p LEFT JOIN round_stats rs ON rs.player_id = p.id "
        + where
        + " GROUP BY p.id ORDER BY 3 DESC, 5 DESC LIMIT ?",
        params + [top],
    ).fetchall()

    print("%-24s %-4s %6s %7s %7s %5s %8s %7s" % (
        "player", "AI", "kills", "deaths", "score", "tks", "capts", "rounds"))
    print("-" * 78)
    for name, is_ai, kills, deaths, score, tks, captures, rounds in rows:
        print("%-24s %-4s %6d %7d %7d %5d %8d %7d" % (
            name, "yes" if is_ai else "no", kills, deaths, score, tks, captures, rounds))

    # Event-derived aggregates: shots / hits exist only in the raw event feed.
    ev_sql = (
        "SELECT p.name, "
        "SUM(CASE WHEN e.name='GEStatsShots' THEN 1 ELSE 0 END), "
        "SUM(CASE WHEN e.name='GEStatsHits' THEN 1 ELSE 0 END), "
        "SUM(CASE WHEN e.name='GEStatsKills' THEN 1 ELSE 0 END), "
        "SUM(CASE WHEN e.name='scoreEvent' AND e.score_type='Kill' THEN 1 ELSE 0 END) "
        "FROM players p LEFT JOIN events e ON e.player_id = p.id "
        + where
        + " GROUP BY p.id ORDER BY 3 DESC LIMIT ?"
    )
    ev_rows = conn.execute(ev_sql, params + [top]).fetchall()
    death_sql = (
        "SELECT p.name, SUM(CASE WHEN e.name='scoreEvent' AND e.score_type IN "
        "('Death','DeathNoMsg') THEN 1 ELSE 0 END) "
        "FROM players p LEFT JOIN events e ON e.victim_id = p.id "
        + where
        + " GROUP BY p.id ORDER BY 2 DESC LIMIT ?"
    )
    death_rows = dict(conn.execute(death_sql, params + [top]).fetchall())

    print()
    print("%-24s %7s %6s %7s %7s" % ("player", "shots", "hits", "kills(evt)", "deaths(feed)"))
    print("-" * 60)
    for name, shots, hits, kills_evt, kills_feed in ev_rows:
        print("%-24s %7d %6d %7d %7d" % (
            name, shots or 0, hits or 0, kills_evt or 0, death_rows.get(name, 0)))
    print()
    print("Round totals above come from the server's per-round stats; the second table "
          "is derived from the raw event feed (deaths via `victim_id`).")


def list_rounds(conn: sqlite3.Connection, top: int) -> None:
    rows = conn.execute(
        "SELECT r.id, r.log_file, r.started_at, r.ended_at, r.map, r.winning_team, "
        "COUNT(e.id) FROM rounds r LEFT JOIN events e ON e.round_id = r.id "
        "GROUP BY r.id ORDER BY r.id DESC LIMIT ?",
        (top,),
    ).fetchall()
    print("%-6s %-34s %-22s %-22s %-20s %-12s %8s" % (
        "id", "file", "start", "end", "map", "winner", "events"))
    print("-" * 130)
    for row in rows:
        print("%-6s %-34s %-22s %-22s %-20s %-12s %8d" % (
            row[0], row[1], row[2] or "", row[3] or "", row[4] or "", row[5] or "", row[6]))


def watch(conn: sqlite3.Connection, logs_dir: str, interval: int) -> None:
    print("Watching %s for *.xml / *.zxml (Ctrl-C to stop)..." % logs_dir)
    try:
        while True:
            for path in find_logs(logs_dir):
                try:
                    n_events, n_players = import_file(conn, path)
                    if n_events or n_players:
                        print("[%s] imported %s: %d events, %d player rows" % (
                            time.strftime("%H:%M:%S"), os.path.basename(path), n_events, n_players))
                except Exception as exc:  # noqa: BLE001 - keep watching on bad files
                    print("[%s] ERROR %s: %s" % (
                        time.strftime("%H:%M:%S"), path, exc), file=sys.stderr)
            time.sleep(interval)
    except KeyboardInterrupt:
        print("\nStopped.")


# --- GameSpy / ASE server query -------------------------------------------------
# The classic UDP "status" query used by the in-game server browser. It returns
# a `\key\value\...` stream with server info and per-player score/kills/deaths/
# ping. It needs to be enabled on the server (see README.md); BF1942 defaults
# to game port 14567 + a GameSpy query port, usually UDP 23000.

GAMESPY_REQUEST = b"\\status\\"


def gamespy_status(host: str, port: int = 23000, timeout: float = 3.0,
                   max_bytes: int = 65536) -> bytes | None:
    """Send a GameSpy status query and return the raw response bytes."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(timeout)
    parts: list[bytes] = []
    total = 0
    try:
        sock.sendto(GAMESPY_REQUEST, (host, port))
        while True:
            try:
                data, _addr = sock.recvfrom(4096)
            except socket.timeout:
                break
            parts.append(data)
            total += len(data)
            if b"\\queryid\\" in data or b"\\final\\" in data:
                break
            if total >= max_bytes:
                break
    finally:
        sock.close()
    return b"".join(parts) if parts else None


def parse_gamespy_response(data: bytes) -> dict:
    """Parse a GameSpy key/value (backslash-delimited) response into a dict."""
    text = data.decode("latin-1", "replace")
    if text.startswith("\\"):
        body = text[1:]
    else:
        idx = text.find("\\")
        body = text[idx + 1:] if idx != -1 else ""
    fields: dict[str, str] = {}
    tokens = body.split("\\")
    i = 0
    while i + 1 < len(tokens):
        fields[tokens[i]] = tokens[i + 1]
        i += 2
    return fields


def extract_players(fields: dict) -> list[dict]:
    """Pull the per-player rows out of a parsed status response."""
    num = _int_or_none(fields.get("numplayers")) or 0
    players = []
    for i in range(num):
        name = fields.get("player_%d" % i)
        if name is None:
            continue
        players.append({
            "name": name,
            "score": _int_or_none(fields.get("score_%d" % i)),
            "kills": _int_or_none(fields.get("kills_%d" % i)),
            "deaths": _int_or_none(fields.get("deaths_%d" % i)),
            "ping": _int_or_none(fields.get("ping_%d" % i)),
            "team": _int_or_none(fields.get("team_%d" % i)),
        })
    return players


def store_snapshot(conn: sqlite3.Connection, host: str, port: int,
                   fields: dict, players: list[dict]) -> int:
    ts = time.strftime("%Y-%m-%d %H:%M:%S")
    cur = conn.execute(
        "INSERT INTO scoreboard_snapshots"
        "(server_address, query_port, ts, map_name, numplayers, maxplayers, raw_json) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)",
        (
            host, port, ts, _text(fields.get("mapname")),
            _int_or_none(fields.get("numplayers")), _int_or_none(fields.get("maxplayers")),
            json.dumps(fields, ensure_ascii=False),
        ),
    )
    snapshot_id = cur.lastrowid
    for p in players:
        conn.execute(
            "INSERT INTO scoreboard_players"
            "(snapshot_id, name, score, kills, deaths, ping, team) "
            "VALUES (?, ?, ?, ?, ?, ?, ?)",
            (snapshot_id, p["name"], p["score"], p["kills"], p["deaths"], p["ping"], p["team"]),
        )
    conn.commit()
    return snapshot_id


def poll(conn: sqlite3.Connection, host: str, port: int, interval: int) -> None:
    print("Polling %s:%d every %ds (Ctrl-C to stop)..." % (host, port, interval))
    try:
        while True:
            data = gamespy_status(host, port)
            if data is None:
                print("[%s] no response from %s:%d "
                      "(query disabled or wrong port?)" % (
                          time.strftime("%H:%M:%S"), host, port))
            else:
                fields = parse_gamespy_response(data)
                players = extract_players(fields)
                snapshot_id = store_snapshot(conn, host, port, fields, players)
                print("[%s] %s | %s | %d/%d players | snapshot %d" % (
                    time.strftime("%H:%M:%S"),
                    fields.get("hostname", host),
                    fields.get("mapname", "?"),
                    len(players),
                    _int_or_none(fields.get("maxplayers")) or 0,
                    snapshot_id))
            time.sleep(interval)
    except KeyboardInterrupt:
        print("\nStopped.")


def show_scoreboard(conn: sqlite3.Connection) -> None:
    row = conn.execute(
        "SELECT id, ts, server_address, query_port, map_name, numplayers, maxplayers "
        "FROM scoreboard_snapshots ORDER BY id DESC LIMIT 1"
    ).fetchone()
    if row is None:
        print("No scoreboard snapshots yet.")
        return
    snap_id, ts, addr, port, map_name, num, maxn = row
    print("%s  %s:%s  map=%s  %s/%s players" % (
        ts, addr, port, map_name or "?", num or 0, maxn or 0))
    rows = conn.execute(
        "SELECT name, score, kills, deaths, ping, team FROM scoreboard_players "
        "WHERE snapshot_id = ? ORDER BY score DESC, kills DESC",
        (snap_id,),
    ).fetchall()
    print("%-24s %7s %6s %7s %6s %5s" % ("player", "score", "kills", "deaths", "ping", "team"))
    print("-" * 62)
    for name, score, kills, deaths, ping, team in rows:
        print("%-24s %7s %6s %7s %6s %5s" % (
            name,
            score if score is not None else "-",
            kills if kills is not None else "-",
            deaths if deaths is not None else "-",
            ping if ping is not None else "-",
            team if team is not None else "-"))


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="bf1942_stats.py",
        description="Collect BF1942 gameplay stats into SQLite.",
    )
    sub = parser.add_subparsers(dest="command", required=True)

    p_init = sub.add_parser("init", help="create an empty database")
    p_init.add_argument("db")

    p_import = sub.add_parser("import", help="import one event-log file or a directory of them")
    p_import.add_argument("db")
    p_import.add_argument("path")
    p_import.add_argument("--force", action="store_true", help="re-import already seen files")

    p_watch = sub.add_parser("watch", help="keep scanning a directory for new logs")
    p_watch.add_argument("db")
    p_watch.add_argument("logs_dir")
    p_watch.add_argument("--interval", type=int, default=5)

    p_query = sub.add_parser("query", help="print aggregated player stats")
    p_query.add_argument("db")
    p_query.add_argument("--player")
    p_query.add_argument("--top", type=int, default=50)
    p_query.add_argument("--rounds", action="store_true", help="list rounds instead")

    p_poll = sub.add_parser("poll", help="poll a server's GameSpy/ASE query for live scoreboard")
    p_poll.add_argument("db")
    p_poll.add_argument("host")
    p_poll.add_argument("--port", type=int, default=23000,
                        help="GameSpy query port (check server console; often UDP 23000)")
    p_poll.add_argument("--interval", type=int, default=5)

    p_score = sub.add_parser("scoreboard", help="print the latest scoreboard snapshot")
    p_score.add_argument("db")

    args = parser.parse_args(argv)

    if args.command == "init":
        init_db(args.db)
        print("Created %s" % args.db)
        return 0

    conn = open_db(args.db)

    if args.command == "import":
        total_events = total_players = 0
        for path in find_logs(args.path):
            n_events, n_players = import_file(conn, path, args.force)
            if n_events or n_players:
                print("imported %s: %d events, %d player rows" % (path, n_events, n_players))
            total_events += n_events
            total_players += n_players
        print("done: %d events, %d player rows" % (total_events, total_players))
        conn.close()
        return 0

    if args.command == "watch":
        watch(conn, args.logs_dir, args.interval)
        conn.close()
        return 0

    if args.command == "query":
        if args.rounds:
            list_rounds(conn, args.top)
        else:
            query_stats(conn, args.player, args.top)
        conn.close()
        return 0

    if args.command == "poll":
        poll(conn, args.host, args.port, args.interval)
        conn.close()
        return 0

    if args.command == "scoreboard":
        show_scoreboard(conn)
        conn.close()
        return 0

    return 1


if __name__ == "__main__":
    raise SystemExit(main())
