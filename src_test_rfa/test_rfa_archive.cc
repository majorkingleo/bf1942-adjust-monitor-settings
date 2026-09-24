/**
 * Testcases for rfa/RfaArchive.h, driven by the real fixtures in tests/data.
 *
 * @author Copyright (c) 2026
 */

#include "test_rfa_archive.h"

#include "rfa/RfaArchive.h"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace rfa;

namespace {

const char * FH      = "fh/Battle_Of_Pavlov-1942.rfa";
const char * MESH    = "tiny/standardMesh_001.rfa";
const char * SALERNO = "tiny/salerno_001.rfa";
const char * PEENE   = "tiny/Peenemunde_001.rfa";

const char * CONQUEST = "bf1942/levels/Battle_Of_Pavlov-1942/Conquest.con";

/**
 * The test programs run from the build directory, which for an in-tree build is the repo
 * root, so a relative path works. RFA_TEST_DATA_DIR overrides the search.
 */
std::string fixture_dir()
{
	std::vector<std::string> candidates;

	if( const char * from_env = std::getenv( "RFA_TEST_DATA_DIR" ) ) {
		if( *from_env ) {
			candidates.push_back( from_env );
		}
	}

	candidates.push_back( "tests/data" );
	candidates.push_back( "../tests/data" );
	candidates.push_back( "../../tests/data" );

	for( const std::string & candidate : candidates ) {
		std::ifstream probe( ( candidate + "/" + FH ).c_str(), std::ios::binary );
		if( probe.good() ) {
			return candidate;
		}
	}

	return std::string();
}

bool fixtures_available()
{
	static const bool available = !fixture_dir().empty();

	if( !available ) {
		std::cout << "[rfa] tests/data was not found relative to the current directory, so "
		             "every fixture-dependent testcase will fail. Run the tests from the "
		             "build directory or set RFA_TEST_DATA_DIR.\n";
	}

	return available;
}

std::string fixture( const char * relative )
{
	const std::string dir = fixture_dir();
	return dir.empty() ? std::string() : ( dir + "/" + relative );
}

bool open_fixture( const char * relative, RfaArchive & archive )
{
	if( !fixtures_available() ) {
		return false;
	}

	std::string error;

	if( !archive.open( fixture( relative ), &error ) ) {
		std::cout << "[rfa] could not open fixture " << relative << ": " << error << "\n";
		return false;
	}

	return true;
}

std::vector<unsigned char> read_whole_file( const std::string & path )
{
	std::vector<unsigned char> data;

	std::ifstream file( path.c_str(), std::ios::binary );
	if( !file ) {
		return data;
	}

	char buffer[8192];
	while( file.read( buffer, sizeof( buffer ) ) || file.gcount() > 0 ) {
		data.insert( data.end(), buffer, buffer + file.gcount() );
		if( file.gcount() < (std::streamsize)sizeof( buffer ) ) {
			break;
		}
	}

	return data;
}

void write_whole_file( const std::string & path, const std::vector<unsigned char> & data )
{
	std::ofstream file( path.c_str(), std::ios::binary );
	file.write( (const char *)data.data(), (std::streamsize)data.size() );
}

bool read_entry( const RfaArchive & archive, const char * name, std::vector<unsigned char> & out )
{
	const Entry * entry = archive.find( name );

	if( entry == nullptr ) {
		return false;
	}

	PayloadReader reader( archive.path() );
	return reader.read( *entry, out );
}

bool starts_with( const std::vector<unsigned char> & data, const char * prefix, std::size_t length )
{
	if( data.size() < length ) {
		return false;
	}

	for( std::size_t i = 0; i < length; ++i ) {
		if( data[i] != (unsigned char)prefix[i] ) {
			return false;
		}
	}

	return true;
}

} // namespace

// ---------------------------------------------------------------------------

TestCasePtr test_archive_all_fixtures_open_without_problems()
{
	return std::make_shared<TestCaseFuncNoInp>(
		"archive_all_fixtures_open_without_problems", true, []() {
			const char * paths[] = { FH, MESH, SALERNO, PEENE };

			for( const char * relative : paths ) {
				RfaArchive archive;
				if( !open_fixture( relative, archive ) ) {
					return false;
				}

				// tools/rfa_probe.py reports zero problems for all four fixtures; the
				// library must agree, otherwise one of the two is wrong.
				if( !archive.problems().empty() ) {
					std::cout << "[rfa] " << relative << " reported problems:\n";
					for( const std::string & problem : archive.problems() ) {
						std::cout << "        " << problem << "\n";
					}
					return false;
				}
			}

			return true;
		} );
}

