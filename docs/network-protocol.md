# Battlefield 1942 — discovered network protocol

This documents everything recovered about how BF1942 (v1.612) talks to itself and
to the outside world. Sources: string extraction from `BF1942.exe` /
`BF1942_w32ded.exe` / `AdminTool\RemoteConsole.exe`, and a live 159 s packet
capture (`traffic.pcapng`) of a client (10.0.0.28) playing on a server
(10.0.0.7). Each fact is marked **verified** (seen in the binaries or the
capture) or **inferred** (reasonable reading, not yet proven).

---

## 1. Endpoints and ports

| Surface | Transport | Port | Direction | Verified? |
|---|---|---|---|---|
| Gameplay stream | UDP | **14567** | client ↔ server | verified (capture) |
| Join handshake | UDP | client ephemeral → 14567 | client → server | verified (capture) |
| LAN server beacon | UDP broadcast | **54915** → `255.255.255.255:54915` | server → LAN | verified (capture) |
| GameSpy server query | UDP | configurable (`game.gameSpyPort`), classic 23000 | client → server | verified (strings) |
| ASE server query | UDP | configurable (`game.ASEPort`) | client → server | verified (strings) |
| Remote console (RCON) | TCP | configurable (`-port`), server must enable it | client → server | verified (strings) |
| Server event log | file | `Mods\<mod>\Logs\ev_*.xml` / `.zxml` | server-side only | verified (strings) |

The game port **14567** also shows up inside the join packet as a big-endian
16-bit value.

---

## 2. LAN server beacon (decoded)

The server broadcasts a **263-byte** UDP datagram to `255.255.255.255:54915`
roughly **once per second**. This is what the in-game LAN server browser listens
to, and what `bf1942_discover` reads.

Layout:

```
offset 0      : 0x00
offset 1..n   : NUL-terminated server name        ("Mk-VII")
offset ..     : opaque binary (see below)
somewhere     : "{<GUID>}" server identifier      {50709c01-5d47-4dbc-8162-7ca9f2e8c5df}
```

The bulk of the payload is the game serialising raw C++ state, so it contains
**leaked 64-bit heap pointers** (e.g. `c0 2a 27 dc 6b 02 00 00`). Those bytes
change per process and per machine, and must not be interpreted as fields. The
only reliably readable data is the server name and the GUID.

**It carries no player, score or map data** — it is a discovery beacon only.

---

## 3. Gameplay stream (partially decoded)

The game runs over UDP 14567 as a custom **Refractor2** object-state + event
stream. It is binary and contains **no plaintext** (no player names, weapon
names or "kill/death" strings anywhere in the capture).

Observed traffic shape over 159 s of two-player gameplay:

| Direction | Dominant payload | Rate | Meaning (inferred) |
|---|---|---|---|
| client → server | 54 bytes (4496×) | ~28/s | input/ack packets |
| client → server | 13, 17 bytes (rare) | — | acks / small events |
| server → client | 97 bytes (2026×) | — | object-state snapshots |
| server → client | 18 bytes (194×) | ~1.2/s | event packets (score?) |
| server → client | 28 bytes (8×) | rare | event packets (tickets/score?) |
| server → client | 87–260 bytes | varied | larger state frames |

**Framing hint (inferred):** server payloads start with `0x00`, then a 4-byte
sequence/type word, then a length byte whose value is about `packet_size − 9`,
then the binary body. This has not been pinned down.

**Not yet decoded:** the per-shot (`GEStatsShots`), per-hit (`GEStatsHits`),
kill (`GEStatsKills`) and score (`GEScoreMsg`) events. They are delta-compressed
binary events without markers.

**Synchronised suicide capture** (`traffic2.pcapng`) gives a concrete lead. The
player logged in and killed themselves; the relevant traffic is:

* join: client → server **84-byte** at t≈23.0 s;
* suicide: client → server **21-byte** at t≈31.3 s
  (`02 c3 f2 ff 07 00 00 0c 00 02 81 20 b0 db 9a a2 88 08 d4 1e 05`) — the only
  21-byte client packet in the capture, so a likely command/event frame;
* death + respawn: server → client **649-byte**, **483-byte** and **109-byte**
  burst at t≈31.5–31.8 s.

The death is confirmed independently via the server query (player `deaths=1`,
`score=-2`). Diffing a *kill* capture against this *suicide* capture should
isolate the score-event packet.

---

## 4. Join handshake (observed)

