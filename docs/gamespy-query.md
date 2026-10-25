# Battlefield 1942 GameSpy / ASE query data

Battlefield 1942 exposes a UDP status query used by the in-game server browser.
The same interface is commonly called the GameSpy or ASE query interface. A
client sends the 8-byte request `\status\` to the server's query port. The
response is a backslash-delimited sequence of key/value pairs and may arrive in
multiple datagrams. The final response can include `\final\` and
`\queryid\<n.m>` markers.

Querying must be enabled by the server. The query port is configurable: the
classic GameSpy port is UDP 23000, but a server may use another port. For
example, the reference server documented in [network-protocol.md](network-protocol.md)
answers on UDP 22000 (its ASE port). Use the port reported by the server rather
than assuming the default.

## Data returned

The following fields have been observed in BF1942 query responses. A server
may omit fields, and additional keys may be present; consumers should preserve
or ignore unrecognized fields rather than assuming this list is exhaustive.

### Server and match fields

| Key | Meaning | Notes |
|---|---|---|
| `gamename` | Game identifier | Example: `battlefield1942` |
| `gamever` | Game version | Example: `1.6` |
| `hostname` | Server's displayed name | |
| `hostport` | Game connection port | Not necessarily the query port |
| `mapname` | Current map | |
| `gametype` | Current game mode | Example: `conquest` |
| `numplayers` | Reported player count | Can be smaller than the number of player rows actually returned |
| `maxplayers` | Player capacity | |
| `tickets1`, `tickets2` | Current tickets for the two sides | Observed in a live response; field availability can depend on server/mode |
| `roundTimeRemain` | Remaining round time | Observed in a live response |
| `status` | Server status value | Observed in a live response; its numeric meaning is not established here |
| `teamname_<n>` | Team name for indexed team `<n>` | Example: `teamname_0=axis`, `teamname_1=allied` |

### Per-player fields

Player data is indexed from zero. The player-name key is `playername_<n>`:

| Key pattern | Meaning | Notes |
|---|---|---|
| `playername_<n>` | Player name | Its presence is the reliable signal that a player row exists |
| `score_<n>` | Current score | Can be negative (for example, a suicide was reported as `-2`) |
| `kills_<n>` | Current kill count | |
| `deaths_<n>` | Current death count | |
| `ping_<n>` | Ping reported by the server | Can be `0` before the server has measured it |
| `team_<n>` | Player's team index | Numeric; interpret with the corresponding `teamname_<n>` fields when supplied |

For example, `playername_0`, `score_0`, `kills_0`, `deaths_0`, `ping_0`, and
`team_0` describe the same player. Do not stop after `numplayers` rows: BF1942
has been observed reporting fewer players than it lists. Scan indexed rows
until the first missing `playername_<n>`.

### Response-control fields

| Key | Meaning |
|---|---|
| `final` | Marks the end of the response when present |
| `queryid` | Query/response identifier, commonly formatted as `<n.m>` |

These are protocol markers, not match statistics.

## What this interface does not provide

The status query is a live snapshot, not a historical record or event feed. It
does not report shots fired, hits, weapon-level activity, or a timeline of
score changes. The repository's server event-log collector is the separate
source for those historical and combat-event details; see the
[stats collector documentation](../stats_collector/README.md).

The query also should not be confused with the LAN discovery beacon or the
gameplay stream. The beacon provides server discovery information, while
gameplay statistics are not exposed as readable text in the binary gameplay
traffic. See [network-protocol.md](network-protocol.md) for those protocol
surfaces.

## Example

An abbreviated response, with line breaks added for readability:

```text
\gamename\battlefield1942\gamever\v1.61\hostname\MK-VII\hostport\14567\
mapname\Bocage\numplayers\3\maxplayers\32\gametype\conquest\
tickets1\180\tickets2\180\roundTimeRemain\925\status\4\
playername_0\[TK]LieutenantORF\score_0\0\kills_0\0\deaths_0\0\ping_0\0\team_0\2\
teamname_0\axis\teamname_1\allied\
\final\\queryid\202.1
```

The exact keys and values depend on the server and current match. In the
example, the game port is `14567`; the query itself must be sent to the
configured query port, which is a separate port.

## Repository client

[`bf1942_query`](../README.md#bf1942_query) and the Python
[stats collector](../stats_collector/README.md) both send `\status\` and parse
the key/value response. They display or store the live server summary and
indexed scoreboard fields described above.
