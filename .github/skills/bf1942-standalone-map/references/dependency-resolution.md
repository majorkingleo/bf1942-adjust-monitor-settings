# Finding a map's external dependencies

A BF1942 level (.rfa) only contains its **own** files: heightmap, terrain textures,
lightmaps, object placement, spawns, and map-specific objects. Everything else —
geometry, vehicles, weapons, kits, shared textures, sounds — is referenced **by
name** and loaded from the other archives in the mod path.

To make a level self-contained, resolve every name the map references to the
files that define it, then copy those files into the standalone mod.

## 1. What the map references (use `scripts/find_deps.ps1`)

Run the scanner on the extracted level:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File find_deps.ps1 "<map dir>" -Resolve "<extracted FH root>"
```

It reports these categories (example values from `Battle_Of_Pavlov-1942`):

| Source | Pattern | Example | Resolves to |
|--------|---------|---------|-------------|
| `PreCache.con` | `Object.create <name>` | `PZIIIJearly`, `Mg34`, `RussianSoldier` | object templates in `Objects/` |
| `StaticObjects.con` | `Object.create <name>` | `pavlov_m1`, `rusruin01_m1` | geometry in `StandardMesh/<name>.sm` (+ `.rs`) |
| `Init.con` | `textureManager.alternativePath <path>` | `Texture/Levels/Pavlov`, `Texture/GENERAL_RUS_WINTER` | whole texture folders |
| `Init.con` | `game.setKit <t> <i> <kit>` | `1German_AssaultK98` | kit object templates |
| `Init.con` | `game.setTeamSkin <t> <skin>` | `GermanSoldier`, `RussianSoldier` | soldier object templates |
| `Conquest/ObjectSpawnTemplates.con` | `setObjectTemplate 1/2 <name>` | `stationary_dp1928`, `pziiijearly` | spawned objects |
| `Conquest/SoldierSpawnTemplates.con` | `SpawnPoint` ids | — | no external assets |
| any `.con` | `run <path>` | `PavlovAllied/go`, `Sounds/Environment` | map-internal files |

## 2. Resolve names to files

### Index-first (preferred): resolve without extracting

`bfmod_tools\all_packages.txt` already lists every file in every archive (see
[archive-index.md](./archive-index.md)). Resolve names against it *before*
unpacking anything large:

```powershell
$q = '.github\skills\bf1942-standalone-map\scripts\query_index.ps1'
# geometry name from StaticObjects.con -> which archive holds the mesh?
powershell -NoProfile -ExecutionPolicy Bypass -File $q -Name "pavlov_m1.sm"
#   -> standardmesh/Pavlov_m1.sm   @ bf/Mods/FH/Archives/standardmesh.rfa
```

Collect the resolved internal paths into a keep-list, then hand it to
`query_index.ps1 -CheckList` to group them per archive and to surface names that
exist **nowhere** (a typo or a cross-mod dependency).

Resolving the *object templates* (`Object.create` names, kits, skins) still
requires file **contents** — the index gives locations, not references. That is
why `objects.rfa` (6 MB) is worth extracting in full: it holds the `.con` chains
you must grep to walk the closure. Only the big leaf archives
(`texture`, `standardmesh`, `sound`, `menu`) need the selective treatment.

### Brute force: extract the shared archives, then grep locally

Extract the shared archives first (into e.g. `work\FH\`):

```powershell
rfaUnpack.exe "Mods\FH\Archives\objects.rfa"      work\FH
rfaUnpack.exe "Mods\FH\Archives\standardmesh.rfa" work\FH
rfaUnpack.exe "Mods\FH\Archives\texture.rfa"      work\FH
rfaUnpack.exe "Mods\FH\Archives\sound.rfa"        work\FH
rfaUnpack.exe "Mods\FH\Archives\animations.rfa"   work\FH
rfaUnpack.exe "Mods\FH\Archives\treemesh.rfa"     work\FH
rfaUnpack.exe "Mods\FH\Archives\aimeshes.rfa"     work\FH
rfaUnpack.exe "Mods\FH\Archives\menu.rfa"         work\FH
rfaUnpack.exe "Mods\FH\Archives\bf1942\game.rfa"  work\FH
```

Resolution rules:

- **Geometry** (`StaticObjects.con` names) → `StandardMesh\<name>.sm` (visual mesh),
  `StandardMesh\<name>.rs` (collision), and any `StandardMesh\<name>.con`.
- **Objects / kits / vehicles / weapons / skins** → the `.con` that contains
  `ObjectTemplate.create <type> <name>`, found by grepping `Objects\**\*.con`.
  That template then names further dependencies via:
  - `ObjectTemplate.geometry <mesh>` → `StandardMesh\<mesh>.sm/.rs`
  - `ObjectTemplate.addTemplate <name>` → another object (follow recursively)
  - `ObjectTemplate.setNetworkableInfo`, `...fireArmsSetInputFire` etc. → `.inc` files
- **Textures** → copy the whole folders from `textureManager.alternativePath`
  (the map only draws textures reachable through those search paths).
- **Sounds** → object templates reference sound `.ssc`/`.wav` via
  `ObjectTemplate.create Sound ...` / `sound/` paths; copy those plus the map's
  `Sounds/Environment.con` targets.

This is a **transitive closure**: keep following `addTemplate`, `geometry`, and
sound/effect references until no new files are found. `PreCache.con` is a strong
hint for the first level of the closure.

## 3. Things that are always kept (tiny / hard to prune safely)

- `bf1942/game.rfa` — damage system, materials, faction config. Keep the FH copy
  **in full** (0.3 MB). The map will not load without it.
- `aimeshes.rfa` (0.02 MB) and `animations.rfa` (3.6 MB) — keep in full.
- `menu.rfa` (28 MB) — loading screens / menu art; keep, or prune later.
- The base game `Mods/BF1942/Archives/` (engine fallback content).

## 4. Verification loop

After repacking, start the map and watch for:
- missing texture (`texture not found` console spam / magenta surfaces),
- missing mesh (invisible or crashing objects),
- missing sound (no error, just silence).

Iterate: add the missing file, repack, retest. A full single-player + LAN
session is the real test; do not trust "it loaded" alone.
