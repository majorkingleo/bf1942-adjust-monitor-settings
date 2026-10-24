"""Tests for bf1942_stats.py against the sample event log fixture."""

import os
import socket
import sqlite3
import sys
import tempfile
import threading
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import bf1942_stats as stats  # noqa: E402

FIXTURE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "sample_event_log.xml")


class ParserTests(unittest.TestCase):
    def setUp(self):
        self.parsed = stats.parse_log(stats.decompress_log(FIXTURE))

    def test_server_settings(self):
        server = self.parsed["server"]
        self.assertEqual(server["server name"], "MAJORLEO")
        self.assertEqual(server["port"], "14567")
        self.assertEqual(server["map"], "Berlin")

    def test_rounds_and_events(self):
        self.assertEqual(len(self.parsed["rounds"]), 1)
        events = self.parsed["rounds"][0]["events"]
        names = [e["name"] for e in events]
        self.assertEqual(names.count("GEStatsShots"), 2)
        self.assertEqual(names.count("GEStatsHits"), 1)
        self.assertEqual(names.count("GEStatsKills"), 1)
        self.assertEqual(names.count("scoreEvent"), 2)

    def test_roundstats(self):
        rs = self.parsed["rounds"][0]["roundstats"]
        self.assertEqual(rs["winning_team"], "1")
        self.assertEqual(rs["team_tickets"], {"1": 80, "2": 0})
        self.assertEqual(len(rs["players"]), 2)
        names = {p["stats"]["player_name"] for p in rs["players"]}
        self.assertEqual(names, {"MajorLeo", "Rotane"})


class ImportTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.db = os.path.join(self.tmp.name, "test.db")
        self.conn = stats.init_db(self.db)
        self.n_events, self.n_players = stats.import_file(self.conn, FIXTURE)

    def tearDown(self):
        self.conn.close()
        self.tmp.cleanup()

    def test_counts(self):
        self.assertEqual(self.n_events, 6)
        self.assertEqual(self.n_players, 2)
        self.assertEqual(self.conn.execute("SELECT COUNT(*) FROM players").fetchone()[0], 2)
        self.assertEqual(self.conn.execute("SELECT COUNT(*) FROM rounds").fetchone()[0], 1)
        self.assertEqual(self.conn.execute("SELECT COUNT(*) FROM events").fetchone()[0], 6)
        self.assertEqual(self.conn.execute("SELECT COUNT(*) FROM round_stats").fetchone()[0], 2)

    def test_round_stats_rows(self):
        row = self.conn.execute(
            "SELECT rs.kills, rs.deaths, rs.score, rs.team, p.name "
            "FROM round_stats rs JOIN players p ON p.id = rs.player_id "
            "WHERE p.name = 'MajorLeo'"
        ).fetchone()
        self.assertEqual(row, (5, 2, 25, 1, "MajorLeo"))

    def test_kill_event_resolution(self):
        row = self.conn.execute(
            "SELECT a.name, v.name, e.weapon "
            "FROM events e "
            "JOIN players a ON a.id = e.player_id "
            "JOIN players v ON v.id = e.victim_id "
            "WHERE e.name = 'scoreEvent' AND e.score_type = 'Kill'"
        ).fetchone()
        self.assertEqual(row, ("MajorLeo", "Rotane", "M1Garand"))

    def test_idempotent_reimport(self):
        n_events, n_players = stats.import_file(self.conn, FIXTURE)
        self.assertEqual((n_events, n_players), (0, 0))
        self.assertEqual(self.conn.execute("SELECT COUNT(*) FROM events").fetchone()[0], 6)

    def test_force_reimport_replaces(self):
        n_events, n_players = stats.import_file(self.conn, FIXTURE, force=True)
        self.assertEqual((n_events, n_players), (6, 2))
        self.assertEqual(self.conn.execute("SELECT COUNT(*) FROM events").fetchone()[0], 6)
        self.assertEqual(self.conn.execute("SELECT COUNT(*) FROM rounds").fetchone()[0], 1)
        self.assertEqual(self.conn.execute("SELECT COUNT(*) FROM players").fetchone()[0], 2)

    def test_query_stats_smoke(self):
        # Just ensure the query path runs without raising.
        stats.query_stats(self.conn, None, 10)
        stats.query_stats(self.conn, "MajorLeo", 10)
        stats.list_rounds(self.conn, 10)


class MockGameSpyServer:
    """Local UDP server that answers `\\status\\` with a canned response."""

    def __init__(self, response: bytes):
        self.response = response
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(("127.0.0.1", 0))
        self.port = self.sock.getsockname()[1]
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def _run(self):
        self.sock.settimeout(0.2)
        while not self._stop.is_set():
            try:
                data, addr = self.sock.recvfrom(4096)
            except socket.timeout:
                continue
            except OSError:
                break  # socket closed during teardown
            if data == b"\\status\\":
                try:
                    self.sock.sendto(self.response, addr)
                except OSError:
                    break

    def close(self):
        self._stop.set()
        self.sock.close()
        self._thread.join(timeout=1)


class QueryTests(unittest.TestCase):
    RESPONSE = (
        b"\\gamename\\battlefield1942\\gamever\\1.6\\hostname\\TestServer"
        b"\\mapname\\Berlin\\numplayers\\2\\maxplayers\\32"
        b"\\player_0\\MajorLeo\\score_0\\25\\kills_0\\5\\deaths_0\\2\\ping_0\\40\\team_0\\1"
        b"\\player_1\\Rotane\\score_1\\10\\kills_1\\1\\deaths_1\\5\\ping_1\\55\\team_1\\2"
        b"\\queryid\\1.1"
    )

    def test_gamespy_roundtrip(self):
        server = MockGameSpyServer(self.RESPONSE)
        try:
            data = stats.gamespy_status("127.0.0.1", server.port, timeout=2.0)
            self.assertIsNotNone(data)
            fields = stats.parse_gamespy_response(data)
            self.assertEqual(fields["hostname"], "TestServer")
            self.assertEqual(fields["mapname"], "Berlin")
            players = stats.extract_players(fields)
            self.assertEqual(len(players), 2)
            self.assertEqual(players[0]["name"], "MajorLeo")
            self.assertEqual(players[0]["kills"], 5)
            self.assertEqual(players[0]["deaths"], 2)
            self.assertEqual(players[1]["name"], "Rotane")
            self.assertEqual(players[1]["deaths"], 5)
        finally:
            server.close()

    def test_store_snapshot(self):
        with tempfile.TemporaryDirectory() as tmp:
            conn = stats.init_db(os.path.join(tmp, "q.db"))
            fields = stats.parse_gamespy_response(self.RESPONSE)
            players = stats.extract_players(fields)
            snap_id = stats.store_snapshot(conn, "10.0.0.7", 23000, fields, players)
            self.assertEqual(
                conn.execute("SELECT COUNT(*) FROM scoreboard_players").fetchone()[0], 2)
            self.assertEqual(
                conn.execute("SELECT COUNT(*) FROM scoreboard_snapshots").fetchone()[0], 1)
            row = conn.execute(
                "SELECT kills FROM scoreboard_players WHERE snapshot_id = ? AND name = 'MajorLeo'",
                (snap_id,),
            ).fetchone()
            self.assertEqual(row[0], 5)
            conn.close()


if __name__ == "__main__":
    unittest.main()
