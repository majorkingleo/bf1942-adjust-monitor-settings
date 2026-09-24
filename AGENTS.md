# AGENTS.md

## Project

`bf1942-adjust-monitor-settings` — a small autotools + `cpputils` C++ tool set,
cross-compiled for Windows with mingw from a Cygwin shell.

Planned work: a command-line-compatible, multi-threaded reimplementation of the
Battlefield 1942 `.rfa` tools (`rfaPack`, `rfaUnpack`). See `PLAN_rfa_tools.md`.

## Layout

One directory per subject. Binaries still land in the **repo root** — there are no `SUBDIRS`
Makefiles, so `bin_PROGRAMS` links next to the top-level `Makefile`.

| Directory | Contents |
|---|---|
| `libcommon/` | `libcommon.a` — shared code of the monitor tools (`common.cc`, `common.h`) plus the debug logging session (`AsyncOutDebug.*`, `AsyncFileLogger.*`, `DebugLog.*`) |
| `src_adjust_monitor_settings/` | `adjust_monitor_settings.cc` |
| `src_create_desktop_icons/` | `create_desktop_icons.cc`, `ShortcutProvider.{h,cc}` |
| `src_list_monitor_resolutions/` | `list_monitor_resolutions.cc` |
| `cli/` | `libcli.a` — the command lines of the `.rfa` tools (`UnpackCli.*`, `PackCli.*` to come). A **library**, so the tests can call the same entry points the programs call: spawning the binaries from a test is not viable (`PLAN_rfa_tools.md` finding 19) |
| `rfa/` | `librfa.a` — `.rfa` container reader/writer plus the LZO1X wrapper |
| `src_rfaUnpack/` | `rfaUnpack.cc` — wrapper over `cli/` |
| `testcommon/` | harness library for `make check` |
| `src_test_rfa/` | one `test_<subject>.{h,cc}` pair per subject |
| `third_party/lzo/` | vendored LZO — five sources plus their header closure, holding the **LZO1X-999** compressor the archives actually use (needs its own `CPPFLAGS`, see below) |

`tools_config.h` must stay in the **repo root**. `cpputils/` headers reach it with
`#include "../../../tools_config.h"`, which only resolves because the command line carries
`-I$(top_srcdir)/cpputils/cpputilsshared/cpputilsformat` — one level deeper than
`cpputilsshared/`, so the three `..` land exactly on the root. Moving the header or dropping
that `-I` breaks `cppdir.cc` (and every other cpputils source including it) with
`fatal error: ../../../tools_config.h: No such file or directory`. The `-I$(top_srcdir)/src`
entry is a leftover from the upstream project and points at nothing.

## Debug logging

Every tool installs a logging session as the **first** statement of `main()`:

```cpp
const ToolLog::Session log( ToolLog::log_file_from_argv( argc, argv ),
                            ToolLog::debug_flag_from_argv( argc, argv ) );
```

`--log-file <path>` adds a file backend, `--debug` adds a console backend. With neither, the
frontend has no subscriber and messages are dropped — both are opt-in, so the existing
output stays as it is (`list_monitor_resolutions.exe` prints a machine-readable list).
Messages are emitted as `CPPDEBUG( Tools::format( ... ) )` and vanish entirely under
`NDEBUG`. The session owns its thread and joins it in the destructor, which is the one
deliberate difference from the reference implementation in `examples/lotr_analyzer`.

Mechanism, design notes and pitfalls: `.github/skills/debug-logging/`.

## Build

The build runs GNU make under Cygwin (`.vscode/tasks.json` → task `make`, default build task):

```
C:\cygwin64\bin\bash.exe -lc "cd '<workspace>' && make"
```

* A **login** shell (`-lc`) is required — only then are `make` and
  `x86_64-w64-mingw32-g++` on `PATH`.
* `reconfigure.sh` regenerates the build system (`aclocal`, `automake`, `autoconf`,
  `configure`). It must keep **LF** line endings (enforced by `.gitattributes`): with CRLF
  it fails under Cygwin, and a CRLF `configure.ac` breaks `configure` outright with a
  message the stray CR then mangles beyond recognition.
* `make check` builds and runs the test suite (VS Code task `make check`). Harness is in
  `testcommon/`; subjects live in `src_test_rfa/` as `test_<subject>.{h,cc}` pairs and are
  registered in `src_test_rfa/test_rfa.cc`. Run one case with `test_rfa.exe -t <idx>`.
* `tests/data/` holds the real fixtures; `tests/data/golden/` holds a small source tree plus
  the archives `bin\rfaPack.orig.exe` produced from it — the writer's oracle, compared
  byte-for-byte in store mode and semantically in compress mode. Regenerate with
  `tools/rfa_golden_archives.py`. Tests never launch the oracle binaries themselves:
  `std::system` goes through cmd.exe, which mangles both forward-slash and absolute paths.
* `AM_CPPFLAGS` contains `-std=c++23`, which is invalid for a C translation unit.
  Any `.c` file added to this tree needs its own per-target `CPPFLAGS`, as
  `third_party/lzo/liblzo.a` does.
