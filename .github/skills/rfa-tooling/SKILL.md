---
name: rfa-tooling
description: 'Develop, extend and verify the rfaPack / rfaUnpack reimplementation in this repository — our own C++ tools, not the 2003 originals. USE FOR: adding or changing a CLI switch, adding a testcase to src_test_rfa, touching the reader/writer/thread pool in rfa/, proving byte-identity against bin\rfaPack.orig.exe or tests/data/golden, measuring pack/unpack performance, deciding whether new output is still readable by the shipped 2003 tools, or looking up a verified .rfa format fact. DO NOT USE FOR: operating the original .rfa tools on a map or mod archive (use rfa-unpack), assembling a standalone map (use bf1942-standalone-map), or debug logging (use debug-logging).'
---

# Working on the rfaPack / rfaUnpack reimplementation

This repository is a command-line-compatible, multi-threaded reimplementation of RFA Pack 1.7
(`rfaPack.exe` / `rfaUnpack.exe`, shipped with Battlefield 1942 in 2003). Its purpose is
**maximum performance at bit-identical output** — that is the whole reason it exists, so both
halves of that sentence are acceptance criteria, not nice-to-haves.

Three sibling skills cover the neighbouring ground: `rfa-unpack` for *operating* archives with
the original binaries, `bf1942-standalone-map` for the binary container layout
(`references/rfa-format.md`), `debug-logging` for `CPPDEBUG` plumbing.

## Hard rules — each of these has already cost a debugging session

| Rule | Why |
|---|---|
| `cpputils/` is a **git submodule** — never add our files there | changes cannot be committed from this repo |
| `bin\rfaPack.exe` / `bin\rfaUnpack.exe` are the **compatibility oracle** | never overwrite one without keeping a `.orig.exe` backup |
| `.sh`, `.ac`, `.am` must keep **LF** (`.gitattributes`) | CRLF breaks `configure` with an unreadable message |
| `make` alone builds nothing named `test_rfa.exe` | use `make check`; the suite is only built by that target |
| `tests/data/golden/tree/**` is `-text` in `.gitattributes` | two files are deliberately CRLF and the oracle archives captured their sizes; conversion makes our writer look 2 bytes short |
| Our own headers open with `#pragma once` (C++23) | `cpputils/` keeps whatever it has — do not "fix" it |
| One `main()` try/catch per program, failure = **exit 1** | `rfaUnpack` keeps exit 0 for a selection miss on purpose: `tests/golden/oracle-cli.json` pins that |

## The pieces

```
libcommon/        libcommon.a     shared monitor-tool code + the debug-logging session
cli/              libcli.a        PackCli/UnpackCli + ThreadSwitch.h, the command lines
rfa/              librfa.a        RfaFormat, RfaStamp, LzoCodec, RfaArchive, RfaWriter,
                                  ParallelFor.h, CpuCount
src_rfaPack/      rfaPack.exe      thin wrapper over cli::run_pack()
src_rfaUnpack/    rfaUnpack.exe    thin wrapper over cli::run_unpack()
src_test_rfa/     test_rfa.exe     one test_<subject>.{h,cc} pair per subject
testcommon/       libtestcommon.a  the harness
third_party/lzo/  liblzo.a         LZO 2.10 - supplies LZO1X-999 (the archives' codec)
tools/                             Python probes, golden generators, the stamp generator
```

`cli/` is a **library**, not a program, so the tests drive the same entry points the binaries
do. Never spawn our own binaries from a test, and never `std::system` the oracle from one
either: it goes through cmd.exe, which mangles both forward-slash and absolute paths.

## Build and test

Run the VS Code tasks (`make`, `make check`, `make -j`, `reconfigure`) or, from PowerShell:

```powershell
C:\cygwin64\bin\bash.exe -lc "cd 'e:\progs\bf1942-adjust-monitor-settings' && make check"
```

