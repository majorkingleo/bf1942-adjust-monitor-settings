# Proving a change did not break compatibility

The oracle is `bin\rfaPack.orig.exe`: whatever it writes is, by definition, right. Two fixtures
make that testable without running it:

| Fixture | What it is |
|---|---|
| `tests/data/golden/tree` | a small source tree with awkward names (`collation/`), a 200000-byte blob, a 1-byte file, and an empty file |
| `tests/data/golden/oracle-store.rfa` | `rfaPack.orig.exe`'s output for that tree, 200996 B |
| `tests/data/golden/oracle-compress.rfa` | the same with `-Compress`, 15943 B |
| `tests/golden/oracle-cli.json` | verbatim stdout and exit codes of the original's command line |

`tests/data/golden/MANIFEST.txt` lists the structure of both archives and the SHA-256 of every
input file. Regenerate goldens **only** with the intent to change the expectation:
`tools/rfa_golden_archives.py` (it drives the oracle; the C++ tests never do).

The two archives that matter for our own work, and the one gotcha: `oracle-store.rfa` is compared
**byte for byte**, `oracle-compress.rfa` only **semantically** by the test suite — and that weaker
check has a reason, see obligation 2.

## 1. Store mode must be byte-identical

```powershell
Set-Location e:\progs\bf1942-adjust-monitor-settings
& .\rfaPack.exe 'tests\data\golden\tree' menu 'examples\tmp_bench\check-store.rfa' | Out-Null
(Get-FileHash 'examples\tmp_bench\check-store.rfa' -Algorithm SHA256).Hash
(Get-FileHash 'tests\data\golden\oracle-store.rfa' -Algorithm SHA256).Hash
```

Both must be `0B6CC31FA9018016BAA9D84E2D05B6BF2F500AC32DF669421011A4667D4F0EAC`. Store mode copies
bytes and writes metadata, so there is no encoder latitude here: any difference is a layout bug.
This is also the check that catches a broken walk order, a wrong `nameLen`, or a stray NUL.

## 2. Compress mode must match, except for zero-length entries

Same command with `-Compress`. The expectation is `28...` only if the tree has no empty file:

| archive | SHA-256 |
|---|---|
| `oracle-compress.rfa` (has `menu/empty.txt`) | `272419FEB4DD8D96A1E136BAF7F8EF4F91DEB1F996EA973C2D22A909DF954273` |
| ours today | `C58BF87EE1E86F3C0526D7E26DCA23A7E09529635DE07ED575B13082291BEB4E` |
| ours, same tree without `empty.txt` | `D98B5BA80B974BB430008C20BF560E32B8F6584AC1BAE83BD6F27CD1511C0985` (= the oracle's) |

For a zero-byte input LZO 2.10 emits 3 bytes where the 2003 encoder emits 4, so that one entry is
19 bytes instead of 20 and everything after it shifts by one. `verify.ps1` skips this comparison
*and* the CRASH that follows from it when the tree contains an empty file, naming both as the same
known cause; `-Strict` turns both into failures, which is what to run once the divergence is fixed.
An **incomplete** extraction is a failure either way — that distinguishes "the known encoder
difference" from "someone broke the reader".

The test suite deliberately does not byte-compare compress archives at all
(`writer_compress_archive_is_interchangeable_with_the_oracle`), and its comment still blames match
selection by an older `lzo1x_1_compress`. That is stale: with LZO1X-999 at level 8 the streams do
agree, measured above. The test is a metadata and round-trip check today.

## 3. The worker count must not change a single byte

No oracle is needed, and no oracle comparison can catch this — compare our own runs with each other.
`--threads` must be a performance knob and nothing else:

```powershell
$tree = 'tests\data\golden\tree'
foreach( $n in 1, 2, 4 ) {
	& .\rfaPack.exe $tree menu "examples\tmp_bench\thr-$n.rfa" -Compress --threads $n | Out-Null
	"threads=$n  $((Get-FileHash "examples\tmp_bench\thr-$n.rfa" -Algorithm SHA256).Hash)"
}
```

All three hashes must be equal, and equal to the run with no switch at all. Do the same for
unpacking: extract with `--threads 1` and `--threads 4` and compare the extracted trees (that is
obligation 4's round trip, run twice).

On the real 618-entry menu tree (in the gitignored `examples/`, if you still have it) the expected
values are store `AB3C2B9E72080914…` / 22,950,118 B and compress `D6E928F8DDFE9F36…` / 7,963,020 B.

## 4. The 2003 tools must still read it

```powershell
New-Item -ItemType Directory -Force 'examples\tmp_bench\oracle-read' | Out-Null
& .\bin\rfaUnpack.orig.exe 'examples\tmp_bench\thr-1.rfa' 'examples\tmp_bench\oracle-read' 2>&1 |
	Select-String 'CRASH|Error'
"rc=$LASTEXITCODE  files=$(@(Get-ChildItem 'examples\tmp_bench\oracle-read' -Recurse -File).Count)"
```

**The exit code is not a success signal** — it is 0 even when the tool printed
`ERROR! CRASH  Decompression()!`. Grep for `CRASH` and count the extracted files; both must be
clean and complete. This is the check that closing finding 27 was about, and it is the reason
LZO1X-999 level 8 is the default rather than the faster LZO1X-1.

## Round-trip against the source tree

The cheapest end-to-end check that the reader and writer agree with each other *and* with the tree
on disk. Remember that entries carry the base folder name, so `tests/data/golden/tree/menu/a.txt`
comes back as `<target>/menu/menu/a.txt`:

```powershell
& .\rfaUnpack.exe 'examples\tmp_bench\thr-1.rfa' 'examples\tmp_bench\rt' --threads 1 | Out-Null
# compare <target>/menu/menu/** against tests/data/golden/tree/menu/**
```

`scripts/verify.ps1` does all four obligations, at several thread counts, and prints a summary.

## Before any benchmark

Measure native binaries started from **PowerShell**, not from a Cygwin-started script: the Cygwin
environment takes part in the numbers and was rejected as a measurement path. Take a median of
several runs, and make the benchmark self-validating — one of them silently failed 292 of 800 file
opens and looked 2.4x faster while writing 508 files instead of 800. Count the files and the bytes
that landed on disk, outside the timed window.
