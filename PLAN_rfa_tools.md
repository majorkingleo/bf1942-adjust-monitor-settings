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

Entries are ordered **ascending by name** (ASCII). `flags` is per-entry; observed
values are `0xFFFFFFFF`, `0x7C001CD8`, `0x77FCB6DE`, `0x00000000`, and `rfaPack.exe`
writes `0` which the engine still loads — so treat it as opaque and preserve it on
`-u` updates.

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

### 2.5 Existing project + test-suite template
* This repo is autotools + vendored `cpputils/`, cross-compiled with mingw through Cygwin
  (`make` task already added to `.vscode/tasks.json`, verified working).
* `cpputilstest` layout to mirror:
  ```
  common/                      TestUtils.{h,cc}, ColBuilder.{h,cc}      <- shared harness
  src_test_<component>/        test_<subject>.{cc,h} pairs             <- one pair per subject
  Makefile.am  configure.ac  reconfigure.sh  tools_config.h
  cpputils/  (submodule there, vendored here)
  ```
* **Collision to avoid:** this repo already has root-level `common.cc` / `common.h` for the
  monitor tools. The new harness directory must be named differently → `testcommon/`.

---

## 3. Deliverable layout

```
bf1942-adjust-monitor-settings/
  rfa/                                   <- new core library (no CLI, unit-testable)
    RfaFormat.h                          // structs, constants, offsets
    RfaArchive.cc/.h                     // read: table parse + entry access (streaming)
    RfaWriter.cc/.h                      // write: pack / update / replace, deterministic order
    LzoCodec.cc/.h                       // LZO1X compress/decompress, per-thread workmem
    ThreadPool.cc/.h                     // or reuse cpputils/thread
  third_party/minilzo/                   // vendored miniLZO: minilzo.c, minilzo.h, lzoconf.h
                                         //   deliberately NOT under cpputils/ - see note below
  rfaPack.cc                             <- CLI program
  rfaUnpack.cc                           <- CLI program
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
| **store-mode output byte-identical to `rfaPack.exe`** | ✅ **PASSES** — one comparison pins the stamp, first data offset (156), reserved field, version, table layout and entry order at once |
| compress-mode output vs the oracle | ⚠️ interchangeable, **not** byte-identical — see below |

**Compress-mode output is not byte-identical, and cannot be.** RFA Pack 1.7 embeds a
2003-era LZO whose `lzo1x_1_compress` picks different — equally valid — matches than the
miniLZO 2.10 we vendor, so the streams differ in length while decompressing to identical
bytes. (Its empty-file stream is 4 bytes where miniLZO's is 3, which is also why
`lzo1x_decompress_safe` rejects it with `LZO_E_INPUT_NOT_CONSUMED`.)
`writer_compress_archive_is_interchangeable_with_the_oracle` therefore asserts what
actually matters: same version, entry order, names, uncompressed sizes and opaque fields,
and payload-for-payload equality after decompression. Byte-matching would mean vendoring
the oracle's LZO version, which buys nothing.

**Not done in this phase:** `-u` in-place update, and a smoke test on a 741 MB archive.
Peak writer memory is bounded by the largest single file (source plus its encoded block),
not by the archive, because files are encoded and appended one at a time.

### Phase 3 — CLI tools
1. `rfaUnpack.cc` / `rfaPack.cc` reproducing §2.4 exactly — **including the chatter strings**,
   so existing pipelines and the `.ps1` skills keep working.
2. Compatibility tests: for each fixture, run **ours** and the **original oracle** on the same
   input and compare (a) extracted file trees byte-for-byte, (b) the archive name/size table,
   (c) exit code. (Extracted trees must match; archive bytes may legitimately differ.)
3. New opt-in features (never change default behaviour):
   * **fix `-f`** — real name matching, so the broken original behaviour is strictly improved.
   * **file replacement** — `-replace <internal/path>=<localFile>` (single or repeated),
     and an in-place `rfaPack -u` that is transactional: write temp, fsync, atomic rename,
     keep `.bak`.
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
| `-u` update semantics unknown | high | probe the original with controlled archives in Phase 0 (same method that cracked the codec); probe matrix is cheap |
| Entry `flags`/`reserved` semantics unknown | medium | sample many archives; **preserve originals verbatim** on `-u` rather than inventing values |
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
4. ▶ **Next: Phase 3** — both CLIs, then the oracle-comparison suite driven by
   `tools/rfa_golden.py`.

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
| 17 | Compress-mode output **cannot** be byte-identical to the oracle | RFA Pack 1.7's 2003-era LZO picks different but equally valid matches than miniLZO 2.10. Store mode **is** byte-identical — the stronger result, since one comparison pins the stamp, first data offset, reserved field, version, table layout and entry order together. Compress mode is asserted semantically instead. |
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
* `-u` semantics still need probing before implementation.
