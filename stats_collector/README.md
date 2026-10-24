# BF1942 Stats Collector

A small, dependency-free tool that reads the stats Battlefield 1942 itself
records — kills, deaths, shots, hits, score, team, objectives — and stores
them in a SQLite database.

## TL;DR

```powershell
# 1. One-time setup (already done for this machine):
#    Mods\bf1942\Settings\ServerSettings.con  ->  game.serverEventLogging 1

# 2. Play BF1942 normally (in-game "Create Game", co-op, or dedicated server).
#    The game writes *.xml event logs into Mods\bf1942\Logs\.

# 3. Import them and query:
python stats_collector\bf1942_stats.py init   stats.db
python stats_collector\bf1942_stats.py import stats.db "C:\Games\bf_tk_mod\bf\Mods\bf1942\Logs"
python stats_collector\bf1942_stats.py query  stats.db --top 20
```

---

## How BF1942 actually records stats (the data source)

BF1942 has a built-in server-side event logging system. It is controlled by
two settings in `Mods\bf1942\Settings\ServerSettings.con`:

```con
game.serverEventLogging 1
game.serverEventLogCompression 0
```

When enabled, **every** game session writes an event log into the active
mod's `Logs\` folder:

* uncompressed XML files end in `.xml` (`serverEventLogCompression 0`), or
* zlib-compressed XML files end in `.zxml` (`serverEventLogCompression 1`).

Crucially, **you do not need the dedicated server** (`BF1942_w32ded.exe`).
The normal client `BF1942.exe` contains the exact same event-logging code,
because BF1942 always runs a local server internally — this covers
single-player/co-op and games hosted from the in-game *Multiplayer → Create
Game* menu. The `is_ai` flag in the log distinguishes bots from humans.

The event log is XML with the DICE namespace. This exact schema was
recovered from the strings inside `BF1942.exe` / `BF1942_w32ded.exe`:

```xml
<?xml version="1.0" encoding="iso-8859-1"?>
<bf:log version="1.1" xmlns:bf="http://www.dice.se/xmlns/bf/1.1">
  <bf:server>
    <bf:setting name="server name">MAJORLEO</bf:setting>
    <bf:setting name="map">Berlin</bf:setting>
    ...
  </bf:server>
  <bf:round timestamp="...">
    <bf:event name="GEStatsShots" timestamp="...">
      <bf:param type="int" name="player_id">0</bf:param>
      <bf:param type="string" name="weapon">M1Garand</bf:param>
    </bf:event>
    ...
    <bf:roundstats timestamp="...">
      <bf:teamtickets team="1">80</bf:teamtickets>
      <bf:winningteam>1</bf:winningteam>
      <bf:victorytype>tickets</bf:victorytype>
      <bf:playerstat playerid="0">
        <bf:statparam name="player_name">MajorLeo</bf:statparam>
        <bf:statparam name="kills">5</bf:statparam>
        <bf:statparam name="deaths">2</bf:statparam>
        <bf:statparam name="score">25</bf:statparam>
        ...
      </bf:playerstat>
    </bf:roundstats>
  </bf:round>
</bf:log>
```

### Event names seen in the binaries

| Event | Meaning |
|---|---|
| `GEStatsShots` | a shot fired (`player_id`, `weapon`) |
| `GEStatsHits` | a shot that hit (`player_id`, `weapon`) |
| `GEStatsKills` | a kill (`player_id`, `weapon`) |
| `scoreEvent` | scoring event; `score_type` is `Kill`, `Death`, `DeathNoMsg`, `Spawned`, `Defence`, `Attack`, `FlagCapture` |
| `GECreatePlayer` / `GEDestroyPlayer` / `GESetTeam` | player lifecycle |
| `GESuicide`, `GEControlPointCaptured`, `GEVote`, ... | other gameplay events |

Common event parameters: `player_id`, `victim_id`, `weapon`, `score_type`,
`player_location` (vec3), `tickets_team1` / `tickets_team2`.

### Per-round aggregates

At the end of every round the server writes a `<bf:roundstats>` block with,
per player: `player_name`, `is_ai`, `team`, `score`, `kills`, `deaths`,
`tks`, `captures`, `attacks`, `defences`, `objectives`, `objectivetks`.
These are the authoritative per-round totals.

---

## Implementation approach (the "suggestion")

The design is a small **event-ingestion pipeline** rather than scraping the
screen or reading game memory:

```
BF1942 (client or dedicated server)
      │  game.serverEventLogging 1
      ▼
XML event logs  (*.xml / *.zxml in Mods\<mod>\Logs\)
      │  bf1942_stats.py: parse + normalize
      ▼
SQLite  (players, rounds, events, round_stats)
      │  bf1942_stats.py query   (or any SQL client / dashboard)
      ▼
per-player / per-match / per-weapon analytics
```

Why this shape:

* **Event-driven, not polling.** Kills, deaths and shots are discrete events;
  ingesting them as they are logged loses nothing and recovers cleanly.
* **Source-agnostic.** The parser only cares about the XML schema, so the
  same code handles client-hosted, co-op and dedicated-server sessions.
* **Raw events + aggregates.** `events` preserves forensic detail (who shot
  whom with what, when); `round_stats` holds the server's own per-round
  totals so you can cross-check both.
* **Idempotent.** `imported_files` tracks what has been ingested, so `import`
  and `watch` can be re-run safely.

### Database schema

* `servers` — one row per log session (name, port, map, mode, all settings).
* `rounds` — one row per round (start/end timestamps, map, winning team,
  team tickets).
* `players` — one row per unique player name (plus `is_ai`).
* `events` — one row per logged event (name, timestamp, actor `player_id`,
  `victim_id`, `weapon`, `score_type`, full params in `params_json`).
