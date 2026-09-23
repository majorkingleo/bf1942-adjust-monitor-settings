/**
 * Writer for Battlefield 1942 .rfa archives.
 *
 * @author Copyright (c) 2026
 */

#include "RfaWriter.h"

#include "LzoCodec.h"
#include "RfaStamp.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <thread>

namespace rfa {

namespace {

void append_u32( std::vector<unsigned char> & out, std::uint32_t value )
{
	out.push_back( (unsigned char)( value & 0xFF ) );
	out.push_back( (unsigned char)( ( value >> 8 ) & 0xFF ) );
	out.push_back( (unsigned char)( ( value >> 16 ) & 0xFF ) );
	out.push_back( (unsigned char)( ( value >> 24 ) & 0xFF ) );
}

unsigned effective_threads( unsigned requested, std::size_t count )
{
	unsigned hardware = std::thread::hardware_concurrency();
	if( hardware == 0 ) {
		hardware = 1;
	}

	unsigned threads = requested ? requested : hardware;
	threads = std::max( 1u, std::min( threads, (unsigned)std::min<std::size_t>( count, 64 ) ) );

	return threads;
}

/**
 * Run fn(i, worker) for every i in [0, count), across `threads` workers.
 *
 * The worker id lets each thread keep its own LzoCodec, since the compressor work buffer
 * is not shareable. Results are indexed by `i`, never appended in completion order, so
 * the output is identical no matter how the work interleaves.
 */
void parallel_for( std::size_t count,
                   unsigned threads,
                   const std::function<void( std::size_t, unsigned )> & fn )
{
	if( count == 0 ) {
		return;
	}

	if( threads <= 1 || count == 1 ) {
		for( std::size_t i = 0; i < count; ++i ) {
			fn( i, 0 );
		}
		return;
	}

	std::atomic<std::size_t> next( 0 );
	std::vector<std::thread> pool;
	pool.reserve( threads );

	for( unsigned worker = 0; worker < threads; ++worker ) {
		pool.emplace_back( [&, worker]() {
			for( ;; ) {
				const std::size_t i = next.fetch_add( 1 );
				if( i >= count ) {
					break;
				}
				fn( i, worker );
			}
		} );
	}

	for( std::thread & thread : pool ) {
		thread.join();
	}
}

bool read_file_bytes( const std::string & path, std::vector<unsigned char> & out, std::string * error )
{
	std::ifstream file( path.c_str(), std::ios::binary );

	if( !file ) {
		if( error ) {
			*error = "cannot open '" + path + "'";
		}
		return false;
	}

	file.seekg( 0, std::ios::end );
	const std::streamoff size = file.tellg();
	file.seekg( 0, std::ios::beg );

	if( size > 0 ) {
		out.resize( (std::size_t)size );
		file.read( (char *)out.data(), (std::streamsize)out.size() );

		if( file.gcount() != (std::streamsize)out.size() ) {
			if( error ) {
				*error = "'" + path + "' is shorter than its reported size";
			}
			return false;
		}
	} else {
		out.clear();
	}

	return true;
}

/// One entry, fully encoded and ready to append: `block` is exactly `stored_size` bytes.
struct EncodedEntry
{
	std::string name;
	std::uint32_t uncompressed_size = 0;
	std::uint32_t stored_size = 0;
	std::vector<unsigned char> block;
};

/**
 * Encode one file into its data block.
 *
 * Store policy: the file bytes ARE the block (no header). Compress policy: a chunk table
 * followed by one LZO1X stream per 32 KiB chunk.
 *
 * Every chunk is compressed even when the result is larger than the input, which is what
 * rfaPack.exe does - a 1-byte file becomes a 5-byte chunk. Storing such a chunk verbatim
 * would be smaller, and the reader accepts it, but it would stop our archives from
 * matching the oracle byte for byte. That trade is deliberate.
 */
bool encode_entry( const SourceFile & file,
                   CompressionPolicy policy,
                   unsigned threads,
                   EncodedEntry & out,
                   std::string * error )
{
	std::vector<unsigned char> source;

	if( !read_file_bytes( file.path, source, error ) ) {
		return false;
	}

	out.name = file.name;
	out.uncompressed_size = (std::uint32_t)source.size();
	out.block.clear();

	if( policy == CompressionPolicy::Store ) {
		out.stored_size = (std::uint32_t)source.size();
		out.block = source;
		return true;
	}

	const std::uint32_t chunk_count = chunk_count_for( out.uncompressed_size );
	std::vector<std::vector<unsigned char>> payloads( chunk_count );

	std::atomic<bool> failed( false );
	std::string failure;

	const unsigned workers = effective_threads( threads, chunk_count );
	std::vector<LzoCodec> codecs( (std::size_t)workers );

	parallel_for( chunk_count, workers, [&]( std::size_t index, unsigned worker ) {
		if( failed.load() ) {
			return;
		}

		const std::size_t begin = index * CHUNK_SIZE;
		const std::size_t end = std::min<std::size_t>( begin + CHUNK_SIZE, source.size() );

		std::vector<unsigned char> compressed;

		if( !codecs[worker].compress( source.data() + begin, end - begin, compressed ) ) {
			if( !failed.exchange( true ) ) {
				failure = "entry '" + file.name + "': chunk " + std::to_string( index )
				        + " failed to compress";
			}
			return;
		}

		payloads[index] = std::move( compressed );
	} );

	if( failed.load() ) {
		if( error ) {
			*error = failure;
		}
		return false;
	}

	const std::uint32_t header_size = BLOCK_HEADER_SIZE + CHUNK_DESCRIPTOR_SIZE * ( chunk_count - 1 );
	std::uint32_t total_compressed = 0;

	for( const std::vector<unsigned char> & payload : payloads ) {
		total_compressed += (std::uint32_t)payload.size();
	}

	out.stored_size = header_size + total_compressed;

	out.block.reserve( out.stored_size );
	append_u32( out.block, chunk_count );

	std::uint32_t running_offset = 0;

	for( std::size_t i = 0; i < payloads.size(); ++i ) {
		const std::size_t begin = i * CHUNK_SIZE;
		const std::size_t end = std::min<std::size_t>( begin + CHUNK_SIZE, source.size() );

		append_u32( out.block, (std::uint32_t)payloads[i].size() );
		append_u32( out.block, (std::uint32_t)( end - begin ) );
		append_u32( out.block, running_offset );

		running_offset += (std::uint32_t)payloads[i].size();
	}

	for( const std::vector<unsigned char> & payload : payloads ) {
		out.block.insert( out.block.end(), payload.begin(), payload.end() );
	}

	return true;
}

/**
 * Append the payloads under `dir` in rfaPack.exe's order.
 *
 * The order is part of the output format: entries land in the archive table in exactly
 * this sequence, so any deviation stops our archives matching the oracle byte for byte.
 * It is also deterministic, unlike a plain readdir walk, because every level is sorted.
 */
void collect_into( const std::filesystem::path & dir,
                   const std::string & prefix,
                   std::vector<SourceFile> & out )
{
	std::error_code ec;

	std::vector<std::filesystem::directory_entry> files;
	std::vector<std::filesystem::directory_entry> directories;

	for( const std::filesystem::directory_entry & entry :
	     std::filesystem::directory_iterator( dir, ec ) ) {
		if( ec ) {
			break;
		}

		std::error_code type_ec;

		if( entry.is_regular_file( type_ec ) ) {
			files.push_back( entry );
		} else if( entry.is_directory( type_ec ) ) {
			directories.push_back( entry );
		}
	}

	const auto by_name = []( const std::filesystem::directory_entry & a,
	                         const std::filesystem::directory_entry & b ) {
		return a.path().filename().string() < b.path().filename().string();
	};

	std::sort( files.begin(), files.end(), by_name );
	std::sort( directories.begin(), directories.end(), by_name );

	for( const std::filesystem::directory_entry & entry : files ) {
		SourceFile file;
		file.path = entry.path().string();
		file.name = prefix.empty() ? entry.path().filename().string()
		                           : prefix + "/" + entry.path().filename().string();

		std::error_code size_ec;
		file.size = (std::uint64_t)entry.file_size( size_ec );

		out.push_back( file );
	}

	for( const std::filesystem::directory_entry & entry : directories ) {
		const std::string child = prefix.empty() ? entry.path().filename().string()
		                                        : prefix + "/" + entry.path().filename().string();
		collect_into( entry.path(), child, out );
	}
}

} // namespace

bool RfaWriter::collect_files( const std::string & root,
                               const std::string & base,
                               std::vector<SourceFile> & out,
                               std::string * error )
{
	std::error_code ec;

	if( !std::filesystem::is_directory( root, ec ) ) {
		if( error ) {
			*error = "'" + root + "' is not a directory";
		}
		return false;
	}

	collect_into( std::filesystem::path( root ), base, out );

	return true;
}

bool RfaWriter::write( const std::vector<SourceFile> & files,
                       const std::string & archive_path,
                       const WriteOptions & options,
                       WriteResult * result,
                       std::string * error )
{
	std::vector<const SourceFile *> ordered;
	ordered.reserve( files.size() );

	for( const SourceFile & file : files ) {
		ordered.push_back( &file );
	}

	// No re-sorting: the order supplied reaches the archive verbatim, and collect_files
	// already produces rfaPack.exe's order.

	// Compress chunks of one file at a time, so peak memory is one file plus its encoded
	// block rather than the whole archive. The parallelism is per chunk, which is finer
	// grained than per file anyway.
	const unsigned threads = effective_threads( options.threads, std::max<std::size_t>( ordered.size(), 1 ) );

	const std::uint32_t version = options.policy == CompressionPolicy::Store ? VERSION_0 : VERSION_1;

	std::ofstream out( archive_path.c_str(), std::ios::binary | std::ios::trunc );

	if( !out ) {
		if( error ) {
			*error = "cannot create '" + archive_path + "'";
		}
		return false;
	}

	// helper: a failure must not leave a half-written archive behind that looks valid
	const auto abandon = [&]( const std::string & message ) {
		out.close();
		std::error_code ec;
		std::filesystem::remove( archive_path, ec );

		if( error ) {
			*error = message;
		}

		return false;
	};

	// [0] tocOffset, [4] version - both patched once the table position is known
	const unsigned char zero_header[MIN_DATA_OFFSET] = { 0, 0, 0, 0, 0, 0, 0, 0 };
	out.write( (const char *)zero_header, MIN_DATA_OFFSET );

	// the reserved region that precedes the first data block
	{
		const std::size_t reserved = (std::size_t)DEFAULT_FIRST_DATA_OFFSET - MIN_DATA_OFFSET;
		const std::size_t stamp_size = default_stamp_size();
		const std::size_t copied = std::min( reserved, stamp_size );

		out.write( (const char *)default_stamp(), (std::streamsize)copied );

		if( reserved > copied ) {
			const std::vector<char> padding( reserved - copied, 0 );
			out.write( padding.data(), (std::streamsize)padding.size() );
		}
	}

	struct TableEntry
	{
		std::string name;
		std::uint32_t stored_size = 0;
		std::uint32_t uncompressed_size = 0;
		std::uint32_t data_offset = 0;
	};

	std::vector<TableEntry> table;
	table.reserve( ordered.size() );

	WriteResult summary;

	for( const SourceFile * file : ordered ) {
		EncodedEntry entry;
		std::string entry_error;

		if( !encode_entry( *file, options.policy, threads, entry, &entry_error ) ) {
			return abandon( entry_error );
		}

		TableEntry row;
		row.name = entry.name;
		row.stored_size = entry.stored_size;
		row.uncompressed_size = entry.uncompressed_size;
		row.data_offset = (std::uint32_t)out.tellp();
		table.push_back( std::move( row ) );

		if( !entry.block.empty() ) {
			out.write( (const char *)entry.block.data(), (std::streamsize)entry.block.size() );

			if( !out ) {
				return abandon( "write failed for '" + archive_path + "'" );
			}
		}

		summary.stored_bytes += entry.stored_size;
		summary.uncompressed_bytes += entry.uncompressed_size;
	}

	const std::uint64_t table_offset = (std::uint64_t)out.tellp();

	// --- directory table ---------------------------------------------------
	std::vector<unsigned char> table_bytes;
	table_bytes.reserve( 4 + table.size() * 48 + 4 );
	append_u32( table_bytes, (std::uint32_t)table.size() );

	for( const TableEntry & row : table ) {
		append_u32( table_bytes, (std::uint32_t)row.name.size() );
		table_bytes.insert( table_bytes.end(), row.name.begin(), row.name.end() );
		append_u32( table_bytes, row.stored_size );
		append_u32( table_bytes, row.uncompressed_size );
		append_u32( table_bytes, row.data_offset );
		append_u32( table_bytes, RFA_PACK_RESERVED1 );
		append_u32( table_bytes, 0 );
		append_u32( table_bytes, 0 );
	}

	append_u32( table_bytes, 0 );   // terminator

	out.write( (const char *)table_bytes.data(), (std::streamsize)table_bytes.size() );

	if( !out ) {
		return abandon( "write failed for '" + archive_path + "'" );
	}

	// --- patch the header --------------------------------------------------
	std::vector<unsigned char> header;
	append_u32( header, (std::uint32_t)table_offset );
	append_u32( header, version );

	out.seekp( 0, std::ios::beg );
	out.write( (const char *)header.data(), (std::streamsize)header.size() );

	if( !out ) {
		return abandon( "cannot patch the header of '" + archive_path + "'" );
	}

	out.close();

	if( result ) {
		summary.entry_count = (std::uint32_t)table.size();
		summary.archive_bytes = table_offset + table_bytes.size();
		*result = summary;
	}

	return true;
}

} // namespace rfa
