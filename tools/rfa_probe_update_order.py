#!/usr/bin/env python3
"""Probe where the original `rfaPack.exe -u` puts an entry it CARRIES OVER.

§2.5 of the plan establishes the rule for `-u`:

    fresh pack of the source tree, with the target's compression policy, plus the
    target's entries whose names are absent from the tree, carried over unchanged

and in the 9 probed cases the result was byte-identical to a fresh pack of the tree
under the target's policy - except for the deletion case, where the fresh pack of the
reduced tree legitimately lacks the carried-over entry. So that comparison pinned
everything EXCEPT the position of a carried-over entry: nothing in those 9 cases says
whether a retained entry is merged into the tree's name order or appended after it.

That matters, because a file table is a sequence and byte-identity is the goal. This
script asks the oracle the one remaining question with a tree whose names split the
archive's own names apart:

    baseline archive : a.txt   c.txt   e.txt
    update tree      : b.txt   d.txt

    appended after the tree   ->  b.txt  d.txt  a.txt  c.txt  e.txt
    merged, canonical order   ->  a.txt  b.txt  c.txt  d.txt  e.txt

Three cases separate a couple of plausible rules, and every case starts from a freshly
packed baseline so they cannot influence one another.

Usage:
    python tools/rfa_probe_update_order.py [--verbose]
"""

from __future__ import annotations

import argparse
import shutil
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import rfa_oracle  # noqa: E402
import rfa_probe  # noqa: E402

BASE = "menu"

# Each case: the baseline tree, the update tree, and what each candidate rule predicts.
CASES: list[tuple[str, dict[str, bytes], dict[str, bytes]]] = [
    (
        "interleaved",
        {"a.txt": b"alpha\r\n", "c.txt": b"charlie\r\n", "e.txt": b"echo\r\n"},
        {"b.txt": b"bravo\r\n", "d.txt": b"delta\r\n"},
    ),
    (
        "all-carried-over-at-the-front",
        {"m.txt": b"mike\r\n", "n.txt": b"november\r\n"},
        {"a.txt": b"alpha\r\n", "z.txt": b"zulu\r\n"},
    ),
    (
        "one-new-one-dropped",
        {"b.txt": b"bravo\r\n", "y.txt": b"yankee kept\r\n"},
        {"a.txt": b"alpha\r\n", "c.txt": b"charlie\r\n"},
    ),
]


def write_tree(root: Path, files: dict[str, bytes]) -> None:
    if root.exists():
        shutil.rmtree(root)
    for relative, data in files.items():
        path = root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)


def entry_names(archive: Path) -> list[str]:
    parsed = rfa_probe.parse(str(archive))
    return [entry.name.split("/", 1)[-1] for entry in parsed.entries]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()

    oracle = rfa_oracle.PACK_ORACLE

    if not oracle.is_file():
        raise SystemExit(f"the oracle is missing: {oracle}")

    scratch = Path(tempfile.mkdtemp(prefix="rfa_update_order_"))
    print(f"scratch: {scratch}\n")

    try:
        for name, baseline_tree, update_tree in CASES:
            root = scratch / name
            baseline_dir = root / "baseline"
            update_dir = root / "update"

            write_tree(baseline_dir, baseline_tree)
            write_tree(update_dir, update_tree)

            archive = root / "target.rfa"
            baseline = rfa_oracle.oracle_pack(
                str(baseline_dir), BASE, str(archive), [], oracle
            )

            if not archive.is_file():
                print(f"{name}: the baseline pack failed\n{baseline}\n")
                continue

            before = entry_names(archive)

            update = rfa_oracle.oracle_pack(
                str(update_dir), BASE, str(archive), ["-u"], oracle
            )

            if not archive.is_file():
                print(f"{name}: -u produced no archive\n{update}\n")
                continue

            after = entry_names(archive)
            carried = [item for item in before if item not in after[: len(before)]]

            print(f"{name}")
            print(f"  baseline archive : {'  '.join(before)}")
            print(f"  update tree      : {'  '.join(sorted(update_tree))}")
            print(f"  after -u         : {'  '.join(after)}")

            tree_names = sorted(update_tree)
            merged = sorted(before + tree_names)
            appended = tree_names + before

            if after == merged:
                verdict = "MERGED into canonical name order"
            elif after == appended:
                verdict = "APPENDED after the tree's own entries"
            else:
                verdict = "neither rule predicts it"

            print(f"  merged predict   : {'  '.join(merged)}")
            print(f"  appended predict : {'  '.join(appended)}")
            print(f"  -> {verdict}")

            if args.verbose:
                print(f"  pack rc={baseline['returncode']} / update rc={update['returncode']}")
                print(f"  carried over: {'  '.join(carried) or '(none)'}")

            print()

        print("scratch kept for inspection; delete it by hand")
    except BaseException:
        shutil.rmtree(scratch, ignore_errors=True)
        raise

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
