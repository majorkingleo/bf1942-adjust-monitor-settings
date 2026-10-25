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

### Server rule fields

BF1942 also exposes its server configuration through the same key/value
interface. These keys are returned by `\status\` as well as by `\rules\`.
They are **configuration values, not live match state**, and their values are
sent as strings, including any unit suffix (`10s`, `90%`). The concrete values
below are examples from a live FH-mod server; the set and values depend on the
server and mod.

| Key | Meaning | Notes / example |
|---|---|---|
| `active_mods` | Active mod/modification identifiers | Example: `FH` |
| `allied_team_ratio`, `axis_team_ratio` | Team-size balance ratios | Example: `1` |
| `allow_nose_cam` | Whether the cockpit nose camera is allowed | `yes`/`no`; example: `yes` |
| `auto_balance_teams` | Whether teams are balanced automatically | `on`/`off`; example: `off` |
| `bandwidth_choke_limit` | Server bandwidth choke limit | Example: `0`; unit not established here |
| `content_check` | Whether the server checks client content | Numeric flag; example: `0` |
| `cpu` | CPU-related server setting | Numeric; example: `4590`; precise semantics not established here |
| `external_view` | Whether external (third-person) views are allowed | `on`/`off`; example: `on` |
| `free_camera` | Whether the free camera is allowed | `on`/`off`; example: `off` |
| `game_start_delay` | Delay before a round starts | Duration string; example: `10s` |
| `hit_indicator` | Whether hit indicators are shown | `on`/`off`; example: `on` |
| `kickback` | Weapon kickback strength | Percentage string; example: `0%` |
| `kickback_on_splash` | Weapon kickback from splash damage | Percentage string; example: `0%` |
| `name_tag_distance` | Distance at which infantry name tags are visible | Example: `50`; unit not established here |
| `name_tag_distance_scope` | Name-tag distance while using a scope | Example: `300` |
| `number_of_rounds` | Number of rounds per match | Example: `1` |
| `soldier_friendly_fire` | Friendly-fire damage multiplier for soldiers | Percentage string; example: `100%` |
| `soldier_friendly_fire_on_splash` | Soldier friendly-fire multiplier for splash damage | Percentage string; example: `100%` |
| `spawn_delay` | Delay before a player may spawn | Duration string; example: `3s` |
| `spawn_wave_time` | Interval between spawn waves | Duration string; example: `7s` |
| `sv_punkbuster` | Whether PunkBuster is enabled | Numeric flag; example: `0` |
| `ticket_ratio` | Ticket ratio/bleed setting | Percentage string; example: `90%` |
| `time_limit` | Round time limit | Example: `20` (minutes) |
| `tk_mode` | Team-kill handling mode | Example: `forgive` |
| `unpure_mods` | Mods permitted without a purity check | String/list; example: empty |
| `vehicle_friendly_fire` | Friendly-fire damage multiplier for vehicles | Percentage string; example: `100%` |
| `vehicle_friendly_fire_on_splash` | Vehicle friendly-fire multiplier for splash damage | Percentage string; example: `100%` |

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

## Probing a live server: `tools/bf1942_gamespy_probe.py`

[`bf1942_gamespy_probe.py`](../tools/bf1942_gamespy_probe.py) is a dependency-
free probe for answering "what does *this* server actually return?". It
auto-detects the query port (22000, 23000, the game port, or an explicit
`--scan` range), sends `\status\` plus the other GameSpy verbs (`\basic\`,
`\info\`, `\rules\`, `\players\`, optionally `\getinfo\`, `\getstatus\`,
`\echo\`, `\list\`), reassembles multi-datagram replies and prints every
key/value pair grouped into server / teams / players. Keys that are not in the
tables above are listed separately, and keys or values that look vehicle-
related are called out.

```bash
# one shot, auto-detect the port
./tools/bf1942_gamespy_probe.py 10.0.0.28

# raw datagrams + JSON
./tools/bf1942_gamespy_probe.py 10.0.0.28 --port 22000 --hex --json

# enter/leave a vehicle while it polls and print a field diff per poll
./tools/bf1942_gamespy_probe.py 10.0.0.28 --port 22000 --watch 120

# compare against a snapshot taken while on foot
./tools/bf1942_gamespy_probe.py 10.0.0.28 --save on_foot.json
./tools/bf1942_gamespy_probe.py 10.0.0.28 --diff on_foot.json --watch 60
```

`--watch` is the empirical vehicle test: if entering a vehicle exposed any
state, a key would be added or a value would change. (Volatile fields such as
`queryid`, `roundTimeRemain`, `tickets1/2` and the uninitialised-looking
`averageFPS` do change on their own; the probe reports them but they are not
vehicle state.)

BF1942's query parser is strict: only the bare `\status\` frame works.
Appending `\queryid\1.1` — which other GameSpy servers accept — makes it drop
the request silently (`--all-variants` demonstrates this). The response itself
already carries `\final\` and `\queryid\<n.m>`.

### Vehicle data: not exposed

A live probe of a FH-mod server (`MAJORLEO`, Adak Island) on UDP 22000 returned
61 keys in the `\status\` reply: the documented server fields, two `teamname_*`
entries, one player row, and ~40 additional **server-rule** keys that `\rules\`
returns as well — including `vehicle_friendly_fire`,
`vehicle_friendly_fire_on_splash`, `spawn_delay`, `spawn_wave_time`,
`ticket_ratio`, `allow_nose_cam`, and so on. These are configuration values,
not live state: there is no `vehicle_<n>`, `vehicle`, `pco_id`, `seat_<n>` or
kit field for a player, and entering/leaving a vehicle adds no key and changes
no player field.

So the answer to "can the query retrieve vehicle data?" is **no** — neither
which vehicle a player occupies nor vehicle health/position/seat. Vehicle
identity (`GESetVehicle`, `GEExitVehicle`, `pco_id`, `vehicle_pos`) appears only
in the binary gameplay stream and in the server event log's `bf:event` params
(see [network-protocol.md](network-protocol.md) §3 and §7.3). The event log is
written on the server host and is the only readable source for vehicle-related
events.
