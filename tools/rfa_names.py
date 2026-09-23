#!/usr/bin/env python3
"""Print the directory table of a `.rfa` as TSV, for diffing and for `-l` list files.

Deliberately narrower than `rfa_probe.py`: this emits one line per entry, sorted by
name, in a form that is stable enough to diff between two archives (including
between our reimplementation and the original tools) — which is what the
compatibility tests need.

    name <TAB> storedSize <TAB> uncompressedSize <TAB> flags <TAB> compressed(0|1)

Usage:
    python tools/rfa_names.py <archive.rfa> [--no-header]

Compare against the PowerShell lister:
    powershell -NoProfile -ExecutionPolicy Bypass \\
        -File .github/skills/bf1942-standalone-map/scripts/list_rfa.ps1 <archive> -Names
"""

from __future__ import annotations

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from rfa_probe import parse  # noqa: E402


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("archive")
    p.add_argument("--no-header", action="store_true", help="omit the column header line")
    args = p.parse_args(argv)

    if not os.path.isfile(args.archive):
        print(f"error: no such file: {args.archive}", file=sys.stderr)
        return 1

    try:
        arc = parse(args.archive)
    except ValueError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    if not args.no_header:
        print("name\tstoredSize\tuncompressedSize\tflags\tcompressed")

    for e in sorted(arc.entries, key=lambda e: e.name):
        print(
            f"{e.name}\t{e.stored_size}\t{e.uncompressed_size}\t"
            f"0x{e.flags:08X}\t{1 if e.payload_is_compressed else 0}"
        )

    for problem in arc.problems:
        print(f"warning: {problem}", file=sys.stderr)

    return 1 if arc.problems else 0


if __name__ == "__main__":
    sys.exit(main())
