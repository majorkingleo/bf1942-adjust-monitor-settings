# Battlefield 1942 RFA archive format (verified)

Reverse-engineered and verified against `shaders.rfa` (base game), `aimeshes.rfa` (FH)
and `Battle_Of_Pavlov-1942.rfa` (FH). All little-endian.

## File layout

| Offset | Size | Field |
|--------|------|-------|
| 0      | 4    | `directoryOffset` (u32) — where the file table starts |
| 4      | 4    | `version` (u32) — always `1` |
| 8      | ...  | file data (contiguous; each entry raw or LZO1X-compressed) |
| `directoryOffset` | ... | directory (file table) |

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
u32 storedSize           // bytes occupied in the data section = 16 + payloadSize
u32 uncompressedSize     // size after decompression
u32 dataOffset           // absolute offset of this file's data (the 16-byte sub-header)
u32 reserved             // 0 in the archives observed so far
u32 reserved             // 0 in the archives observed so far
u32 flags                // PER-ENTRY, not an archive constant (see below)
```

## File data (at `dataOffset`)

```
u32 tag                  // always 1
u32 payloadSize          // size of the payload that follows
u32 uncompressedSize     // same as in the directory entry
u32 reserved            // always 0
byte[payloadSize] payload
```

If `payloadSize < uncompressedSize`, the payload is **LZO1X**-compressed (the
Oberhumer LZO1X-1 stream format, as implemented by miniLZO's `lzo1x_1_compress`
and `lzo1x_decompress`). It is NOT zlib/deflate, NOT FastLZ and NOT RefPack —
7-Zip and .NET `DeflateStream` fail on it.
If `payloadSize == uncompressedSize`, the payload is the raw file bytes.

Verified on this machine: packing incompressible N-byte input with
`rfaPack.exe -Compress` gives `payloadSize = N + 4`, whose first byte is `N + 17`
(the LZO1X literal-run length byte), followed by N literals and the 3-byte LZO1X
end marker `11 00 00`.

## Facts used when repacking

- `storedSize = 16 + payloadSize`
- `flags` is **per-entry and varies inside a single archive**: a real FH archive
  held 246 entries with `0xFFFFFFFF` and 5 with `0x7C001CD8`. The `0x028A0220`
  quoted in older notes is a misreading of `0x7C001CD8`. Further values observed
  across fixtures: `0x77FCB6DE` and `0x00000000`. `rfaPack.exe` writes `0`
  for new archives and the engine still loads them — so treat this field as
  opaque and **preserve the original value on in-place `-u` updates**.
- Entries are ordered **ascending by name** (ASCII) in the archives observed.
- The directory terminator (the u32 `0` after the last entry) is always zero and
  the table ends at EOF.
- Use `rfaPack.exe` (with `-Compress`) to build archives — it produces exactly
  this format. `scripts/list_rfa.ps1` reads the table without decompressing.
- LZO1X is stateless apart from a caller-supplied work buffer, so decompression
  and compression parallelise cleanly across threads.
