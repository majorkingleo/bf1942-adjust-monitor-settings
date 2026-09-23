#!/usr/bin/env python3
"""Drive the original vendor `rfaPack` / `rfaUnpack` binaries as a test oracle.

The originals are the compatibility reference for the reimplementation: the C++
tests compare our output against theirs on the same input. This script wraps them
so the comparison is easy to drive from Python, from CI, or by hand while
debugging, and so their chatter is captured rather than printed.

By default it uses the immutable backups `bin/rfaPack.orig.exe` and
`bin/rfaUnpack.orig.exe`. Those copies are never overwritten by our builds, so
the oracle stays stable once the new binaries are promoted into `bin/`.

Usage:
    python tools/rfa_oracle.py unpack <archive.rfa> <outdir> [extra args...]
    python tools/rfa_oracle.py pack <srcdir> <baseFolderName> <archive.rfa> [extra args...]
    python tools/rfa_oracle.py tree <dir> [--json]

Options:
    --json PATH    write the captured result (rc, stdout, stderr, file tree) to PATH
    --no-create    do not pre-create <outdir> for `unpack` (reproduces the
                   "Directory does not exist" failure path)
    --bin PATH     use a specific oracle binary instead of the default

Examples:
    # full extract, then list the resulting tree
    python tools/rfa_oracle.py unpack tests/data/fh/Battle_Of_Pavlov-1942.rfa out/fh
    python tools/rfa_oracle.py tree out/fh

    # extract a single entry by 0-based index
    python tools/rfa_oracle.py unpack tests/data/fh/Battle_Of_Pavlov-1942.rfa out/x -i0
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
BIN_DIR = REPO_ROOT / "bin"

UNPACK_ORACLE = BIN_DIR / "rfaUnpack.orig.exe"
PACK_ORACLE = BIN_DIR / "rfaPack.orig.exe"


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest().upper()


def snapshot(root: str | Path) -> dict[str, dict]:
    """Map every file under `root` to {size, sha256}, keyed by forward-slash relpath."""
    root = Path(root)
    tree: dict[str, dict] = {}
    if not root.exists():
        return tree
    for path in sorted(root.rglob("*")):
        if path.is_file():
            rel = path.relative_to(root).as_posix()
            tree[rel] = {"size": path.stat().st_size, "sha256": sha256_file(path)}
    return tree


def run(binary: Path, argv: list[str], cwd: str | Path | None = None) -> dict:
    if not binary.is_file():
        raise FileNotFoundError(f"oracle binary not found: {binary}")
    proc = subprocess.run(
        [str(binary), *argv],
        capture_output=True,
        cwd=str(cwd) if cwd else None,
    )
    return {
        "binary": str(binary),
        "argv": argv,
        "rc": proc.returncode,
        "stdout": proc.stdout.decode("utf-8", "replace"),
        "stderr": proc.stderr.decode("utf-8", "replace"),
    }


def oracle_unpack(archive: str, outdir: str, extra: list[str], binary: Path, create: bool) -> dict:
    if create:
        os.makedirs(outdir, exist_ok=True)
    result = run(binary, [archive, outdir, *extra])
    result["archive"] = archive
    result["outdir"] = outdir
    result["tree"] = snapshot(outdir)
    result["fileCount"] = len(result["tree"])
    return result


def oracle_pack(srcdir: str, base: str, archive: str, extra: list[str], binary: Path) -> dict:
    os.makedirs(os.path.dirname(os.path.abspath(archive)) or ".", exist_ok=True)
    result = run(binary, [srcdir, base, archive, *extra])
    result["srcdir"] = srcdir
    result["base"] = base
    result["archive"] = archive
    if os.path.isfile(archive):
        result["archiveSize"] = os.path.getsize(archive)
        result["archiveSha256"] = sha256_file(Path(archive))
    return result


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    p.add_argument("mode", choices=["unpack", "pack", "tree"])
    p.add_argument("args", nargs="*", help="positional arguments for the chosen mode")
    p.add_argument("--json", metavar="PATH", help="write the captured result as JSON")
    p.add_argument("--no-create", action="store_true", help="do not pre-create the output dir")
    p.add_argument("--bin", metavar="PATH", help="override the oracle binary")
    args = p.parse_args(argv)

    if args.mode == "tree":
        if len(args.args) != 1:
            p.error("tree requires exactly one directory")
        print(json.dumps(snapshot(args.args[0]), indent=2))
        return 0

    if args.mode == "unpack":
        if len(args.args) < 2:
            p.error("unpack requires <archive.rfa> <outdir>")
        archive, outdir, extra = args.args[0], args.args[1], args.args[2:]
        binary = Path(args.bin) if args.bin else UNPACK_ORACLE
        result = oracle_unpack(archive, outdir, extra, binary, not args.no_create)
    else:
        if len(args.args) < 3:
            p.error("pack requires <srcdir> <baseFolderName> <archive.rfa>")
        srcdir, base, archive, extra = args.args[0], args.args[1], args.args[2], args.args[3:]
        binary = Path(args.bin) if args.bin else PACK_ORACLE
        result = oracle_pack(srcdir, base, archive, extra, binary)

    if args.json:
        Path(args.json).write_text(json.dumps(result, indent=2), encoding="utf-8")

    print(f"rc={result['rc']}")
    if result["stdout"].strip():
        print("--- stdout ---")
        print(result["stdout"].rstrip())
    if result["stderr"].strip():
        print("--- stderr ---")
        print(result["stderr"].rstrip())
    if "fileCount" in result:
        print(f"--- files written: {result['fileCount']} ---")
    return result["rc"]


if __name__ == "__main__":
    sys.exit(main())
