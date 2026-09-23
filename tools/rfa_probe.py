#!/usr/bin/env python3
"""Dump and validate the container structure of a Battlefield 1942 `.rfa` archive.

This reads only the directory table plus each entry's 16-byte block header. It
never decompresses a payload, so it is safe on 700 MB archives and needs no LZO
support. Use it to check that a file really is a well-formed RFA, to compare the
name/size table of two archives, or to grab the exact internal path names that
`rfaUnpack.exe -l` requires.

Usage:
    python tools/rfa_probe.py <archive.rfa> [--names] [--json] [--payload-hex N]
                              [--quiet]

Exit status: 0 if the archive parsed and validated, 1 otherwise.

Reference:
    .github/skills/bf1942-standalone-map/references/rfa-format.md
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import struct
import sys
from dataclasses import dataclass, field

BLOCK_HEADER_SIZE = 16  # chunkCount + the first chunk descriptor
CHUNK_DESCRIPTOR_SIZE = 12  # compressedSize + uncompressedSize + payloadOffset
CHUNK_SIZE = 32768  # 32 KiB; every chunk except the last is exactly this big
ENTRY_FIELDS_SIZE = 24  # 6 x u32
MIN_DATA_OFFSET = 8  # right after the 8-byte file header


@dataclass
class Chunk:
    compressed_size: int
    uncompressed_size: int
    payload_offset: int

    @property
    def is_compressed(self) -> bool:
        # Inequality, NOT less-than: LZO1X expands incompressible input, and a
        # compressedSize == uncompressedSize chunk is the verbatim case.
        return self.compressed_size != self.uncompressed_size


@dataclass
class Entry:
    index: int
    name: str
    name_len: int
    stored_size: int
    uncompressed_size: int
    data_offset: int
    reserved: tuple[int, int]
    flags: int
    variant: str = "unknown"  # "raw" | "empty" | "chunked"
    chunks: list["Chunk"] = field(default_factory=list)

    @property
    def chunk_count(self) -> int:
        return len(self.chunks)

    @property
    def header_size(self) -> int:
        """Bytes of chunk table at dataOffset; raw/empty entries have none."""
        if self.variant in ("raw", "empty"):
            return 0
        return BLOCK_HEADER_SIZE + CHUNK_DESCRIPTOR_SIZE * (self.chunk_count - 1)

    @property
    def payload_offset(self) -> int:
        return self.data_offset + self.header_size

    @property
    def payload_is_compressed(self) -> bool:
        return any(c.is_compressed for c in self.chunks)


@dataclass
class Archive:
    path: str
    size: int
    toc_offset: int
    version: int
    entries: list[Entry] = field(default_factory=list)
    problems: list[str] = field(default_factory=list)

    @property
    def compressed_count(self) -> int:
        return sum(1 for e in self.entries if e.payload_is_compressed)

    @property
    def stored_count(self) -> int:
        return sum(1 for e in self.entries if e.variant in ("raw", "empty"))

    @property
    def variant_counts(self) -> dict[str, int]:
        counts: dict[str, int] = {}
        for e in self.entries:
            counts[e.variant] = counts.get(e.variant, 0) + 1
        return counts

    @property
    def flag_values(self) -> list[int]:
        return sorted({e.flags for e in self.entries})


def _u32(buf: bytes, off: int) -> int:
    return struct.unpack_from("<I", buf, off)[0]


def _fill_payload_model(data: bytes, entry: Entry, problems: list[str]) -> None:
    """Decide the payload variant and validate the chunk table, in place.

    The variant is structural, not version-based: 235 entries inside version-1
    archives are raw, and version-0 archives are always raw.
    """
    label = f"entry {entry.index} ({entry.name})"
    doff, stored, unc = entry.data_offset, entry.stored_size, entry.uncompressed_size

    if doff < MIN_DATA_OFFSET:
        problems.append(f"{label}: dataOffset {doff} sits inside the file header")
        return
    if doff + stored > len(data):
        problems.append(f"{label}: data block [{doff}, {doff + stored}) runs past EOF")
        return

    if unc == 0:
        entry.variant = "empty"
        return
    if stored == unc:
        # no block header at all - the payload starts at dataOffset
        entry.variant = "raw"
        return

    if doff + BLOCK_HEADER_SIZE > len(data):
        problems.append(f"{label}: chunk header runs past EOF")
        return

    chunk_count, c0, u0, rel0 = struct.unpack_from("<IIII", data, doff)
    if chunk_count == 0 or chunk_count > 1_000_000:
        problems.append(f"{label}: implausible chunkCount {chunk_count}")
        return

    entry.variant = "chunked"
    header = BLOCK_HEADER_SIZE + CHUNK_DESCRIPTOR_SIZE * (chunk_count - 1)
    if doff + header > len(data):
        problems.append(f"{label}: chunk descriptor table runs past EOF")
        return

    chunks = [Chunk(c0, u0, rel0)]
    for k in range(1, chunk_count):
        c, u, rel = struct.unpack_from(
            "<III", data, doff + BLOCK_HEADER_SIZE + CHUNK_DESCRIPTOR_SIZE * (k - 1)
        )
        chunks.append(Chunk(c, u, rel))
    entry.chunks = chunks

    expected = max(1, -(-unc // CHUNK_SIZE))
    if chunk_count != expected:
        problems.append(
            f"{label}: chunkCount {chunk_count} != ceil(unc/{CHUNK_SIZE}) = {expected}"
        )
    total_compressed = sum(c.compressed_size for c in chunks)
    if stored != header + total_compressed:
        problems.append(
            f"{label}: storedSize {stored} != {header} + sum(compressedSize) {total_compressed}"
        )
    total_uncompressed = sum(c.uncompressed_size for c in chunks)
    if total_uncompressed != unc:
        problems.append(
            f"{label}: sum(chunk uncompressedSize) {total_uncompressed} != {unc}"
        )
    running = 0
    for k, chunk in enumerate(chunks):
        if chunk.payload_offset != running:
            problems.append(
                f"{label}: chunk {k} payloadOffset {chunk.payload_offset} != {running}"
            )
        running += chunk.compressed_size
        if chunk.uncompressed_size > CHUNK_SIZE:
            problems.append(
                f"{label}: chunk {k} uncompressedSize {chunk.uncompressed_size} > {CHUNK_SIZE}"
            )
        elif k < len(chunks) - 1 and chunk.uncompressed_size != CHUNK_SIZE:
            problems.append(f"{label}: non-final chunk {k} is not full")


def parse(path: str, read_headers: bool = True) -> Archive:
    """Parse an RFA archive. Raises ValueError on a structurally unusable file."""
    with open(path, "rb") as fh:
        data = fh.read()

    if len(data) < 8:
        raise ValueError(f"{path}: file is only {len(data)} bytes, too small to hold a header")

    toc_offset = _u32(data, 0)
    version = _u32(data, 4)
    arc = Archive(path=path, size=len(data), toc_offset=toc_offset, version=version)

    if version not in (0, 1):
        # Both 0 and 1 occur in the wild (version 0 archives are always raw-payload)
        # and rfaUnpack.exe reads both, so neither is an error.
        arc.problems.append(f"version is {version}; only 0 and 1 have been observed")
    if not (MIN_DATA_OFFSET <= toc_offset <= len(data) - 4):
        raise ValueError(
            f"{path}: tocOffset {toc_offset} is outside the file (size {len(data)})"
        )

    count = _u32(data, toc_offset)
    off = toc_offset + 4

    for i in range(count):
        if off + 4 > len(data):
            arc.problems.append(f"entry {i}: truncated before nameLen")
            break
        name_len = _u32(data, off)
        off += 4

        if off + name_len > len(data):
            arc.problems.append(f"entry {i}: name of {name_len} bytes runs past EOF")
            break
        raw_name = data[off : off + name_len]
        off += name_len
        # nameLen is the EXACT length; tolerate a stray NUL if some writer emits one.
        name = raw_name.rstrip(b"\x00").decode("latin-1")
        if raw_name.endswith(b"\x00"):
            arc.problems.append(
                f"entry {i}: name is NUL-terminated (nameLen {name_len} for {len(name)} chars)"
            )

        if off + ENTRY_FIELDS_SIZE > len(data):
            arc.problems.append(f"entry {i}: truncated in the fixed fields")
            break
        (
            stored_size,
            uncompressed_size,
            data_offset,
            reserved1,
            reserved2,
            flags,
        ) = struct.unpack_from("<IIIIII", data, off)
        off += ENTRY_FIELDS_SIZE

        entry = Entry(
            index=i,
            name=name,
            name_len=name_len,
            stored_size=stored_size,
            uncompressed_size=uncompressed_size,
            data_offset=data_offset,
            reserved=(reserved1, reserved2),
            flags=flags,
        )

        if read_headers:
            _fill_payload_model(data, entry, arc.problems)

        arc.entries.append(entry)

    # table terminator
    if off + 4 <= len(data):
        terminator = _u32(data, off)
        if terminator != 0:
            arc.problems.append(f"table terminator is 0x{terminator:08X}, expected 0")
        off += 4
        if off != len(data):
            arc.problems.append(
                f"{len(data) - off} bytes follow the directory table (table ends at {off})"
            )
    else:
        arc.problems.append("directory table is truncated before its terminator")

    return arc


def payload_hex(path: str, entry: Entry, nbytes: int) -> str:
    """Hex of the first chunk's compressed bytes (empty for raw entries)."""
    if not entry.chunks:
        return ""
    with open(path, "rb") as fh:
        fh.seek(entry.payload_offset)
        payload = fh.read(min(nbytes, entry.chunks[0].compressed_size))
    return payload.hex(" ")


