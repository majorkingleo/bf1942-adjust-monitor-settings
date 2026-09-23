---
name: bf1942-standalone-map
description: 'Build a self-contained Battlefield 1942 / Forgotten Hope mod that runs only one map (e.g. Pavlov) with multiplayer. Use for extracting .rfa archives, resolving a map''s object/geometry/texture/sound dependencies, pruning archives, repacking with rfaPack/rfaUnpack, and creating a minimal standalone mod folder with a dedicated server.'
---

# BF1942 Standalone Map

Turn one Battlefield 1942 / Forgotten Hope map into a minimal, self-contained
mod that loads only that map and still supports multiplayer (LAN + dedicated
server).

Sibling skill: `bf1942-level-editing` — for working out what is *inside* a level
(objects, flags, spawns, sky) and changing it, once the level already loads.

## When to use

- "Remove everything except map X" / "make a standalone mod"
- Prune a mod archive to only the assets a single map needs
- Unpack / list / repack `.rfa` archives
- Set up a trimmed game root for a single map with multiplayer

## Tools (already on this machine)

| Tool | Path | Use |
|------|------|-----|
| `rfaUnpack.exe` | `bin\rfaUnpack.exe` | unpack `.rfa` (handles FH compression) |
| `rfaPack.exe` | `bin\rfaPack.exe` | pack folder into `.rfa` (`-Compress` optional) |
| `convert.exe` | `bin\convert.exe` | ImageMagick (DDS/TGA conversion) |
| `winRFA.exe` | `bfmod_tools\winRFA.exe` | GUI alternative for pack/unpack |
| BFTools (MDT) | `bfmod_tools\bfmdt2_75.zip` | official mod dev toolkit (installer) |

The same binaries also exist at the legacy checkout `E:\bf_tk_mod\tk_mod\bin\`;
prefer the in-repo `bin\` copies. For unpacking details, the selective-extraction
switches and the tool's failure modes, follow the `rfa-unpack` skill instead of
calling `rfaUnpack.exe` directly.

Reference structure to copy from: `E:\bf_tk_mod\tk_mod\dist\FH\` — extracted
maps live as `<Map>\bf1942\levels\<Map>\...`; `create_and_sync.bat` shows the
exact `rfaPack.exe` command lines.

CLI syntax:

```
rfaUnpack.exe <archive.rfa> <extractToPath>
rfaPack.exe <sourceDir> <baseFolderName> <archive.rfa> [-u] [-Compress]
```

`baseFolderName` is the internal top folder (e.g. `bf1942`, `texture`,
`standardMesh`). See [rfa-format.md](./references/rfa-format.md).

## Archive index — `bfmod_tools\all_packages.txt`

A 132,421-line inventory of every file inside every archive in the install, one
`<archive>: ./<internal path>` line each. Use it to find **which archive owns an
asset** so you can extract only what the map needs instead of unpacking ~1.3 GB
and deleting most of it.

```powershell
$q = '.github\skills\bf1942-standalone-map\scripts\query_index.ps1'
powershell -NoProfile -ExecutionPolicy Bypass -File $q -Name "Pavlov_m1.sm"
```

See [archive-index.md](./references/archive-index.md) for the format, verified
coverage numbers and query recipes.

## Procedure

### 1. Pick the map and locate its archive

```
<game>\Mods\FH\Archives\bf1942\levels\<MapName>.rfa
```

### 2. Extract the map and the shared archives into a work tree

The output directories must exist before `rfaUnpack.exe` runs (use
`scripts\unpack-rfa.ps1` from the `rfa-unpack` skill, which creates them):

```powershell
New-Item -ItemType Directory -Force work\map, work\FH | Out-Null
& .\bin\rfaUnpack.exe "<game>\Mods\FH\Archives\bf1942\levels\Battle_Of_Pavlov-1942.rfa" work\map
& .\bin\rfaUnpack.exe "<game>\Mods\FH\Archives\objects.rfa"      work\FH
& .\bin\rfaUnpack.exe "<game>\Mods\FH\Archives\standardmesh.rfa" work\FH
& .\bin\rfaUnpack.exe "<game>\Mods\FH\Archives\texture.rfa"      work\FH
& .\bin\rfaUnpack.exe "<game>\Mods\FH\Archives\sound.rfa"        work\FH
& .\bin\rfaUnpack.exe "<game>\Mods\FH\Archives\animations.rfa"   work\FH
& .\bin\rfaUnpack.exe "<game>\Mods\FH\Archives\treemesh.rfa"     work\FH
& .\bin\rfaUnpack.exe "<game>\Mods\FH\Archives\aimeshes.rfa"     work\FH
& .\bin\rfaUnpack.exe "<game>\Mods\FH\Archives\menu.rfa"         work\FH
& .\bin\rfaUnpack.exe "<game>\Mods\FH\Archives\bf1942\game.rfa"  work\FH
```

Each archive recreates its own top folder under the target
(`work\FH\texture\...`, `work\FH\objects\...`).

**Index-first alternative (much cheaper):** extracting `texture.rfa` (707 MB),
`sound.rfa` (440 MB) and `standardmesh.rfa` (190 MB) only to delete most of it is
wasteful. Instead extract the small archives (`objects.rfa` 6 MB is worth having
in full), scan the map, resolve the referenced names through
`all_packages.txt`, and pull just those paths with `rfaUnpack.exe -l`. See
[archive-index.md](./references/archive-index.md) and step 3.

### 3. Scan the map for its dependencies

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\find_deps.ps1 "work\map\bf1942\levels\Battle_Of_Pavlov-1942" -Resolve "work\FH"
```