A **login** shell (`-lc`) is required — only then are `make` and `x86_64-w64-mingw32-g++` on
PATH. Commit from **PowerShell**, not from Cygwin bash: Cygwin has its own `$HOME`, so `git`
there fails with "Author identity unknown". `ChangeLog` is a `git log` dump that is regenerated
and committed with each change; generate it *before* committing (LF, no BOM), never while
amending a commit.

`test_rfa.exe -t <idx>` runs one case; `-t 99` exits 1 by design, which is how the suite proves
it can report failure. Fixtures: `tests/data/` (real archives), `tests/data/golden/` (the
writer's oracle). Tests resolve `tests/data` relative to the current directory, so run them from
the repo root or set `RFA_TEST_DATA_DIR`.

## Shipping to the consumer repository

These binaries are used outside this repository too: `E:\progs\bf_pablov_mod` (a **separate git
repository**, the KWG mod) runs them as `bin\rfaPack.exe` / `bin\rfaUnpack.exe`, with the 2004
originals kept beside them as `*.orig.exe`. That repository carries its own skill, `rfa-pack-unpack`,
which documents the swap, the symptom table and how to fall back.

Two tasks drive it, and `tools/deploy_rfa_tools.ps1` does the work:

| Task | What it does |
|---|---|
| `deploy rfa tools` | builds, strips, installs both binaries, keeps the originals, verifies what landed by hash, and rewrites the hash table in the consumer's skill |
| `verify deployed rfa tools` | runs the consumer's `Test-RfaTools.ps1` against a real 20 MB level archive, comparing our output with the 2004 originals file by file |

Strip before copying — the build carries `-g` debug info, 31 MB per binary against 2 MB stripped. The
script also zeroes the PE `TimeDateStamp` and `CheckSum`, because `strip` stamps the header with the
*current* time: without that, identical source deploys different bytes on every run and the
consumer's git shows a binary change that means nothing. Verified by forcing a relink in between —
the deployed hash does not move.

## The CLI contract, as ours differs from the original

```
rfaUnpack.exe <Archive.rfa> [ExtractToPath] [-i<index>] [-f<name>] [-l<list.lst>] [--threads N]
rfaPack.exe   <sourceDir> <baseFolderName> <archive.rfa> [-u] [-Compress] [--lzo-fast] [--threads N]
```

* The original's switches behave exactly as `rfa-unpack/references/rfaunpack-cli.md` records,
  including its quirks. `ExtractToPath` must already exist.
* `--threads N` (`--threads=N`, `-j N`, `-jN`; long form case-insensitive) is **ours**. `0` is
  the default: the CPU count for packing, `max(1, cpus/3)` for unpacking. See
  [references/internals.md](./references/internals.md).
* `--lzo-fast` is **ours**: LZO1X-1 instead of the era's LZO1X-999. Faster, ~21% larger, and the
  shipped 2003 tools *cannot* read its streams (the game can). It is the only thing this program
  writes to **stderr**; a warning must stay distinguishable from a refusal by the stream alone.
* Entry names are `"<base folder name>/" + relative path`. `RfaPack.exe <dir> menu out.rfa`
  stores `menu/...`, and extraction therefore lands in `<target>/menu/...`. Tests find extracted
  files with `tree_files(target)` rather than guessing a path.
* Three switches consume a **separated** value (`--threads`, `-j`) — the parser must advance the
  index, or the value silently becomes a positional: the base folder name in `rfaPack`, the
  extract directory in `rfaUnpack`. `cli/ThreadSwitch.h` exists to keep that in one place.

## Proof obligations

Any change to the writer, the reader or the CLI has to survive four checks, all of them in
[references/verification.md](./references/verification.md) with the exact commands:

1. **Store mode is byte-identical** to `rfaPack.exe`'s archive — always, including the entry
   table, `flags` and the reserved region.
2. **Compress mode is byte-identical** to it as well, *except* for zero-length entries — see the
   divergence below. Same scheme (LZO1X-999 level 8), same sizes, same opaque fields.
3. **The thread count cannot change a single byte.** Two worker counts have to produce the same
   archive, or the switch is a correctness hazard rather than a performance knob. No byte
   comparison against the oracle catches this — compare our own runs with each other.
4. **The 2003 tools can still read what we wrote.** `bin\rfaUnpack.orig.exe` exits 0 *even when
   it prints `ERROR! CRASH  Decompression()!`*, so the exit code proves nothing: grep its output
   for `CRASH` and count the extracted files.

`scripts/verify.ps1` runs 1-4 against the committed golden fixture:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .github\skills\rfa-tooling\scripts\verify.ps1
```

## Known divergence: zero-length entries under `-Compress`

**The one case where we do not reproduce the era encoder.** For a zero-byte input, LZO 2.10's 999
emits **3 bytes** (`11 00 00`) where the 2003 encoder emits **4** (`11 11 00 00`). The entry's
block is therefore 19 bytes instead of 20 and every following `dataOffset` shifts by one.

Measured on `tests/data/golden/tree` (which contains a `menu/empty.txt`):

| archive | SHA-256 | 2003 reader |
|---|---|---|
| `oracle-compress.rfa` | `272419FEB4DD8D96…` (15943 B) | clean |
| ours | `C58BF87EE1E86F3C…` (15942 B) | `ERROR! CRASH  Decompression()!` |

Remove that one file and the same tree packs **byte-identically** (`D98B5BA80B974BB4…`) and the
2003 reader is clean — so the whole difference is the empty entry, not match selection. `-Compress`
on a tree without empty files (the 618-entry shipping menu tree, for instance) is byte-identical
today. Until this is fixed, "our compressed output is readable by the shipped tools" holds only
for trees without empty entries; `verify.ps1 -Strict` fails on the golden tree on purpose.

## Measurements worth not re-deriving

Ryzen 9 7900X, 12 physical / 24 logical, Defender real-time protection **on**.

* Packing a 110.1 MB / 550-file tree: ours **0.420 s** vs the oracle's **10.003 s** = 23.8x.
* Unpacking the 618-entry menu tree: **0.117 s** vs the original's 0.239 s = 2.04x, at 2.14 of
  the 8 allowed cores.
* Compressing costs ~68 ms CPU per MB, decompressing ~0.7 ms per MB — a ~100x asymmetry, which
  is why unpack can never gain what pack gained.
* Creating files saturates at ~4 concurrent writers (2.13x over one, 2.07x at eight): the ceiling
  is the virus scanner's filter driver plus filesystem metadata, not our threads. That is the
  reason the unpacking default is a *fraction* of the CPU count.
* **The measurement environment matters.** A Cygwin-started shell participates in the numbers and
  was rejected for benchmarking; run native binaries from PowerShell. `examples/` is gitignored,
  so benchmarks live there as throwaway scratch and must never be referenced as if they shipped.

## Adding a switch

1. Parse it in `cli/PackCli.cc` or `cli/UnpackCli.cc` — before the `-Xvalue` handling if it may
   carry a separated value.
2. Echo it only when it was given, or the golden CLI chatter in `tests/golden/oracle-cli.json`
   changes for every existing scenario. Echo the **effective** value, not the requested one.
3. Refuse an unusable value with exit 1 on **stdout** (that is where the original's argument
   errors go) and write nothing, so a caller cannot mistake it for success.
4. Add a testcase to `src_test_rfa/test_rfa_cli.{h,cc}`, register it in
   `src_test_rfa/test_rfa.cc`, and place the switch in the **middle** of the argument list so a
   value that was not consumed is caught.
5. `make check`, then the four proof obligations above.

Findings, measurements and the reasoning behind every format rule live in `PLAN_rfa_tools.md`
(numbered; the code comments cite them as "finding N"). Read the relevant finding before
changing behaviour it describes — several rules there are counter-intuitive on purpose.
