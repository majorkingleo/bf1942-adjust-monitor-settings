# Archive index — `bfmod_tools/all_packages.txt`

A precomputed inventory of **every archive in the install and the files inside
it**. This is what makes name → file resolution possible *without* unpacking the
440 MB / 707 MB archives.

## Format

One line per entry:

```
<archive path relative to game root>: ./<internal path inside the archive>
```

Example:

```
bf/Mods/FH/Archives/objects.rfa: ./Objects/Vehicles/Land/Willy/Objects.con
bf/Mods/FH/Archives/standardmesh.rfa: ./standardmesh/Pavlov_m1.sm
bf/Mods/FH/Archives/bf1942/levels/Battle_Of_Pavlov-1942.rfa: ./bf1942/levels/Battle_Of_Pavlov-1942/Init.con
```

| Part | Meaning |
|---|---|
| `bf/...` | prefix is the game root. Verified: stripping `bf/` and swapping `/`→`\` yields a real file under this workspace's `origin\` snapshot. |
| `<archive>` | the `.rfa` that owns the file |
| `./<internal path>` | path **inside** the archive, exactly the form `rfaUnpack.exe -l` needs (minus the `./`) |

The index has **no header** and reports no archive-level options — it is a flat
file list.

## Coverage & verified numbers

| Metric | Value |
|---|---|
| File size | 13.02 MB |
| Lines | 132,421 (all parseable — no malformed lines) |
| FH entries | 61,311 across 69 level archives + 9 shared archives |

FH shared archives by entry count:

| Archive | Entries |
|---|---|
| `objects.rfa` | 11,681 |
| `standardmesh.rfa` | 9,236 |
| `texture.rfa` | 5,693 |
| `sound.rfa` | 3,228 |
| `animations.rfa` | 1,212 |
| `menu.rfa` | 1,132 |
| `bf1942/game.rfa` | 115 |
| `treemesh.rfa` | 91 |
| `aimeshes.rfa` | 74 |
| `menu_001.rfa` | 2 |

**Accuracy check:** the index reports 251 entries for
`bf/Mods/FH/Archives/bf1942/levels/Battle_Of_Pavlov-1942.rfa`, and
`rfaUnpack.exe` extracts exactly 251 files from that archive — the index matches
the real directory table.

Beyond FH, the index also covers `bf/Mods/bf1942/Archives/*`, `XPack1`, `XPack2`,
`DesertCombat`, `DC_Extended` and `bf/Patch Data/*`, so it can tell you when an
asset only exists in another mod (a pruning hazard).

## Querying it

Helper (handles parsing, dedupes, strips `./`, detects missing entries):

```powershell
$q = '.github\skills\bf1942-standalone-map\scripts\query_index.ps1'

# which archive holds this file?
powershell -NoProfile -ExecutionPolicy Bypass -File $q -Name "Pavlov_m1.sm"

# wildcards, restricted to one archive
powershell -NoProfile -ExecutionPolicy Bypass -File $q -Name "pavlov*_m1.*" -Archive "*/FH/Archives/standardmesh.rfa"

# per-archive counts for a folder
powershell -NoProfile -ExecutionPolicy Bypass -File $q -PathLike "texture/Levels/Pavlov/*" -Summary

# resolve a keep-list (one internal path per line) -> owning archive
powershell -NoProfile -ExecutionPolicy Bypass -File $q -CheckList work\keep.txt
```

`-CheckList` output groups the keep-list by owning archive and lists anything not
found, which is exactly the input shape for the pruning step:

```
Resolved 1/2 path(s); 1 missing

=== per-archive extraction load ===
        1  bf/Mods/FH/Archives/standardmesh.rfa

=== NOT FOUND (typo, or asset really absent) ===
  texture/Levels/Pavlov/nope.dds
```

Ad-hoc alternative without the helper:

```powershell
Select-String -Path bfmod_tools\all_packages.txt -Pattern 'Pavlov_m1\.sm' -SimpleMatch
```

## Why this matters for the standalone build

Without the index the workflow is "extract ~1.3 GB of shared archives, then
delete everything the map does not use". With the index it becomes:

1. Extract the **map** archive (small: 251 files / 11.5 MB) and `objects.rfa` (6 MB).
2. Scan the map's `.con` files for referenced names (see
   [dependency-resolution.md](./dependency-resolution.md)).
3. Resolve each name through the index to `(archive, internal path)`.
4. Extract **only** those paths with `rfaUnpack.exe -l` — nothing is extracted
   just to be deleted.

Bonus: a name that resolves to *no* archive is caught immediately, instead of
surfacing later as a missing-asset error in-game.

## Caveats

- **It is an inventory, not a dependency graph.** It says where a file lives, not
  which file references it — the transitive closure still comes from scanning
  `.con` contents.
- **It was generated against `E:\bf_tk_mod\bf`** (the working copy), not against
  `origin\`. The FH archive set was verified to match `origin\` one-for-one; if
  the two installs ever diverge, re-verify a single lookup with
  `list_rfa.ps1` before trusting the index for a new archive.
- **Duplicate basenames are normal** — `objects.rfa`, `standardmesh.rfa` etc.
  exist in FH *and* in DesertCombat/DC_Extended/XPack. Always filter with
  `-Archive` when the target is FH, or you may pull an asset from the wrong mod.
- **Archive-internal top folders can differ in case from the `.rfa` name**
  (`standardmesh.rfa` contains `standardmesh/...`, not `StandardMesh/...`). Use
  the index's spelling when building `rfaPack.exe <sourceDir> <baseFolderName>`
  arguments.
- **Entry count ≠ unpacked byte size.** The index has no sizes; use
  `list_rfa.ps1` (which reads `storedSize`/`uncompressedSize`) when you need to
  judge how much an extraction will cost.
