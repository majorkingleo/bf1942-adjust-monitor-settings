# AGENTS.md

## Project

`bf1942-adjust-monitor-settings` — a small autotools + `cpputils` C++ tool set,
cross-compiled for Windows with mingw from a Cygwin shell.

Planned work: a command-line-compatible, multi-threaded reimplementation of the
Battlefield 1942 `.rfa` tools (`rfaPack`, `rfaUnpack`). See `PLAN_rfa_tools.md`.

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
* Test harness lives in `testcommon/` (root-level `common.cc`/`common.h` already exist for the
  monitor tools, so `common/` would be ambiguous).
* One `test_<subject>.{cc,h}` pair per subject in `src_test_*/`.