This lists the objects, static geometry, texture paths, kits, skins and spawn
templates the map uses. See [dependency-resolution.md](./references/dependency-resolution.md)
for how each name resolves to a file.

The scanner emits **names**; the archive index turns those into **files**. For a
first cut, map the geometry names straight to meshes and ask the index where they
live:

```powershell
$q = '.github\skills\bf1942-standalone-map\scripts\query_index.ps1'
# StandardMesh name -> owning archive (e.g. pavlov_m1 -> standardmesh/Pavlov_m1.sm)
powershell -NoProfile -ExecutionPolicy Bypass -File $q -Name "pavlov_m1.sm"
```

### 4. Prune the work tree

Delete from `work\FH` everything the map does not reference:

- `StandardMesh\` → keep only the `.sm`/`.rs`/`.con` named by `StaticObjects.con`
  plus everything transitively referenced by the kept object templates.
- `Texture\` → keep only the `textureManager.alternativePath` folders
  (`Texture/Levels/Pavlov`, `Texture/GENERAL_RUS_WINTER`, ...).
- `Objects\` → keep only the `.con` chains that define the PreCache objects,
  kits, skins and spawner templates (follow `addTemplate`/`geometry` recursively).
- `Sound\` → keep referenced sounds + the map's `Sounds/Environment.con` targets.
- Keep `aimeshes\`, `animations\` and `bf1942\game\` **in full**.

### 5. Repack the pruned archives

```powershell
rfaPack.exe work\FH\texture      texture       Battle_Of_Pavlov-1942\texture.rfa
rfaPack.exe work\FH\standardMesh standardMesh  Battle_Of_Pavlov-1942\standardMesh.rfa
rfaPack.exe work\FH\objects      objects       Battle_Of_Pavlov-1942\objects.rfa
rfaPack.exe work\FH\sound        sound         Battle_Of_Pavlov-1942\sound.rfa
rfaPack.exe work\FH\aimeshes     aimeshes      Battle_Of_Pavlov-1942\aimeshes.rfa
rfaPack.exe work\FH\animations   animations    Battle_Of_Pavlov-1942\animations.rfa
rfaPack.exe work\FH\menu         menu          Battle_Of_Pavlov-1942\menu.rfa
rfaPack.exe work\map\Battle_Of_Pavlov-1942\bf1942 bf1942 Battle_Of_Pavlov-1942\bf1942\levels\Battle_Of_Pavlov-1942.rfa
```

### 6. Assemble the standalone mod folder

Build the tree described in [standalone-layout.md](./references/standalone-layout.md):

1. Copy engine files (`BF1942.exe`, DLLs, `Core\`, dedicated-server exes).
2. `Mods\BF1942\` — base game, trimmed (no `bf1942/levels`).
3. `Mods\bf_1942_kwg_mod\` — the custom mod with the pruned archives + `init.con`.
4. Copy `lexiconall.dat` (FH) into the custom mod root.

### 7. Launch and verify (multiplayer is the real test)

```powershell
BF1942.exe +game bf_1942_kwg_mod
```

- Load the map, then host a LAN game and join from a second machine running the
  **same** folder. Fix missing-texture/mesh errors by adding the file and
  repeating steps 4-5.

## Scripts

- `scripts/extract_rfa.ps1` — extract an archive via `rfaUnpack.exe` (handles compression).
- `scripts/list_rfa.ps1` — list archive contents without unpacking.
- `scripts/find_deps.ps1` — scan a map and resolve its external references.
- `scripts/query_index.ps1` — query `all_packages.txt`: find the archive that owns
  a file, count entries per archive, or resolve a keep-list to its owner archives.
- `scripts/resolve_keep_list.ps1` — walk the dependency closure (templates,
  `addTemplate`, `geometry` → `GeometryTemplate.file`, texture paths) and emit one
  `work\keep\<archive>.lst` per archive plus `_manifest.tsv`, ready for
  `rfaUnpack.exe -l`. Also writes `UNRESOLVED.txt` for names that exist nowhere or
  only outside the target mod. Falls back to the vanilla base game/XPack archives
  for assets FH references but does not ship (`-ExcludeVanilla` to disable).
- `scripts/extract_keep_lists.ps1` — apply every keep-list from the manifest,
  merging FH and vanilla files into one staging tree by internal path, then repair
  whitespace-named entries via index extraction.

## References

- `references/rfa-format.md` — verified binary format of `.rfa` archives.
- `references/archive-index.md` — `all_packages.txt` format, verified coverage,
  query recipes and pruning workflow.
- `references/dependency-resolution.md` — name → file resolution rules.
- `references/standalone-layout.md` — target folder tree, `init.con`, multiplayer
  checklist, launch commands.