TestCasePtr test_archive_entry_counts_match_fixtures()
{
	return std::make_shared<TestCaseFuncNoInp>(
		"archive_entry_counts_match_fixtures", true, []() {
			struct Expectation { const char * path; std::uint32_t count; };
			const Expectation expectations[] = {
				{ FH, 251 }, { MESH, 6 }, { SALERNO, 1 }, { PEENE, 1 },
			};

			for( const Expectation & expectation : expectations ) {
				RfaArchive archive;
				if( !open_fixture( expectation.path, archive ) ) {
					return false;
				}

				if( archive.entries().size() != expectation.count ) {
					return false;
				}
			}

			return true;
		} );
}

TestCasePtr test_archive_variants_match_fixtures()
{
	return std::make_shared<TestCaseFuncNoInp>(
		"archive_variants_match_fixtures", true, []() {
			RfaArchive fh;
			RfaArchive peene;

			if( !open_fixture( FH, fh ) || !open_fixture( PEENE, peene ) ) {
				return false;
			}

			// The map archive is entirely chunked, compressed or not.
			if( fh.count_of( PayloadVariant::Chunked ) != 251 ) {
				return false;
			}

			// Peenemunde is the raw-payload fixture: version 0, storedSize ==
			// uncompressedSize, no block header at all.
			if( peene.count_of( PayloadVariant::Raw ) != 1 || peene.version() != VERSION_0 ) {
				return false;
			}

			// Documented coverage gap: NONE of the committed fixtures contains an empty
			// entry, so the Empty path is not exercised by real data yet.
			if( fh.count_of( PayloadVariant::Empty ) != 0
			 || peene.count_of( PayloadVariant::Empty ) != 0 ) {
				return false;
			}

			return true;
		} );
}

TestCasePtr test_archive_every_entry_has_consistent_sizes()
{
	return std::make_shared<TestCaseFuncNoInp>(
		"archive_every_entry_has_consistent_sizes", true, []() {
			const char * paths[] = { FH, MESH, SALERNO, PEENE };

			for( const char * relative : paths ) {
				RfaArchive archive;
				if( !open_fixture( relative, archive ) ) {
					return false;
				}

				for( const Entry & entry : archive.entries() ) {
					if( entry.variant == PayloadVariant::Unknown ) {
						return false;
					}

					if( entry.variant == PayloadVariant::Empty ) {
						if( entry.uncompressed_size != 0 ) {
							return false;
						}
						continue;
					}

					if( entry.variant == PayloadVariant::Raw ) {
						if( entry.stored_size != entry.uncompressed_size ) {
							return false;
						}
						continue;
					}

					// chunked
					if( entry.chunk_count() != chunk_count_for( entry.uncompressed_size ) ) {
						return false;
					}

					if( entry.stored_size != entry.header_size() + entry.total_compressed_size() ) {
						return false;
					}

					std::uint64_t uncompressed = 0;
					std::uint32_t running_offset = 0;

					for( const Chunk & chunk : entry.chunks ) {
						if( chunk.payload_offset != running_offset ) {
							return false;
						}
						running_offset += chunk.compressed_size;
						uncompressed += chunk.uncompressed_size;
					}

					if( uncompressed != entry.uncompressed_size ) {
						return false;
					}
				}
			}

			return true;
		} );
}

TestCasePtr test_archive_multi_chunk_entry_matches_its_table()
{
	return std::make_shared<TestCaseFuncNoInp>(
		"archive_multi_chunk_entry_matches_its_table", true, []() {
			RfaArchive archive;
			if( !open_fixture( FH, archive ) ) {
				return false;
			}

			const Entry * biggest = nullptr;
			std::size_t multi_chunk_count = 0;

			for( const Entry & entry : archive.entries() ) {
				if( entry.chunk_count() > 1 ) {
					++multi_chunk_count;

					if( biggest == nullptr || entry.chunk_count() > biggest->chunk_count() ) {
						biggest = &entry;
					}
				}
			}

			// 92 entries in this archive span more than one chunk.
			if( multi_chunk_count < 90 || biggest == nullptr ) {
				return false;
			}

			if( biggest->chunk_count() != 22 || biggest->uncompressed_size != 699192u ) {
				return false;
			}

			// every chunk but the last must be exactly CHUNK_SIZE uncompressed
			for( std::size_t i = 0; i + 1 < biggest->chunks.size(); ++i ) {
				if( biggest->chunks[i].uncompressed_size != CHUNK_SIZE ) {
					return false;
				}
			}

			// 21 full chunks plus the remainder
			const std::uint64_t remainder = biggest->chunks.back().uncompressed_size;
			return remainder == 699192u - 21u * CHUNK_SIZE;
		} );
}

