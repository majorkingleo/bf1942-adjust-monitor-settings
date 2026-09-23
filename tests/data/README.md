# `tests/data` — RFA test fixtures

Copied from `E:\progs\bf_pablov_mod` on 2026-09-23. These are **real** Battlefield 1942 /
Forgotten Hope archives used as integration fixtures and as input to the compatibility oracle
(`bin\rfaUnpack.exe`, `bin\rfaPack.exe`).

See `../../.github/skills/bf1942-standalone-map/references/rfa-format.md` for the container spec.

## Contents

| File | Bytes | Entries | LZO1X | stored | `tocOffset` | `flags` values seen | SHA-256 |
|---|---|---|---|---|---|---|---|
| `fh/Battle_Of_Pavlov-1942.rfa` | 6,002,393 | 251 | 248 | 3 | 5,975,511 | `0x7C001CD8`, `0xFFFFFFFF` | `7DCAEFE37266F797876B6859544F316B5FE2F65460F284E809A4062FA3A1CDEA` |
| `tiny/standardMesh_001.rfa` | 2,199 | 6 | 6 | 0 | 1,837 | `0x00000000` | `B33EDB3ABD09CE3220D392F22400C24D3859D3963D6A91D19B23AF63D7DB9A08` |
| `tiny/salerno_001.rfa` | 1,355 | 1 | 1 | 0 | 1,289 | `0x77FCB6DE` | `084314E9C333DB18111360D5C606FFA5A5B1E8137FFA3A720E9C8290A19DD591` |
| `tiny/Peenemunde_001.rfa` | 1,221 | 1 | 0 | 1 | 1,147 | `0x00000000` | `F531156299CAD5370F73E9C8B94CA129C20A20F4087683655009FDBC714CE46C` |

### Provenance

| Destination | Source inside `E:\progs\bf_pablov_mod` |
|---|---|
| `fh/Battle_Of_Pavlov-1942.rfa` | `origin\Mods\FH\Archives\bf1942\levels\Battle_Of_Pavlov-1942.rfa` |
| `tiny/standardMesh_001.rfa` | `bf_1942_kwg_mod\Mods\bf_1942_kwg_mod\Archives\standardMesh_001.rfa` |
| `tiny/salerno_001.rfa` | `origin\Mods\XPack1\Archives\Bf1942\Levels\salerno_001.rfa` |
| `tiny/Peenemunde_001.rfa` | `work\good_copy\Mods\XPack2\Archives\bf1942\Levels\Peenemunde_001.rfa` |

## Why these four

* **`fh/Battle_Of_Pavlov-1942.rfa`** — the archive the original CLI probe matrix was recorded
  against (251 files, 11.49 MB extracted). It is the main integration fixture: it contains both
  compressed and stored entries, and **two different `flags` values in one archive**.
* **`tiny/salerno_001.rfa`** — smallest compressed archive; a single LZO1X entry with a third
  distinct `flags` value.
* **`tiny/Peenemunde_001.rfa`** — smallest archive overall; a single **stored** (uncompressed)
  entry, so it exercises the `payloadSize == uncompressedSize` path.
* **`tiny/standardMesh_001.rfa`** — several entries, all compressed, `flags == 0`.

Together the tiny fixtures cover both payload paths and three distinct `flags` values for very
little repository weight.

## Notes / caveats

* ⚠️ `tiny/standardMesh_001.rfa` measured **1,521 bytes** when it was first surveyed earlier the
  same day and **2,199 bytes** when copied. `bf_pablov_mod` appears to be an active pipeline
  workspace, so treat all fixtures as **snapshots** and rely on the SHA-256 column to detect
  drift. Generate synthetic fixtures in tests for anything that must be byte-stable.
* `flags` is **per-entry and not an archive constant** — observed values so far:
  `0xFFFFFFFF`, `0x7C001CD8`, `0x77FCB6DE`, `0x00000000`. Treat it as opaque and preserve it on
  in-place `-u` updates.
* These are input fixtures only. Expected outputs are *not* stored here — the tests derive them
  by running the oracle binaries in `bin\` (`rfaPack.exe` / `rfaUnpack.exe`, currently the
  original vendor builds) instead, so the expectations cannot silently drift from the reference
  implementation. When the new builds are promoted, the originals are renamed to
  `*.orig.exe` and become the oracle.