* `round_stats` — per-player, per-round totals from `<bf:roundstats>`.
* `imported_files` — bookkeeping so re-imports are no-ops.

Timestamps are stored as the game wrote them (text) — BF1942 uses its own
format, and ordering is preserved by file/round order rather than by
re-parsing the timestamp.

---

## Usage

```powershell
cd E:\progs\bf1942-adjust-monitor-settings

# create the database
python stats_collector\bf1942_stats.py init stats.db

# one-shot import of a file or a whole Logs folder
python stats_collector\bf1942_stats.py import stats.db "C:\Games\bf_tk_mod\bf\Mods\bf1942\Logs"

# keep scanning for new logs while the game runs (Ctrl-C to stop)
python stats_collector\bf1942_stats.py watch stats.db "C:\Games\bf_tk_mod\bf\Mods\bf1942\Logs" --interval 5

# aggregate stats
python stats_collector\bf1942_stats.py query stats.db --top 20
python stats_collector\bf1942_stats.py query stats.db --player MajorLeo

# list rounds
python stats_collector\bf1942_stats.py query stats.db --rounds
```

Only the Python standard library is required (Python 3.10+).

### Live scoreboard (GameSpy / ASE query)

The event log gives full history, but it is server-side only. For a **live**
kill/death/score view from any machine, use the server's GameSpy/ASE query
protocol — the same UDP protocol the in-game server browser uses:

```powershell
# poll a server every 5 s and store scoreboard snapshots
python stats_collector\bf1942_stats.py poll stats.db 10.0.0.7 --port 22000 --interval 5

# print the latest snapshot
python stats_collector\bf1942_stats.py scoreboard stats.db
```

The query returns `hostname`, `mapname`, `numplayers`, `maxplayers` and, per
player, `score_<n>`, `kills_<n>`, `deaths_<n>`, `ping_<n>`, `team_<n>`. The
player name is `playername_<n>`, `numplayers` can be smaller than the listed
players, and `score` can be negative (a suicide is −2). It does **not** include
shots fired — that exists only in the server's event log.

A compiled, native alternative for the same query lives in the repo as
[`bf1942_query.exe`](../README.md#bf1942_query) (`src_bf1942_query/`, built by
the main `make`): it prints the live scoreboard (or CSV) without a database.

### Packet-capture analysis

[`analyze_traffic.py`](analyze_traffic.py) inspects a `.pcapng` capture without
external dependencies:

```powershell
python stats_collector\analyze_traffic.py traffic.pcapng
python stats_collector\analyze_traffic.py traffic.pcapng --dump-gameplay
python stats_collector\analyze_traffic.py traffic.pcapng --search-ascii Mk-VII
```

It lists the UDP flows, pulls the server name + GUID out of the LAN beacon
(`UDP 54915 -> 255.255.255.255`), and hex-dumps or searches the gameplay
payloads on port 14567. The gameplay stream is binary Refractor2 object state +
events with no plaintext, so this is a RE aid, not a stats source.

**The server must have querying enabled**, otherwise the UDP probe times out.
The reference server (MK-VII) answers on **UDP 22000** (the All-Seeing Eye
`game.ASEPort`); the classic GameSpy default is **23000**. Read the actual port
from the server console lines `GameSpy query port: ...` / `ASE query port: ...`
and pass it with `--port`.

### Example analytics SQL

```sql
-- kills per weapon
SELECT e.weapon, COUNT(*) AS kills
FROM events e
WHERE e.name = 'scoreEvent' AND e.score_type = 'Kill'
GROUP BY e.weapon ORDER BY kills DESC;

-- kill/death ratio
SELECT p.name, SUM(rs.kills) AS k, SUM(rs.deaths) AS d,
       ROUND(1.0 * SUM(rs.kills) / NULLIF(SUM(rs.deaths), 0), 2) AS kd
FROM players p JOIN round_stats rs ON rs.player_id = p.id
GROUP BY p.id ORDER BY kd DESC;

-- accuracy (hits / shots)
SELECT p.name, SUM(CASE WHEN e.name='GEStatsHits' THEN 1 ELSE 0 END) AS hits,
              SUM(CASE WHEN e.name='GEStatsShots' THEN 1 ELSE 0 END) AS shots
FROM players p JOIN events e ON e.player_id = p.id
GROUP BY p.id ORDER BY hits DESC;
```

---

## Getting stats going (what changed on this machine)

`C:\Games\bf_tk_mod\bf\Mods\bf1942\Settings\ServerSettings.con` now has

```con
game.serverEventLogging 1
```

(a backup `ServerSettings.con.bak-*` was left next to it). The setting only
takes effect for **newly started** sessions, so restart the game before
playing. After a round, a `.xml` file should appear in
`C:\Games\bf_tk_mod\bf\Mods\bf1942\Logs\` — import it and run `query`.

### Caveats

* **Joining a remote server records nothing on your machine.** Event
  logging is a *server* feature: the `ev_*.xml` file is written by the
  machine that hosts the game. If you connect as a client
  (`+joinServer <ip>` / in-game *Join Game*), the stats are only ever
  produced on the server. To capture them, enable
  `game.serverEventLogging 1` on the server (in the active mod's
  `Settings\ServerSettings.con`) and run this tool on the server against
  that mod's `Logs\` folder.
* `GEStatsShots` / `GEStatsHits` / `GEStatsKills` may be emitted per shot or
  as periodic counters depending on the game build; either way they are
  stored faithfully, and the per-round `kills`/`deaths` totals are
  authoritative. Verify with a real log from your own build.
* This tool lives outside the repo's autotools/mingw build on purpose — it
  is Python and has no C++ build dependencies. A native C++ port would need
  an XML parser and a SQLite binding; see the repo's `PLAN_rfa_tools.md` for
  how a new native tool would slot into `Makefile.am`.