A fresh connection (client port 64065 in the capture) looks like:

1. client → server : **84-byte** request; contains the game port `14567` as a
   big-endian u16 (`38 e7`) near the end.
2. server → client : **18-byte** acknowledgement(s).
3. client → server : **524-byte** level-load / state packets (again containing
   leaked heap pointers).
4. server → client : a **71-byte** packet, then the normal snapshot stream.

Player names were already known to the client before this capture began, so the
join here does not show how names are serialised.

---

## 5. GameSpy / ASE server query (decoded, live-verified)

The same UDP protocol the in-game server browser uses. `bf1942_query` implements
it.

* Request (UDP): `\status\` (8 bytes)
* Response: a backslash-delimited `\key\value\...` stream, possibly split over
  several datagrams, terminated by `\final\` + `\queryid\<n.m>`:

```
\gamename\bfield1942\gamever\v1.61\hostname\MK-VII\hostport\14567\
mapname\Bocage\numplayers\3\maxplayers\32\gametype\conquest\
tickets1\180\tickets2\180\roundTimeRemain\925\status\4\...
\playername_0\[TK]LieutenantORF\score_0\0\kills_0\0\deaths_0\0\ping_0\0\team_0\2\
\playername_1\[TK]der.General\...\
...
\teamname_0\axis\teamname_1\allied\
\final\\queryid\202.1
```

Verified live against the reference server: it answers on **UDP 22000** (the
All-Seeing Eye port — `game.ASEPort`), **not** the classic GameSpy 23000, so:

```powershell
bf1942_query.exe 10.0.0.7 --port 22000
```

Gotchas discovered from the real response:

* The player name field is **`playername_<n>`** — not `player_<n>`.
* `numplayers` can be **smaller** than the number of listed players (the
  reference server reported `numplayers\3` while listing 4 players), so scan
  player rows until the first missing `playername_<n>`.
* `score` can be **negative** (a suicide is −2) and `ping` is 0 until the
  server has measured it.

It exposes **live score, kills, deaths, ping and team per player** — but **not
shots fired**, and no history. The query only works while the server has it
enabled; it is on here (port 22000), but a server with `game.ASEPort 0` /
`game.gameSpyPort 0` will time out.

---

## 6. Remote console (RCON)

BF1942 ships an official TCP remote console: `AdminTool\RemoteConsole.exe`.

```
Usage RemoteConsole -m -u [username] -p [password] -port [port] [serverip]
```

The server side is the "remote console services" (`Couldn't initialize remote
console services` in the binary), gated by `admin.enableRemoteAdmin 1` plus a
password. Commands observed in the client binary: `ConsoleMessage`,
`ConsoleRun`, `game.setRemoteConsoleCurrentLine`; server-side console commands
include `statusMonitor`, `showPorts`, `showVersion`. The protocol exchanges
console text and can drive the server remotely, but its framing has not been
reversed here.

---

## 7. Server event log — the authoritative stats sink

The cleanest source of **shots, hits, kills and deaths** is the server's own
event log, written when `game.serverEventLogging 1` is set (works on the client
host too, because BF1942 always runs a local server internally). This is what
`stats_collector/bf1942_stats.py` parses.

```con
game.serverEventLogging 1
game.serverEventLogCompression 0   ; .xml files
```

File name: `Logs/ev_YYYYMMDD_HHMM.xml` (or `.zxml`, zlib-compressed) in the
active mod's `Logs\` folder.

### 7.1 XML structure

```xml
<?xml version="1.0" encoding="iso-8859-1"?>
<bf:log version="1.1" xmlns:bf="http://www.dice.se/xmlns/bf/1.1">
  <bf:server>
    <bf:setting name="server name">Mk-VII</bf:setting>
    <bf:setting name="port">14567</bf:setting>
    <bf:setting name="map">Bocage</bf:setting>
    ... <!-- dedicated, modid, mapid, game mode, gametime, scorelimit,
             maxplayers, norounds, spawn settings, ff settings, ... -->
  </bf:server>
  <bf:round timestamp="...">
    <bf:event name="..." timestamp="...">
      <bf:param type="int|string|vec3" name="...">value</bf:param>
      ...
    </bf:event>
    ...
    <bf:roundstats timestamp="...">
      <bf:teamtickets team="1">...</bf:teamtickets>
      <bf:teamtickets team="2">...</bf:teamtickets>
      <bf:winningteam>...</bf:winningteam>
      <bf:victorytype>...</bf:victorytype>
      <bf:playerstat playerid="...">
        <bf:statparam name="player_name">...</bf:statparam>
        <bf:statparam name="is_ai">0|1</bf:statparam>
        <bf:statparam name="team">...</bf:statparam>
        <bf:statparam name="score">...</bf:statparam>
        <bf:statparam name="kills">...</bf:statparam>
        <bf:statparam name="deaths">...</bf:statparam>
        <bf:statparam name="tks">...</bf:statparam>
        <bf:statparam name="captures">...</bf:statparam>
        <bf:statparam name="attacks">...</bf:statparam>
        <bf:statparam name="defences">...</bf:statparam>
        <bf:statparam name="objectives">...</bf:statparam>
        <bf:statparam name="objectivetks">...</bf:statparam>
      </bf:playerstat>
    </bf:roundstats>
  </bf:round>