TestCasePtr test_archive_reader_expands_every_entry_to_declared_size()
{
	// The strongest thing available before the Phase 3 oracle comparison: decompress
	// every payload of every fixture and check the length is exactly what the table
	// promised. A wrong chunk offset, a wrong LZO stream or an off-by-one in the
	// descriptor arithmetic all show up here.
	return std::make_shared<TestCaseFuncNoInp>(
		"archive_reader_expands_every_entry_to_declared_size", true, []() {
			const char * paths[] = { PEENE, SALERNO, MESH, FH };

			for( const char * relative : paths ) {
				RfaArchive archive;
				if( !open_fixture( relative, archive ) ) {
					return false;
				}

				PayloadReader reader( archive.path() );
				if( !reader.good() ) {
					return false;
				}

				for( const Entry & entry : archive.entries() ) {
					std::vector<unsigned char> payload;
					std::string error;

					if( !reader.read( entry, payload, &error ) ) {
						std::cout << "[rfa] " << relative << " " << error << "\n";
						return false;
					}

					if( payload.size() != entry.uncompressed_size ) {
						std::cout << "[rfa] " << relative << " entry '" << entry.name
						          << "' expanded to " << payload.size() << " bytes, table says "
						          << entry.uncompressed_size << "\n";
						return false;
					}
				}
			}

			return true;
		} );
}

TestCasePtr test_archive_conquest_con_matches_phase0_ground_truth()
{
	// Phase 0 extracted this file with the ORIGINAL rfaUnpack.exe and read the bytes off
	// disk: 373 bytes starting with "Game.setNumberOfTickets 1 115\r\n". Pinning it here
	// ties the C++ reader all the way back to the vendor binary.
	return std::make_shared<TestCaseFuncNoInp>(
		"archive_conquest_con_matches_phase0_ground_truth", true, []() {
			RfaArchive archive;
			if( !open_fixture( FH, archive ) ) {
				return false;
			}

			std::vector<unsigned char> payload;
			if( !read_entry( archive, CONQUEST, payload ) ) {
				return false;
			}

			static const char expected[] = "Game.setNumberOfTickets 1 115\r\n";

			return payload.size() == 373
			    && starts_with( payload, expected, sizeof( expected ) - 1 );
		} );
}

TestCasePtr test_archive_raw_entry_is_read_verbatim()
{
	// Peenemunde's entry has no block header: the payload IS the file content.
	return std::make_shared<TestCaseFuncNoInp>(
		"archive_raw_entry_is_read_verbatim", true, []() {
			RfaArchive archive;
			if( !open_fixture( PEENE, archive ) ) {
				return false;
			}

			if( archive.entries().size() != 1 ) {
				return false;
			}

			std::vector<unsigned char> payload;
			PayloadReader reader( archive.path() );

			if( !reader.read( archive.entries()[0], payload ) ) {
				return false;
			}

			static const char expected[] = "Game.setLocalized 1";

			return payload.size() == 991
			    && starts_with( payload, expected, sizeof( expected ) - 1 );
		} );
}

TestCasePtr test_archive_find_returns_known_paths()
{
	return std::make_shared<TestCaseFuncNoInp>(
		"archive_find_returns_known_paths", true, []() {
			RfaArchive archive;
			if( !open_fixture( FH, archive ) ) {
				return false;
			}

			const Entry * conquest = archive.find( CONQUEST );

			return conquest != nullptr
			    && conquest->uncompressed_size == 373
			    && conquest->variant == PayloadVariant::Chunked
			    && conquest->chunk_count() == 1
			    && archive.find( "bf1942/levels/NoSuchMap/Init.con" ) == nullptr;
		} );
}

TestCasePtr test_archive_detects_truncated_file()
{
	return std::make_shared<TestCaseFuncOneFile>(
		"archive_detects_truncated_file",
		[]( const std::string & file ) {
			if( !fixtures_available() ) {
				return false;
			}

			const std::vector<unsigned char> original = read_whole_file( fixture( SALERNO ) );
			if( original.size() < 400 ) {
				return false;
			}

			// keep the header, drop the table entirely: the tocOffset now points past EOF
			write_whole_file( file, std::vector<unsigned char>( original.begin(), original.begin() + 300 ) );

			RfaArchive archive;
			std::string error;

			if( archive.open( file, &error ) ) {
				// opening succeeded, so it must at least have complained
				return !archive.problems().empty();
			}

			return true;
		},
		std::ios::out );
}

