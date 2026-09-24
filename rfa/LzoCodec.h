/**
 * LZO1X codec wrapper around the vendored LZO implementation.
 *
 * Two compressors, because the archives need a specific one:
 *
 *   Era   LZO1X-999 at level 8. This produced every shipping BF1942 archive, and
 *         bin\rfaPack.orig.exe -Compress reproduces those payloads byte for byte (finding 31),
 *         so it is the default: our compressed output is identical to the original tool's and
 *         the shipped 2003 tools can read it.
 *   Fast  LZO1X-1. ~15x faster per byte and ~21% larger on the menu tree, and its streams are
 *         NOT accepted by the 2003 decoder in RFA Pack 1.7 (finding 27) - the game reads them,
 *         the old tools do not. Opt-in through --lzo-fast.
 *
 * An instance owns the compressor work buffer, which for 999 is ~448 KB rather than the ~16 KB
 * of 1x, so each thread needs its own instance: compress() is not reentrant. decompress() is
 * static and stateless, so it is safe to call concurrently from any number of threads, which is
 * what makes per-chunk parallel decompression straightforward.
 *
 * @author Copyright (c) 2026
 */

#pragma once

#include <cstddef>
#include <vector>

namespace rfa {

/// Which compressor to use. See the class comment for what each one costs.
enum class LzoVariant
{
	/// LZO1X-999 level 8: what the shipping archives and rfaPack.exe use.
	Era,

	/// LZO1X-1: much faster, ~21% larger, and unreadable by the shipped tools.
	Fast,
};

class LzoCodec
{
public:
	LzoCodec();
	~LzoCodec();

	LzoCodec( const LzoCodec & ) = delete;
	LzoCodec & operator=( const LzoCodec & ) = delete;

	/// True when the LZO library initialised. Checked once, process-wide.
	static bool available();

	/// Upper bound for the compressed size of `src_len` input bytes. Compression can
	/// legitimately exceed the input size for incompressible data, and callers must be
	/// prepared to store a chunk verbatim instead - the RFA format allows it.
	static std::size_t max_compressed_size( std::size_t src_len );

	/// Compresses into `out`, which is resized to the result. Returns false on refusal.
	bool compress( const unsigned char * src,
	               std::size_t src_len,
	               std::vector<unsigned char> & out,
	               LzoVariant variant = LzoVariant::Era );

	/// Decompresses into `dst`, which must have room for `expected_len` bytes. Returns
	/// false on any LZO error, and also if the stream does not produce exactly
	/// `expected_len` bytes - a size mismatch means the entry table lied, and silently
	/// accepting it would produce corrupt files.
	static bool decompress( const unsigned char * src,
	                        std::size_t src_len,
	                        unsigned char * dst,
	                        std::size_t expected_len );

private:
	std::vector<unsigned char> work_;
};

} // namespace rfa


