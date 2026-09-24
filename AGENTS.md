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
| `libcommon/` | `libcommon.a` — shared code of the monitor tools (`common.cc`, `common.h`) |
| `src_adjust_monitor_settings/` | `adjust_monitor_settings.cc` |
| `src_create_desktop_icons/` | `create_desktop_icons.cc`, `ShortcutProvider.{h,cc}` |
| `src_list_monitor_resolutions/` | `list_monitor_resolutions.cc` |
| `rfa/` | `librfa.a` — `.rfa` container reader/writer plus the LZO1X wrapper |
| `testcommon/` | harness library for `make check` |
| `src_test_rfa/` | one `test_<subject>.{h,cc}` pair per subject |
| `third_party/minilzo/` | vendored miniLZO (needs its own `CPPFLAGS`, see below) |

`tools_config.h` must stay in the **repo root**. `cpputils/` headers reach it with
`#include "../../../tools_config.h"`, which only resolves because the command line carries
`-I$(top_srcdir)/cpputils/cpputilsshared/cpputilsformat` — one level deeper than
`cpputilsshared/`, so the three `..` land exactly on the root. Moving the header or dropping
that `-I` breaks `cppdir.cc` (and every other cpputils source including it) with
`fatal error: ../../../tools_config.h: No such file or directory`. The `-I$(top_srcdir)/src`
entry is a leftover from the upstream project and points at nothing.

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
* `AM_CPPFLAGS` contains `-std=c++20`, which is invalid for a C translation unit.
  Any `.c` file added to this tree needs its own per-target `CPPFLAGS`, as
  `third_party/minilzo/libminilzo.a` does.
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

* RFA payload compression is **LZO1X** (Oberhumer LZO1X-1), applied **independently per
  32 KiB chunk**. It is **not** zlib/deflate, not FastLZ, not RefPack. miniLZO's
  `lzo1x_1_compress` / `lzo1x_decompress` are compatible.
* ⚠️ The `RefractorForge` "RFA_Format_Notes" found online describe a **different** custom
  LZ77 codec for **Battlefield Vietnam**. They do **not** apply to BF1942.
* The per-entry data block has **two variants** and you must choose between them from the
  sizes, *not* from `version`:
  * `storedSize == uncompressedSize` (or `uncompressedSize == 0`) → **raw**, no block
    header, payload starts at `dataOffset`;
  * otherwise → the first u32 at `dataOffset` is a **chunk count**, followed by 12-byte
    chunk descriptors, then the concatenated per-chunk LZO1X payloads.
  Do not assume a constant `tag == 1` header — that mistake is invisible on small files.
* A chunk is an **LZO1X stream iff `compressedSize != uncompressedSize`** — inequality,
  not less-than. LZO1X often *expands* small or incompressible input (1 byte → 5), and a
  less-than test misreads those chunks as verbatim, returning compressed bytes as content.
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
* Test harness lives in `testcommon/`; the monitor tools' shared code lives in `libcommon/`.
  `AM_CPPFLAGS` carries `-I$(top_srcdir)/libcommon`, so each tool keeps its plain
  `#include "common.h"` from inside its own `src_<program>/` directory.
* One directory per subject: `src_<program>/` for a program, `lib<name>/` for a library.
  New shared code goes into `libcommon/`, never into a program directory.
* One `test_<subject>.{cc,h}` pair per subject in `src_test_*/`.
