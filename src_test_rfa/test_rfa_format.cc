/**
 * Testcases for rfa/RfaFormat.h.
 *
 * @author Copyright (c) 2026
 */

#include "test_rfa_format.h"

#include "rfa/RfaFormat.h"

#include <string>
#include <vector>

using namespace rfa;

namespace {

Entry make_chunked_entry( const std::vector<Chunk> & chunks, std::uint32_t uncompressed_size )
{
	Entry entry;
	entry.variant = PayloadVariant::Chunked;
	entry.chunks = chunks;
	entry.uncompressed_size = uncompressed_size;
	return entry;
}

} // namespace

TestCasePtr test_format_constants_match_the_documented_layout()
{
	// These numbers are the contract with tools/rfa_probe.py and the format doc. If any
	// of them changes, the validator and this library disagree about the format.
	return std::make_shared<TestCaseFuncNoInp>(
		"format_constants_match_the_documented_layout", true, []() {
			return CHUNK_SIZE == 32768u
			    && BLOCK_HEADER_SIZE == 16u
			    && CHUNK_DESCRIPTOR_SIZE == 12u
			    && MIN_DATA_OFFSET == 8u
			    && ENTRY_FIELDS_SIZE == 24u
			    && EMPTY_STORED_SIZE == 4u;
		} );
}

TestCasePtr test_format_chunk_count_for_uncompressed_size()
{
	return std::make_shared<TestCaseFuncNoInp>(
		"format_chunk_count_for_uncompressed_size", true, []() {
			return chunk_count_for( 0 ) == 1u          // an empty entry still has one chunk
			    && chunk_count_for( 1 ) == 1u
			    && chunk_count_for( 32767 ) == 1u
			    && chunk_count_for( 32768 ) == 1u      // exactly one full chunk
			    && chunk_count_for( 32769 ) == 2u      // one byte spills into a second
			    && chunk_count_for( 65536 ) == 2u
			    && chunk_count_for( 65537 ) == 3u;
		} );
}

TestCasePtr test_format_chunk_count_matches_the_real_fixture_entry()
{
	// The largest entry in tests/data/fh/Battle_Of_Pavlov-1942.rfa: 699,192 bytes across
	// 22 chunks. Pinning a real number keeps the arithmetic honest against the wild data
	// the sweep was run on, not just against round figures.
	return std::make_shared<TestCaseFuncNoInp>(
		"format_chunk_count_matches_the_real_fixture_entry", true, []() {
			return chunk_count_for( 699192 ) == 22u;
		} );
}

TestCasePtr test_format_header_size_grows_by_twelve_per_chunk()
{
	return std::make_shared<TestCaseFuncNoInp>(
		"format_header_size_grows_by_twelve_per_chunk", true, []() {
			const std::vector<Chunk> one( 1, Chunk{ 100, 100, 0 } );
			const std::vector<Chunk> two( 2, Chunk{ 100, 100, 0 } );
			const std::vector<Chunk> four( 4, Chunk{ 100, 100, 0 } );
			const std::vector<Chunk> twentytwo( 22, Chunk{ 100, 100, 0 } );

			// 16, then 16+12, 16+36, and 16+12*21 for the real fixture's biggest entry
			return make_chunked_entry( one, 100 ).header_size() == 16u
			    && make_chunked_entry( two, 100 ).header_size() == 28u
			    && make_chunked_entry( four, 100 ).header_size() == 52u
			    && make_chunked_entry( twentytwo, 100 ).header_size() == 268u;
		} );
}

TestCasePtr test_format_header_size_is_zero_for_raw_and_empty()
{
	// Raw and Empty entries have NO block header - the payload starts at dataOffset. This
	// is the single easiest thing to get wrong, and getting it wrong shifts every byte.
	return std::make_shared<TestCaseFuncNoInp>(
		"format_header_size_is_zero_for_raw_and_empty", true, []() {
			Entry raw;
			raw.variant = PayloadVariant::Raw;
			raw.data_offset = 156;
			raw.stored_size = 991;
			raw.uncompressed_size = 991;

			Entry empty;
			empty.variant = PayloadVariant::Empty;
			empty.data_offset = 100;

			Entry unknown;
			unknown.variant = PayloadVariant::Unknown;

			return raw.header_size() == 0u
			    && raw.payload_offset() == 156u
			    && empty.header_size() == 0u
			    && empty.payload_offset() == 100u
			    && unknown.header_size() == 0u;
		} );
}

TestCasePtr test_format_chunk_is_compressed_rule()
{
	// Equality, not less-than. A chunk that LZO expanded is still an LZO stream: treating
	// it as verbatim silently returns compressed bytes as if they were file content.
	return std::make_shared<TestCaseFuncNoInp>(
		"format_chunk_is_compressed_rule", true, []() {
			const Chunk shrunk{ 100, 200, 0 };      // compressed well
			const Chunk grown{ 200, 100, 0 };       // LZO made it bigger - still a stream
			const Chunk tiny{ 5, 1, 0 };            // a 1-byte file becomes 5 bytes
			const Chunk verbatim{ 100, 100, 0 };    // stored as-is

			return shrunk.is_compressed()
			    && grown.is_compressed()
			    && tiny.is_compressed()
			    && !verbatim.is_compressed();
		} );
}

TestCasePtr test_format_entry_compressed_when_any_chunk_is_compressed()
{
	return std::make_shared<TestCaseFuncNoInp>(
		"format_entry_compressed_when_any_chunk_is_compressed", true, []() {
			const std::vector<Chunk> mixed{ Chunk{ 100, 100, 0 }, Chunk{ 50, 200, 100 } };
			const std::vector<Chunk> all_stored{ Chunk{ 100, 100, 0 }, Chunk{ 100, 100, 100 } };

			return make_chunked_entry( mixed, 300 ).payload_is_compressed()
			    && !make_chunked_entry( all_stored, 200 ).payload_is_compressed();
		} );
}

TestCasePtr test_format_total_compressed_size_sums_chunks()
{
	return std::make_shared<TestCaseFuncNoInp>(
		"format_total_compressed_size_sums_chunks", true, []() {
			const std::vector<Chunk> chunks{ Chunk{ 4339, 32768, 0 }, Chunk{ 3379, 29448, 4339 } };

			// this is the game.rfa entry the sweep classified as chunkCount == 2
			return make_chunked_entry( chunks, 62216 ).total_compressed_size() == 7718u;
		} );
}

TestCasePtr test_format_variant_names()
{
	return std::make_shared<TestCaseFuncNoInp>(
		"format_variant_names", true, []() {
			return std::string( to_string( PayloadVariant::Raw ) ) == "raw"
			    && std::string( to_string( PayloadVariant::Empty ) ) == "empty"
			    && std::string( to_string( PayloadVariant::Chunked ) ) == "chunked"
			    && std::string( to_string( PayloadVariant::Unknown ) ) == "unknown";
		} );
}
