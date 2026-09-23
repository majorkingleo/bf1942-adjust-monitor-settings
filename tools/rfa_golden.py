#!/usr/bin/env python3
"""Capture the original tools' exact stdout/stderr/exit codes as golden files.

The reimplementation must reproduce the originals' user-visible behaviour, including
the terse error strings that the `.ps1` skills in `.github/skills/` pattern-match on
("Directory does not exist", "Name Not found!", "item out of range to extract").
Those strings are captured here, once, so the C++ tests can assert on them without
hard-coding them a second time.

Absolute paths are normalised to tokens before writing, so the golden file is
stable across machines and across runs:

    {ARCHIVE}  basename of the archive      {OUTDIR}   the extract target
    {LISTFILE} the generated list file      {SRCDIR}   the pack source directory
    {TMP}      the scratch root

Usage:
    python tools/rfa_golden.py            # (re)write tests/golden/oracle-cli.json
    python tools/rfa_golden.py --check    # re-run and diff against the stored golden
    python tools/rfa_golden.py --show     # print the stored golden in readable form
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from rfa_oracle import PACK_ORACLE, REPO_ROOT, UNPACK_ORACLE, run  # noqa: E402
from rfa_probe import parse  # noqa: E402

GOLDEN = REPO_ROOT / "tests" / "golden" / "oracle-cli.json"
FH_ARCHIVE = REPO_ROOT / "tests" / "data" / "fh" / "Battle_Of_Pavlov-1942.rfa"
TINY_ARCHIVE = REPO_ROOT / "tests" / "data" / "tiny" / "salerno_001.rfa"


def _tokens(result: dict, tmp: Path, substitutions: dict[str, str]) -> dict:
    """Replace volatile values with stable tokens so the golden file is portable."""
    out = dict(result)
    for key in ("stdout", "stderr"):
        text = result.get(key, "")
        text = text.replace(str(tmp), "{TMP}")
        for token, value in substitutions.items():
            text = text.replace(str(value), token)
        # rfaPack reports elapsed milliseconds, which naturally varies per run.
        text = re.sub(r"TimeTaken: \d+", "TimeTaken: {MS}", text)
        text = text.replace("\r\n", "\n").rstrip("\n")
        out[key] = text
    for key in ("archive", "outdir", "listfile", "srcdir", "binary"):
        if key in out:
            value = str(out[key])
            value = value.replace(str(tmp), "{TMP}")
            for token, replacement in substitutions.items():
                value = value.replace(str(replacement), token)
            out[key] = value
    out.pop("tree", None)
    return out


def capture() -> dict:
    tmp = Path(tempfile.mkdtemp(prefix="rfa_golden_"))
    scenarios: dict[str, dict] = {}
    try:
        names = [e.name for e in sorted(parse(str(FH_ARCHIVE)).entries, key=lambda e: e.name)]
        # a name that certainly exists, and one that certainly does not
        hit, miss = names[0], "no/such/path/Nonexistent.con"

        list_ok = tmp / "list_ok.lst"
        list_ok.write_text(hit + "\n" + names[1] + "\n", encoding="ascii")
        list_base = tmp / "list_base.lst"
        list_base.write_text(os.path.basename(hit) + "\n", encoding="ascii")

        existing = tmp / "existing"
        existing.mkdir()

        # Every extract target must pre-exist: rfaUnpack.exe aborts with
        # "Error! Directory does not exist" otherwise, which would mask the
        # behaviour these scenarios are meant to capture.
        by_list = tmp / "by_list"
        by_base = tmp / "by_base"
        full = tmp / "full"
        for d in (by_list, by_base, full):
            d.mkdir()

        subs_fh = {"{ARCHIVE}": FH_ARCHIVE, "{LISTFILE}": list_ok}
        subs_tiny = {"{ARCHIVE}": TINY_ARCHIVE}

        def add(name: str, result: dict, substitutions: dict, **extra) -> None:
            entry = _tokens(result, tmp, substitutions)
            entry.update(extra)
            scenarios[name] = entry

        # 1/2 - no arguments at all
        add("unpack_no_args", run(UNPACK_ORACLE, []), {})
        add("pack_no_args", run(PACK_ORACLE, []), {})

        # 3 - output directory does not exist (the single most common failure)
        add(
            "unpack_missing_outdir",
            run(UNPACK_ORACLE, [str(TINY_ARCHIVE), str(tmp / "does_not_exist")]),
            subs_tiny,
        )

        # 4/5 - by 0-based index, in range and out of range
        add("unpack_index_0", run(UNPACK_ORACLE, [str(FH_ARCHIVE), str(existing), "-i0"]), subs_fh)
        add(
            "unpack_index_out_of_range",
            run(UNPACK_ORACLE, [str(FH_ARCHIVE), str(existing), "-i9999"]),
            subs_fh,
        )

        # 6 - list file with full internal paths (works)
        add(
            "unpack_list_full_paths",
            run(UNPACK_ORACLE, [str(FH_ARCHIVE), str(by_list), f"-l{list_ok}"]),
            subs_fh,
        )

        # 7 - list file with a bare basename (fails per entry, run continues)
        add(
            "unpack_list_basename",
            run(UNPACK_ORACLE, [str(FH_ARCHIVE), str(by_base), f"-l{list_base}"]),
            subs_fh,
        )

        # 8 - -f is broken in the shipped build; recorded so our fix is a deliberate change
        add("unpack_f_broken", run(UNPACK_ORACLE, [str(FH_ARCHIVE), str(existing), "-fInit.con"]), subs_fh)

        # 9 - full extract, to capture the chatter of a normal run
        add(
            "unpack_full_tiny",
            run(UNPACK_ORACLE, [str(TINY_ARCHIVE), str(full)]),
            subs_tiny,
        )

        # 10 - pack with no -Compress, and with -Compress
        srcdir = tmp / "src" / "menu"
        srcdir.mkdir(parents=True)
        (srcdir / "a.txt").write_text("hello rfa\n" * 40, encoding="ascii")
        add(
            "pack_plain",
            run(PACK_ORACLE, [str(tmp / "src"), "menu", str(tmp / "plain.rfa")]),
            {},
        )
        add(
            "pack_compress",
            run(PACK_ORACLE, [str(tmp / "src"), "menu", str(tmp / "comp.rfa"), "-Compress"]),
            {},
        )
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    return {
        "note": (
            "Verbatim stdout/stderr/exit codes of the original vendor binaries. "
            "Regenerate with: python tools/rfa_golden.py"
        ),
        "generatedBy": "tools/rfa_golden.py",
        "oracle": {
            "unpack": str(UNPACK_ORACLE.relative_to(REPO_ROOT)),
            "pack": str(PACK_ORACLE.relative_to(REPO_ROOT)),
        },
        "caveats": [
            "Line ORDER within stdout is not stable under redirection: the banner is "
            "written through a different stream than the progress chatter, so piping "
            "reorders them relative to what a console user sees. Compare message "
            "presence, not emission order.",
            "`TimeTaken: {MS}` is genuinely variable and has been normalised.",
            "Exit codes are not a reliable success signal in the original: "
            "`-i9999` (index out of range) and the broken `-f` path both print an "
            "error yet exit 0.",
            "`-f` cannot match any file in this build, so `unpack_f_broken` records "
            "the broken behaviour deliberately; fixing it is an intentional divergence.",
        ],
        "scenarios": scenarios,
    }


def show(golden: dict) -> None:
    for name, entry in golden["scenarios"].items():
        print(f"=== {name}  rc={entry['rc']} ===")
        print(f"argv: {entry.get('argv')}")
        if entry.get("stdout"):
            print("--- stdout ---")
            print(entry["stdout"])
        if entry.get("stderr"):
            print("--- stderr ---")
            print(entry["stderr"])
        print()


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--check", action="store_true", help="re-run and diff against the stored golden")
    p.add_argument("--show", action="store_true", help="print the stored golden")
    args = p.parse_args(argv)

    if args.show:
        if not GOLDEN.is_file():
            print(f"error: no golden at {GOLDEN}; run without --show first", file=sys.stderr)
            return 1
        show(json.loads(GOLDEN.read_text(encoding="utf-8")))
        return 0

    current = capture()

    if args.check:
        if not GOLDEN.is_file():
            print(f"error: no golden at {GOLDEN}", file=sys.stderr)
            return 1
        stored = json.loads(GOLDEN.read_text(encoding="utf-8"))
        bad = 0
        for name, want in stored["scenarios"].items():
            got = current["scenarios"].get(name)
            if got is None:
                print(f"MISSING  {name}")
                bad += 1
                continue
            for field in ("rc", "stdout", "stderr"):
                if want.get(field) != got.get(field):
                    print(f"CHANGED  {name}.{field}")
                    print(f"  want: {want.get(field)!r}")
                    print(f"  got : {got.get(field)!r}")
                    bad += 1
        print("golden matches" if not bad else f"{bad} difference(s)")
        return 1 if bad else 0

    GOLDEN.parent.mkdir(parents=True, exist_ok=True)
    GOLDEN.write_text(json.dumps(current, indent=2), encoding="utf-8")
    print(f"wrote {GOLDEN.relative_to(REPO_ROOT)} ({len(current['scenarios'])} scenarios)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
