# Plan: `rfaPack` / `rfaUnpack` reimplementation + cpputilstest-style test suite

Project home: `E:\progs\bf1942-adjust-monitor-settings`
Deploy target: `E:\progs\bf_pablov_mod\bin\rfaPack.exe`, `rfaUnpack.exe`

---

## 1. Goal

Replace the legacy BF1942 `.rfa` tools with a C++ reimplementation that is:

1. **CLI compatible** with the originals (same arguments, same behaviour, same messages).
2. **Multi-threaded** — uses all CPU cores.
3. **Extended** — new opt-in features are allowed (file replacement / in-place update, fixed `-f`, etc.).
4. **Tested** by an autotools test suite modelled on <https://github.com/majorkingleo/cpputilstest>.
5. **Deployed** into `E:\progs\bf_pablov_mod\bin`, with the original binaries kept as a test oracle.

---

## 2. Research findings (verified, not assumed)

These change what the old documentation says. All were checked on this machine.

### 2.1 `rfaPack.exe` / `rfaUnpack.exe`
| | |
|---|---|
| Path (dev copy) | `bin\rfaPack.exe` (90,112 B), `bin\rfaUnpack.exe` (86,016 B) |
| Identical copies | `E:\progs\bf_pablov_mod\bin\` |
| Console app | yes — writes the usage banner and progress chatter to stdout |
| CLI spec | `bin\Readme.txt` (647 B) |
| Value | **keep as golden oracle** — never overwrite without a backup copy |

### 2.2 Container format — re-validated against 814 archives / 448,127 entries

Machine-validated on 2026-09-23 across **every** `.rfa` in the `bf_pablov_mod` tree
(814 archives, 448,127 entries): **99.99% structurally consistent** with the model
below. Tooling: `tools/rfa_probe.py` (dumper + validator).

```
[0]  u32  tocOffset          (directoryOffset)
[4]  u32  version            (0 or 1 - does NOT select the payload layout)
[8 .. tocOffset)             data blocks. The first block is commonly at offset 156;
                             the bytes before it are an opaque fixed region that no
                             entry references.
[tocOffset]                  directory:
                               u32 fileCount
                               fileCount x entry
                               u32 0                       // terminator, table runs to EOF
entry:
    u32  nameLen             // EXACT string length, NO NUL terminator
    char name[nameLen]       // e.g. "bf1942/levels/Battle_Of_Pavlov-1942/Conquest.con"
    u32  storedSize
    u32  uncompressedSize
    u32  dataOffset          // absolute offset of this entry's data block
    u32  reserved            // 0 in the archives observed
    u32  reserved
    u32  flags               // per-entry, NOT a constant
