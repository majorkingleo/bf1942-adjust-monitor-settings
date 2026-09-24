#!/usr/bin/env python3
"""Generate rfa/RfaStamp.h from the bytes rfaPack.exe writes before the first data block.

Every observed real archive reserves 148 bytes between the 8-byte file header and the
first data block, so the first entry's dataOffset is 156 rather than 8. The content of
that region is not referenced by any entry and its meaning is unknown, but it is
byte-identical across archives produced by the same producer (rfaPack.exe output, and
several shipped DICE archives; the Forgotten Hope archive differs).

Two things are known about it:

* it is NOT required to read an archive - a hand-built compact archive whose data starts
  at offset 8 is read correctly by the original rfaUnpack.exe
* `-u` updates must preserve the existing archive's stamp, not overwrite it

Because transcribing 148 opaque bytes by hand is error-prone, this script extracts them
from a freshly built reference archive and writes the header. Re-run it only if the
reference producer changes.

Usage:
    python tools/rfa_stamp_gen.py [--check]

--check regenerates in memory and compares against the committed header.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
PACK = REPO_ROOT / "bin" / "rfaPack.orig.exe"
HEADER = REPO_ROOT / "rfa" / "RfaStamp.h"

STAMP_START = 8
STAMP_SIZE = 148
EXPECTED_OFFSET = STAMP_START + STAMP_SIZE  # 156


def build_reference_archive(tmp: Path) -> Path:
    src = tmp / "menu"
    src.mkdir(parents=True, exist_ok=True)
    (src / "seed.txt").write_bytes(b"stamp reference\n")

    archive = tmp / "reference.rfa"
    proc = subprocess.run(
        [str(PACK), str(src), "menu", str(archive)],
        capture_output=True,
        text=True,
    )
    if not archive.is_file():
        raise SystemExit(f"rfaPack.exe produced no archive (rc={proc.returncode})\n{proc.stdout}\n{proc.stderr}")
    return archive


def extract_stamp() -> tuple[bytes, int, int]:
    tmp = Path(tempfile.mkdtemp(prefix="rfa_stamp_"))
    try:
        archive = build_reference_archive(tmp)
        data = archive.read_bytes()

        toc, version = struct.unpack_from("<II", data, 0)
        count, = struct.unpack_from("<I", data, toc)
        if count == 0:
            raise SystemExit("reference archive has no entries")

        off = toc + 4
        name_len, = struct.unpack_from("<I", data, off)
        off += 4 + name_len
        stored, uncompressed, data_offset, r1, r2, flags = struct.unpack_from("<IIIIII", data, off)

        return data[STAMP_START:EXPECTED_OFFSET], data_offset, len(data)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def render(stamp: bytes, data_offset: int, archive_size: int) -> str:
    digest = hashlib.sha256(stamp).hexdigest().upper()

    rows = []
    for i in range(0, len(stamp), 12):
        chunk = stamp[i : i + 12]
        rows.append("\t" + " ".join(f"0x{b:02X}," for b in chunk))
    body = "\n".join(rows)

    return f"""/**
 * The {STAMP_SIZE}-byte region that precedes the first data block. GENERATED - do not edit by hand.
 *
 * Produced by tools/rfa_stamp_gen.py from a freshly built rfaPack.exe reference archive.
 *
 * Provenance of the reference:
 *   producer                 bin/rfaPack.orig.exe (RFA Pack 1.7)
 *   reference archive size   {archive_size} bytes
 *   first entry dataOffset   {data_offset}   <- {STAMP_START} + {STAMP_SIZE}
 *   sha256 of the region     {digest}
 *
 * What is known:
 *   - no entry references this region, and no parser needs it: an archive whose data
 *     starts at offset 8 reads correctly with the original rfaUnpack.exe
 *   - its meaning is unknown; it is byte-identical across archives from the same producer
 *     (rfaPack output and several shipped DICE archives), but the Forgotten Hope archive
 *     differs, so it is producer-specific rather than a universal constant
 *   - an in-place `-u` update REPLACES it rather than preserving it, even when the target's
 *     region is a different size: the FH archive's region is 4078 bytes at offset 8..4086,
 *     and one `-u` normalised it to these {STAMP_SIZE} bytes at offset {EXPECTED_OFFSET}
 *     (probed - PLAN_rfa_tools.md section 2.5). The earlier note here claimed the opposite;
 *     it was wrong
 *
 * We write it because every observed archive lays its data out this way, making our
 * output structurally identical to the archives the game ships.
 *
 * @author Copyright (c) 2026
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace rfa {{

/// Offset at which the first data block starts in every observed archive.
constexpr std::uint32_t DEFAULT_FIRST_DATA_OFFSET = {EXPECTED_OFFSET}u;

inline const unsigned char * default_stamp()
{{
	static const unsigned char stamp[{STAMP_SIZE}] = {{
{body}
	}};

	return stamp;
}}

inline constexpr std::size_t default_stamp_size()
{{
	return {STAMP_SIZE};
}}

}} // namespace rfa
"""


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true", help="compare against the committed header instead of writing it")
    args = parser.parse_args(argv)

    stamp, data_offset, archive_size = extract_stamp()
    text = render(stamp, data_offset, archive_size)

    if data_offset != EXPECTED_OFFSET:
        print(
            f"warning: reference archive puts the first data block at {data_offset}, "
            f"not {EXPECTED_OFFSET}",
            file=sys.stderr,
        )

    if args.check:
        if not HEADER.is_file():
            print(f"error: no committed header at {HEADER}", file=sys.stderr)
            return 1
        if HEADER.read_text(encoding="utf-8") != text:
            print("RfaStamp.h differs from a freshly generated reference", file=sys.stderr)
            return 1
        print("RfaStamp.h matches the reference stamp")
        return 0

    HEADER.parent.mkdir(parents=True, exist_ok=True)
    HEADER.write_text(text, encoding="utf-8", newline="\n")
    print(f"wrote {HEADER.relative_to(REPO_ROOT)} ({len(stamp)} bytes, sha256 {hashlib.sha256(stamp).hexdigest()[:16]})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