def to_json(arc: Archive) -> dict:
    return {
        "path": os.path.basename(arc.path),
        "size": arc.size,
        "tocOffset": arc.toc_offset,
        "version": arc.version,
        "fileCount": len(arc.entries),
        "compressedCount": arc.compressed_count,
        "storedCount": arc.stored_count,
        "flagValues": [f"0x{f:08X}" for f in arc.flag_values],
        "problems": arc.problems,
        "entries": [
            {
                "index": e.index,
                "name": e.name,
                "nameLen": e.name_len,
                "storedSize": e.stored_size,
                "uncompressedSize": e.uncompressed_size,
                "dataOffset": e.data_offset,
                "flags": f"0x{e.flags:08X}",
                "variant": e.variant,
                "chunkCount": e.chunk_count,
                "headerSize": e.header_size,
                "compressed": e.payload_is_compressed,
                "chunks": [
                    {
                        "compressedSize": c.compressed_size,
                        "uncompressedSize": c.uncompressed_size,
                        "payloadOffset": c.payload_offset,
                    }
                    for c in e.chunks
                ],
            }
            for e in arc.entries
        ],
    }


def sha256(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest().upper()


def make_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        description="Dump and validate the container structure of a BF1942 .rfa archive.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p.add_argument("archive", help="path to the .rfa file")
    p.add_argument(
        "--names",
        action="store_true",
        help="print one internal path per line, sorted (for diffing / -l list files)",
    )
    p.add_argument("--json", action="store_true", help="emit the full structure as JSON")
    p.add_argument(
        "--payload-hex",
        type=int,
        metavar="N",
        default=0,
        help="hex-dump the first N bytes of each compressed payload",
    )
    p.add_argument("--quiet", action="store_true", help="suppress the per-entry table")
    p.add_argument("--sha256", action="store_true", help="also print the file hash")
    return p


def main(argv: list[str] | None = None) -> int:
    args = make_parser().parse_args(argv)

    if not os.path.isfile(args.archive):
        print(f"error: no such file: {args.archive}", file=sys.stderr)
        return 1

    try:
        arc = parse(args.archive)
    except ValueError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    if args.json:
        print(json.dumps(to_json(arc), indent=2))
        return 1 if arc.problems else 0

    if args.names:
        for e in sorted(arc.entries, key=lambda e: e.name):
            print(e.name)
        return 1 if arc.problems else 0

    print(f"file            {os.path.basename(arc.path)}")
    print(f"size            {arc.size} bytes")
    print(f"tocOffset       {arc.toc_offset}")
    print(f"version         {arc.version}")
    print(f"fileCount       {len(arc.entries)}")
    print(
        "variants        "
        + ", ".join(f"{k}={v}" for k, v in sorted(arc.variant_counts.items()))
        + "   (raw/empty carry no block header)"
    )
    print(f"compressed      {arc.compressed_count} entries carry at least one LZO1X chunk")
    print(f"flags values    {', '.join(f'0x{f:08X}' for f in arc.flag_values) or '-'}")
    if args.sha256:
        print(f"sha256          {sha256(arc.path)}")

    if not args.quiet and arc.entries:
        print()
        print(
            f"{'#':>4}  {'expanded':>10}  {'stored':>10}  {'dataOff':>9}  "
            f"{'chunks':>6}  {'c':>1}  {'variant':<8}  flags       name"
        )
        for e in arc.entries:
            print(
                f"{e.index:>4}  {e.uncompressed_size:>10}  {e.stored_size:>10}  "
                f"{e.data_offset:>9}  {e.chunk_count:>6}  "
                f"{'C' if e.payload_is_compressed else '.':>1}  "
                f"{e.variant:<8}  0x{e.flags:08X}  {e.name}"
            )

    if args.payload_hex:
        print()
        for e in arc.entries:
            if e.payload_is_compressed:
                print(f"payload[{e.index}] {e.name}:")
                print(f"  {payload_hex(arc.path, e, args.payload_hex)}")

    if arc.problems:
        print()
        print(f"{len(arc.problems)} problem(s):")
        for problem in arc.problems:
            print(f"  - {problem}")
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
