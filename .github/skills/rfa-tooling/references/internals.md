# Internals: reader, writer, thread pool

Detail for the parts of `rfa/` and `cli/` that are easy to break and hard to notice. Every claim
here was measured or is pinned by a testcase; where a rule is counter-intuitive, the reason is
given, because several of them were arrived at by getting them wrong first.

## The payload format in one paragraph

An entry is **raw** when `storedSize == uncompressedSize` (or `uncompressedSize == 0`), and
otherwise **chunked**: a `u32` chunk count, then 12-byte descriptors, then the concatenated
per-chunk payloads. Chunk size is 32 KiB. A chunk is an **LZO1X stream** unless the sizes prove
otherwise, and the test is `compressedSize != uncompressedSize`, never `<`: LZO1X *expands*
incompressible input, so a less-than test hands back compressed bytes as content. Equality is not
proof either — LZO can compress a chunk into exactly its own length, and a shipping archive does
(`Battle_of_Britain.rfa`, `Willy.con`: a 30-byte stream for a 30-byte file). So the sizes are a
**hint**: `PayloadReader` tries LZO first and copies verbatim only when the codec refuses, which is
safe because `lzo1x_decompress_safe` rejects non-streams and insists on the expected output length.
Never size a decompression buffer from the compressed size.

`version` (`0`/`1`) does **not** select the layout — both occur, and 235 raw entries live in
version-1 archives. `rfaPack.exe` writes 0 without `-Compress` and 1 with it. A version-1 archive
containing a raw non-empty entry makes the original `rfaUnpack.exe` crash, so we read that
combination but never write it.

## Writer (`rfa/RfaWriter.cc`)

```
write(files, options)
  for each batch of files      <= BATCH_TARGET_BYTES = 64 MiB, never splitting a file
      read the batch serially  (I/O is not the bottleneck, and this keeps memory bounded)
      ONE flat queue of chunks across ALL files in the batch  -> rfa::parallel_for
      assemble in file order, then append
```

* **One codec pool for the whole archive**, not per file. LZO1X-999 needs a 448 KiB work buffer
  per worker; building the pool inside the per-file loop allocated and zeroed ~470 MiB to produce a
  22.9 MB archive, and made CPU time wander (0.64/1.31/1.80 s at a constant 0.37 s wall) while
  changing nothing. Reuse is safe — the compressor clears its own state per call, which is already
  exercised by one codec serving several chunks inside one file, and proven by byte-identity.
* The unit of work is the **chunk**, not the file. A tree holding one 22.9 MB file is 1 file but
  700 chunks. Clamping the worker count by *files* reduced that tree to a single thread and packed
  it at 0.99 of 24 cores (finding 33); 541 of the shipping menu tree's 618 files are a single
  chunk each, so 87% of them can only ever feed one worker.
* Store mode has no queue to fill and is serial by construction, so `planned_threads()` returns 1
  for it and `--threads` is ignored. That is why `--threads 2` on a store pack echoes
  ` Threads: 1`.

`WriteOptions::threads == 0` means "use the CPU count". `planned_threads()` is **public on
purpose**: a wrong clamp still writes byte-identical archives, so no byte comparison can catch it,
and the testcase `writer_thread_budget_follows_chunk_count_not_file_count` pins it directly.

## Thread pool (`rfa/ParallelFor.h`, `rfa/CpuCount.cc`)

```cpp
rfa::parallel_for( count, threads, fn );   // fn( index, worker )
```

`std::jthread` workers pulling from an atomic counter; the callable is a template parameter, so no
`std::function` indirection. Width is decided by the callers as `min(jobs, workers)` — never more
threads than there is work to give them.

`usable_cpu_count()` is **not** `std::thread::hardware_concurrency()`. On Windows it respects the
process's affinity mask (`GetProcessAffinityMask` + `std::popcount`, falling back to
`GetSystemInfo`), so a process pinned to a subset of cores does not oversubscribe. The POSIX path
is behind `RFA_HAS_CPU_AFFINITY` — a *feature* test, not `defined(__linux__)`, because Cygwin ships
`<sched.h>` without the affinity API and a platform test failed to compile there. It calls
`sched_getaffinity`, falls back to `sysconf(_SC_NPROCESSORS_ONLN)`, then to
`hardware_concurrency()`, and is documented to return at least 1.

⚠️ The POSIX/affinity branch has **never been compiled** in this workspace (there is no Linux
toolchain here). It is written against the documented glibc interface and is the first thing to
re-check on the first Linux build.

## Reader (`rfa/RfaArchive.h`, `PayloadReader`)

`PayloadReader` owns the archive's file handle precisely so that concurrent extraction needs no
locking and no shared file position: one instance per worker. A single `RfaArchive` is shared
read-only for the entry table.

Extraction in `cli/UnpackCli.cc`:

```
threads = --threads N  (0 -> max(1, usable_cpu_count()/3))
one PayloadReader per worker, created up front
rfa::parallel_for( entries.size(), threads, ... )
    per-index std::vector<std::string> messages, atomics for extracted/failed
then print the messages in INDEX ORDER
```

The full extract loop prints nothing per successful entry, only errors, so parallelising it keeps
stdout byte-identical **only** because messages are collected per index and emitted afterwards in
entry order. Preserve that if you touch it.

## Naming and order (finding 28)

Entry order is each directory's own files (sorted) then its subdirectories (sorted), recursively —
not an ASCII-ascending sort of full paths. Comparison folds ASCII to **uppercase** (`fold_ascii()`,
`name_less()`), which is what the original does and what the `collation/` fixture in
`tests/data/golden/tree` pins — `Icon_PT_Mine.txt` before `icon_artillery.txt` is *not* what a
plain ordinal sort gives.

`rfa/RfaStamp.h` is **generated** by `tools/rfa_stamp_gen.py`, which therefore carries the header's
text in a Python template too: changing one requires changing the other.
`python tools/rfa_stamp_gen.py --check` regenerates in memory and compares, and is the only thing
that proves the two agree.

## Performance shape (why the two directions differ so much)

Measured on a Ryzen 9 7900X (12 physical / 24 logical, Defender real-time protection on):

| | value |
|---|---|
| pack, 110.1 MB / 550 files | ours 0.420 s vs the oracle's 10.003 s — **23.8x** |
| unpack, 618-entry menu tree | ours 0.117 s vs the original's 0.239 s — **2.04x**, at 2.14 of 8 cores |
| compress | ~68 ms CPU per MB |
| decompress | ~0.7 ms per MB (~1.4 GB/s per core) |

The ~100x asymmetry is the whole story: packing has CPU work to spread, unpacking mostly does not.
Unpacking is dominated by per-file cost instead — 204 stored files (no codec at all) cost 0.083 s
while the same 21.8 MB as one file costs 0.023 s. Creating files saturates at about four concurrent
writers, because the bottleneck is external (the virus scanner's filter driver inspecting every new
file, plus filesystem metadata); that is why the unpacking default is a fraction of the CPU count
rather than all of it.

Extra threads past the plateau buy nothing on the I/O-bound part and only risk thrashing an HDD,
which is the argument for the default. `--threads` exists so that the trade-off can be measured
without a rebuild.
