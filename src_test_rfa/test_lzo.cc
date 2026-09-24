/**
 * Testcases for the vendored LZO codec, and for the two compression variants the archives need.
 *
 * @author Copyright (c) 2026
 */

#include "test_lzo.h"

#include "rfa/LzoCodec.h"

#include "lzo/lzo1x.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

typedef std::vector<lzo_byte> Buffer;

// Deterministic LCG: <random> distributions are not portable across standard library
// versions, and a flaky codec test would be worse than useless.
std::uint32_t g_rng_state = 0;

void rng_seed( std::uint32_t seed )
{
	g_rng_state = seed;
}

std::uint32_t rng_next()
{
	g_rng_state = g_rng_state * 1664525u + 1013904223u;
	return g_rng_state;
}

Buffer make_random( std::size_t size, std::uint32_t seed )
{
	rng_seed( seed );

	Buffer out;
	out.reserve( size );
	for( std::size_t i = 0; i < size; ++i ) {
		out.push_back( (lzo_byte)( rng_next() >> 24 ) );
	}
	return out;
}

Buffer make_compressible( std::size_t size )
{
	static const char pattern[] = "Game.setNumberOfTickets 1 115\r\n";

	Buffer out;
	out.reserve( size );
	for( std::size_t i = 0; i < size; ++i ) {
		out.push_back( (lzo_byte)pattern[i % ( sizeof( pattern ) - 1 )] );
	}
	return out;
}

bool lzo_is_usable()
{
	static const bool ok = ( lzo_init() == LZO_E_OK );
	return ok;
}

Buffer lzo_compress( const Buffer & src )
{
	Buffer work( (std::size_t)LZO1X_1_MEM_COMPRESS );
	Buffer dst( src.size() + src.size() / 16 + 64 + 3 );

	// the compressor wants a dereferenceable pointer even when the input is empty
	static const lzo_byte empty_input = 0;
	const lzo_bytep src_ptr = src.empty() ? &empty_input : src.data();

	lzo_uint dst_len = (lzo_uint)dst.size();

	if( lzo1x_1_compress( src_ptr, (lzo_uint)src.size(), dst.data(), &dst_len, work.data() ) != LZO_E_OK ) {
		return Buffer();
	}

	dst.resize( (std::size_t)dst_len );
	return dst;
}

/**
 * Decompress into a buffer sized from the known uncompressed length - which is exactly
 * what the RFA reader does, since the directory table always supplies uncompressedSize.
 *
 * Guessing the capacity from the compressed size does NOT work: highly repetitive input
 * can compress by well over 64x, and an undersized buffer surfaces as
 * LZO_E_OUTPUT_OVERRUN. That mistake is what this helper originally made.
 */
bool lzo_decompress( const Buffer & compressed, std::size_t uncompressed_size, Buffer & out )
{
	out.assign( uncompressed_size + 64, 0 );

	lzo_uint out_len = (lzo_uint)out.size();

	const int rc = lzo1x_decompress_safe( compressed.data(),
	                                      (lzo_uint)compressed.size(),
	                                      out.data(),
	                                      &out_len,
	                                      nullptr );

	out.resize( (std::size_t)out_len );
	return rc == LZO_E_OK;
}

bool roundtrip( const Buffer & original )
{
	const Buffer compressed = lzo_compress( original );

	if( compressed.empty() && !original.empty() ) {
		return false;   // compression failed outright
	}

	Buffer restored;
	if( !lzo_decompress( compressed, original.size(), restored ) ) {
		return false;
	}

	return restored == original;
}

} // namespace

TestCasePtr test_lzo_init()
{
	return std::make_shared<TestCaseFuncNoInp>(
		"lzo_init", true, []() { return lzo_is_usable(); } );
}

TestCasePtr test_lzo_roundtrip_empty()
{
	return std::make_shared<TestCaseFuncNoInp>(
		"lzo_roundtrip_empty", true, []() { return roundtrip( Buffer() ); } );
}

TestCasePtr test_lzo_roundtrip_single_byte()
{
	return std::make_shared<TestCaseFuncNoInp>(
		"lzo_roundtrip_single_byte", true, []() { return roundtrip( Buffer( 1, 'A' ) ); } );
}

TestCasePtr test_lzo_roundtrip_random_incompressible()
{
	return std::make_shared<TestCaseFuncNoInp>(
		"lzo_roundtrip_random_incompressible", true, []() {
			// incompressible input exercises the literal path, and is the case where
			// the compressed form is legitimately larger than the input
			return roundtrip( make_random( 4096, 1 ) )
			    && roundtrip( make_random( 100000, 2 ) );
		} );
}

