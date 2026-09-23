#!/usr/bin/env python3
"""Probe the original `rfaPack.exe`'s `-u` (update) semantics.

Nothing in the vendor readme or in the observed CLI chatter explains what `-u`
actually does, so the reimplementation cannot guess at it. This script asks the
oracle directly, with controlled archives and controlled trees, using the same
black-box method that identified the codec:

  * does `-u` append, replace or delete entries?
  * does it rebuild the whole archive, or reuse the existing payload offsets?
  * does it preserve the 148-byte reserved region before the first data block?
  * does it preserve per-entry fields (flags, reserved words)?
  * what does it do when the target archive does not exist?
  * can it change the compression policy of an existing archive?

Every case starts from a freshly packed baseline, so the cases cannot influence
one another, and the scratch directory is removed afterwards.

Usage:
    python tools/rfa_probe_update.py [--json out.json] [--keep] [--verbose]
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import rfa_oracle  # noqa: E402
import rfa_probe  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parent.parent
REAL_ARCHIVE = REPO_ROOT / "tests" / "data" / "fh" / "Battle_Of_Pavlov-1942.rfa"

BASE = "menu"

# The 8-byte archive header holds tocOffset (and the version), so it legitimately
# changes whenever the archive grows. The reserved region we care about is the
# 148 bytes that follow it.
HEADER_SIZE = 8

# Baseline tree. Three entries: two top-level files and one in a subdirectory, so
# the walk order (files then subdirectories) is represented too.
BASE_TREE = {
    "a.txt": b"alpha, stored verbatim, sixty-ish bytes long..................\r\n",
    "b.txt": b"bravo, also stored verbatim.................................\r\n",
    "sub/c.txt": b"charlie, inside a subdirectory.........................\r\n",
}


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest().upper()[:16]


def write_tree(root: Path, files: dict[str, bytes]) -> None:
    if root.exists():
        shutil.rmtree(root)
    for rel, data in files.items():
        path = root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)


def read_prefix(path: Path, n: int) -> bytes:
    with open(path, "rb") as fh:
        return fh.read(n)


def describe(path: Path) -> dict:
    """Parse an archive, tolerating an unparseable result rather than raising."""
    try:
        arc = rfa_probe.parse(str(path))
    except Exception as exc:  # noqa: BLE001 - a probe must report, not crash
        return {"error": f"{type(exc).__name__}: {exc}"}
    return {
        "size": arc.size,
        "version": arc.version,
        "toc_offset": arc.toc_offset,
        "count": len(arc.entries),
        "problems": list(arc.problems),
        "entries": [
            {
                "index": e.index,
                "name": e.name,
                "variant": e.variant,
                "stored_size": e.stored_size,
                "uncompressed_size": e.uncompressed_size,
                "data_offset": e.data_offset,
                "flags": e.flags,
                "reserved": list(e.reserved),
            }
            for e in arc.entries
        ],
    }


def first_data_offset(desc: dict) -> int:
    entries = desc.get("entries") or []
    return entries[0]["data_offset"] if entries else 0


def verify_readable(archive: Path, outdir: Path) -> dict:
    """Can the ORIGINAL rfaUnpack still read the archive we just produced?"""
    if outdir.exists():
        shutil.rmtree(outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    result = rfa_oracle.oracle_unpack(
        str(archive), str(outdir), [], rfa_oracle.UNPACK_ORACLE, create=True
    )
    return {
        "rc": result["rc"],
        "fileCount": result["fileCount"],
        "files": sorted(result["tree"]),
        "stdout": result["stdout"],
    }


def diff(before: dict, after: dict) -> dict:
    if "error" in before or "error" in after:
        return {"error": before.get("error") or after.get("error")}

    tb = {e["name"]: e for e in before["entries"]}
    ta = {e["name"]: e for e in after["entries"]}

    added = sorted(set(ta) - set(tb))
    removed = sorted(set(tb) - set(ta))
    added_detail = [
        {
            "name": name,
            "variant": ta[name]["variant"],
            "stored_size": ta[name]["stored_size"],
            "uncompressed_size": ta[name]["uncompressed_size"],
            "flags": ta[name]["flags"],
            "data_offset": ta[name]["data_offset"],
        }
        for name in added
    ]

    resized: list[str] = []
    moved: list[str] = []
    flag_changed: list[str] = []
    reserved_changed: list[str] = []
    variant_changed: list[str] = []
    for name in sorted(set(tb) & set(ta)):
        b, a = tb[name], ta[name]
        if (b["stored_size"], b["uncompressed_size"]) != (a["stored_size"], a["uncompressed_size"]):
            resized.append(name)
        if b["data_offset"] != a["data_offset"]:
            moved.append(name)
        if b["flags"] != a["flags"]:
            flag_changed.append(name)
        if b["reserved"] != a["reserved"]:
            reserved_changed.append(name)
        if b["variant"] != a["variant"]:
            variant_changed.append(f"{name}:{b['variant']}->{a['variant']}")

    return {
        "added": added,
        "added_detail": added_detail,
        "removed": removed,
        "resized": resized,
        "moved": moved,
        "flag_changed": flag_changed,
        "reserved_changed": reserved_changed,
        "variant_changed": variant_changed,
        "count_before": before["count"],
        "count_after": after["count"],
        "version_before": before["version"],
        "version_after": after["version"],
        "size_before": before["size"],
        "size_after": after["size"],
        "toc_before": before["toc_offset"],
        "toc_after": after["toc_offset"],
    }


class Probe:
    def __init__(self, root: Path, verbose: bool) -> None:
        self.root = root
        self.verbose = verbose
        self.cases: list[dict] = []

    def baseline(self, name: str, compress: bool = False) -> tuple[Path, Path, dict]:
        """Pack BASE_TREE into <name>.rfa and return (tree, archive, description)."""
        tree = self.root / f"{name}.tree"
        archive = self.root / f"{name}.rfa"
        write_tree(tree, BASE_TREE)
        args = ["-Compress"] if compress else []
        result = rfa_oracle.oracle_pack(str(tree), BASE, str(archive), args, rfa_oracle.PACK_ORACLE)
        return tree, archive, result

    def case(
        self,
        name: str,
        question: str,
        tree_files: dict[str, bytes] | None,
        extra: list[str],
        baseline_compress: bool = False,
        expect_archive: bool = True,
        touch: bool = False,
    ) -> dict:
        tree, archive, _ = self.baseline(name, compress=baseline_compress)

        before_bytes = archive.read_bytes()
        before_prefix = read_prefix(archive, 512)
        before = describe(archive)

        if not expect_archive:
            archive.unlink()

        if tree_files is not None:
            write_tree(tree, tree_files)

        if touch:
            # Same bytes, different modification time: separates a content
            # comparison from an mtime one.
            stamp = 978307200  # 2001-01-01, safely older than the baseline
            for path in sorted(tree.rglob("*")):
                if path.is_file():
                    os.utime(path, (stamp, stamp))

        cmd = [str(tree), BASE, str(archive), *extra]
        result = rfa_oracle.run(rfa_oracle.PACK_ORACLE, cmd)

        after_exists = archive.is_file()
        after = describe(archive) if after_exists else {"error": "archive not written"}
        after_prefix = read_prefix(archive, 512) if after_exists else b""

        # Skip the 8-byte header: it carries tocOffset, which must change whenever
        # the archive grows, and including it would swamp the question being asked.
        prefix_before = before_prefix[HEADER_SIZE : first_data_offset(before)] if "error" not in before else b""
        prefix_after = after_prefix[HEADER_SIZE : first_data_offset(after)] if "error" not in after else b""

        # The decisive simplification: is `-u` just a fresh pack of the same tree?
        # If so the reimplementation needs no update algorithm at all. Two policies
        # are tried, because `-u` may silently ignore the -Compress flag.
        fresh_same = self.root / f"{name}.fresh_same.rfa"
        fresh_base = self.root / f"{name}.fresh_base.rfa"
        rfa_oracle.oracle_pack(
            str(tree), BASE, str(fresh_same),
            ["-Compress"] if "-Compress" in extra else [], rfa_oracle.PACK_ORACLE,
        )
        rfa_oracle.oracle_pack(
            str(tree), BASE, str(fresh_base),
            ["-Compress"] if baseline_compress else [], rfa_oracle.PACK_ORACLE,
        )
        after_bytes = archive.read_bytes() if after_exists else b""

        record = {
            "name": name,
            "question": question,
            "extra": extra,
            "baseline_compress": baseline_compress,
            "rc": result["rc"],
            "stdout": result["stdout"],
            "stderr": result["stderr"],
            "archive_existed_after": after_exists,
            "archive_bytes_identical": after_exists and after_bytes == before_bytes,
            "fresh_same_flags_identical": after_exists and after_bytes == fresh_same.read_bytes(),
            "fresh_baseline_policy_identical": after_exists and after_bytes == fresh_base.read_bytes(),
            "reserved_region_preserved": prefix_before == prefix_after and bool(prefix_before),
            "reserved_region_sha_before": sha256_bytes(prefix_before),
            "reserved_region_sha_after": sha256_bytes(prefix_after),
            "before": before,
            "after": after,
            "diff": diff(before, after),
        }

        # The decisive question for -u: is the archive it produced still readable by
        # the tool that wrote it? Exit codes are not trustworthy, so compare the
        # extracted file set against the tree we packed.
        expected = sorted(f"{BASE}/{rel}" for rel in (tree_files or {}))
        verified = verify_readable(archive, self.root / f"{name}.out") if after_exists else None
        if verified is not None:
            verified["expected"] = expected
            verified["complete"] = verified["files"] == expected
            verified["extra"] = sorted(set(verified["files"]) - set(expected))
            verified["missing"] = sorted(set(expected) - set(verified["files"]))
        record["verify"] = verified

        self.cases.append(record)
        return record

    def real_archive_cases(self) -> None:
        """Behaviours only a foreign archive can exhibit.

        The cases above all pack with the oracle first, so the target archive already has
        the standard 148-byte reserved region and the same base name. A real shipped
        archive has neither, which is where the two destructive `-u` behaviours live.
        """
        if not REAL_ARCHIVE.is_file():
            return

        original = rfa_probe.parse(str(REAL_ARCHIVE))
        original_names = [e.name for e in original.entries]
        before_bytes = REAL_ARCHIVE.read_bytes()
        before_region = before_bytes[HEADER_SIZE : original.entries[0].data_offset]
        probe_name = original_names[0]

        for base in ("bf1942", "menu", "bf1942x"):
            arc = self.root / f"fh_{base}.rfa"
            shutil.copy2(REAL_ARCHIVE, arc)
            tree = self.root / f"fh_{base}.tree"
            write_tree(tree, {"zzz_probe.txt": b"probe\r\n"})

            result = rfa_oracle.run(rfa_oracle.PACK_ORACLE, [str(tree), base, str(arc), "-u"])

            after = rfa_probe.parse(str(arc))
            after_names = [e.name for e in after.entries]
            added_name = f"{base}/zzz_probe.txt"
            actual = sorted(n for n in after_names if n != added_name)

            # The rule the probe derived: retained name = base + "/" + stored[len(base)+1:]
            predicted = sorted(f"{base}/{n[len(base) + 1:]}" for n in original_names)

            after_region = arc.read_bytes()[HEADER_SIZE : after.entries[0].data_offset]
            sample_before = probe_name
            sample_after = next((n for n in after_names if n.endswith("/Conquest.con")), "?")

            self.cases.append(
                {
                    "name": f"real_base_{base}",
                    "question": f"real FH archive updated with base={base!r} (archive's own base is 'bf1942')",
                    "extra": ["-u"],
                    "real_archive": True,
                    "rc": result["rc"],
                    "stdout": result["stdout"],
                    "stderr": result["stderr"],
                    "archive_existed_after": True,
                    "base": base,
                    "count_before": len(original_names),
                    "count_after": len(after_names),
                    "renaming_rule_matches": actual == predicted,
                    "names_unchanged": actual == sorted(original_names),
                    "sample_before": sample_before,
                    "sample_after": sample_after,
                    "region_len_before": len(before_region),
                    "region_len_after": len(after_region),
                    "region_sha_before": sha256_bytes(before_region),
                    "region_sha_after": sha256_bytes(after_region),
                    "region_preserved": before_region == after_region,
                    "first_data_offset_before": original.entries[0].data_offset,
                    "first_data_offset_after": after.entries[0].data_offset,
                    "flags_before": sorted({hex(e.flags) for e in original.entries}),
                    "flags_after": sorted({hex(e.flags) for e in after.entries}),
                }
            )

    def real_report(self) -> str:
        out: list[str] = []
        for case in self.cases:
            if not case.get("real_archive"):
                continue
            out.append("=" * 78)
            out.append(f"{case['name']}:  {case['question']}")
            out.append(f"  rc      : {case['rc']}   entries: {case['count_before']} -> {case['count_after']}")
            out.append(f"  retained names "
                       f"{'UNCHANGED' if case['names_unchanged'] else 'REWRITTEN'}"
                       f"   matches predicted rule: {case['renaming_rule_matches']}")
            out.append(f"    example before: {case['sample_before']}")
            out.append(f"    example after : {case['sample_after']}")
            out.append(
                f"  reserved region: {case['region_len_before']} bytes @ "
                f"{case['first_data_offset_before']} [{case['region_sha_before']}]"
            )
            out.append(
                f"                -> {case['region_len_after']} bytes @ "
                f"{case['first_data_offset_after']} [{case['region_sha_after']}]"
            )
            out.append(f"  region preserved: {case['region_preserved']}")
            out.append(f"  flags: {case['flags_before']} -> {case['flags_after']}")
        if out:
            out.append("=" * 78)
        return "\n".join(out)

    def run_all(self) -> None:
        modified_same = dict(BASE_TREE)
        modified_same["a.txt"] = b"ALPHA, stored verbatim, sixty-ish bytes long..................\r\n"

        modified_grow = dict(BASE_TREE)
        modified_grow["a.txt"] = BASE_TREE["a.txt"] + b"and then a lot more text appended to it\r\n"

        added = dict(BASE_TREE)
        added["d.txt"] = b"delta, a brand new entry................................\r\n"

        removed = {k: v for k, v in BASE_TREE.items() if k != "sub/c.txt"}

        self.case("u_add", "does -u append a file that is not in the archive?", added, ["-u"])
        self.case(
            "u_touch_only",
            "is a same-content, new-mtime tree re-published? (content vs mtime comparison)",
            dict(BASE_TREE),
            ["-u"],
            touch=True,
        )
        self.case(
            "u_modify_same_size",
            "does -u replace an entry whose content changed but whose size did not?",
            modified_same,
            ["-u"],
        )
        self.case("u_modify_grow", "does -u replace an entry that grew?", modified_grow, ["-u"])
        self.case("u_delete", "does -u drop an entry that is missing from the tree?", removed, ["-u"])
        self.case("u_unchanged", "is -u on an unchanged tree a no-op?", dict(BASE_TREE), ["-u"])
        self.case("u_missing_archive", "what happens when the target archive does not exist?", dict(BASE_TREE), ["-u"], expect_archive=False)
        self.case("u_compress_baseline", "does -Compress -u recompress a stored archive?", dict(BASE_TREE), ["-u", "-Compress"])
        self.case(
            "u_add_compress",
            "does -u -Compress compress a NEWLY ADDED entry, and what happens to version?",
            added,
            ["-u", "-Compress"],
        )
        self.case(
            "u_add_plain_on_compress",
            "does plain -u add a RAW entry to a compressed archive? (crash-inducing mix?)",
            added,
            ["-u"],
            baseline_compress=True,
        )
        self.case(
            "u_plain_on_compress",
            "does plain -u strip compression from a compressed archive?",
            dict(BASE_TREE),
            ["-u"],
            baseline_compress=True,
        )

    def report(self) -> str:
        out: list[str] = []
        for case in self.cases:
            out.append("=" * 78)
            out.append(f"{case['name']}:  {case['question']}")
            out.append(f"  args    : -u probe, extra={case['extra'] or '[]'}"
                       f"{' (baseline was -Compress)' if case['baseline_compress'] else ''}")
            out.append(f"  rc      : {case['rc']}")
            stdout = case["stdout"].replace("\r\n", "\n").strip()
            for line in stdout.split("\n") if stdout else []:
                out.append(f"  stdout  | {line}")
            if case["stderr"].strip():
                for line in case["stderr"].replace("\r\n", "\n").strip().split("\n"):
                    out.append(f"  stderr  | {line}")

            d = case["diff"]
            if "error" in d and d["error"]:
                out.append(f"  ---- after: {d['error']}")
                if not case["archive_existed_after"]:
                    out.append("  ---- archive was NOT written")
                continue

            out.append(
                f"  entries : {d['count_before']} -> {d['count_after']}"
                f"   size: {d['size_before']} -> {d['size_after']}"
                f"   version: {d['version_before']} -> {d['version_after']}"
                f"   tocOffset: {d['toc_before']} -> {d['toc_after']}"
            )
            out.append(f"  added   : {d['added'] or '-'}")
            for item in d.get("added_detail", []):
                out.append(
                    f"  + added : {item['name']:<24} {item['variant']:<8}"
                    f" stored={item['stored_size']:<8} unc={item['uncompressed_size']:<8}"
                    f" off={item['data_offset']:<8} flags=0x{item['flags']:08X}"
                )
            out.append(f"  removed : {d['removed'] or '-'}")
            out.append(f"  resized : {d['resized'] or '-'}")
            out.append(f"  moved   : {d['moved'] or '-'}")
            out.append(f"  variant : {d['variant_changed'] or '-'}")
            out.append(f"  flags   : {d['flag_changed'] or '-'}")
            out.append(f"  reserved: {d['reserved_changed'] or '-'}")
            out.append(f"  payload offsets reused: "
                       f"{max(0, (d['count_before'] - len(d['moved']))) }/{d['count_before']} entries kept dataOffset")
            out.append(
                f"  reserved region (8..firstData) preserved: {case['reserved_region_preserved']}"
                f"  [{case['reserved_region_sha_before']} -> {case['reserved_region_sha_after']}]"
            )
            out.append(f"  whole archive byte-identical to baseline: {case['archive_bytes_identical']}")
            out.append(
                f"  equivalent to a FRESH pack with the same flags: {case['fresh_same_flags_identical']}"
                f"   with the baseline's policy: {case['fresh_baseline_policy_identical']}"
            )

            verify = case.get("verify")
            if verify:
                out.append(
                    f"  ORACLE RE-READ: rc={verify['rc']}"
                    f" files={verify['fileCount']}/{len(verify['expected'])}"
                    f" complete={verify['complete']}"
                )
                if verify["missing"]:
                    out.append(f"    missing from extraction: {verify['missing']}")
                if verify["extra"]:
                    out.append(f"    still present (not deleted by -u): {verify['extra']}")
                unpack_out = verify["stdout"].replace("\r\n", "\n").strip()
                for line in unpack_out.split("\n") if unpack_out else []:
                    out.append(f"    unpack | {line}")

            if case["before"].get("problems"):
                out.append(f"  ! before problems: {case['before']['problems']}")
            if case["after"].get("problems"):
                out.append(f"  ! after problems : {case['after']['problems']}")

        out.append("=" * 78)
        return "\n".join(out)


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--json", help="also write the raw records to this path")
    p.add_argument("--keep", action="store_true", help="keep the scratch directory for inspection")
    p.add_argument("--verbose", action="store_true", help="print every entry table, not just the diff")
    args = p.parse_args(argv)

    root = Path(tempfile.mkdtemp(prefix="rfa_update_probe_"))
    try:
        probe = Probe(root, args.verbose)
        probe.run_all()
        print(probe.report())
        probe.real_archive_cases()
        print(probe.real_report())

        if args.verbose:
            for case in probe.cases:
                for label in ("before", "after"):
                    desc = case[label]
                    print(f"--- {case['name']} {label}: {desc.get('count', desc.get('error'))} entries")
                    for e in desc.get("entries", []):
                        print(
                            f"    [{e['index']:>3}] {e['name']:<24} {e['variant']:<8}"
                            f" stored={e['stored_size']:<8} unc={e['uncompressed_size']:<8}"
                            f" off={e['data_offset']:<8} flags=0x{e['flags']:08X}"
                            f" reserved={e['reserved']}"
                        )

        if args.json:
            Path(args.json).write_text(json.dumps(probe.cases, indent=2), encoding="utf-8")
            print(f"wrote {args.json}")
    finally:
        if args.keep:
            print(f"scratch kept at {root}")
        else:
            shutil.rmtree(root, ignore_errors=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