TestCasePtr test_archive_detects_garbage()
{
	return std::make_shared<TestCaseFuncOneFile>(
		"archive_detects_garbage",
		[]( const std::string & file ) {
			const char * junk = "this is definitely not an rfa archive, not even close";
			{
				std::ofstream out( file.c_str(), std::ios::binary );
				out.write( junk, 54 );
			}

			RfaArchive archive;
			std::string error;

			// 54 bytes: tocOffset is read from the junk and lands outside the file
			return !archive.open( file, &error ) && !error.empty();
		},
		std::ios::out );
}

TestCasePtr test_archive_detects_empty_file()
{
	return std::make_shared<TestCaseFuncOneFile>(
		"archive_detects_empty_file",
		[]( const std::string & file ) {
			{ std::ofstream out( file.c_str(), std::ios::binary ); }

			RfaArchive archive;
			std::string error;

			return !archive.open( file, &error ) && !error.empty();
		},
		std::ios::out );
}

TestCasePtr test_archive_unreadable_path_fails_cleanly()
{
	return std::make_shared<TestCaseFuncNoInp>(
		"archive_unreadable_path_fails_cleanly", true, []() {
			RfaArchive archive;
			std::string error;

			return !archive.open( "no/such/archive.rfa", &error )
			    && !error.empty()
			    && archive.entries().empty();
		} );
}

TestCasePtr test_archive_reads_a_chunk_that_compresses_to_its_own_length()
{
	// Finding 30: a chunk can be an LZO1X stream even when compressedSize ==
	// uncompressedSize, so the size comparison cannot decide on its own. Found on the
	// shipping Battle_of_Britain.rfa, where our reader returned the COMPRESSED bytes as
	// file content - silently, because the length was right.
	//
	// The payload below is that entry, copied out of the archive byte for byte, together
	// with the content the original tool extracts from it. Reproducing the trap with the
	// real bytes beats inventing a stream: a synthetic one would only prove what miniLZO
	// does with input we chose.
	//
	//   Bf1942/Levels/Battle_of_Britain/Objects/Willy/Willy.con
	//   data block: chunkCount 1, compressed 30, uncompressed 30, payloadOffset 0
	//   payload   : 1e 72 75 ... 11 00 00   (an LZO1X stream, 30 bytes)
	//   content   : "run objects\r\nrun weapons\r\n\r\n\r\n"   (30 bytes)
	return std::make_shared<TestCaseFuncOneFile>(
		"archive_reads_a_chunk_that_compresses_to_its_own_length",
		[]( const std::string & file ) {
			static const unsigned char payload[] = {
				0x1E, 'r', 'u', 'n', ' ', 'o', 'b', 'j', 'e', 'c', 't', 's', 0x0D, 0x0A,
				0x70, 0x01, 0x03, 'w', 'e', 'a', 'p', 'o', 'n',
				0x50, 0x01, 0x64, 0x00, 0x11, 0x00, 0x00
			};

			const std::string expected = "run objects\r\nrun weapons\r\n\r\n\r\n";
			const std::string name     = "Bf1942/Levels/Battle_of_Britain/Objects/Willy/Willy.con";

			static_assert( sizeof( payload ) == 30, "the payload must be the measured 30 bytes" );

			if( expected.size() != sizeof( payload ) ) {
				std::cout << "[rfa] the fixture is inconsistent: content is "
				          << expected.size() << " bytes, the stream is " << sizeof( payload ) << "\n";
				return false;
			}

			// --- build a one-entry version-1 archive around it ------------------
			const std::uint32_t reserved = 148;
			const std::uint32_t data_offset = MIN_DATA_OFFSET + reserved;
			const std::uint32_t block_size = BLOCK_HEADER_SIZE + (std::uint32_t)sizeof( payload );
			const std::uint32_t toc_offset = data_offset + block_size;

			std::vector<unsigned char> archive( toc_offset, 0 );

			const auto put_u32 = []( std::vector<unsigned char> & into, std::uint32_t value ) {
				into.push_back( (unsigned char)( value & 0xFF ) );
				into.push_back( (unsigned char)( ( value >> 8 ) & 0xFF ) );
				into.push_back( (unsigned char)( ( value >> 16 ) & 0xFF ) );
				into.push_back( (unsigned char)( ( value >> 24 ) & 0xFF ) );
			};

			// header patched last
			// data block at 156: chunkCount, then compressedSize/uncompressedSize/payloadOffset
			std::size_t at = data_offset;
			const std::uint32_t header_fields[] = {
				1u, (std::uint32_t)sizeof( payload ), (std::uint32_t)sizeof( payload ), 0u
			};

			for( std::uint32_t value : header_fields ) {
				for( int byte = 0; byte < 4; ++byte ) {
					archive[at + byte] = (unsigned char)( ( value >> ( 8 * byte ) ) & 0xFF );
				}
				at += 4;
			}

			std::memcpy( archive.data() + data_offset + BLOCK_HEADER_SIZE, payload, sizeof( payload ) );

			// directory table at toc_offset
			std::vector<unsigned char> table;
			put_u32( table, 1 );
			put_u32( table, (std::uint32_t)name.size() );
			table.insert( table.end(), name.begin(), name.end() );
			put_u32( table, block_size );
			put_u32( table, (std::uint32_t)sizeof( payload ) );
			put_u32( table, data_offset );
			put_u32( table, RFA_PACK_RESERVED1 );
			put_u32( table, 0 );
			put_u32( table, 0 );
			put_u32( table, 0 );

			archive.insert( archive.end(), table.begin(), table.end() );

			// the 8-byte header
			std::vector<unsigned char> head;
			put_u32( head, toc_offset );
			put_u32( head, VERSION_1 );

			for( std::size_t i = 0; i < head.size(); ++i ) {
				archive[i] = head[i];
			}

			write_whole_file( file, archive );

			if( read_whole_file( file ).size() != archive.size() ) {
				std::cout << "[rfa] could not write the synthetic archive\n";
				return false;
			}

			// --- read it back ---------------------------------------------------
			RfaArchive parsed;

			if( !parsed.open( file ) ) {
				std::cout << "[rfa] the synthetic archive did not open:";
				for( const std::string & problem : parsed.problems() ) {
					std::cout << "\n        " << problem;
				}
				std::cout << "\n";
				return false;
			}

			const Entry * entry = parsed.find( name );

			if( entry == nullptr ) {
				std::cout << "[rfa] the entry is missing from the table\n";
				return false;
			}

			// the trap itself: the sizes say "verbatim"
			if( entry->chunks.size() != 1 || entry->chunks[0].is_compressed() ) {
				std::cout << "[rfa] the fixture no longer reproduces the trap\n";
				return false;
			}

			std::vector<unsigned char> content;
			std::string error;
			PayloadReader reader( parsed.path() );

			if( !reader.read( *entry, content, &error ) ) {
				std::cout << "[rfa] the reader refused the entry: " << error << "\n";
				return false;
			}

			const std::string actual( content.begin(), content.end() );

			if( actual != expected ) {
				std::cout << "[rfa] the reader returned the wrong content; that it returned "
				             "the compressed stream instead is exactly finding 30:\n";
				std::cout << "        got  " << actual.size() << " bytes: " << actual << "\n";
				std::cout << "        want " << expected.size() << " bytes: " << expected << "\n";
				return false;
			}

			return true;
		},
		std::ios::out );
}