TestCasePtr test_lzo_roundtrip_highly_compressible()
{
	return std::make_shared<TestCaseFuncNoInp>(
		"lzo_roundtrip_highly_compressible", true, []() {
			return roundtrip( make_compressible( 200000 ) );
		} );
}

TestCasePtr test_lzo_roundtrip_at_chunk_boundaries()
{
	// CHUNK_SIZE is 32768 in the RFA format, so these are the sizes that matter most:
	// a chunk is compressed on its own, and the neighbours decide whether an off-by-one
	// produces a spurious extra chunk.
	return std::make_shared<TestCaseFuncNoInp>(
		"lzo_roundtrip_at_chunk_boundaries", true, []() {
			const std::size_t sizes[] = { 32767, 32768, 32769, 65536 };

			for( std::size_t size : sizes ) {
				if( !roundtrip( make_compressible( size ) ) ) {
					return false;
				}
				if( !roundtrip( make_random( size, (std::uint32_t)size ) ) ) {
					return false;
				}
			}

			return true;
		} );
}

TestCasePtr test_lzo_compressible_input_actually_shrinks()
{
	return std::make_shared<TestCaseFuncNoInp>(
		"lzo_compressible_input_actually_shrinks", true, []() {
			const Buffer original = make_compressible( 100000 );
			const Buffer compressed = lzo_compress( original );

			return !compressed.empty() && compressed.size() < original.size() / 4;
		} );
}

TestCasePtr test_lzo_single_run_encoding_matches_format_doc()
{
	// This is the Phase 0 finding pinned in code. rfaPack.exe -Compress on incompressible
	// N-byte input produced compressedSize == N + 4, whose first byte is N + 17 (the
	// LZO1X literal-run count) followed by N literals and the 3-byte end marker
	// 11 00 00. If a future LZO version changes that, the RFA format assumptions in
	// rfa-format.md need revisiting.
	return std::make_shared<TestCaseFuncNoInp>(
		"lzo_single_run_encoding_matches_format_doc", true, []() {
			const std::size_t sizes[] = { 20, 30, 40, 60, 100 };

			for( std::size_t n : sizes ) {
				const Buffer original = make_random( n, 1234 + (std::uint32_t)n );
				const Buffer compressed = lzo_compress( original );

				if( compressed.size() != n + 4 ) {
					return false;
				}
				if( compressed[0] != (lzo_byte)( n + 17 ) ) {
					return false;
				}
				if( compressed[n + 1] != 0x11 || compressed[n + 2] != 0x00 || compressed[n + 3] != 0x00 ) {
					return false;
				}
				if( !std::equal( original.begin(), original.end(), compressed.begin() + 1 ) ) {
					return false;
				}
			}

			return true;
		} );
}

TestCasePtr test_lzo_decompressing_truncated_data_fails_cleanly()
{
	return std::make_shared<TestCaseFuncNoInp>(
		"lzo_decompressing_truncated_data_fails_cleanly", true, []() {
			const Buffer original = make_compressible( 50000 );
			const Buffer compressed = lzo_compress( original );

			if( compressed.size() < 10 ) {
				return false;
			}

			// must not crash, and must not silently report success with wrong bytes
			Buffer truncated( compressed.begin(), compressed.end() - 5 );
			Buffer restored;

			const bool ok = lzo_decompress( truncated, original.size(), restored );
			return !ok || restored != original;
		} );
}

// ---------------------------------------------------------------------------
// rfa::LzoCodec - the wrapper the library actually uses
// ---------------------------------------------------------------------------

TestCasePtr test_lzo_codec_available()
{
	return std::make_shared<TestCaseFuncNoInp>(
		"lzo_codec_available", true, []() { return rfa::LzoCodec::available(); } );
}

TestCasePtr test_lzo_codec_roundtrip_via_wrapper()
{
	return std::make_shared<TestCaseFuncNoInp>(
		"lzo_codec_roundtrip_via_wrapper", true, []() {
			rfa::LzoCodec codec;

			const std::size_t sizes[] = { 0, 1, 32768, 32769, 200000 };

			for( std::size_t size : sizes ) {
				const Buffer original = make_compressible( size );

				std::vector<unsigned char> compressed;
				if( !codec.compress( original.data(), original.size(), compressed ) ) {
					return false;
				}

				std::vector<unsigned char> restored( size );
				if( !rfa::LzoCodec::decompress( compressed.data(), compressed.size(),
				                                 restored.data(), size ) ) {
					return false;
				}

				if( restored != original ) {
					return false;
				}
			}

			return true;
		} );
}