</bf:log>
```

The `version` attribute does **not** select the payload layout; each `<bf:event>`
carries its own typed `<bf:param>` children.

### 7.2 Game event names (enum order in the binaries)

`GEPunkBusterFragment`, `GEChatFragment`, `GEAckEvent`, `GEChallengeResponse`,
`GEChallenge`, `GESetPlayerName`, `GERemoteCommand`, `GEHitFromPos`,
`GETriggerObjectSpawner`, `GERadioMessage`, `GESetLevel`, `GESetTeam`,
`GEDatabaseCompleteAck`, `GEDatabaseComplete`, `GESuicide`, `GESpawnRequest`,
`GEScoreMsg`, `GEStatsHits`, `GEStatsShots`, `GEStatsKills`,
`GEControlPointCaptured`, `GEGameStatus`, `GEHandlePickup`, `GEMap`, `GEVote`,
`GECreatePlayerAck`, `GEFlagHolder`, `GEServerInfoEvent2`, `GEServerInfoEvent`,
`GECheckSumEvent`, `GEConnectionType`, `GEUpdateRate`, `GEDestroyPlayer`,
`GESetTempCamera`, `GEExitVehicle`, `GESetVehicle`, `GESetDefaultVehicle`,
`GECreatePlayer`, `GECreateMultipleObjects`, `GECreateObject`, `GEDestroyObject`,
`GESimulation`, `GEDebugEvent`.

The stats-relevant ones: `GEStatsShots` (shot fired), `GEStatsHits` (shot hit),
`GEStatsKills` (kill), `GEScoreMsg` / `scoreEvent` (scoring), plus
`GESuicide`, `GESpawnRequest`, `GEControlPointCaptured`, `GESetTeam`.

### 7.3 Event parameter names

`player_id`, `victim_id`, `weapon`, `score_type`, `player_location` (vec3),
`tickets_team1`, `tickets_team2`, `roundInit`, `pickupKit`, `pickupFlag`,
`is_fake`, `vehicle`, `enterVehicle`, `exitVehicle`, `is_default`, `pco_id`,
`vehicle_pos`, `destroyVehicle`, `spawnEvent`, `reSpawnEvent`, `tagPlayerId`.

### 7.4 `score_type` values

`Kill`, `Death`, `DeathNoMsg`, `Spawned`, `Defence`, `Attack`, `FlagCapture`.

A kill event is `player_id` = killer, `victim_id` = victim, `weapon`, and
`score_type=Kill`; a death is the same feed seen from the victim's side
(`score_type=Death`/`DeathNoMsg`).

---

## 8. How the repo's tools map onto this

| Tool | Uses | Gives |
|---|---|---|
| `bf1942_discover` | §2 LAN beacon | server name + GUID, passively |
| `bf1942_query` | §5 GameSpy/ASE query | live kills/deaths/score/ping |
| `stats_collector/bf1942_stats.py` | §7 event log | shots/hits/kills/deaths history → SQLite |
| `stats_collector/analyze_traffic.py` | §3/§4 capture | flows, beacon, gameplay hex-dump/search |

## 9. Open questions

* Exact framing of the 14567 stream (sequence/length header).
* Where per-shot/hit/kill events sit in the server→client stream — needs a
  synchronised capture.
* RCON framing and default port.
* Whether the LAN beacon's binary fields ever carry useful, stable state (the
  leaked pointers suggest not).

Reference capture: `traffic.pcapng` (client 10.0.0.28 ↔ server 10.0.0.7,
159 s, two players).
