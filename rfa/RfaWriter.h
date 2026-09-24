/**
 * Writer for Battlefield 1942 .rfa archives.
 *
 * The layout rules here are not guesses - they were established by probing the original
 * rfaPack.exe and are pinned by the tests:
 *
 *   Store policy (no -Compress)  -> version 0, every entry RAW
 *                                   (storedSize == uncompressedSize, no block header)
 *   Compress policy (-Compress)  -> version 1, every entry CHUNKED in 32 KiB pieces
 *
 * Mixing them is what breaks the original tool: a version-1 archive containing a raw,
 * non-empty entry makes rfaUnpack.exe report "CRASH Decompression()". Real version-1
 * archives in the wild do contain 235 such entries, which is a producer defect we read
 * correctly but must never write.
 *
 * @author Copyright (c) 2026
 */

#ifndef RFA_RFAWRITER_H
#define RFA_RFAWRITER_H

#include "LzoCodec.h"
#include "RfaFormat.h"

#include <cstdint>
#include <string>
#include <vector>

namespace rfa {

enum class CompressionPolicy
{
	/// No -Compress: version 0, raw entries. What rfaPack.exe does by default.
	Store,

	/// -Compress: version 1, chunked LZO1X entries. What rfaPack.exe -Compress does.
	Compress,
};

struct WriteOptions
{
	CompressionPolicy policy = CompressionPolicy::Store;

	/// Which LZO compressor to use when the policy is Compress. The default is the era
	/// encoder, so our compressed archives are byte-identical to rfaPack.exe's and readable by
	/// the shipped tools (finding 31). `LzoVariant::Fast` is ~15x faster, ~21% larger, and only
	/// the game can read it (finding 27) - opt in through `--lzo-fast`.
	LzoVariant lzo = LzoVariant::Era;

	/// Worker threads used to compress the chunks of one file. 0 means
	/// std::thread::hardware_concurrency().
	unsigned threads = 0;
};

struct WriteResult
{
	std::uint32_t entry_count = 0;
	std::uint64_t uncompressed_bytes = 0;
	std::uint64_t stored_bytes = 0;
	std::uint64_t archive_bytes = 0;
};

/// One file to place into the archive.
struct SourceFile
{
	/// Internal path, forward slashes, relative to the archive root.
	std::string name;

	/// Where to read it from on disk.
	std::string path;

	std::uint64_t size = 0;
};

class RfaWriter
{
public:
	/**
	 * Recursively collect every regular file under `root`.
	 *
	 * Names become "<base>/<path relative to root>" with forward slashes, which is the
	 * form rfaPack.exe produces and the form rfaUnpack.exe -l requires.
	 *
	 * The ORDER matters. rfaPack.exe does not sort by full path; it walks each directory
	 * emitting its own regular files first (sorted by name) and only then descending into
	 * its subdirectories (sorted by name), recursively. Verified with a tree holding two
	 * nesting levels and three sibling directories:
	 *
	 *     menu/a.txt  menu/z.txt  menu/d1/c.txt  menu/d1/e/f.txt  menu/d2/b.txt  menu/d3/g.txt
	 *
	 * whereas ASCII-ascending order would put menu/z.txt last. Entry order reaches the
	 * archive verbatim, so matching it is what makes byte-identical output possible.
	 *
	 * Names are compared in byte order after folding to **uppercase**, which a plain byte or
	 * lowercase comparison gets wrong as soon as a directory holds siblings like `InGame` and
	 * `InfantryControlsPage1`, or `loading_full` and `loadingfull` - see finding 28 in
	 * PLAN_rfa_tools.md and name_less() in RfaWriter.cc for the measurement behind it.
	 */
	static bool collect_files( const std::string & root,
	                           const std::string & base,
	                           std::vector<SourceFile> & out,
	                           std::string * error = nullptr );

	/**
	 * Write a new archive.
	 *
	 * Returns false and fills `error` on I/O failure. A partially written file is removed
	 * rather than left behind, so a failed pack never leaves a half archive that looks
	 * valid.
	 */
	bool write( const std::vector<SourceFile> & files,
	            const std::string & archive_path,
	            const WriteOptions & options,
	            WriteResult * result = nullptr,
	            std::string * error = nullptr );
};

} // namespace rfa

#endif /* RFA_RFAWRITER_H */
