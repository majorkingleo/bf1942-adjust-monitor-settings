# bf1942-adjust-monitor-settings

Tools for running **Battlefield 1942** on a modern Windows machine, plus a command-line-compatible
reimplementation of the game's `.rfa` archive tools.

Two groups, five programs:

| Program | What it does |
|---|---|
| [`adjust_monitor_settings`](#adjust_monitor_settings) | writes `Video.con` for every profile so the game uses the monitor's real mode |
| [`create_desktop_icons`](#create_desktop_icons) | creates desktop shortcuts for the game and its mods |
| [`list_monitor_resolutions`](#list_monitor_resolutions) | prints the display modes the machine offers |
| [`rfaUnpack`](#rfaunpack) | extracts `.rfa` archives |
| [`rfaPack`](#rfapack) | creates `.rfa` archives |

Windows console applications, cross-compiled with mingw from a Cygwin shell. Every one of them
accepts the same two logging switches — see [Logging](#logging) — and exits **0** on success and
**1** on a usage, argument or I/O failure.

---

## adjust_monitor_settings

The tool this repository started as. The high-resolution patch for BF1942 only works if the
settings in `Video.con` match the monitor exactly — width, height, colour depth *and* refresh
rate. This program regenerates that file for every profile from the mode the display is currently
running.

```
adjust_monitor_settings.exe
```

Run it in the Battlefield folder or in a profile settings folder; it looks for `bf1942.exe` in
`.` and then in `..` and reports what it cannot find.

What it does:

* reads the current mode with `EnumDisplaySettings` and prints it
  (`Monitor: width: … height: … color depth: … refresh rate: …`) — the same mode it writes
* rewrites `Video.con` of every profile under `Mods\bf1942\Settings\Profiles`, skipping `Default`
  and `Custom`, with `game.setGameDisplayMode <width> <height> <depth> <refresh>` followed by the
  graphics block the original tool used (detail texture 5, shadows, environment mapping, graphics
  quality 3, …)
* clears the read-only attribute before writing and sets it again afterwards, so the file cannot be
  modified by accident inside the game
* waits for a key at the end, so a double-clicked window does not close before the message is read
  — including on the failure path, which is why a failure is recorded in a status instead of
  returning straight out of the error handler

The menu is still displayed at a low resolution; that part is the game's, not this tool's.

## create_desktop_icons

Creates `.lnk` shortcuts on the desktop for the game and for the mods installed next to it — one
per `+game` argument, each with the icon from `<battlefield folder>\icons\`:

| Shortcut | Mod |
|---|---|
| `BF1942.lnk` | the base game |
| `BF DC.lnk` | DesertCombat |
| `BF DCX.lnk` | DC_Extended |
| `BF FH.lnk` | Forgotten Hope |
| `BF FHCTF.lnk` | FHTCTFMOD |

```
create_desktop_icons.exe
```

Needs `bf1942.exe` in `.` or `..`, for the same reason as above. It does **not** pause.

## list_monitor_resolutions

Prints every mode the display reports as one `WIDTHxHEIGHT` per line, deduplicated and sorted — a
machine-readable list, so nothing else goes to stdout:

```
list_monitor_resolutions.exe
```

```
640x480
800x600
1280x720
1920x1080
…
```

---

## rfaUnpack

```
rfaUnpack.exe <Archive.rfa> [ExtractToPath] [-option]
```

| Option | Meaning |
|---|---|
| *(none)* | extract everything |
| `-i<index>` | extract the entry at this **0-based** position in the directory table |
| `-f<name>` | extract by name — full internal path, or just the file name |
| `-l<listFile>` | extract every name listed in the file, one per line, **full internal paths with `/`** |

* `ExtractToPath` is the **parent** directory; the archive's internal paths are recreated verbatim
  beneath it. It must already exist, otherwise `Error! Directory does not exist: <dir>` and exit 1.
  Omit it and the current directory is used.
* The 2003 build cannot match anything with `-f`; ours can, which is a deliberate improvement.
* Selection misses (`-i` out of range, a name that cannot be resolved) print an error but exit
  **0**, because the original does and the captured golden pins that.
* Individual failures are non-fatal: the run continues, and existing files are overwritten
  silently.

## rfaPack

```
rfaPack.exe <sourceDir> <PackDirName> <Archive.rfa> [-u] [-Compress] [--lzo-fast]
```

`sourceDir` is the directory whose contents become `<PackDirName>/…` inside the archive: packing
`D:\menu` with the name `menu` yields entries `menu/Texture/…`, `menu/…`, never `menu/menu/…`.

| Option | Meaning |
|---|---|
| *(none)* | **store**: version 0, entries stored raw |
| `-Compress` | **compress**: version 1, entries in independent 32 KiB LZO1X-999 chunks |
| `--lzo-fast` | compress with LZO1X-1 instead of LZO1X-999 |
| `-u` | update an existing archive — **not implemented**, see below |

**Which mode to use.** Store and compress both produce output that is **byte-identical to
`bin\rfaPack.orig.exe`**, so either is safe to hand to the game or to the original tools. Compress
is by far the smaller one — on the shipped 618-entry `menu.rfa` tree, 7.96 MB against 22.95 MB —
and packing it is ~4.7× faster than the original. `--lzo-fast` is for when speed matters more than
size: it is ~3× faster per byte and ~21% larger, and the 2003 tools cannot read its streams (the
game can). It warns on stderr, because that trade must not be silent.

`-u` is recognised and echoed the way the original echoes it, then refused with
`Error! -u is not implemented yet` and exit 1. Asking for an update must not quietly produce a
plain pack.

Entry order, the reserved region, the version and the chatter all follow the original — see
[`.rfa` format](#rfa-format) below.

---

## Logging

Every program installs a logging session as its first action and accepts:

| Option | Effect |
|---|---|
| `--log-file <path>` | append timestamped debug messages to this file |
| `--debug` | also write them to stderr |

With neither switch the messages are dropped and the program's normal output is unchanged — both
backends are opt-in, which matters because `list_monitor_resolutions.exe` prints a
machine-readable list. Both switches are consumed before the program sees its own arguments, and
debug output is compiled out entirely under `NDEBUG`.

The mechanism (an asynchronous publisher/backend pair, one background thread, a deliberately
synchronous shutdown) and its pitfalls are described in `libcommon/` and in
`.github/skills/debug-logging/`.

---

## Build

Requires Cygwin with `make`, `gcc` and the mingw-w64 cross compiler. From a Cygwin login shell:

```sh
./reconfigure.sh      # regenerate the build system, then configure — only after build-file changes
make                  # binaries land in the repository root
make check            # build and run the test suite
make -j"$(nproc)"     # parallel build
```

VS Code tasks for all four exist in `.vscode/tasks.json`.

Four things about this tree are easy to break:

* **A login shell is required** (`bash -lc`) — only then are `make` and `x86_64-w64-mingw32-g++` on
  `PATH`.
* **`tools_config.h` must stay in the repository root.** The `cpputils/` headers reach it as
  `../../../tools_config.h`, which only resolves because `AM_CPPFLAGS` carries one particular `-I`
  entry. Moving the file breaks `cppdir.cc` and everything else that includes it.
* **`reconfigure.sh`, `configure.ac` and `Makefile.am` must keep LF line endings** (enforced by
  `.gitattributes`). With CRLF, `reconfigure.sh` dies under Cygwin and a CRLF `configure.ac` makes
  `configure` fail with a mangled message.
* **`-liconv` is required** when linking: `cpputils/io` converts files with `iconv`.

The original vendor binaries are kept as `bin\rfaPack.orig.exe` and `bin\rfaUnpack.orig.exe`. They
are the **compatibility oracle** — never overwrite them.

## Test suite

`make check` runs `test_rfa.exe`, 91 testcases, modelled on
[cpputilstest](https://github.com/majorkingleo/cpputilstest). A single case runs with
`test_rfa.exe -t <index>`.

The interesting half of the suite compares against the original rather than against ourselves:

* `tests/golden/oracle-cli.json` — the originals' verbatim stdout, stderr and exit codes, captured
  once and re-checkable with `python tools/rfa_golden.py --check`. Compared as a *set of lines*,
  because the originals' line order is not stable under redirection.
* `tests/data/golden/` — a small tree plus the archives `bin\rfaPack.orig.exe` produced from it,
  regenerated with `tools/rfa_golden_archives.py`. Our store pack must match the store archive
  **byte for byte**.

Several rules in the format could only be found this way: a small fixture cannot expose an
ordering rule or an ambiguous chunk, and self-consistency is not correctness. The findings are
numbered and dated in `PLAN_rfa_tools.md`.

## `.rfa` format

Short version, for orientation:

* little-endian; `tocOffset` and `version` in the first 8 bytes, a reserved region, then the data
  blocks, then the directory table
* an entry holds `storedSize`, `uncompressedSize`, `dataOffset` and two opaque fields; names are
  stored without a terminator
* the data block before an entry's payload is either absent (stored raw) or a chunk count followed
  by 12-byte descriptors per 32 KiB chunk
* payloads are **LZO1X-999 at level 8**, one independently compressed stream per 32 KiB chunk
* entries appear in directory-walk order — each directory's own files, sorted, before its
  subdirectories — with names compared after folding to **uppercase**

The full model, with the measurement behind every rule, is in
`.github/skills/bf1942-standalone-map/references/rfa-format.md`.

## Repository layout

| Directory | Contents |
|---|---|
| `libcommon/` | shared code of the monitor tools and the logging session (`libcommon.a`) |
| `src_adjust_monitor_settings/`, `src_create_desktop_icons/`, `src_list_monitor_resolutions/` | the monitor tools, one directory each |
| `rfa/` | `.rfa` container reader/writer and the LZO codec (`librfa.a`) |
| `cli/` | the command lines, as a library so tests can call the same entry points (`libcli.a`) |
| `src_rfaUnpack/`, `src_rfaPack/` | thin program wrappers over `cli/` |
| `testcommon/` | the test harness (`libtestcommon.a`) |
| `src_test_rfa/` | one `test_<subject>.{h,cc}` pair per subject |
| `tests/data/`, `tests/golden/` | fixtures and the captured oracle behaviour |
| `third_party/lzo/` | vendored LZO — five sources holding the LZO1X-999 compressor the archives use |
| `cpputils/` | vendored utility library (a **git submodule**; nothing of ours goes in it) |
| `tools/` | Python probes and generators — oracle driver, format validator, golden capture |
| `.github/skills/` | domain knowledge: `.rfa` unpacking, the standalone-map workflow, debug logging |

`AGENTS.md` holds the conventions and the verified facts a contributor needs before touching
anything; `PLAN_rfa_tools.md` is the working plan and the record of every finding.

## Status

* **Store and compress both produce byte-identical archives** to `rfaPack.exe` — verified on the
  shipped 618-entry `menu.rfa`, the 548-entry `Battle_of_Britain.rfa`, the 251-entry FH Pavlov
  level and the committed golden tree.
* The original tools read everything we write, in both modes, with zero errors.
* **The game loads our archives**: `menu.rfa`, `Battle_of_Britain.rfa` and
  `Battle_Of_Pavlov-1942.rfa` were replaced with our packs, and BF1942 starts and plays.
* Unpacking is ~1.4× faster than the original; packing compressed ~4.7× faster.
* Deliberate differences: `-f` actually works, failures exit non-zero, archives the original
  crashes on are read correctly, and there is an opt-in `--lzo-fast`.
* Not implemented yet: `-u` (update).

## Licence

GPLv3 — see `LICENSE`. The vendored LZO is GPLv2-or-later; its terms are in
`third_party/lzo/COPYING`.