TestCasePtr test_lzo_codec_detects_size_mismatch()
{
	// The RFA reader hands the codec the uncompressedSize from the directory table. If
	// the payload disagrees, the codec must refuse rather than hand back a short or
	// over-long buffer - that is what stops a corrupt archive becoming a corrupt file.
	return std::make_shared<TestCaseFuncNoInp>(
		"lzo_codec_detects_size_mismatch", true, []() {
			rfa::LzoCodec codec;
			const Buffer original = make_compressible( 5000 );

			std::vector<unsigned char> compressed;
			if( !codec.compress( original.data(), original.size(), compressed ) ) {
				return false;
			}

			std::vector<unsigned char> out( original.size() );

			return !rfa::LzoCodec::decompress( compressed.data(), compressed.size(),
			                                    out.data(), original.size() - 1 );
		} );
}

TestCasePtr test_lzo_codec_max_compressed_size_is_sufficient()
{
	return std::make_shared<TestCaseFuncNoInp>(
		"lzo_codec_max_compressed_size_is_sufficient", true, []() {
			rfa::LzoCodec codec;

			// incompressible input is the worst case, and the one where the result is
			// legitimately LARGER than the input
			const Buffer original = make_random( 65536, 7 );

			std::vector<unsigned char> compressed;
			if( !codec.compress( original.data(), original.size(), compressed ) ) {
				return false;
			}

			return compressed.size() <= rfa::LzoCodec::max_compressed_size( original.size() )
			    && compressed.size() > original.size();
		} );
}
TestCasePtr test_lzo_codec_era_variant_reproduces_the_archive_encoder()
{
	// The one expectation in this suite that comes from a shipping archive rather than from our
	// own earlier behaviour: for 200 bytes of "ab" the BF1942 encoder emits exactly these ten
	// bytes, read out of bin\rfaPack.orig.exe's own output and out of menu.rfa.
	//
	// It is here because it is the cheapest possible proof that LzoVariant::Era really is the
	// era encoder. If a future change swaps the variant, or the vendored LZO is updated and its
	// 999 compressor starts making different choices, this fails immediately instead of
	// silently producing archives the shipped tools cannot read (finding 27).
	static const unsigned char expected[] = {
		0x13, 'a', 'b', 0x20, 0xA5, 0x04, 0x00, 0x11, 0x00, 0x00
	};

	return std::make_shared<TestCaseFuncNoInp>(
		"lzo_codec_era_variant_reproduces_the_archive_encoder", true, []() {
			std::vector<unsigned char> input( 200 );

			for( std::size_t i = 0; i < input.size(); ++i ) {
				input[i] = (unsigned char)( ( i % 2 ) ? 'b' : 'a' );
			}

			rfa::LzoCodec codec;
			std::vector<unsigned char> compressed;

			if( !codec.compress( input.data(), input.size(), compressed, rfa::LzoVariant::Era ) ) {
				return false;
			}

			if( compressed.size() != sizeof( expected ) ) {
				std::cout << "[rfa] era variant produced " << compressed.size()
				          << " bytes, the archive encoder produces " << sizeof( expected ) << "\n";
				return false;
			}

			for( std::size_t i = 0; i < sizeof( expected ); ++i ) {
				if( compressed[i] != expected[i] ) {
					std::cout << "[rfa] era variant stream differs at byte " << i << "\n";
					return false;
				}
			}

			return true;
		} );
}

TestCasePtr test_lzo_codec_fast_variant_is_the_other_compressor()
{
	// The two variants must not silently become the same one. 200 bytes of "ab" is the case
	// where they differ most sharply: 29 bytes for LZO1X-1 against 10 for the era encoder.
	return std::make_shared<TestCaseFuncNoInp>(
		"lzo_codec_fast_variant_is_the_other_compressor", true, []() {
			std::vector<unsigned char> input( 200 );

			for( std::size_t i = 0; i < input.size(); ++i ) {
				input[i] = (unsigned char)( ( i % 2 ) ? 'b' : 'a' );
			}

			rfa::LzoCodec codec;
			std::vector<unsigned char> era;
			std::vector<unsigned char> fast;

			if( !codec.compress( input.data(), input.size(), era, rfa::LzoVariant::Era )
			 || !codec.compress( input.data(), input.size(), fast, rfa::LzoVariant::Fast ) ) {
				return false;
			}

			if( era == fast ) {
				std::cout << "[rfa] both variants produced the same stream\n";
				return false;
			}

			// both must still round-trip: the fast one is larger, not broken
			std::vector<unsigned char> restored( input.size() );

			return fast.size() > era.size()
			    && rfa::LzoCodec::decompress( fast.data(), fast.size(),
			                                  restored.data(), input.size() )
			    && restored == input;
		} );
}