```

**Payload layout - two variants, chosen per entry from the sizes, not from `version`:**

*Raw, no block header* — when `storedSize == uncompressedSize`, or
`uncompressedSize == 0` (which stores 4 bytes). The payload sits directly at
`dataOffset`, `storedSize` bytes long. Seen 34,617 times: 34,382 in version-0 archives
**and 235 inside version-1 archives**, so version is not a usable discriminator.

*Chunked / LZO1X* — the common case, 413,510 entries:

```
u32 chunkCount
u32 chunk[0].compressedSize
u32 chunk[0].uncompressedSize
u32 chunk[0].payloadOffset      // always 0
(chunkCount - 1) x { u32 compressedSize; u32 uncompressedSize; u32 payloadOffset }
byte[sum(compressedSize)] payload   // chunks concatenated, in order
```

Verified invariants:

* `chunkCount == ceil(uncompressedSize / 32768)` — chunks are **32 KiB**; every chunk
  but the last has `uncompressedSize == 32768`
* `storedSize == 16 + 12 * (chunkCount - 1) + sum(compressedSize)`
* `sum(chunk.uncompressedSize) == entry.uncompressedSize`
* `payloadOffset[i] == sum(compressedSize[0..i-1])`
* each chunk is independently LZO1X if `compressedSize < uncompressedSize`
* `compressedSize` may exceed `uncompressedSize` for incompressible chunks

> ⚠️ **This corrects the first pass of this plan.** The 16-byte prefix is *not* a
> constant `tag == 1` sub-header holding one payload: the first field is the
> **chunk count**, and the descriptor table grows by 12 bytes per extra chunk. The
> error is invisible on single-chunk entries — which is exactly what the small
> fixtures contain — so it survived the original probe matrix, a ground-truth
> extraction and two rounds of doc review. Only the 814-archive sweep exposed it.

Entries are ordered by **directory walk, not by full path**: each directory contributes its
own files (sorted by name) and only then its subdirectories (sorted by name), recursively.
An ASCII sort of full paths agrees on flat archives and diverges on nested ones — see
finding 16. `flags` is per-entry; observed values are `0xFFFFFFFF`, `0x7C001CD8`,
`0x77FCB6DE`, `0x00000000`, and `rfaPack.exe` writes `0` which the engine still loads — so
treat it as opaque. The original's `-u` zeroes it rather than preserving it (§2.5).

**Documentation corrections required** (the workspace's own reference docs were wrong):

| Old claim | Reality |
|---|---|
| "`nameLen` includes the NUL terminator" | exact length; no NUL is written |
| "`version` is always 1" | `0` and `1` both occur; version 0 archives are always raw-payload |
| "block header is `tag(=1), payloadSize, uncompressedSize, reserved`" | **`chunkCount` + 12-byte chunk descriptors** |
| "`payloadSize == uncompressedSize` → payload stored raw" (still with a header) | `storedSize == uncompressedSize` → **no header at all** |
| "`flags` is an archive-level constant" | per-entry; varies within a single archive |
| "`0x028A0220` for base game archives" | misreading — the real value is `0x7C001CD8` |
| "custom compression algorithm (not zlib)" | **LZO1X**, applied per 32 KiB chunk (see 2.3) |

### 2.3 Compression is **LZO1X** — confirmed two independent ways

**a) Reference implementation.** The working open-source RFA tool `yann-papouin/bga`
(a WinRFA replacement) bundles **miniLZO** and calls `lzo1x_1_compress` /
`lzo1x_decompress` for RFA payloads, crediting Oberhumer's LZO library.

**b) Own controlled experiment.** Packed incompressible N-byte files with the
original `rfaPack.exe -Compress`. These inputs are smaller than one chunk, so each
produced a **single-chunk** block and `compressedSize` is that chunk's size:

| input N | compressedSize | first byte | rule |
|---|---|---|---|
| 20 | 24 | `0x25` (37) | 20 + 17 |
| 30 | 34 | `0x2f` (47) | 30 + 17 |
| 40 | 44 | `0x39` (57) | 40 + 17 |
| 60 | 64 | `0x4d` (77) | 60 + 17 |
| 100 | 104 | `0x75` (117) | 100 + 17 |

That is exactly LZO1X's literal-run rule (`count = byte − 17`), and `compressedSize = N + 4`
(1 length byte + N literals + the 3-byte LZO1X end marker `11 00 00`).
Cross-checked on the real FH archive: first entry decodes as opcode `0x30` → 31 literals
`"Game.setNumberOfTickets 1 115\r\n"`, matching the ground-truth file extracted with
`rfaUnpack.exe` (373 bytes).

> ⚠️ **Do not use the `RefractorForge` RFA notes** found on the web. They describe a
> *different* custom LZ77 codec for **Battlefield Vietnam** and state it is not stock LZO.
> BF1942 is LZO1X.

**Consequence for the "all CPUs" requirement:** LZO1X compress/decompress is
*stateless* apart from a caller-supplied work buffer (`LZO1X_1_MEM_COMPRESS`).
One work buffer per thread ⇒ trivially parallel, no locks, deterministic output.
This is the key enabler for the multi-core requirement.

### 2.4 CLI behaviour of the original (probe matrix already recorded)
```
rfaUnpack.exe <archive.rfa> [ExtractToPath] [-i<index>] [-f<name>] [-l<listfile>]
rfaPack.exe   <sourceDir> <baseFolderName> <archive.rfa> [-u] [-Compress]
```
* No args → usage banner.
* `ExtractToPath` must **already exist**, else `Error! Directory does not exist: <dir>`.
* `-i` is a 0-based file-table index; out of range → `ERROR! item out of range to extract! <n>`.
* `-l` needs **full internal paths with `/`**; a plain basename → `ERROR! Could not ExtractByName: X / Name not found!`.
* `-l` splits entries on whitespace ⇒ names containing spaces are unreachable via a list (must use `-i`).
* **`-f` is broken** in the shipped build — it always reports `Name not found!`.
* Failures are non-fatal; the run continues and overwrites silently.
* `ExtractToPath` is a **parent** — internal paths are recreated verbatim beneath it.

### 2.5 Update (`-u`) semantics — probed, not guessed

Nothing in the vendor readme or in the CLI chatter explains `-u`, so it was probed with
controlled archives and trees (`tools/rfa_probe_update.py`; 9 cases, each starting from a
freshly packed baseline, each result re-extracted by the original to prove it stays
readable). Every outcome is explained by **one** rule:

> **`-u` behaves exactly like a fresh pack of the source tree, except that entries already in
the target archive whose names are absent from the tree are carried over unchanged. The
compression policy comes from the target archive, not from `-Compress`.**

This was verified by byte comparison, not by inference: in all 9 cases the result is
byte-identical to `rfaPack.exe <tree> <base> <out.rfa>` run fresh with the *target archive's*
policy (store for a version-0 target, `-Compress` for a version-1 target). The only
non-matching case is the deletion case, where the fresh pack of the reduced tree legitimately
lacks the retained entry.

| # | Question | Answer |
|---|---|---|
| 1 | Append a file absent from the archive? | Yes, inserted in canonical walk order |
| 2 | Replace a file whose content changed but whose size did not? | Yes — content is compared, not size |
| 3 | Replace a file that grew? | Yes; the following entries shift |
| 4 | Delete an entry missing from the tree? | **No** — retained. Extraction returned 3 files for a 2-file tree, so this is proven by the oracle, not by inspection |
| 5 | `-u` on an unchanged tree | Byte-identical no-op |
| 6 | `-Compress` on `-u`, store target | **Ignored** — entries stay raw, `version` stays 0 |
| 7 | Plain `-u`, compress target | **Ignored** — new entries come out compressed, `version` stays 1 |
| 8 | `-u` with no existing archive | Creates it, exactly like a plain pack; no error |
| 9 | Exit code | 0 in every case, including the deletion case |
| 10 | Base name differs from the archive's own | **Every retained entry is renamed** — see below |
| 11 | Reserved region on a real FH archive | **Replaced** with the standard 148-byte stamp at offset 156 — see below |

Consequences for the reimplementation:

* **`-u` needs no in-place update algorithm.** It is "fresh pack with the policy read from the
  target, plus carried-over entries" — a far smaller and more testable feature than a patcher,
  and it inherits the byte-identity we already have for fresh packs.
* ⚠️ **The reserved region is NOT preserved.** On `tests/data/fh/Battle_Of_Pavlov-1942.rfa`
  the region runs from offset 8 to **4086** (4,078 bytes, sha256 `131D3DBF...`) and the first
  data block starts there. After one `-u` the region is the standard **148 bytes at offset
  156** (sha256 `13BA8D23...`) — the archive was rebuilt on the packer's own layout and
  ~3.9 KB of producer-specific bytes were discarded. The earlier claim that an in-place
  update *must* preserve those bytes was wrong. Our self-produced archives cannot show this,
  because there the region already is the constant, so "preserved" and "rewritten with our
  stamp" are indistinguishable.
* ⚠️ **Carried-over entries are RENAMED, and the rule is `newBase + "/" + storedName.substr(strlen(newBase) + 1)`.**
  Verified by prediction and measurement on the FH archive, which stores
  `bf1942/levels/...`:

  | `base` argument | resulting name of the retained entry |
  |---|---|
  | `bf1942` (matches the archive) | `bf1942/levels/...` — unchanged, the documented case works |
  | `menu` (shorter) | `menu/2/levels/...` — the stray `2` is the tail of the eaten `bf194` |
  | `bf1942x` (longer) | `bf1942x/evels/...` — one character too many was consumed |

  So `-u` silently renames every entry whenever the base name differs from the archive's own,
  and nobody notices because the documented workflow always passes the matching name. We must
  **not** reproduce this: we carry entries over verbatim, name included. Byte-identity with the
  original then holds exactly where the original is correct (matching base) and diverges only
  where it corrupts names. See D7.
* ⚠️ **Opaque per-entry fields are NOT preserved.** Setting `flags` to `0xFFFFFFFF` and
  `reserved2` to `0xDEADBEEF` on every entry and then running `-u` reset all of them to `0` —
  the values a fresh pack writes. On the FH archive, whose entries carry `0xFFFFFFFF` and
  `0x7C001CD8`, a single `-u` collapsed every one of them to `0`. This contradicts the advice
  in `AGENTS.md` and the skills ("treat `flags` as opaque and preserve it on `-u`"). See D6.
* `reserved1` is `1253856` = `0x001321E0` (the FH archive stores `0` here), and it survives only
  because the packer always writes that constant — nothing is preserved on its behalf.
* Table layout, confirmed by hex-dumping the oracle's own output: `u32 entryCount`, then per
  entry `u32 nameLen + name + 24 bytes`, then a trailing `u32` that is zero.

### 2.6 Existing project + test-suite template
* This repo is autotools + vendored `cpputils/`, cross-compiled with mingw through Cygwin
  (`make` task already added to `.vscode/tasks.json`, verified working).
* `cpputilstest` layout to mirror:
  ```
  common/                      TestUtils.{h,cc}, ColBuilder.{h,cc}      <- shared harness
  src_test_<component>/        test_<subject>.{cc,h} pairs             <- one pair per subject
  Makefile.am  configure.ac  reconfigure.sh  tools_config.h
  cpputils/  (submodule there, vendored here)
  ```
* **Collision to avoid (history):** the repo originally kept `common.cc` / `common.h` in its
  root for the monitor tools, so the shared harness directory had to be named differently →
  `testcommon/`. Those two files have since moved into `libcommon/` (`libcommon.a`);
  `testcommon/` keeps its name.

---

## 3. Deliverable layout

```
bf1942-adjust-monitor-settings/
  libcommon/                             <- shared code of the monitor tools (libcommon.a)
  src_adjust_monitor_settings/           <- the three pre-existing tools,
  src_create_desktop_icons/              <-   one directory per program
  src_list_monitor_resolutions/
  rfa/                                   <- new core library (no CLI, unit-testable)
    RfaFormat.h                          // structs, constants, offsets
    RfaArchive.cc/.h                     // read: table parse + entry access (streaming)
    RfaWriter.cc/.h                      // write: pack / update / replace, deterministic order
    LzoCodec.cc/.h                       // LZO1X compress/decompress, per-thread workmem
    ThreadPool.cc/.h                     // or reuse cpputils/thread
  third_party/minilzo/                   // vendored miniLZO: minilzo.c, minilzo.h, lzoconf.h
                                         //   deliberately NOT under cpputils/ - see note below
  cli/                                   <- libcli.a: the command lines themselves,
    UnpackCli.cc/.h                        //   a library so the tests can call them
    PackCli.cc/.h                          //   directly (finding 19)
  src_rfaUnpack/rfaUnpack.cc             <- CLI program, thin wrapper over cli/
  src_rfaPack/rfaPack.cc                 <- CLI program, thin wrapper over cli/
  testcommon/                            <- cpputilstest-style harness
    TestUtils.cc/.h
    ColBuilder.cc/.h
  src_test_rfa/
    test_rfa_format.cc/.h                // container parse/serialize
    test_lzo.cc/.h                       // codec vs miniLZO + round-trip + fuzz
    test_rfa_archive.cc/.h               // reader/writer, edge cases
    test_rfa_cli.cc/.h                   // CLI compatibility vs the original oracle
    test_main.cc                         // runner, registers all suites
  tests/data/                            <- copied test data (see §5)
  .github/skills/                        <- copied skills (see §6)
  .vscode/tasks.json                     // add make check / deploy tasks
  PLAN_rfa_tools.md                      // this file
