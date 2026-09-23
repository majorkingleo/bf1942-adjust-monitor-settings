/**
 * Reader for Battlefield 1942 .rfa archives.
 *
 * @author Copyright (c) 2026
 */

#include "RfaArchive.h"

#include "LzoCodec.h"

#include <cstring>

namespace rfa {

namespace {

/// Sanity bound on a name length. Real entries are well under 100 bytes; anything huge
/// means we have lost sync with the table and should stop rather than allocate wildly.
constexpr std::uint32_t MAX_NAME_LEN = 4096;

/// Sanity bound on chunkCount. 741 MB / 32 KiB is ~22,600, so this is generous.
constexpr std::uint32_t MAX_CHUNK_COUNT = 1'000'000;

std::uint32_t read_u32( const unsigned char * p )
{
	return (std::uint32_t)p[0]
	     | ( (std::uint32_t)p[1] << 8 )
	     | ( (std::uint32_t)p[2] << 16 )
	     | ( (std::uint32_t)p[3] << 24 );
}

/// Reads exactly `n` bytes at `offset`. Returns false on a short read.
bool read_exact( std::ifstream & file, std::uint64_t offset, void * buffer, std::size_t n )
{
	if( n == 0 ) {
		return true;
	}

	file.seekg( (std::streamoff)offset, std::ios::beg );
	if( !file ) {
		return false;
	}

	file.read( (char *)buffer, (std::streamsize)n );
	return file.gcount() == (std::streamsize)n;
}

std::string join_problem( const std::string & name, const std::string & message )
{
	return name.empty() ? message : ( name + ": " + message );
}

std::string to_hex( std::uint32_t value )
{
	static const char digits[] = "0123456789ABCDEF";
	std::string out = "0x";

	for( int shift = 28; shift >= 0; shift -= 4 ) {
		out += digits[( value >> shift ) & 0xF];
	}

	return out;
}

} // namespace

// ---------------------------------------------------------------------------
// RfaArchive
// ---------------------------------------------------------------------------

bool RfaArchive::open( const std::string & path, std::string * error )
{
	path_ = path;
	entries_.clear();
	problems_.clear();
	file_size_ = 0;
	toc_offset_ = 0;
	version_ = 0;

	std::ifstream file( path.c_str(), std::ios::binary );

	if( !file ) {
		if( error ) {
			*error = "cannot open '" + path + "'";
		}
		return false;
	}

	file.seekg( 0, std::ios::end );
	file_size_ = (std::uint64_t)file.tellg();
	file.seekg( 0, std::ios::beg );

	if( file_size_ < MIN_DATA_OFFSET ) {
		if( error ) {
			*error = "'" + path + "' is only " + std::to_string( file_size_ )
			       + " bytes: too small to hold an RFA header";
		}
		return false;
	}

	unsigned char header[MIN_DATA_OFFSET];
	if( !read_exact( file, 0, header, sizeof( header ) ) ) {
		if( error ) {
			*error = "'" + path + "': could not read the file header";
		}
		return false;
	}

	toc_offset_ = read_u32( header );
	version_ = read_u32( header + 4 );

	if( version_ != VERSION_0 && version_ != VERSION_1 ) {
		problems_.push_back( "version is " + std::to_string( version_ )
		                   + ", only 0 and 1 have been observed" );
	}

	if( toc_offset_ < MIN_DATA_OFFSET || toc_offset_ + 4 > file_size_ ) {
		if( error ) {
			*error = "'" + path + "': tocOffset " + std::to_string( toc_offset_ )
			       + " is outside the file (size " + std::to_string( file_size_ ) + ")";
		}
		return false;
	}

	if( !parse_table( file, error ) ) {
		return false;
	}

	for( Entry & entry : entries_ ) {
		classify_entry( file, entry );
	}

	return true;
}

bool RfaArchive::parse_table( std::ifstream & file, std::string * error )
{
	unsigned char buffer[ENTRY_FIELDS_SIZE];

	std::uint64_t position = toc_offset_;

	unsigned char count_bytes[4];
	if( !read_exact( file, position, count_bytes, 4 ) ) {
		if( error ) {
			*error = "'" + path_ + "': directory table is truncated before its file count";
		}
		return false;
	}
	position += 4;

	const std::uint32_t count = read_u32( count_bytes );
	entries_.reserve( count );

	for( std::uint32_t i = 0; i < count; ++i ) {
		unsigned char name_len_bytes[4];
		if( !read_exact( file, position, name_len_bytes, 4 ) ) {
			problems_.push_back( "entry " + std::to_string( i ) + ": truncated before nameLen" );
			return true;   // keep what we have; open() still succeeds
		}
		position += 4;

		const std::uint32_t name_len = read_u32( name_len_bytes );

		if( name_len == 0 || name_len > MAX_NAME_LEN ) {
			problems_.push_back( "entry " + std::to_string( i ) + ": implausible nameLen "
			                   + std::to_string( name_len ) + " - table is likely corrupt" );
			return true;
		}

		std::vector<char> raw_name( name_len );
		if( !read_exact( file, position, raw_name.data(), name_len ) ) {
			problems_.push_back( "entry " + std::to_string( i ) + ": name runs past EOF" );
			return true;
		}
		position += name_len;

		if( !read_exact( file, position, buffer, ENTRY_FIELDS_SIZE ) ) {
			problems_.push_back( "entry " + std::to_string( i ) + ": truncated in the fixed fields" );
			return true;
		}
		position += ENTRY_FIELDS_SIZE;

		Entry entry;
		// nameLen is the exact length; tolerate a stray NUL rather than rejecting the file
		std::size_t used = name_len;
		while( used > 0 && raw_name[used - 1] == '\0' ) {
			--used;
		}
		if( used != name_len ) {
			problems_.push_back( "entry " + std::to_string( i )
			                   + ": name is NUL-terminated, which the format does not use" );
		}
		entry.name.assign( raw_name.data(), used );

		entry.stored_size = read_u32( buffer );
		entry.uncompressed_size = read_u32( buffer + 4 );
		entry.data_offset = read_u32( buffer + 8 );
		entry.reserved1 = read_u32( buffer + 12 );
		entry.reserved2 = read_u32( buffer + 16 );
		entry.flags = read_u32( buffer + 20 );

		entries_.push_back( entry );
	}

	unsigned char terminator[4];
	if( !read_exact( file, position, terminator, 4 ) ) {
		problems_.push_back( "directory table is truncated before its terminator" );
		return true;
	}
	position += 4;

	if( read_u32( terminator ) != 0 ) {
		problems_.push_back( "table terminator is " + to_hex( read_u32( terminator ) )
		                   + ", expected 0" );
	}

	if( position != file_size_ ) {
		problems_.push_back( std::to_string( file_size_ - position )
		                   + " bytes follow the directory table" );
	}

	return true;
}

bool RfaArchive::classify_entry( std::ifstream & file, Entry & entry )
{
	const std::string label = "entry '" + entry.name + "'";
	const std::uint64_t data_end = (std::uint64_t)entry.data_offset + entry.stored_size;

	if( entry.data_offset < MIN_DATA_OFFSET ) {
		problems_.push_back( label + ": dataOffset " + std::to_string( entry.data_offset )
		                   + " sits inside the file header" );
		return false;
	}

	if( data_end > file_size_ ) {
		problems_.push_back( label + ": data block ends at " + std::to_string( data_end )
		                   + " which is past EOF (" + std::to_string( file_size_ ) + ")" );
		return false;
	}

	// Variant selection is purely structural. `version` is NOT a discriminator: 235 raw
	// entries live inside version-1 archives.
	if( entry.uncompressed_size == 0 ) {
		entry.variant = PayloadVariant::Empty;

		if( entry.stored_size != EMPTY_STORED_SIZE ) {
			problems_.push_back( label + ": empty entry has storedSize "
			                   + std::to_string( entry.stored_size ) + ", expected "
			                   + std::to_string( EMPTY_STORED_SIZE ) );
		}
		return true;
	}

	if( entry.stored_size == entry.uncompressed_size ) {
		entry.variant = PayloadVariant::Raw;
		return true;
	}

	unsigned char header[BLOCK_HEADER_SIZE];
	if( !read_exact( file, entry.data_offset, header, BLOCK_HEADER_SIZE ) ) {
		problems_.push_back( label + ": chunk header runs past EOF" );
		return false;
	}

	const std::uint32_t chunk_count = read_u32( header );
	if( chunk_count == 0 || chunk_count > MAX_CHUNK_COUNT ) {
		problems_.push_back( label + ": implausible chunkCount " + std::to_string( chunk_count ) );
		return false;
	}

	entry.variant = PayloadVariant::Chunked;
	entry.chunks.clear();
	entry.chunks.reserve( chunk_count );

	entry.chunks.push_back( Chunk{ read_u32( header + 4 ), read_u32( header + 8 ), read_u32( header + 12 ) } );

	if( chunk_count > 1 ) {
		std::vector<unsigned char> extra( CHUNK_DESCRIPTOR_SIZE * ( chunk_count - 1 ) );
		if( !read_exact( file, (std::uint64_t)entry.data_offset + BLOCK_HEADER_SIZE,
		                 extra.data(), extra.size() ) ) {
			problems_.push_back( label + ": chunk descriptor table runs past EOF" );
			entry.chunks.clear();
			return false;
		}

		for( std::uint32_t i = 1; i < chunk_count; ++i ) {
			const unsigned char * p = extra.data() + CHUNK_DESCRIPTOR_SIZE * ( i - 1 );
			entry.chunks.push_back( Chunk{ read_u32( p ), read_u32( p + 4 ), read_u32( p + 8 ) } );
		}
	}

	// --- invariants --------------------------------------------------------
	const std::uint32_t expected_chunks = chunk_count_for( entry.uncompressed_size );
	if( chunk_count != expected_chunks ) {
		problems_.push_back( label + ": chunkCount " + std::to_string( chunk_count )
		                   + " != ceil(uncompressedSize/" + std::to_string( CHUNK_SIZE )
		                   + ") = " + std::to_string( expected_chunks ) );
	}

	std::uint64_t total_compressed = 0;
	std::uint64_t total_uncompressed = 0;
	std::uint32_t running_offset = 0;

	for( std::size_t i = 0; i < entry.chunks.size(); ++i ) {
		const Chunk & chunk = entry.chunks[i];

		if( chunk.payload_offset != running_offset ) {
			problems_.push_back( label + ": chunk " + std::to_string( i ) + " payloadOffset "
			                   + std::to_string( chunk.payload_offset ) + " != "
			                   + std::to_string( running_offset ) );
		}
		running_offset += chunk.compressed_size;

		if( chunk.uncompressed_size > CHUNK_SIZE ) {
			problems_.push_back( label + ": chunk " + std::to_string( i )
			                   + " uncompressedSize " + std::to_string( chunk.uncompressed_size )
			                   + " exceeds " + std::to_string( CHUNK_SIZE ) );
		} else if( i + 1 < entry.chunks.size() && chunk.uncompressed_size != CHUNK_SIZE ) {
			problems_.push_back( label + ": non-final chunk " + std::to_string( i )
			                   + " is not full" );
		}

		total_compressed += chunk.compressed_size;
		total_uncompressed += chunk.uncompressed_size;
	}

	const std::uint32_t header_size = entry.header_size();
	if( entry.stored_size != header_size + total_compressed ) {
		problems_.push_back( label + ": storedSize " + std::to_string( entry.stored_size )
		                   + " != " + std::to_string( header_size ) + " + sum(compressedSize) "
		                   + std::to_string( total_compressed ) );
	}

	if( total_uncompressed != entry.uncompressed_size ) {
		problems_.push_back( label + ": sum(chunk uncompressedSize) "
		                   + std::to_string( total_uncompressed ) + " != uncompressedSize "
		                   + std::to_string( entry.uncompressed_size ) );
	}

	return true;
}

const Entry * RfaArchive::find( const std::string & name ) const
{
	for( const Entry & entry : entries_ ) {
		if( entry.name == name ) {
			return &entry;
		}
	}

	return nullptr;
}

std::uint64_t RfaArchive::total_uncompressed_size() const
{
	std::uint64_t total = 0;
	for( const Entry & entry : entries_ ) {
		total += entry.uncompressed_size;
	}
	return total;
}

std::uint64_t RfaArchive::total_stored_size() const
{
	std::uint64_t total = 0;
	for( const Entry & entry : entries_ ) {
		total += entry.stored_size;
	}
	return total;
}

std::size_t RfaArchive::count_of( PayloadVariant variant ) const
{
	std::size_t count = 0;
	for( const Entry & entry : entries_ ) {
		if( entry.variant == variant ) {
			++count;
		}
	}
	return count;
}

// ---------------------------------------------------------------------------
// PayloadReader
// ---------------------------------------------------------------------------

PayloadReader::PayloadReader( const std::string & path )
: file_( path.c_str(), std::ios::binary )
{
}

bool PayloadReader::read( const Entry & entry, std::vector<unsigned char> & out, std::string * error )
{
	out.clear();

	if( entry.variant == PayloadVariant::Empty ) {
		return true;   // expands to nothing
	}

	if( entry.variant == PayloadVariant::Raw ) {
		out.resize( entry.uncompressed_size );

		if( !read_exact( file_, entry.data_offset, out.data(), out.size() ) ) {
			if( error ) {
				*error = "entry '" + entry.name + "': cannot read the raw payload";
			}
			out.clear();
			return false;
		}

		return true;
	}

	if( entry.variant != PayloadVariant::Chunked || entry.chunks.empty() ) {
		if( error ) {
			*error = "entry '" + entry.name + "': payload variant is not readable";
		}
		return false;
	}

	out.resize( entry.uncompressed_size );

	const std::uint64_t payload_base = entry.payload_offset();
	std::vector<unsigned char> compressed;
	std::size_t written = 0;

	for( std::size_t i = 0; i < entry.chunks.size(); ++i ) {
		const Chunk & chunk = entry.chunks[i];
		const std::uint64_t chunk_offset = payload_base + chunk.payload_offset;

		if( chunk.is_compressed() ) {
			compressed.resize( chunk.compressed_size );

			if( !read_exact( file_, chunk_offset, compressed.data(), compressed.size() ) ) {
				if( error ) {
					*error = "entry '" + entry.name + "': cannot read chunk "
					       + std::to_string( i ) + " of " + std::to_string( entry.chunks.size() );
				}
				out.clear();
				return false;
			}

			if( !LzoCodec::decompress( compressed.data(),
			                           chunk.compressed_size,
			                           out.data() + written,
			                           chunk.uncompressed_size ) ) {
				if( error ) {
					*error = "entry '" + entry.name + "': chunk " + std::to_string( i )
					       + " failed to decompress";
				}
				out.clear();
				return false;
			}
		} else {
			if( !read_exact( file_, chunk_offset, out.data() + written, chunk.uncompressed_size ) ) {
				if( error ) {
					*error = "entry '" + entry.name + "': cannot read stored chunk "
					       + std::to_string( i );
				}
				out.clear();
				return false;
			}
		}

		written += chunk.uncompressed_size;
	}

	if( written != entry.uncompressed_size ) {
		if( error ) {
			*error = "entry '" + entry.name + "': expanded to " + std::to_string( written )
			       + " bytes but the table says " + std::to_string( entry.uncompressed_size );
		}
		out.clear();
		return false;
	}

	return true;
}

} // namespace rfa
