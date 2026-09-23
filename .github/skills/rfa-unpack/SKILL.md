---
name: rfa-unpack
description: 'Unpack, selectively extract or repack Battlefield 1942 / Forgotten Hope .rfa archives with rfaUnpack.exe and rfaPack.exe — the only tools that handle BF1942''s LZO1X-compressed payloads. Use when extracting a map or mod archive, pulling a single file or a file list out of a big archive, repacking a pruned folder tree, or debugging rfaUnpack errors such as "Name Not found", "Directory does not exist" or "item out of range to extract".'
---

# rfaUnpack / rfaPack — working with `.rfa` archives

Battlefield 1942 stores mod content in `.rfa` archives whose payloads are
**LZO1X**-compressed (the Oberhumer LZO format — not zlib/deflate, not FastLZ,
not RefPack). 7-Zip, .NET `DeflateStream` and most generic tools fail on them;
`rfaUnpack.exe` handles both compressed and stored entries transparently, and
`rfaPack.exe` is the matching writer. See `references/rfaunpack-cli.md` and the
sibling `bf1942-standalone-map` skill's `references/rfa-format.md`.

## When to use

- Extract a map or mod `.rfa` into an editable folder tree
- Pull only a few files out of a huge archive (e.g. `texture.rfa`, 707 MB)
- Repack a pruned folder back into `.rfa`
- Diagnose rfaUnpack's terse error messages

## Tools

| Tool | Path | Purpose |
|------|------|---------|
| `rfaUnpack.exe` | `bin\rfaUnpack.exe` | unpack / selective extract |
| `rfaPack.exe` | `bin\rfaPack.exe` | pack a folder into `.rfa` (`-Compress` optional) |
| `convert.exe` | `bin\convert.exe` | ImageMagick 7 (`.dds` / `.tga` conversion) |
| `bin\Readme.txt` | — | original tool notes (reproduced in `references/rfaunpack-cli.md`) |

The same binaries also exist at the legacy checkout `E:\bf_tk_mod\tk_mod\bin\`;
prefer the in-repo `bin\` copy so builds are reproducible.

Call them with the call operator and always quote paths:

```powershell
& .\bin\rfaUnpack.exe "origin\Mods\FH\Archives\texture.rfa" "work\FH"
```

If invoking one of the `.ps1` helpers in a fresh shell, this machine's execution
policy rejects in-session `&` calls, so go through `-File`:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .github\skills\rfa-unpack\scripts\unpack-rfa.ps1 -Archive <a.rfa> -OutDir work\FH
```

Because `-File` does **not** split comma lists into arrays (`-Only "a,b"` arrives
as the single string `a,b`), pass list arguments as one comma-separated quoted
argument. The wrapper handles both that and a real array.

## Syntax (verified)

```
rfaUnpack.exe <Archive.rfa> [ExtractToPath] [-option]
    -i<index>        extract the file at 0-based file-table index
    -l<listfile>     extract every file listed in <listfile>
    -f<name>         NOT WORKING in this build — see "Gotchas"

rfaPack.exe <sourceDir> <baseFolderName> <archive.rfa> [-u] [-Compress]
```

Running `rfaUnpack.exe` with no arguments prints the usage banner.

## Workflow

### 1. Create the output directory first

The tool does **not** create it — this is the most common failure:

```
Error! Directory does not exist: work\_probe
```

```powershell
New-Item -ItemType Directory -Force work\FH | Out-Null
& .\bin\rfaUnpack.exe "origin\Mods\FH\Archives\texture.rfa" "work\FH"
```

### 2. Understand the output layout (paths are preserved in full)

Entry names inside an archive already include their top folder, and rfaUnpack
recreates that structure verbatim under `ExtractToPath`:

```
Battle_Of_Pavlov-1942.rfa
  internal: bf1942/levels/Battle_Of_Pavlov-1942/Init.con
  on disk : work\map\bf1942\levels\Battle_Of_Pavlov-1942\Init.con
                     ^^^^^^ ExtractToPath is a PARENT, not the leaf folder
```

Verified on the FH Pavlov map: **251 files, 11.49 MB** from a 5.7 MB archive.

### 3. Selective extraction (useful for 440–707 MB archives)

**By list file** — the reliable way. Full internal paths, forward slashes, one
per line:

