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
  cpputils/cpputilsshared/lzo/           // vendored miniLZO (or clean-room Lzo1x.cc)
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

### Phase 1 — test-suite scaffolding (cpputilstest pattern)
1. Create `testcommon/` with `TestUtils`/`ColBuilder` in the cpputilstest style.
2. Create `src_test_rfa/` with one `test_*.{cc,h}` pair per subject and a runner.
3. Extend `Makefile.am`: `check_PROGRAMS`, `TESTS`, `TESTS_ENVIRONMENT`, plus `EXTRA_DIST`
   for `tests/data`. Wire `make check`.
4. Vendor miniLZO into `cpputils/cpputilsshared/lzo/` **or** add a clean-room
   `Lzo1x.cc` (decision D4) and add it to `noinst_LIBRARIES`.
5. Run `./reconfigure.sh` once so the generated `Makefile.in` knows the new targets.

**Exit gate:** `make check` runs and reports a green (if empty) suite.

### Phase 2 — core library `rfa/`
1. `RfaArchive` — parse the table and **both payload variants** (§2.2): raw
   (`storedSize == uncompressedSize`, no header) and chunked (`chunkCount` + 12-byte
   descriptors). Validate every invariant in §2.2. Expose entries and chunk ranges
   lazily. Must handle a 741 MB `texture.rfa` **without** slurping it into RAM — the
   5693-entry archives have large per-entry descriptor tables too.
2. `LzoCodec` — miniLZO wrappers; **one workmem per thread**; expose a "store raw"
   fallback, since a chunk's `compressedSize` legitimately exceeds its
   `uncompressedSize` for incompressible data.
3. `RfaWriter` — pack a directory tree, `-u` update, deterministic entry ordering and
   layout; emit the raw variant when compression does not pay off.
4. Parallelism: worker pool sized by `std::thread::hardware_concurrency()` (overridable);
   compress/decompress **per 32 KiB chunk** — chunks are independent of one another, so
   the natural unit of work is finer than a whole file. Read the source region in offset
   order so I/O stays sequential.

**Exit gate (unit tests):**
* table round-trip: parse → serialize → parse, byte-identical table.
* variant coverage: round-trip a raw entry, a single-chunk entry and a multi-chunk entry
   (≥ 100 KiB, i.e. ≥ 4 chunks), plus an empty entry (`uncompressedSize == 0`).
* LZO round-trip over random data, all-empty, all-same, 1-byte, `CHUNK_SIZE ± 1` and
   exact-multiple-of-`CHUNK_SIZE` sizes.
* fuzz: random truncated/corrupt archives must fail cleanly, never crash or over-read.
* determinism: 1 thread vs 8 threads → identical archive bytes.
* sweep: validate every fixture plus a large real archive via `tools/rfa_probe.py`.

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
| D4 | Codec dependency | **Vendor miniLZO** into `cpputils/cpputilsshared/lzo/` (GPLv2+, compatible with this repo's GPLv3) |
| D5 | Deployment | **Shadow in `bin\new\` first** — validate against the real `.ps1` skills before touching the originals |

### Consequences folded into the plan
* `Makefile.am` gains `check_PROGRAMS` + `TESTS` + `EXTRA_DIST`; `reconfigure.sh` is re-run once.
* miniLZO is added to `noinst_LIBRARIES` (or built directly into the two programs + the test runner)
  with `-DMINILZO_HAVE_CONFIG_H` off (standalone build, no config.h requirement).
* Phase 4 step 3 becomes: copy new binaries to `E:\progs\bf_pablov_mod\bin\new\`, run the existing
  `.ps1` skills against them, and only then promote them one level up with `.orig.exe` backups.

---

## 9. Immediate next actions

1. ✅ Back up the oracle binaries; add `tools/rfa_oracle.py` — done, Phase 0 complete.
2. ▶ **Next: Phase 1** — `testcommon/` harness, `src_test_rfa/` skeletons, `check_PROGRAMS`/
   `TESTS` wiring in `Makefile.am`, vendor miniLZO, then `./reconfigure.sh && make check`.
3. Phase 2 — `RfaArchive` + `LzoCodec` with the variant/chunk tests listed above.
4. Phase 3 — both CLIs, then the oracle-comparison suite driven by `tools/rfa_golden.py`.

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
