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

/// Hard ceiling on worker threads, so a pathological --threads cannot exhaust the process.
constexpr unsigned MAX_WORKERS = 64u;

/// A batch of files is grown until it holds this much source data, and is never split inside
/// a file. It bounds the memory a batch holds; it is large enough that an entire ordinary
/// tree (the shipping menu is 22.9 MB) is one batch, so the parallelism does not depend on
/// where a batch boundary happens to fall.
constexpr std::uint64_t BATCH_TARGET_BYTES = 64ull * 1024 * 1024;

/**
 * Worker threads to use: `requested`, or the hardware concurrency when that is 0, clamped to
 * the work available and to MAX_WORKERS.
 *
 * `work_items` must be a count of CHUNKS, not of files. The unit of work is the 32 KiB
 * chunk, and the two differ by orders of magnitude - a tree holding one 22.9 MB file is 1
 * file but 700 chunks. Clamping by the file count, as this did until finding 33, gave that
 * tree `threads = min(24, 1) = 1` and packed it fully serially: measured 0.99 of 24 cores,
 * while the same bytes split over 24 files reached 8.9.
 */
unsigned effective_threads( unsigned requested, std::uint64_t work_items )
{
	unsigned hardware = std::thread::hardware_concurrency();
	if( hardware == 0 ) {
		hardware = 1;
	}

	unsigned threads = requested ? requested : hardware;
	threads = std::max( 1u, std::min( threads,
	                                 (unsigned)std::min<std::uint64_t>( work_items, MAX_WORKERS ) ) );

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
 * Assemble a chunked data block from already-compressed payloads: the chunk count, one
 * 12-byte descriptor per chunk, then the payloads concatenated in order.
 *
 * The descriptors repeat the chunking rule (32 KiB, last chunk short) rather than storing
 * the input sizes, so a reader rebuilds them from the count alone. That is why this needs
 * `source` - not to read it, only to know where the last chunk ends.
 *
 * Every chunk is present even when compression made it larger than its input, which is what
 * rfaPack.exe does: a 1-byte file becomes a 5-byte chunk. Storing such a chunk verbatim
 * would be smaller and our reader would accept it, but it would stop the archive matching
 * the oracle byte for byte. That trade is deliberate.
 */
void build_chunked_block( const std::vector<unsigned char> & source,
                          const std::vector<std::vector<unsigned char>> & payloads,
                          EncodedEntry & out )
{
	const std::uint32_t chunk_count = (std::uint32_t)payloads.size();
	const std::uint32_t header_size =
		BLOCK_HEADER_SIZE + CHUNK_DESCRIPTOR_SIZE * ( chunk_count - 1 );

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
}

/// ASCII-only case folding, to UPPERCASE. Non-ASCII bytes are left alone: the names in the
/// archives are ASCII, and folding them through a locale would make the order depend on the
/// machine.
///
/// Uppercase, not lowercase, is the point. `_` folds to itself in both, but it sits at 0x5F,
/// which is below `a` (0x61) and above `A` (0x41): folding down puts `loading_full` before
/// `loadingfull`, folding up puts `loadingfull` first, and the original does the latter.
inline char fold_ascii( char c )
{
	return ( c >= 'a' && c <= 'z' ) ? static_cast<char>( c - 'a' + 'A' ) : c;
}

/**
 * rfaPack.exe's comparison for entry names within one directory: byte order after folding
 * to **uppercase**.
 *
 * Measured, not guessed, on the shipping `menu.rfa` (618 entries). A plain byte comparison
 * reproduced the same file size while putting 250 of 618 indices in a different order and
 * differing in 1,003,071 bytes; folding to lowercase left 68 indices wrong. Three pairs
 * pin the rule, and uppercase folding is the only one of the three candidates that orders
 * all of them the original's way:
 *
 *   | pair | byte | lower | upper | original |
 *   |---|---|---|---|---|
 *   | `InGame` / `InfantryControlsPage1` | InGame | Infantry… | Infantry… | Infantry… |
 *   | `Icon_PT_Mine.dds` / `icon_artillery.dds` | Icon_PT… | icon_art… | icon_art… | icon_art… |
 *   | `loading_full_256x16.dds` / `loadingfull_256x16.dds` | loading_full | loading_full | **loadingfull** | **loadingfull** |
 *
 * Only the third pair separates lowercase from uppercase: `_` is 0x5F, so it is above `A`
 * and below `a`, and the original treats it as above `F`.
 *
 * Two names that fold to the same thing fall back to the byte comparison, which keeps the
 * order total and deterministic. That branch is unobservable rather than measured: Windows
 * cannot hold such a pair in one directory, and none of the 70 archives in the base-game
 * install contains one (`tools/probe_case_ties.py`).
 */
bool name_less( const std::string & a, const std::string & b )
{
	const std::size_t common = std::min( a.size(), b.size() );

	for( std::size_t i = 0; i < common; ++i ) {
		const char ca = fold_ascii( a[i] );
		const char cb = fold_ascii( b[i] );

		if( ca != cb ) {
			return ca < cb;
		}
	}

	if( a.size() != b.size() ) {
		return a.size() < b.size();
	}

	return a < b;
}

/**
 * Append the payloads under `dir` in rfaPack.exe's order.
 *
 * The order is part of the output format: entries land in the archive table in exactly
 * this sequence, so any deviation stops our archives matching the oracle byte for byte.
 * It is also deterministic, unlike a plain readdir walk, because every level is sorted -
 * with name_less(), which compares case-insensitively.
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
		return name_less( a.path().filename().string(), b.path().filename().string() );
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

unsigned RfaWriter::planned_threads( const std::vector<SourceFile> & files,
                                     const WriteOptions & options )
{
	// Store has no compression queue - the block IS the file - so there is nothing to spread.
	if( options.policy != CompressionPolicy::Compress ) {
		return 1u;
	}

	std::uint64_t chunks = 0;

	for( const SourceFile & file : files ) {
		chunks += chunk_count_for( file.size );
	}

	return effective_threads( options.threads, chunks );
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

	// --- worker budget ---------------------------------------------------------
	//
	// Clamped against the TOTAL chunk count, never against the file count. The unit of work
	// is the 32 KiB chunk: a tree holding one 22.9 MB file is 1 file but 700 chunks, and
	// clamping by files reduced it to a single thread (finding 33).
	const unsigned workers = planned_threads( files, options );

	// One compressor per worker, built once for the whole archive and reused by every batch.
	// 999 needs a ~448 KB work buffer, and building these per FILE cost ~470 MiB of
	// allocate-and-zero for the shipping 22.9 MB menu tree - more churn than the archive
	// itself, and the likely cause of a CPU-time spread of 0.64 to 1.80 s at a constant
	// 0.37 s wall. Reuse is safe: the compressor clears what it needs on every call, which
	// is already proven by one codec serving several chunks within a file.
	std::vector<LzoCodec> codecs( (std::size_t)workers );

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

	// --- encode in batches -----------------------------------------------------
	//
	// A batch is a contiguous run of `ordered`, grown until it holds BATCH_TARGET_BYTES of
	// source data and never split inside a file (a file's chunks share one source buffer).
	// A file larger than the budget therefore becomes a batch of its own - which is what we
	// want, because its chunks alone can fill the pool.
	//
	// Within a batch the work is a FLAT queue of chunks across all its files, so 618
	// one-chunk files and one 700-chunk file fill the pool equally well. That is the whole
	// point: a per-file queue gives a one-chunk file a single worker, and most files in a
	// real tree are one chunk (541 of the shipping menu's 618).
	std::size_t batch_begin = 0;

	while( batch_begin < ordered.size() ) {
		std::size_t batch_end = batch_begin;
		std::uint64_t batch_bytes = 0;

		while( batch_end < ordered.size() ) {
			batch_bytes += ordered[batch_end]->size;
			++batch_end;

			if( batch_bytes >= BATCH_TARGET_BYTES ) {
				break;
			}
		}

		const std::size_t batch_size = batch_end - batch_begin;

		// --- read the batch ---
		//
		// Serial on purpose. Reading 22.9 MB costs 0.043 s against 0.37 s of compression, so
		// spreading it would buy a few percent while forcing the failure path to be reported
		// across threads, for an error that must abandon the archive.
		std::vector<std::vector<unsigned char>> sources( batch_size );
		std::string read_error;

		for( std::size_t i = 0; i < batch_size; ++i ) {
			if( !read_file_bytes( ordered[batch_begin + i]->path, sources[i], &read_error ) ) {
				return abandon( read_error );
			}
		}

		// --- compress every chunk of the batch, as one queue ---
		std::vector<std::vector<std::vector<unsigned char>>> payloads;
		std::atomic<bool> failed( false );
		std::string failure;

		if( options.policy == CompressionPolicy::Compress ) {
			struct Job
			{
				std::size_t slot;      // index within the batch
				std::uint32_t chunk;
			};

			std::vector<Job> jobs;
			payloads.resize( batch_size );

			for( std::size_t i = 0; i < batch_size; ++i ) {
				const std::uint32_t count = chunk_count_for( (std::uint64_t)sources[i].size() );
				payloads[i].resize( count );

				for( std::uint32_t chunk = 0; chunk < count; ++chunk ) {
					jobs.push_back( Job{ i, chunk } );
				}
			}

			// Never spawn more threads than there are chunks to give them: a batch whose
			// files are all one chunk would otherwise create a pool to hand out one job.
			const unsigned width = (unsigned)std::min<std::size_t>( jobs.size(), workers );

			parallel_for( jobs.size(), width, [&]( std::size_t index, unsigned worker ) {
				if( failed.load() ) {
					return;
				}

				const Job & job = jobs[index];
				const std::vector<unsigned char> & source = sources[job.slot];
				const std::size_t begin = (std::size_t)job.chunk * CHUNK_SIZE;
				const std::size_t end = std::min<std::size_t>( begin + CHUNK_SIZE, source.size() );

				std::vector<unsigned char> compressed;

				if( !codecs[worker].compress( source.data() + begin, end - begin,
				                              compressed, options.lzo ) ) {
					if( !failed.exchange( true ) ) {
						failure = "entry '" + ordered[batch_begin + job.slot]->name + "': chunk "
						        + std::to_string( job.chunk ) + " failed to compress";
					}
					return;
				}

				payloads[job.slot][job.chunk] = std::move( compressed );
			} );

			if( failed.load() ) {
				return abandon( failure );
			}
		}

		// --- assemble each entry and append it, in order ---
		for( std::size_t i = 0; i < batch_size; ++i ) {
			EncodedEntry entry;
			entry.name = ordered[batch_begin + i]->name;
			entry.uncompressed_size = (std::uint32_t)sources[i].size();

			if( options.policy == CompressionPolicy::Store ) {
				entry.stored_size = (std::uint32_t)sources[i].size();
				entry.block = std::move( sources[i] );   // the bytes ARE the block
			} else {
				build_chunked_block( sources[i], payloads[i], entry );

				// Drop the source as we go, so a batch of large files is not held at full
				// size until its end. The payloads are all that is still needed.
				std::vector<unsigned char>().swap( sources[i] );
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

		batch_begin = batch_end;
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