```powershell
# work\list.lst
bf1942/levels/Battle_Of_Pavlov-1942/Init.con
bf1942/levels/Battle_Of_Pavlov-1942/Conquest.con

& .\bin\rfaUnpack.exe "origin\Mods\FH\Archives\bf1942\levels\Battle_Of_Pavlov-1942.rfa" "work\out" "-lwork\list.lst"
```

Or let the wrapper build the list for you:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .github\skills\rfa-unpack\scripts\unpack-rfa.ps1 `
    -Archive "origin\Mods\FH\Archives\texture.rfa" -OutDir work\FH `
    -Only "texture/Levels/Pavlov/x.dds,texture/GENERAL_RUS_WINTER/y.dds"
```

Get the exact names from `list_rfa.ps1` (sibling skill) — the `Name` column is
already in the required form.

**By index** — 0-based position in the file table:

```powershell
& .\bin\rfaUnpack.exe <archive.rfa> "work\out" -i0   # -> Conquest.con for the Pavlov map
```

### 4. Repack

`set sourceDir` = the folder whose *contents* live at the top of the archive,
`baseFolderName` = that top folder name:

```powershell
# work\map\bf1942\levels\... -> archive internal bf1942/levels/...
& .\bin\rfaPack.exe "work\map\bf1942" bf1942 "out\Battle_Of_Pavlov-1942.rfa" -Compress
```

Round-trip invariant: unpacking then repacking with the matching
`baseFolderName` reproduces the original internal layout. Verify with
`list_rfa.ps1` after packing.

## Verified behaviour

Probed against `Battle_Of_Pavlov-1942.rfa` (FH, 251 files):

| Invocation | Observed result |
|---|---|
| *(no arguments)* | usage banner printed, exits |
| `<rfa> <existing dir>` | full extract, internal paths preserved |
| `<rfa> <missing dir>` | `Error! Directory does not exist` — nothing extracted |
| `-i0`, `-i5` | 1 file extracted each (0-based) |
| `-i9999` | `ERROR! item out of range to extract! 9999` |
| `-l<list>` with full internal paths | **works** |
| `-l<list>` with plain basenames | `ERROR! Could not ExtractByName: ... Name Not found!` |
| `-fInit`, `-fInit.con`, `-fPreCache.con`, `-fObjects` | always `Name Not found!` — **unusable** |

Compression is handled transparently for both archive families (FH
`flags=0xFFFFFFFF`, base game `0x028A0220`).

## Gotchas

- **`-f` cannot match anything** in this build; use `-l` with full internal paths.
- **Create the output directory yourself** — no auto-create, hard error otherwise.
- **`ExtractToPath` is a parent.** `texture.rfa` lands in `<out>\texture\...`,
  not directly in `<out>`.
- **`-l` paths must be full internal paths with `/`.** Basenames are silently
  skipped (the run continues; only the reported count tells you).
- **`-l` splits entries on whitespace**, so an archive entry whose name contains a
  space is never extracted by a list — verified on `texture/ENVIROMENT_WINTER/green_T .dds`,
  `texture/ENVIROMENT_WINTER/stnwall french1_s.dds` and
  `texture/GENERAL_GER_WINTER/Panzer2 mesh map.dds`. Extract those by index with
  `-i<N>` instead: parse the archive's directory table (see
  `scripts/list_rfa.ps1`) and pass the entry's 0-based position.
  `scripts/extract_keep_lists.ps1` in the `bf1942-standalone-map` skill does this
  automatically as a repair pass.
- **Re-extraction overwrites silently** — no prompt, no backup.
- **A partial `-l` failure does not stop the run**, so always count the files
  written afterwards.
- The tool prints chatter (`LoadIndex`, `rfa_file_name`, `unpackedSize_MB`) on
  stdout; parse the resulting file count instead of scraping that output.
- **`powershell -File` mangles array arguments**: `-Only "a,b"` arrives as the
  single string `a,b`, which used to silently extract nothing. The wrapper now
  splits on commas/semicolons, but if you build your own list file, put one full
  path per line.

## Scripts

- `scripts/unpack-rfa.ps1` — wrapper that creates the output dir, converts
  `-Only <paths>` into the working `-l` form (avoiding the broken `-f`), supports
  `-Index`, and reports how many files were written. Prefer this over calling
  `rfaUnpack.exe` directly.

## References

- `references/rfaunpack-cli.md` — verbatim `bin\Readme.txt` notes, the real usage
  banner, and the full probe matrix.
- Sibling skill `bf1942-standalone-map` — where unpacking fits in the
  standalone-map pipeline, plus `references/rfa-format.md` for the binary layout.