```

> **Note:** the monitor tools were reorganised into `src_<program>/` + `libcommon/` before
> Phase 2 began. Follow that pattern: one `src_<name>/` directory per program, shared code in
> a `lib<name>/` library, binaries still linked into the repo root (no `SUBDIRS` Makefiles).

> **Why not `cpputils/`:** `cpputils` is a **git submodule** (`.gitmodules` is tracked and
> `git submodule status` resolves it). Writing our files there would dirty the submodule,
> put a dependency of ours inside someone else's tree, and could not be committed from
> this repository. Vendored third-party sources go in `third_party/` instead.

---

## 4. Phases

### Phase 0 — baseline & oracle harness  *(no product risk)* — ✅ DONE
1. ✅ Backed up the originals as `bin/rfaPack.orig.exe` / `bin/rfaUnpack.orig.exe`.
   SHA-256 verified identical to both `bin/*.exe` and the `bf_pablov_mod` copies, so the
   oracle is provably the shipped build.
2. ✅ Oracle harness added (the throwaway probes were consolidated, not just moved):
   * `tools/rfa_probe.py` — container dumper **and structural validator**; also absorbs
     the container-summary and name-dump roles
   * `tools/rfa_names.py` — stable sorted TSV of the directory table, for diffing
   * `tools/rfa_oracle.py` — drives `*.orig.exe`, captures rc/stdout/stderr and snapshots
     the extracted tree with sizes + SHA-256
   * `tools/rfa_golden.py` — captures 11 CLI scenarios (usage banners, the terse error
     strings the `.ps1` skills match on, exit codes) into `tests/golden/oracle-cli.json`,
     with `--check` to re-verify
3. ✅ Golden files recorded and reproducible — `python tools/rfa_golden.py --check` → match.

**Exit gate: ✅ PASSED** — `python tools/rfa_probe.py <fixture>` validates all four
fixtures with exit 0 and zero problems.

**Unexpected payoff:** running the gate exposed that §2.2 was wrong, and extending the
check to every archive in the mod tree produced the corrected format model now in §2.2.
Findings that would have cost days inside Phase 2 were found in an afternoon of Phase 0.
See §10 for the full list.

### Phase 1 — test-suite scaffolding (cpputilstest pattern) — ✅ DONE
1. ✅ Created `testcommon/`: `TestUtils.h/.cc` (`TestCaseBase` + `TestCaseFuncBool`,
   `TestCaseFuncEqual`, `TestCaseFuncNoInp`, `TestCaseFuncOneFile`) and
   `ColBuilder.h/.cc` (the ASCII result table), API-compatible with cpputilstest.
   Added `TestRunner.h/.cc` on top: the upstream harness repeats the run/report loop in
   every `test_<component>.cc`; hoisting it means each program is a one-liner list.
2. ⚠️ **Partial.** `src_test_rfa/` holds `test_rfa.cc` (the runner) plus two subjects with
   real cases: `test_testcommon` (harness self-tests) and `test_lzo` (vendored codec).
   `test_rfa_format`, `test_rfa_archive` and `test_rfa_cli` are deliberately deferred to
   Phase 2/3 — creating empty stubs now would add files with no assertions.
3. ✅ `Makefile.am`: `check_PROGRAMS = test_rfa`, `TESTS = test_rfa`, `noinst_LIBRARIES +=
   testcommon/libtestcommon.a`.
4. ✅ Vendored miniLZO 2.10 into `third_party/minilzo/` (unmodified, plus `COPYING`) with
   a provenance README. It needs its own `CPPFLAGS` because `AM_CPPFLAGS` carries
   `-std=c++20`, which is not valid for a C translation unit.
5. ✅ `./reconfigure.sh` re-run so `Makefile.in` knows the new targets (this required
   fixing the CRLF bug below).

**Exit gate: ✅ PASSED** — `make check` → `PASS: test_rfa.exe`, 21/21 testcases, and the
runner's own failure paths were verified by hand (`-t 99` → exit 1, `--bogus` → exit 1,
`--help` → exit 0).

**Deviations, stated plainly:**
* `tests/data/` is **not** in `EXTRA_DIST`. 6 MB of fixture binaries would land in any
  `make dist` tarball and nothing in this workflow calls it. Noted in `Makefile.am`.
* `testcommon/` is standard-library only; upstream pulls `ColoredOutput`/`Arg`/`OutDebug`
  from `cpputils/io`, which this project does not build. Colour can be added later.

### Phase 2 — core library `rfa/` — ✅ DONE (except `-u`)

1. ✅ `RfaFormat.h` — the container model: `CHUNK_SIZE`, `BLOCK_HEADER_SIZE`,
   `CHUNK_DESCRIPTOR_SIZE`, `PayloadVariant`, `Chunk`, `Entry`, `chunk_count_for()`.
   Header-only, no I/O, so the arithmetic is testable in isolation.
2. ✅ `LzoCodec` — miniLZO wrapper. One instance per thread owns the work buffer;
   `decompress()` is static and stateless, so concurrent decompression needs no locking.
   `compress()` reports the size so callers can see what happened.
3. ✅ `RfaArchive` + `PayloadReader` — reader for **both payload variants**. Validates
   every invariant in §2.2 and collects complaints in `problems()` instead of failing
   hard. Parsing reads the table and the chunk descriptor tables but **never a payload**,
   so a 741 MB archive opens cheaply. `PayloadReader` holds its own file handle, which is
   what makes per-entry parallel extraction safe by construction.
4. ✅ `RfaWriter` — packs a tree into either policy, reproducing `rfaPack.exe`'s layout:
   version 0 + raw entries without `-Compress`, version 1 + 32 KiB chunked entries with it,
   data at offset 156 behind the generated stamp, entries in the oracle's walk order. A
   failed pack removes its partial output rather than leaving a half archive.
   ⏳ **`-u` in-place update is NOT implemented** — it needs the probe work in §7.
5. ✅ Parallelism — chunk compression across a pool sized by
   `std::thread::hardware_concurrency()` (overridable), one 32 KiB chunk per work item.
   Results are indexed by chunk and never appended in completion order, so the output
   cannot depend on scheduling. Verified byte-identical at 1 and 8 threads.

**Exit gate:**

| Item | Status |
|---|---|
| table round-trip (parse → serialize → parse) | ✅ subsumed by the store byte-identity test |
| variant coverage: raw | ✅ store-mode archives; `Peenemunde_001.rfa` |
| variant coverage: single-chunk / multi-chunk | ✅ `salerno_001.rfa`; `Battle_Of_Pavlov-1942.rfa` (92 multi-chunk entries, largest 22 chunks) and a synthetic 7-chunk file |
| variant coverage: empty entry | ✅ all three encodings round-trip, including the oracle's `-Compress` form |
| LZO round-trip incl. `CHUNK_SIZE ± 1` and multiples | ✅ |
| corrupt input fails cleanly, never crashes | ✅ truncated / garbage / empty / payload-corrupted |
| every fixture entry decompresses to its declared size | ✅ 259 entries across the 4 fixtures |
| determinism: 1 vs 8 threads → identical bytes | ✅ |
| **store-mode output byte-identical to `rfaPack.exe`** | ✅ **PASSES** — one comparison pins the stamp, first data offset (156), reserved field, version, table layout and entry order at once. Verified on the fixtures **and** on the real 618-entry vendor `menu.rfa` (finding 28) |
| **an archive we wrote is really read by BF1942** | ✅ **PASSES** — our store pack installed as `Mods\bf1942\Archives\menu.rfa` brings `BF1942.exe +game bf1942` up to its **main menu**. The front end cannot start without that archive, so this is the engine itself accepting our container (finding 28) |
| compress-mode output vs the oracle | ⚠️ **one-directional only** — see below and finding 27 |

**Compress-mode output is not byte-identical, and cannot be.** RFA Pack 1.7 embeds a
2003-era LZO whose `lzo1x_1_compress` picks different — equally valid — matches than the
miniLZO 2.10 we vendor, so the streams differ in length while decompressing to identical
bytes. (Its empty-file stream is 4 bytes where miniLZO's is 3, which is also why
`lzo1x_decompress_safe` rejects it with `LZO_E_INPUT_NOT_CONSUMED`.)

> ⚠️ **Corrected 2026-09-24 — this gate was too generous.** It was passed by reading *both*
> archives with *our* reader. Nobody asked the original to read *ours*, and it cannot: see
> finding 27. The interchangeability is one-directional, and **store mode is the only mode
> that is actually interchangeable today**.
> `writer_compress_archive_is_interchangeable_with_the_oracle` asserts the metadata match
> and the payload round-trip through our own reader, which is all it ever proved.

**Not done in this phase:** `-u` in-place update, and a smoke test on a 741 MB archive.
Peak writer memory is bounded by the largest single file (source plus its encoded block),
not by the archive, because files are encoded and appended one at a time.

### Phase 3 — CLI tools — 🔄 `rfaUnpack` done, `rfaPack` next
1. `rfaUnpack.cc` / `rfaPack.cc` reproducing §2.4 exactly — **including the chatter strings**,
   so existing pipelines and the `.ps1` skills keep working.
2. Compatibility tests: for each fixture, run **ours** and the **original oracle** on the same
   input and compare (a) extracted file trees byte-for-byte, (b) the archive name/size table,
   (c) exit code. (Extracted trees must match; archive bytes may legitimately differ.)
3. New opt-in features (never change default behaviour):
   * **`-u` update**, implemented per §2.5 as "fresh pack with the target's policy, plus
     carried-over entries" — not as an in-place patcher. `--reset-entry-flags` reproduces the
     original's clobbering; the default preserves the target's per-entry fields (D6).
   * **fix `-f`** — real name matching, so the broken original behaviour is strictly improved.
   * **file replacement** — `-replace <internal/path>=<localFile>` (single or repeated).
   * `--threads N` / `-j N`, `--quiet`, `--list`, `--version`.
   * verify-after-write (`--verify`) comparing re-read payloads.
4. Decide explicit exit codes (0 ok, 1 error, 2 usage) and document them — the original
   is loose here, so compatibility means "0 on success, non-zero on failure".

**Exit gate:** the full oracle-comparison suite is green on all fixtures, and a manual
run on `texture.rfa` produces an identical tree to the original.

### Phase 4 — test data + skills + deployment
1. Copy the fixtures from §5 into `tests/data/`; add a `tests/data/README.md` recording
   provenance (`origin` path, size, SHA-256, file count).
2. Copy the skills from §6 into `.github/skills/`, applying the §2 corrections to the docs.
3. **Shadow first (D5):** copy the new binaries to `E:\progs\bf_pablov_mod\bin\new\` and run the
   existing `.ps1` skills against that path. Only after they pass, promote them into
   `E:\progs\bf_pablov_mod\bin\` with the originals renamed to `*.orig.exe`.
4. Add tasks to `.vscode/tasks.json`: `make` (exists), `make check`,
   `make -j` (exists), `deploy to bf_pablov_mod\bin\new`.

**Exit gate:** `rfaUnpack.exe`/`rfaPack.exe` in the mod's `bin\new\` are the new builds and the
existing `.ps1` skills run unmodified against them.

### Phase 5 — real-world verification & benchmark
1. Full round-trip on: tiny patch archives, the FH Pavlov map, `objects.rfa`,
   `standardMesh.rfa`, and a ≥100 MB archive.
2. Confirm game-loadability: an archive written with `-Compress` **and** one written
   stored-only both enumerate and extract identically with the original `rfaUnpack.exe`.
3. Benchmark 1 vs N threads on a large archive; record wall-clock and CPU% in the plan's
   results section.

---

## 5. Test data to copy

| Priority | File | Size | Why |
|---|---|---|---|
| 1 | `work\ssc\Battle_Of_Pavlov-1942.rfa` (or `origin\Mods\FH\Archives\bf1942\levels\Battle_Of_Pavlov-1942.rfa`) | 6.0 MB / 251 files | the archive the original probe matrix used; 246 compressed + 5 differently-flagged entries |
| 1 | `bf_1942_kwg_mod\Mods\bf_1942_kwg_mod\Archives\standardMesh_001.rfa` | 1,521 B | patch-archive variant |
| 1 | `origin\Mods\XPack1\Archives\Bf1942\Levels\salerno_001.rfa` | 1,355 B | smallest real archive |
| 1 | `work\good_copy\Mods\XPack2\Archives\bf1942\Levels\Peenemunde_001.rfa` | 1,221 B | smallest real archive |
| 2 | `keep\*_manifest.tsv`, `keep\UNRESOLVED.txt` | small | expected-content manifests |
| 2 | `keep\*.rfa.lst` (22 listings) | 1.7 MB total | expected name lists per archive → assert our lister matches |
| 3 | generated by the test suite | — | synthetic fixtures (pure literals, pure matches, empty, 1-byte, names with spaces, unicode names) |

**Policy:** synthetic fixtures are mandatory for unit tests; the real archives are for
integration/oracle tests. Add each copied archive's SHA-256 to `tests/data/README.md`.

---

## 6. Skills to copy

From `E:\progs\bf_pablov_mod\.github\skills\` into this repo's `.github/skills/`:

**Locked scope (D2) — RFA-relevant subset only:**

**Required (RFA work):**
* `rfa-unpack/` — whole dir: `SKILL.md`, `references/rfaunpack-cli.md`, `scripts/unpack-rfa.ps1`
* `bf1942-standalone-map/references/rfa-format.md` → replace with the corrected §2.2 spec
* `bf1942-standalone-map/scripts/list_rfa.ps1`, `extract_rfa.ps1`, `extract_keep_lists.ps1`,
  `repack_standalone.ps1`, `resolve_keep_list.ps1`, `query_index.ps1`
* `bin\Readme.txt` → `tests/data/legacy-CLI-Readme.txt` (the CLI contract)

An `AGENTS.md` / `copilot-instructions.md` that points at these skills should also be created
in this repo, since `bf_pablov_mod` does not have one.

**Not copied:** `bf1942-level-editing/`, `bf1942-mesh-authoring/`, `kwg11-origin-building/`,
`kwg11-target-building/` (they consume RFA tooling but are outside this task's scope).

---

## 7. Risks & mitigations

| Risk | Severity | Mitigation |
|---|---|---|
| Codec misidentified | ~~high~~ **resolved** | LZO1X confirmed by reference tool + own experiments; Phase 2 tests pin it against both |
| `-u` update semantics unknown | ~~high~~ **resolved** | Probed with controlled archives (`tools/rfa_probe_update.py`): `-u` is a fresh pack with the target's policy plus carried-over entries. See §2.5 |
| Entry `flags`/`reserved` semantics unknown | medium | Still unknown, but now known to be **clobbered** by the original on `-u`, so they cannot carry meaning the game depends on. Decision D6 |
| Licensing of miniLZO (GPLv2+) | medium | compatible with this repo's GPLv3, but a clean-room `Lzo1x.cc` avoids the dependency (D4) |
| Parallelism on huge archives (707 MB) | high | streaming per-file buffers, bounded worker queue, offset-ordered reads; never load whole archive |
| Non-deterministic archive layout | medium | sort entries by name; assemble in the main thread; test 1 vs N threads for byte equality |
| Oracle compatibility drift | medium | golden tests compare against `bin\*.orig.exe` on every fixture |
| Repo test-data bloat | low | only 4 real archives (~6 MB) + generated fixtures; list ~6 MB `.lst` excluded |

---

## 8. Decisions — LOCKED

| # | Decision | Chosen |
|---|---|---|
| D1 | Test suite location | **Integrate into this repo** — `testcommon/` + `src_test_rfa/`, wired via `check_PROGRAMS`/`TESTS` so `make check` runs it |
| D2 | Skills to copy | **RFA-relevant subset only** — `rfa-unpack/`, `bf1942-standalone-map` refs+scripts, `bin\Readme.txt`; plus a new `AGENTS.md` |
| D3 | Harness directory name | **`testcommon/`** — avoids ambiguity with the existing root `common.cc` / `common.h` |
| D4 | Codec dependency | **Vendor miniLZO** into `third_party/minilzo/` (GPLv2+, compatible with this repo's GPLv3). **Not** under `cpputils/`, which is a submodule |
| D5 | Deployment | **Shadow in `bin\new\` first** — validate against the real `.ps1` skills before touching the originals |
| D6 | `flags`/`reserved2` on `-u` | **Preserve the target's existing values by default** (§2.5 finding: the original silently zeroes them). `--reset-entry-flags` reproduces the original byte-for-byte. Chosen because clobbering is destructive, the fields have never been shown to matter, and an archive stays readable by the original either way — the compatibility requirement is that the oracle can still read it, not that the bytes match |
| D7 | Carried-over entry names on `-u` | **Carry over verbatim; do not reproduce the renaming bug** (§2.5 mapping). The bug renames files with no way to ask for it, only ever triggers when the base name differs, and cannot be what anyone wants. Byte-identity with the original is therefore claimed for the matching-base case only, which is the case the documented workflow uses |
| D8 | Reserved region on `-u` | **Normalise to the standard 148-byte stamp at offset 156**, matching the original, so `-u` output stays byte-comparable with a fresh pack. The larger producer-specific region found in the FH archives is discarded — as the original does. `--keep-reserved-region` (opt-in) preserves it instead, for anyone updating an archive they do not want rewritten |

### Consequences folded into the plan
* `Makefile.am` gains `check_PROGRAMS` + `TESTS` + `EXTRA_DIST`; `reconfigure.sh` is re-run once.
* miniLZO lives in `third_party/minilzo/` — never inside `cpputils/`, which is a submodule — and is
  added to `noinst_LIBRARIES` (or built directly into the two programs and the test runner)
  with `-DMINILZO_HAVE_CONFIG_H` off (standalone build, no config.h requirement).
* Phase 4 step 3 becomes: copy new binaries to `E:\progs\bf_pablov_mod\bin\new\`, run the existing
  `.ps1` skills against them, and only then promote them one level up with `.orig.exe` backups.

---

## 9. Immediate next actions

1. ✅ Back up the oracle binaries; add `tools/rfa_oracle.py` — done, Phase 0 complete.
2. ✅ Phase 1 — `testcommon/` harness, `src_test_rfa/`, `check_PROGRAMS`/`TESTS` wiring,
   vendored miniLZO. Done.
3. ✅ Phase 2 — `RfaArchive`, `LzoCodec`, `RfaWriter` with the variant/chunk/writer tests
   and the oracle comparison. Done except `-u`.
4. 🔄 **Phase 3, part 1 — `rfaUnpack` done** (2026-09-24). Next: `rfaPack`, then the CLI
   comparison suite driven by `tools/rfa_golden.py`.

> Before writing `-u`: the original's update semantics are still **unknown**, and the CLI
> reference records nothing about the mode. Probe it first, with the controlled-archive
> method that cracked the codec — do not guess.

---

## 10. Progress log

### Phase 0 — DONE (2026-09-23)

Delivered: `bin/*.orig.exe` oracle backups (SHA-256 verified), `tools/rfa_probe.py`,
`tools/rfa_names.py`, `tools/rfa_oracle.py`, `tools/rfa_golden.py`,
`tests/golden/oracle-cli.json` (11 scenarios, `--check` reproducible),
`tests/data/` (4 real fixtures with provenance + hashes). Exit gate passed.

### Corrections found during Phase 0

| # | Finding | Impact |
|---|---|---|
| 1 | The block header's first field is a **chunk count**, not `tag == 1`; descriptor table grows 12 bytes per extra chunk | Rewrote §2.2 and the format skill doc. Without this the reader works on small files and silently corrupts anything > 32 KiB |
| 2 | Payload compression is **per 32 KiB chunk**, not per file | Changes the parallelism unit (finer than per file) and the codec API |
| 3 | `storedSize == uncompressedSize` means **no block header at all** (raw payload at `dataOffset`) | A whole second variant; 34,617 entries |
| 4 | `version` is `0` or `1` and does **not** select the layout — 235 raw entries live in version-1 archives | Version-based branching would be wrong |
| 5 | 194 of 814 archives (23%) are version-0 raw; one fixture (`Peenemunde_001.rfa`) is one of them | Without a fixture for this, the variant would have been missed |
| 6 | `uncompressedSize == 0` entries have `storedSize == 4`, not `0` | Needs an explicit case; 56 entries |
| 7 | Original exit codes are not a success signal: `-i9999` (index out of range) and the broken `-f` both print an error yet **exit 0** | Compatibility tests must not rely on exit codes alone |
| 8 | `rfaPack.exe` output contains a variable `TimeTaken: N` | Golden comparison needs normalisation |
| 9 | Line **order** in the originals' stdout is not stable under redirection (mixed stream buffering) | Compare message presence, not emission order |
| 10 | A short opaque region precedes the first data block (first block commonly at offset 156) | Must not be regenerated blindly; preserve or replicate |

### Carried into Phase 1/2

* `rfa_probe.py` now validates all four fixtures with **zero problems**; reuse it as the
  Phase 2 sweep tool.
* `tests/data/README.md` records that `bf_pablov_mod` is an actively changing pipeline
  workspace (`standardMesh_001.rfa` changed size mid-session) — fixtures are snapshots.
* Deliberate divergence to decide in Phase 3: fixing `-f` changes behaviour that the
  original exits 0 on; the golden file records the broken behaviour so the change is explicit.

### Phase 1 — DONE (2026-09-23)

Delivered: `testcommon/` (TestUtils, ColBuilder, TestRunner), `src_test_rfa/test_rfa.cc`
+ `test_testcommon.{h,cc}` + `test_lzo.{h,cc}`, `third_party/minilzo/` (miniLZO 2.10),
`Makefile.am` wiring (`check_PROGRAMS`/`TESTS`), a `make check` VS Code task.
`make check` → PASS, 21/21 testcases.

| # | Finding | Impact |
|---|---|---|
| 11 | `configure.ac` and `Makefile.am` were checked in with **CRLF** endings | `AC_CONFIG_FILES` entry became `"Makefile\r"`, so `configure` died with `cannot find input file`. Worse, the stray CR overwrote the error text in the terminal, making it near-unreadable. Fixed the files and added `.gitattributes` (`*.sh`, `*.ac`, `*.am` → `eol=lf`) so it cannot regress. |
| 12 | `reconfigure.sh` was also CRLF | It failed outright under Cygwin (`$'\r': command not found`), which means the `reconfigure` task added in Phase 0 never worked. Fixed by the same change. |
| 13 | `AM_CPPFLAGS` carries `-std=c++20` | Not valid for a C translation unit, so `minilzo.c` gets a per-target `CPPFLAGS` override. Any future `.c` file in this tree needs the same treatment. |
| 14 | Sizing a decompression buffer from the *compressed* size is wrong | Caught by the new suite, not by review: 33-byte-repeating input compresses well over 64x, so the guessed buffer was too small and `lzo1x_decompress_safe` returned `LZO_E_OUTPUT_OVERRUN`. The RFA reader always knows `uncompressedSize`, so it must use it. Recorded in `test_lzo.cc`. |

Note that finding 14 is exactly the class of bug that silently corrupts archives in
production, and it was found by a test written minutes earlier — the harness paid for
itself on day one.

### Carried into Phase 2

* `test_lzo` already pins the LZO1X literal-run encoding against the Phase 0 experiment,
  so a mis-vendored LZO fails loudly instead of producing unreadable archives.
* `CHUNK_SIZE`, the 12-byte descriptor arithmetic and the two payload variants are
  documented in §2.2 and need to be implemented in `RfaArchive`/`RfaWriter`.
* `test_rfa_format`, `test_rfa_archive`, `test_rfa_cli` still to be created.

### Phase 2 — DONE except `-u` (2026-09-23)

Delivered: `rfa/RfaFormat.h`, `rfa/RfaStamp.h` (generated by `tools/rfa_stamp_gen.py`),
`rfa/LzoCodec.{h,cc}`, `rfa/RfaArchive.{h,cc}`, `rfa/RfaWriter.{h,cc}`,
`src_test_rfa/test_rfa_format|archive|lzo|writer.{h,cc}`, `tests/data/golden/` (tree +
`oracle-store.rfa` + `oracle-compress.rfa` + `MANIFEST.txt`),
`tools/rfa_golden_archives.py`. `make check` → **PASS, 59/59 testcases**, zero diagnostics.

| # | Finding | Impact |
|---|---|---|
| 15 | A chunk is LZO1X **iff `compressedSize != uncompressedSize`** — inequality, not less-than | The reader tested `<`, so every LZO-*expanded* chunk (1 byte → 5, incompressible 32 KiB) was treated as verbatim and returned compressed bytes as file content. Size-only self-consistency could not see it — the oracle comparison caught it. Fixed in `Chunk::is_compressed()`; the format skill doc had stated the wrong rule and is corrected. |
| 16 | `rfaPack.exe` does **not** sort entries by full path | Order is each directory's own files (sorted) then its subdirectories (sorted), recursively. A full-path sort agrees on flat trees and diverges on nested ones; proven with a 2-level/3-sibling tree. The `sort_entries` option was removed rather than kept as a trap. |
| 17 | Compress-mode output **cannot** be byte-identical to the oracle | RFA Pack 1.7's 2003-era LZO picks different but equally valid matches than miniLZO 2.10. Store mode **is** byte-identical — the stronger result, since one comparison pins the stamp, first data offset, reserved field, version, table layout and entry order together, and it now holds byte-for-byte on the real 618-entry `menu.rfa` too (finding 28). Compress mode is asserted semantically instead. |
| 18 | The oracle's `-Compress` empty entry is **not** a valid LZO1X stream | miniLZO emits 3 bytes where the 2003-era LZO emitted 4, so `lzo1x_decompress_safe` fails with `LZO_E_INPUT_NOT_CONSUMED`. Three encodings exist for "zero bytes"; readers must short-circuit on `uncompressedSize == 0` rather than consult the codec. |
| 19 | Driving the oracle from inside a C++ test is not viable | `std::system` goes through cmd.exe, which reads `/` as a switch and resolves relative paths against its own cwd — `bin/rfaPack.orig.exe` failed, and so did absolute paths. Replaced by committed golden archives produced once by `tools/rfa_golden_archives.py`, which also makes the tests hermetic and fast. |

Small fixtures hid findings 1 and 15 both: a single-chunk file cannot reveal a
constant-`tag` header, and a compressible file cannot reveal an expanded chunk. The
lesson repeated twice is that **self-consistency is not correctness** — only the oracle
comparison is, and it should have been written before the writer tests.

### Carried into Phase 3

* The CLI contract is in `.github/skills/rfa-unpack/references/rfaunpack-cli.md` and
  `tests/golden/oracle-cli.json`; reproduce the chatter strings, not just the effects.
* The originals' **exit codes are not a success signal** (findings 7) and their stdout
  **line order is unstable under redirection** (finding 9) — compare message presence.
* Fixing the broken `-f` is a deliberate divergence to decide explicitly (Phase 0 note).
* `-u` semantics were then probed — see the section below; §2.5 supersedes the earlier
  "unknown" status.

### Probe: `-u` update semantics — DONE (2026-09-23)

Delivered: `tools/rfa_probe_update.py` (9 controlled cases, each re-extracted by the original
to prove the result stays readable). Findings in §2.5; the four that change the design:

| # | Finding | Impact |
|---|---|---|
| 20 | `-u` is byte-for-byte a **fresh pack of the tree using the target archive's compression policy**, plus carried-over entries for names absent from the tree | `-u` needs no in-place algorithm at all. It reuses the fresh-pack path we have already proven byte-identical to the oracle, so its risk profile collapses |
| 21 | **`-Compress` is ignored during `-u`**, in both directions: `-u -Compress` leaves a store archive raw, and plain `-u` adds *compressed* entries to a compress archive. The flag is parsed and echoed as `UseCompression: N` but does not reach the archive | Compatibility requires reproducing this, however counter-intuitive; a naive `-u` honouring the flag would silently produce archives the original never writes |
| 22 | **`-u` never deletes.** An entry missing from the tree is retained and still extracted | Any pruning feature must be explicit (`--prune`), never a side effect of `-u` |
| 23 | **Opaque per-entry fields are clobbered.** Patching `flags` to `0xFFFFFFFF` and `reserved2` to `0xDEADBEEF` then running `-u` reset every entry to `0` — the fresh-pack defaults | Directly contradicts the "preserve `flags` on `-u`" advice in `AGENTS.md` and the skills, which is corrected. Decision D6 |
| 24 | The entry table is `u32 count` + per-entry `u32 nameLen`/name/24 bytes + a trailing zero `u32`; `reserved1` is `0x001321E0` | Confirms the 24-byte field block our reader/writer already uses; recorded as a cross-check, not a change |
| 25 | **`-u` RENAMES carried-over entries** as `newBase + "/" + storedName.substr(strlen(newBase) + 1)`. Predicted from the `menu/2/levels/...` anomaly and then confirmed: base `bf1942` leaves the FH names untouched, `menu` yields `menu/2/levels/...`, `bf1942x` yields `bf1942x/evels/...` | Silent, destructive, and invisible in the documented workflow because that workflow always passes the matching base name. Must not be reproduced — D7 |
| 26 | **`-u` replaces a foreign reserved region.** The FH archive's region is 4,078 bytes at offset 8..4086; after one `-u` it is the standard 148 bytes at 156, so ~3.9 KB of producer-specific bytes were dropped and the layout normalised | Retires the "must preserve the target's reserved bytes" claim in `rfa/RfaStamp.h`. Also explains why the self-produced probes could not detect this: there the region already *is* the constant — D8 |

Finding 25 is the one worth dwelling on: the anomaly was visible in the very first FH run as
`menu/2/levels/...`, an obviously impossible path. It would have been easy to note it as noise,
or to assume the tool "re-prefixes entries with the base" and implement that. Stating it as a
prediction (`strip strlen(base)+1`) and then testing three base lengths is what turned it into a
precise rule — and it is a rule we deliberately break.

Worth noting how this was established: rule 20 was only visible because every case was
compared against a *fresh pack*, and rule 23 only because the fields were patched to values
the packer never produces. Reading the output and reasoning about what the code "must" do
would have produced a wrong `-u` design in both cases — the same lesson as findings 15 and 16.

### Phase 3, part 1: `rfaUnpack` — DONE (2026-09-24)

Delivered: `cli/UnpackCli.{h,cc}`, `src_rfaUnpack/rfaUnpack.cc`,
`src_test_rfa/test_rfa_cli.{h,cc}` (9 testcases), `rfaUnpack.exe`. `make check` → **PASS, 78/78**.

**The command line is a library, not a program.** `cli/UnpackCli.cc` holds
`cli::run_unpack( args, out )`; the program is a three-line wrapper. Spawning the binary from a
test was already ruled out (finding 19), and calling the same entry point the program calls is
hermetic, fast, and hands the test the chatter as a string to assert on. `cli/libcli.a` is where
`PackCli` goes too.

**Verification against the oracle.** Every unpack scenario in `tests/golden/oracle-cli.json` was
re-run through our binary and compared as a *set of lines* — the golden's own caveat is that the
original's order is not stable under redirection — after applying the golden's normalisation
(`{TMP}`, `{ARCHIVE}`, `{LISTFILE}`, `{ADDR}`):

| Scenario | rc ours / oracle | Lines |
|---|---|---|
| `unpack_no_args` | 1 / 1 | identical |
| `unpack_missing_outdir` | 1 / 1 | identical |
| `unpack_index_0` | 0 / 0 | identical |
| `unpack_index_out_of_range` | 0 / 0 | identical |
| `unpack_list_full_paths` | 0 / 0 | identical |
| `unpack_list_basename` | 0 / 0 | identical |
| `unpack_full_tiny` | 0 / 0 | identical |
| `unpack_f_broken` | 0 / 0 | **intended divergence** — D9 |

And the Phase 3 exit gate itself: extracting `tests/data/fh/Battle_Of_Pavlov-1942.rfa` with ours
and with `bin\rfaUnpack.orig.exe` produced **251 files on both sides, with identical paths and an
identical SHA-256 for every single file**. That is the only *external* check of the writer's
output; the in-suite equivalent compares against `PayloadReader`, whose own external evidence is
Phase 2's oracle-written golden archives.

**Decisions taken here:**

| # | Question | Decision |
|---|---|---|
| D9 | `-f` | **Fixed.** The shipped build matches nothing with `-f` (pinned as `unpack_f_broken`) and Phase 3 asked for real matching, so ours tries the full internal path and then the basename. Everything else about the switch, including the chatter, is identical to the oracle's |
| D10 | Exit codes | **0 on success, 1 for usage/argument/I/O failure, and deliberately 0 for selection misses.** `-i9999` and an unresolvable name both exit 0 in the original and the golden pins that. The earlier "2 for usage" idea is dropped — it would break `unpack_no_args` |
| D11 | `ExtractToPath` omitted | The golden never exercises it. Ours falls back to `.`, which keeps the tool usable. Marked as unpinned rather than guessed at |
| D12 | Unpinned failure text | An unreadable archive or list file gets `Error! Cannot open archive: …` / `Error! Cannot open file list: …` and exit 1. No captured scenario reaches those paths, so the strings are ours, not reproductions |

**Not yet done:** `rfaPack` — the whole program, including `-u` per §2.5 and D6–D8. Also
untested: `unpackedSize_MB` is integer-MB division, and the only golden covering it is a 0 MB
archive, so the rounding for a large archive is unverified.

### Finding 27 — the original cannot read our `-Compress` output (2026-09-24)

Found while writing `rfaPack`'s command line, by doing the one thing the Phase 2 gate never
did: pointing `bin\rfaUnpack.orig.exe` at an archive **we** wrote.

| Probe | Result |
|---|---|
| we pack `tests/data/golden/tree` in store mode | **byte-identical** to `oracle-store.rfa`, 200,451 B |
| the oracle reads our **compress** archive | `ERROR! CRASH  Decompression()!` → `ERROR! Extract() fileSize: 0 should be: 200000` → `ERROR! Could not extract file!`, and the run stops. rc is still **0** |
| the oracle reads its own `oracle-compress.rfa` | 5 files, no error |
| our reader reads our compress archive | 5 files, correct sizes and contents |
| 1 file, 3,000 B of `"hello rfa "`, `-Compress` | **CRASH** |
| 1 file, 200 B of `"ab"`, `-Compress` — a *single* chunk | **CRASH** |
| 1 file, 31 B, incompressible (`Game.setNumberOfTickets…`) | **OK** — the only input that works |

So it is **not** the container layout, **not** multi-chunk handling and **not** entry size: one
200-byte single-chunk entry is already enough. The only stream the original accepts is one
that holds nothing but literals — i.e. one LZO1X *expanded*. **The 2003-era decoder in RFA
Pack 1.7 does not accept the match opcodes miniLZO 2.10 emits.**

**Impact.** `-Compress` output is unreadable by both shipped `.rfa` tools, and in all
likelihood by the game's loader, which is the same 2003 code. Phase 5's real-world
verification would have caught this; far better here. It also means the Phase 2 exit gate for
compress mode passed on a claim never tested in the direction that matters — corrected in §4.

**Not yet known:** which opcode subset the old decoder implements, and whether a conservative
encoder (one that never emits the extended match-length form, or caps match length) would be
accepted. The reference implementation named in §2.3 (`yann-papouin/bga`) or an LZO release
from that era would answer it. That is a research task, not a patch.

**Consequence for the CLI:** `rfaPack -Compress` still writes a valid archive — we round-trip
it — but it now prints a warning on **stderr** (stdout stays golden-exact) naming this finding,
because producing something the target cannot read must not be silent. Store mode is unaffected
and remains byte-identical.

#### … and the engine is a different decoder (2026-09-24)

Finding 27 says "the shipped `.rfa` tools cannot read our `-Compress` output". It says nothing
about the game, so that was tested: our `-Compress` pack of the vendor `menu` tree (618 entries,
10,064,159 B) was installed as `Mods\bf1942\Archives\menu.rfa`, and `BF1942.exe +game bf1942`
came up to its **main menu**. The shipping `menu.rfa` is itself version 1 with 618 chunked
entries, so the engine decodes an LZO stream there every launch — and it decodes ours too.

The old tools' failure, measured properly on the same archive, turns out to be narrower than the
earlier single-file probes suggested:

| | result |
|---|---|
| our reader, full round trip | 618/618 files identical to the vendor tree |
| the 2003 decoder on our pack | extracted **11 files correctly**, then `ERROR! Extract() fileSize: 50942 should be: 51538` and stopped |
| the 2003 decoder on the vendor tool's own `-Compress` pack | 618 files, no error |

So it is not "every match opcode is rejected" — it is **data-dependent**, and the probes that
produced the earlier wording were too small to show it. Eleven menu entries decoded before one
came out 596 bytes short, which means the two decoders agree on most of the format and diverge
on one construct.

#### The era compressor is reproducible, and ours is a different one

Comparing each **shipping** archive with the vendor tool's own re-pack of its extracted tree,
entry by entry and by *payload* rather than by offset (the shipping files do not keep their data
blocks in table order, which makes a whole-region comparison meaningless):

| archive | payloads byte-identical | sizes differing | table order |
|---|---|---|---|
| `menu.rfa` (618 entries) | **618 / 618** | 0 | differs |
| FH `Battle_Of_Pavlov-1942.rfa` (251 entries) | **251 / 251** | 0 | differs |

DICE's packer and RFA Pack 1.7 therefore share **one** LZO encoder, reproducible byte for byte.
Only the container differs: entry order, the placement of data blocks, `reserved1` (the shipping
archives store 0 where rfaPack writes 1253856) and `flags`. This also explains the size identity
noted in the phase record below — it is not a coincidence.

That reframes the size penalty as a purely algorithmic question. For 200 bytes of `ab`, the era
encoder emits **10 bytes** and miniLZO 2.10 emits **29**; over the whole `menu` tree ours is 26%
larger (10,064,159 vs 7,963,020 B). The `lzo1x_1` we vendor is not the encoder behind these
archives.

**Open question, deliberately not guessed at:** which LZO variant produces those streams. The
era encoder's output for repetitive input uses the textbook long-match form
(`13 61 62 20 a5 04 00 11 00 00` = 2 literals, then a 198-byte match at distance 2, then the end
marker), so the candidate is a stronger variant of the same family — LZO1X-999 being the obvious
one — not a different codec. Testing it needs LZO sources: there is no LZO library on this
machine (`/usr/include/lzo`, `/usr/lib/liblzo*` and the mingw sysroot are all empty) and no
Python LZO module, so it is a download-and-try task, and vendoring a second codec is a
dependency decision belonging to D4 rather than to a probe.

If it turns out to be LZO1X-999, compress mode becomes byte-identical to the oracle's **and**
readable by the old tools — findings 17 and 27 both close. Until then: store mode is the
interoperable one, compress mode is for the engine and for us.

### Finding 28 — the original sorts entries by name, folded to UPPERCASE (2026-09-24)

Found while repacking the real vendor `menu.rfa` (618 entries) to try it in the game. Our store
pack and the original's store pack of the same tree came out the **same size and different
bytes**: 22,950,118 B each, **250 of 618 indices in a different order**, 1,003,071 differing
bytes in the data region.

Diagnosed with `work\menu_test\cmp_toc.py` (directory table in *file order*) and
`work\menu_test\cmp_dirs.py` (both orders side by side, per directory).

**The rule has two halves, and the first one alone is not enough.**

*Half one — the comparison is not byte-wise.* In `menu/`:

| index | ours (byte order) | the original |
|---|---|---|
| 40 | `menu/InGame` | `menu/InfantryControlsPage1` |
| 41 | `menu/InfantryControlsPage1` | `menu/InfantryControlsPage2` |
| 42 | `menu/InfantryControlsPage2` | `menu/InGame` |

and in `menu/Texture/Ammo/` we put `Icon_PT_Mine.dds` before `icon_artillery.dds`, the original
the other way round. Byte-wise `G`(0x47) < `f`(0x66) and `P`(0x50) < `a`(0x61) settle both in
our favour; both settle the original's way once case is folded out. Case folding cut the 250
mismatches to 68 — but not to zero.

*Half two — the fold goes to UPPERCASE, not lowercase.* After the case-folding fix, exactly
**one** directory of 618 entries still differed, and in it exactly one pair:

```
ours   menu/Texture/loading_full_256x16.dds     <-- ours
       menu/Texture/loadingfull_256x16.dds
theirs menu/Texture/loadingfull_256x16.dds      <-- the original
       menu/Texture/loading_full_256x16.dds
```

`_` is 0x5F: it folds to itself, and it sits **above `A` (0x41) and below `a` (0x61)**. Folding
down therefore compares `_`(0x5F) < `f`(0x66) and puts `loading_full` first; folding up compares
`_`(0x5F) > `F`(0x46) and puts `loadingfull` first. The original does the latter, i.e. it
compares **uppercase-folded** names.

Three pairs pin the rule, and uppercase folding is the only one of the three candidates
(byte, lowercase, uppercase) that orders all three the original's way:

| pair | byte order | lower-cased | UPPER-CASED | the original |
|---|---|---|---|---|
| `InGame` / `InfantryControlsPage1` | `InGame` | `Infantry…` | `Infantry…` | `Infantry…` |
| `Icon_PT_Mine.dds` / `icon_artillery.dds` | `Icon_PT…` | `icon_art…` | `icon_art…` | `icon_art…` |
| `loading_full_256x16.dds` / `loadingfull_256x16.dds` | `loading_full` | `loading_full` | **`loadingfull`** | **`loadingfull`** |

Only the third pair separates lowercase from uppercase. A fixture holding just `InGame` /
`Infantry…` and `Icon_` / `icon_` siblings therefore **cannot** test the rule — which is why the
first fix looked correct and still produced different bytes. (Fourth time a small fixture hid a
rule: findings 1, 15, 16, 28.)

| | ours | the original |
|---|---|---|
| entryCount / tocOffset / version | 618 / 22908572 / 0 | identical |
| names and all six per-entry fields | — | identical per name, **0 field diffs** |
| entry order | — | identical once both halves are implemented |
| whole file | `AB3C2B9E72080914…` | **same SHA-256** |

**Verified** by repacking `work\menu_test\orig\menu` (618 files extracted from the vendor
archive) and comparing the result with the vendor tool's own store pack of the same tree:
22,950,118 B, **SHA-256 identical**
(`AB3C2B9E72080914551D9B2E6A5A5F4D8D28A30AE2A32B6074DCA3324D7F3155`). Alongside store mode on
the fixtures this is the strongest writer check in the project — a real shipping archive, not a
fixture.

**And it runs in the game.** The same file, installed as
`origin\Mods\bf1942\Archives\menu.rfa`, brings `BF1942.exe +game bf1942` up to its **main
menu**. The front end mounts that archive at startup and refuses to start without it, so this
confirms the archive is not merely byte-identical to the oracle's but is actually parsed and
consumed by the engine. That closes Phase 2 with a real archive and a real client instead of a
fixture — the one check a unit test cannot make. Helper for the swap:
`examples/bf_pablov_mod/work/menu_test/swap_menu.ps1` (`ours` / `oracle` / `vendor` / `status`);
the vendor archive is kept as `Mods\bf1942\Archives\menu.rfa.vendor`.

**Impact if unfixed.** Selection and content were never affected — the original reads our repack
back to 618 files with every path and SHA-256 identical, because lookup is by name and the
payloads are the same bytes, merely placed in a different order. But Phase 2's headline
"store-mode output is byte-identical to `rfaPack.exe`" only held for trees whose sibling names
were case- and punctuation-unambiguous, which is exactly what the five-file golden tree and the
synthetic writer tree were.

**Tie-break.** Two names that fold to the same thing fall back to the byte comparison, keeping
the order total and deterministic. That branch is **unobservable rather than measured**: Windows
cannot hold such a pair in one directory, and none of the 70 archives in the base-game install
contains one (`tools/probe_case_ties.py`).

**Fixed in:** `RfaWriter::name_less()` (uppercase folding, ASCII-only, documented fallback) and
`test_writer_orders_names_the_way_the_original_folds_them`, whose tree contains all three
discriminating pairs.

### Finding 30 — equal sizes do NOT mean a chunk is stored verbatim (2026-09-24)

Found by round-tripping the shipping `Battle_of_Britain.rfa` (46,644,201 B, 548 entries). Our
extraction produced 548 files and **one of them was wrong**; the vendor tool's extraction of the
same archive was right.

```
Bf1942/Levels/Battle_of_Britain/Objects/Willy/Willy.con    stored=46   uncompressed=30
  chunk 0 : compressed=30   uncompressed=30   payloadOffset=0
  block header : 01 00 00 00 1e 00 00 00 1e 00 00 00 00 00 00 00
  payload      : 1e 72 75 6e 20 6f 62 6a 65 63 74 73 0d 0a 70 01 03 77 65 61 70 6f 6e
                 50 01 64 00 11 00 00
  content      : "run objects\r\nrun weapons\r\n\r\n\r\n"   <- what the original extracts
  ours, before : those same 30 payload bytes, verbatim         <- silently wrong
```

`1e` is LZO1X's literal-run opcode for the 13 bytes that follow and `11 00 00` is its end
marker: the payload is a stream that happens to compress to exactly the length of the file it
produces. `Chunk::is_compressed()` compares the two sizes, so it said "verbatim" and
`PayloadReader` handed the stream back as file content. The length was right, so nothing could
notice it - no error, no failed size check, no structural problem. Only comparing the bytes
against a second implementation showed it.

**This retires a claim in `AGENTS.md`:** "a chunk is an LZO1X stream iff `compressedSize !=
uncompressedSize`" holds in one direction only. Inequality still means compressed (and is still
what catches LZO-*expanded* chunks that a less-than test misreads), but equality proves nothing.
The sizes are a hint; the codec is the verdict.

**Fix:** when the sizes are equal, `PayloadReader` tries LZO first and falls back to a verbatim
copy when the codec refuses. Trusting `lzo1x_decompress_safe` in that direction is safe - it
rejects anything that is not a stream and insists on producing exactly `uncompressedSize`
bytes, so a wrong answer would require file content to be a valid LZO1X stream of its own,
which the refusal we just handled proves it is not.

**Verified:** re-extracting the same archive with the fixed reader now gives **548 of 548 files
identical** to the vendor tool's extraction.

**Pinned by** `test_rfa_archive`'s `archive_reads_a_chunk_that_compresses_to_its_own_length`,
which builds a one-entry version-1 archive around the measured 30 bytes - no 46 MB fixture
needed, and the testcase asserts that the sizes still say "verbatim" so it keeps testing the
trap rather than a fixture that drifted.

**Why the sweeps missed it.** `rfa_probe.py` validated 814 archives structurally (tables,
offsets, sizes) and the suite checked that every fixture entry decompresses to its *declared
size*. Neither looks at content. `menu.rfa` (618 entries) had matched the vendor tool entry for
entry, so this chunk shape did not occur there; `Battle_of_Britain.rfa` is the first archive
where it did. Same lesson as findings 1, 15, 16 and 28, one level down: it is not enough for a
fixture to contain the case - the comparison has to be able to see it.

### Phase 3, part 2: real archives, and the game — DONE (2026-09-24)

The first validation against shipping archives rather than fixtures, and the first one that
involved the engine. Everything below was produced from trees the **vendor tool** extracted, so
the writer is judged without the reader's mistakes in the way (which is how finding 30 was
found rather than baked in).

| archive | entries | our store pack | against the vendor tool |
|---|---|---|---|
| `Battle_of_Britain.rfa` (base `Bf1942`) | 548 | 63,728,303 B | **byte-identical** to its store pack of the same tree, `00B4A9D9…`; it extracts 548/548 files back out, all identical |
| FH `Battle_Of_Pavlov-1942.rfa` (base `bf1942`) | 251 | 12,079,164 B | **byte-identical**, `574E6840…`; 251/251 files back out, all identical |

**And the game reads them.** Both repacked levels were installed in their mods and start: the
front end comes up and the level archives load. That is the end-to-end check no unit test can
make - the engine's own decoder accepting containers we wrote, on real levels.

Timings on a 548-entry, 63.7 MB tree (median of 3, run interleaved with ours first so the
original always has the warmer cache):

| operation | ours | RFA Pack 1.7 | |
|---|---|---|---|
| unpack 46.6 MB → 548 files | 0.29 s | 0.43 s | 1.5× faster |
| pack store (63.7 MB) | 0.09 s | 0.05 s | 1.8× slower |
| pack compress | 0.26 s | 5.56 s | **21× faster** |

The compress win is per-chunk parallelism over miniLZO against a single-threaded 2003 encoder.
It is not free: our output is 8% larger on `Battle_of_Britain` (50,553,156 vs 46,644,201 B) and
14% larger on Pavlov (6,839,603 vs 6,002,393 B), because `lzo1x_1` settles for weaker matches
than the era encoder - and the era decoder rejects it outright (finding 27). Store mode is the
interchangeable one, and it is the one that is byte-identical.

**Also seen here:** the shipping `Battle_of_Britain.rfa` carries `flags = 0x028A0220` on every
entry, while the FH archive carries `0x7C001CD8` and `0xFFFFFFFF`. §2.5's note that
`0x028A0220` was a misreading of `0x7C001CD8` is therefore wrong - both values occur, in
different archives. The field stays opaque, and the engine demonstrably loads our archives with
it set to 0.