TestCasePtr test_archive_corrupt_payload_never_returns_pristine_bytes()
{
	// Flipping bytes inside a compressed payload must either fail outright or produce
	// different output. A silent no-op here would mean extraction ignores corruption.
	return std::make_shared<TestCaseFuncOneFile>(
		"archive_corrupt_payload_never_returns_pristine_bytes",
		[]( const std::string & file ) {
			if( !fixtures_available() ) {
				return false;
			}

			const std::vector<unsigned char> original = read_whole_file( fixture( SALERNO ) );

			RfaArchive pristine_archive;
			if( !pristine_archive.open( fixture( SALERNO ) ) ) {
				return false;
			}

			std::vector<unsigned char> pristine;
			if( !read_entry( pristine_archive, "Bf1942/Levels/salerno/Init.con", pristine ) ) {
				return false;
			}

			// entry data starts at 156, payload after the 16-byte chunk header
			std::vector<unsigned char> corrupt( original );
			for( std::size_t i = 220; i < 260 && i < corrupt.size(); ++i ) {
				corrupt[i] ^= 0xFF;
			}
			write_whole_file( file, corrupt );

			RfaArchive corrupt_archive;
			if( !corrupt_archive.open( file ) ) {
				return true;   // rejected at open - acceptable
			}

			std::vector<unsigned char> damaged;
			const Entry * entry = corrupt_archive.find( "Bf1942/Levels/salerno/Init.con" );

			if( entry == nullptr ) {
				return false;
			}

			PayloadReader reader( corrupt_archive.path() );

			if( !reader.read( *entry, damaged ) ) {
				return true;   // detected
			}

			return damaged != pristine;
		},
		std::ios::out );
}
