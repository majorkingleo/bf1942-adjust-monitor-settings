/**
 * LZO1X codec wrapper around the vendored miniLZO implementation.
 *
 * An instance owns the compressor work buffer (~256 KB), so each thread needs its own
 * instance: compress() is not reentrant. decompress() is static and stateless, so it is
 * safe to call concurrently from any number of threads, which is what makes per-chunk
 * parallel decompression straightforward.
 *
 * @author Copyright (c) 2026
 */

#ifndef RFA_LZOCODEC_H
#define RFA_LZOCODEC_H

#include <cstddef>
#include <vector>

namespace rfa {

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
	               std::vector<unsigned char> & out );

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

#endif /* RFA_LZOCODEC_H */
