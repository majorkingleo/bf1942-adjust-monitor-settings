/**
 * LZO1X codec wrapper.
 *
 * @author Copyright (c) 2026
 */

#include "LzoCodec.h"

#include "minilzo.h"

#include <mutex>

namespace rfa {

namespace {

bool g_init_ok = false;
std::once_flag g_init_flag;

void ensure_init()
{
	std::call_once( g_init_flag, []() {
		g_init_ok = ( lzo_init() == LZO_E_OK );
	} );
}

} // namespace

LzoCodec::LzoCodec()
: work_( (std::size_t)LZO1X_1_MEM_COMPRESS )
{
	ensure_init();
}

LzoCodec::~LzoCodec() = default;

bool LzoCodec::available()
{
	ensure_init();
	return g_init_ok;
}

std::size_t LzoCodec::max_compressed_size( std::size_t src_len )
{
	// The bound miniLZO itself documents: input + input/16 + 64 + 3.
	return src_len + src_len / 16 + 64 + 3;
}

bool LzoCodec::compress( const unsigned char * src,
                         std::size_t src_len,
                         std::vector<unsigned char> & out )
{
	if( !available() ) {
		return false;
	}

	out.resize( max_compressed_size( src_len ) );

	lzo_uint out_len = (lzo_uint)out.size();

	// LZO wants a dereferenceable pointer even for a zero-length input.
	static const unsigned char empty_input = 0;
	const lzo_bytep src_ptr = src_len ? src : &empty_input;

	const int rc = lzo1x_1_compress( src_ptr,
	                                 (lzo_uint)src_len,
	                                 out.data(),
	                                 &out_len,
	                                 work_.data() );

	if( rc != LZO_E_OK ) {
		out.clear();
		return false;
	}

	out.resize( (std::size_t)out_len );
	return true;
}

bool LzoCodec::decompress( const unsigned char * src,
                           std::size_t src_len,
                           unsigned char * dst,
                           std::size_t expected_len )
{
	if( !available() ) {
		return false;
	}

	static unsigned char empty_output = 0;
	unsigned char * dst_ptr = expected_len ? dst : &empty_output;

	lzo_uint out_len = (lzo_uint)expected_len;

	const int rc = lzo1x_decompress_safe( src,
	                                      (lzo_uint)src_len,
	                                      dst_ptr,
	                                      &out_len,
	                                      nullptr );

	// A size mismatch is as bad as an error: it means the entry table is inconsistent
	// with the payload, so the extracted file would be silently wrong.
	return rc == LZO_E_OK && (std::size_t)out_len == expected_len;
}

} // namespace rfa
