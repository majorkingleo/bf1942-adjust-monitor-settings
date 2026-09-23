# rfaUnpack / rfaPack — CLI reference and verified behaviour

Source of truth for the command lines documented in `../SKILL.md`. Everything in
the "Probe matrix" section was tested on this machine against the Forgotten Hope
archive `origin\Mods\FH\Archives\bf1942\levels\Battle_Of_Pavlov-1942.rfa`
(5.7 MB, 251 files) using `bin\rfaUnpack.exe`.

## Original `bin\Readme.txt` (verbatim)

```
convert.exe
is from ImageMagick 7.1.0-57 package


RFA - PACK

NEW! "-Compress" option enables compression


rfaPack.exe [folder to pack] [baseFolderName] [archive.rfa]
	rfaPack.exe d:\contentForMyMod\menu menu "c:\bf 1942\mods\myMod\Archives\menu.rfa"


ProgramName [sourceDir] [PackDirName] [Archive.rfa] [ -u update existing .rfa | -Compress]
   RfaPack.exe d:/menu menu menu.rfa
   RfaPack.exe d:/menu menu menu.rfa -u
   RfaPack.exe d:/menu menu menu.rfa -Compress
   RfaPack.exe d:/menu menu menu.rfa -u -Compress


RFA - UNPACK

rfaUnpack.exe [archive.rfa] [extractToPath]
	rfaUnpack.exe objects.rfa c:\myExtractedFiles
```

Note the readme documents only the two positional arguments. The real binary has
more switches — see below.

## Actual usage banner (`rfaUnpack.exe` with no arguments)

```
|| .RFA UNPACK ||
 Usage: rfaUnpack.exe <Archive.rfa> [ExtractToPath] [-option]
 Options:
   -i[indexToExtract]       -i123   -f[filenameToExtract]    -fObjects/Vehicles/Land/Willy/Objects.con   -l[listFile.lst]         -lC:\extractFileList.lst
```

Key differences from the readme:

- `ExtractToPath` is **optional**.
- `-i`, `-f` and `-l` are undocumented selection switches. `-l` works; `-f` does
  not.

## Probe matrix

All runs used an **existing** output directory.

| Invocation | Result |
|---|---|
| *(no args)* | usage banner, no extraction |
| `<rfa> <existing dir>` | full extract; 251 files / 11.49 MB; internal paths preserved |
| `<rfa> <missing dir>` | `Error! Directory does not exist: <dir>` — nothing written |
| `<rfa> <dir> -i0` | extracted `bf1942\levels\Battle_Of_Pavlov-1942\Conquest.con` |
| `<rfa> <dir> -i5` | extracted 1 file (0-based index) |
| `<rfa> <dir> -i9999` | `ERROR! item out of range to extract! 9999` |
| `<rfa> <dir> -l<list>` where list = `bf1942/levels/Battle_Of_Pavlov-1942/Init.con` | **works** — extracted `...\Init.con`, echoing `EXTRACTING BY NAME: bf1942/levels/Battle_Of_Pavlov-1942/Init.con` |
| `<rfa> <dir> -l<list>` where list = `Init.con` | `EXTRACTING BY NAME: Conquest.con` → `ERROR! Could not ExtractByName: Conquest.con / Name Not found!` |
| `<rfa> <dir> -fInit` | `ERROR! Could not ExtractByName: Init / Name Not found!` |
| `<rfa> <dir> -fInit.con` | `ERROR! Could not ExtractByName: Init.con / Name Not found!` |
| `<rfa> <dir> -fPreCache.con` | `ERROR! Could not ExtractByName: PreCache.con / Name Not found!` |
| `<rfa> <dir> -fObjects` | `ERROR! Could not ExtractByName: Objects / Name Not found!` |

Conclusions:

1. **`-f` is broken in this build.** Every attempt, with or without extension,
   for files that certainly exist, reports `Name Not found!`. Do not use it; use
   `-l` instead.
2. **`-l` requires full internal paths** (relative to the archive root, `/`
   separators, extension included). Basenames do not match.
3. **The output directory must pre-exist**, otherwise the tool aborts before
   extracting anything.
4. **`-i` uses a 0-based index** into the archive's file table; out-of-range
   values are rejected with an explicit error.
5. Errors are **not fatal to the whole run**: with a list file, entries that fail
   are reported and the remaining entries are still processed. Always verify by
   counting the files written.

## Compression handling

`rfaUnpack.exe` transparently decompresses the payload compression, which is
**LZO1X** (the Oberhumer LZO1X-1 stream format — see the sibling skill's
`references/rfa-format.md` for the verified evidence). Within a chunked entry each
32 KiB chunk is an LZO1X stream **iff `compressedSize != uncompressedSize`** —
inequality, not less-than, because LZO1X *expands* small or incompressible chunks.
Generic tools (7-Zip, .NET `DeflateStream`) cannot decompress these payloads.

The entry `flags` field is **per-entry**, not an archive-level constant: a real FH
archive contained 246 entries with `0xFFFFFFFF` and 5 with `0x7C001CD8`. Treat it
as opaque. Note that the original `rfaPack.exe -u` **overwrites `flags` and `reserved2` with
zeros** rather than preserving them: an archive's non-zero `flags` values do not survive an
update by the original tool.

## Tool locations

| Copy | Path | Notes |
|---|---|---|
| In-repo (preferred) | `bin\rfaUnpack.exe`, `bin\rfaPack.exe`, `bin\convert.exe` | versioned with the workspace |
| Legacy checkout | `E:\bf_tk_mod\tk_mod\bin\` | same binaries; also has `E:\bf_tk_mod\tk_mod\dist\FH` and `create_and_sync.bat` as working examples |
