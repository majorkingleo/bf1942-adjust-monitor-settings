# Battlefield 1942 RFA archive format (verified)

Re-derived and machine-validated on 2026-09-23 against **every** `.rfa` in the
`bf_pablov_mod` tree: 814 archives, 448,127 entries, **99.99% structurally
consistent**. All multi-byte integers are little-endian.

## File layout

| Offset | Size | Field |
|--------|------|-------|
| 0      | 4    | `directoryOffset` (u32) — where the file table starts |
| 4      | 4    | `version` (u32) — `0` or `1`; **does not** select the payload layout |
| 8      | ...  | file data blocks |
| `directoryOffset` | ... | directory (file table), which runs to EOF |

There is a short fixed region before the first data block — the first block is
commonly at offset 156. It is referenced by no entry: treat it as opaque, and do
not regenerate it blindly.

## Directory

```
u32 numFiles
numFiles x entry
u32 0            (4-byte terminator)
```

### Entry

```
u32 nameLen              // EXACT length of the name; there is NO null terminator
char[nameLen] name       // e.g. "bf1942/levels/Battle_Of_Pavlov-1942/Init.con"
u32 storedSize           // bytes occupied by this entry's data block
u32 uncompressedSize     // size after decompression
u32 dataOffset           // absolute offset of this entry's data block
u32 reserved             // 0 in the archives observed so far
u32 reserved             // 0 in the archives observed so far
u32 flags                // PER-ENTRY, not an archive constant (see below)
```

## Payload layout — two variants

The variant is decided **per entry** from the table, *not* from `version`:

### Raw (no block header)

Condition: `storedSize == uncompressedSize`, or `uncompressedSize == 0`
(which stores 4 bytes).

```
byte[storedSize] payload        // at dataOffset, the verbatim file bytes
```

Observed: 34,617 entries — 34,382 in `version 0` archives but also **235 inside
`version 1` archives**, so version is not a reliable discriminator. Decide
structurally, from the sizes.

### Chunked / LZO1X

Condition: `storedSize != uncompressedSize` and `uncompressedSize != 0`
(413,510 entries — the common case).

```
u32 chunkCount
u32 chunk[0].compressedSize
u32 chunk[0].uncompressedSize
u32 chunk[0].payloadOffset      // always 0
(chunkCount - 1) x {
    u32 compressedSize
    u32 uncompressedSize
    u32 payloadOffset           // running sum of the preceding compressedSize values
}
byte[sum(compressedSize)] payload   // chunks concatenated, in order
```

Invariants, all machine-verified:

* `chunkCount == ceil(uncompressedSize / 32768)` — chunks are **32 KiB**
  (`0x8000`); every chunk except the last has `uncompressedSize == 32768`.
* `storedSize == 16 + 12 * (chunkCount - 1) + sum(compressedSize)`
* `sum(chunk.uncompressedSize) == entry.uncompressedSize`
* `payloadOffset[i] == sum(compressedSize[0..i-1])`
* each chunk is independently **LZO1X**-compressed iff
  `compressedSize < uncompressedSize`, otherwise stored verbatim
* `compressedSize` may exceed `uncompressedSize` for incompressible chunks

> ⚠️ Earlier revisions of this document described the 16-byte prefix as a constant
> `tag == 1` sub-header holding one payload. That was wrong on both counts: the
> first field is the **chunk count**, and the descriptor table grows by 12 bytes
> per additional chunk. The error is invisible on single-chunk entries — which is
> exactly what the small fixture archives contain, so it survived several passes.

Compression is **LZO1X** (Oberhumer LZO1X-1, as implemented by miniLZO's
`lzo1x_1_compress` / `lzo1x_decompress`). It is NOT zlib/deflate, NOT FastLZ and
NOT RefPack — 7-Zip and .NET `DeflateStream` fail on it.

Verified: packing incompressible N-byte input with `rfaPack.exe -Compress` yields a
single chunk with `compressedSize == N + 4`, whose payload starts with the LZO1X
literal-run byte `N + 17`. LZO1X streams are stateless apart from a caller-supplied
work buffer, and chunks are independent of one another — so both compression and
decompression parallelise cleanly **per chunk**, which is finer-grained than
per file.

## Facts used when repacking

* `flags` is **per-entry and varies inside a single archive**: a real FH archive
  held 246 entries with `0xFFFFFFFF` and 5 with `0x7C001CD8`. The `0x028A0220`
  quoted in older notes is a misreading of `0x7C001CD8`. Further values observed:
  `0x77FCB6DE`, `0x00000000`. `rfaPack.exe` writes `0` for new archives and the
  engine still loads them — treat this field as opaque and **preserve the original
  value on in-place `-u` updates**.
* Entries are ordered **ascending by name** (ASCII).
* The directory terminator (u32 `0` after the last entry) is always zero, and the
  table runs to EOF.
* Use `rfaPack.exe` (with `-Compress`) to build archives. `scripts/list_rfa.ps1`
  and `tools/rfa_probe.py` read the table without decompressing.
* Entries with `uncompressedSize == 0` store 4 bytes and were excluded from the
  structural validation above — confirm their contents before writing them.