* `cpputils/io` is not part of `libcpputilsshared.a`; this tree builds it separately as
  `cpputils/io/libcpputilsio.a` (CpputilsDebug, OutDebug, DetectLocale, ColoredOutput,
  read_file). It needs **`-liconv`** in `LIBS`: `read_file.cc` implements
  `ReadFile::convert` with `iconv` and `DetectLocale` calls it, so the link fails with
  ``undefined reference to `iconv_open'`` without it. The Cygwin mingw-w64 sysroot ships
  `libiconv.a`, which `-static` prefers over `libiconv.dll.a`.
* The suite must be able to report failure: `test_rfa.exe -t 99` exits 1 by design.

## Skills

Domain knowledge lives in `.github/skills/`:

| Skill | Use for |
|---|---|
| `rfa-unpack/` | unpack / selectively extract / repack `.rfa` archives; CLI contract for `rfaPack`/`rfaUnpack` |
| `bf1942-standalone-map/` | RFA binary format (`references/rfa-format.md`) plus listing/assembly helper scripts |

Read `rfa-unpack/references/rfaunpack-cli.md` for the verified CLI behaviour, and
`bf1942-standalone-map/references/rfa-format.md` for the container layout.

## Critical facts (verified — do not re-derive from the internet)

* RFA payload compression is **LZO1X**, applied **independently per 32 KiB chunk**. It is
  **not** zlib/deflate, not FastLZ, not RefPack.
* ⚠️ The compressor is **LZO1X-999 at compression level 8**, *not* LZO1X-1. Measured: for
  200 bytes of `ab` the era encoder emits 10 bytes where `lzo1x_1` emits 29, and on the `menu`
  tree 999 is 21% smaller — our `-Compress` output is byte-identical to `rfaPack.orig.exe`'s
  only because we use 999 (finding 31). `--lzo-fast` selects LZO1X-1 deliberately: ~3x faster,
  visibly larger, and its streams are **not** readable by the shipped 2003 tools (finding 27).
  The game reads either.
* ⚠️ The `RefractorForge` "RFA_Format_Notes" found online describe a **different** custom
  LZ77 codec for **Battlefield Vietnam**. They do **not** apply to BF1942.
* The per-entry data block has **two variants** and you must choose between them from the
  sizes, *not* from `version`:
  * `storedSize == uncompressedSize` (or `uncompressedSize == 0`) → **raw**, no block
    header, payload starts at `dataOffset`;
  * otherwise → the first u32 at `dataOffset` is a **chunk count**, followed by 12-byte
    chunk descriptors, then the concatenated per-chunk LZO1X payloads.
  Do not assume a constant `tag == 1` header — that mistake is invisible on small files.
* A chunk is an **LZO1X stream** unless the sizes prove otherwise. The size test is
  **inequality** (`compressedSize != uncompressedSize`), not less-than: LZO1X often *expands*
  small or incompressible input (1 byte → 5), and a less-than test reads those chunks as
  verbatim, handing back compressed bytes as content. Inequality has a converse failure too,
  though — LZO1X can compress a chunk into *exactly* as many bytes as it started with, and a
  shipping archive does it (`Battle_of_Britain.rfa`, `Willy.con`: a 30-byte stream for a
  30-byte file). So the sizes are a **hint**: when they are equal, the codec decides —
  `PayloadReader` tries LZO first and copies verbatim only when it refuses, which is safe
  because `lzo1x_decompress_safe` rejects non-streams and insists on the expected output
  length. Finding 30.
* `version` is `0` or `1` and does **not** select the payload layout; both occur, and
  235 raw entries live in version-1 archives. `rfaPack.exe` writes 0 without `-Compress`
  and 1 with it. A version-1 archive containing a raw non-empty entry makes the original
  `rfaUnpack.exe` crash, so never write that combination.
* Entry order is each directory's own files (sorted) then its subdirectories (sorted),
  recursively — **not** ASCII-ascending full paths.
* Entry `nameLen` is the exact name length — there is **no** NUL terminator in the stream.
* The entry `flags` field is **per-entry**, not an archive constant. Treat it as opaque. Note
  that the original `rfaPack.exe -u` **overwrites** it (and `reserved2`) with zeros — it does
  not preserve them; see `PLAN_rfa_tools.md` §2.5 / D6.
* `rfaPack.exe -u` is byte-for-byte a **fresh pack of the source tree using the target
  archive's compression policy** (`-Compress` is ignored on `-u`), plus entries carried over
  for names absent from the tree. It never deletes, and it exits 0 regardless.
* The original `bin\rfaPack.exe` / `bin\rfaUnpack.exe` are the **compatibility oracle** for
  tests. Never overwrite them without keeping a `.orig.exe` backup.

## Conventions

* `cpputils/` is a **git submodule** (`.gitmodules` is tracked and `git submodule status`
  resolves it) — do not add our own files inside it. Third-party code we vendor goes in
  `third_party/`.
* Language standard is **C++23** (`-std=c++23` in `AM_CPPFLAGS`). Our own headers open with
  `#pragma once` instead of include guards; `cpputils/` keeps whatever it already has.
* Test harness lives in `testcommon/`; the monitor tools' shared code lives in `libcommon/`.
  `AM_CPPFLAGS` carries `-I$(top_srcdir)/libcommon`, so each tool keeps its plain
  `#include "common.h"` from inside its own `src_<program>/` directory.
* One directory per subject: `src_<program>/` for a program, `lib<name>/` for a library.
  New shared code goes into `libcommon/`, never into a program directory.
* Every `main()` is a try/catch boundary: report to `std::cerr` and **return 1** on failure.
  0 means success, 1 means a usage, argument or I/O failure. The monitor tools used to fall
  out of `main()` with 0 even after printing an error, which made a failure indistinguishable
  from success. `rfaUnpack` keeps exiting 0 for a selection miss (`-i` out of range, a name it
  cannot resolve) because `tests/golden/oracle-cli.json` pins the original's behaviour there.
  `adjust_monitor_settings` records the failure in a status and still runs its `system("PAUSE")`,
  so a double-clicked window does not close before the message is read.
* One `test_<subject>.{cc,h}` pair per subject in `src_test_*/`.
