/**
 * Testcases for rfa/RfaFormat.h: the container model and its arithmetic.
 *
 * These are pure model tests - no files, no I/O - so a mistake in the chunk arithmetic
 * is caught here rather than as a corrupt archive later.
 *
 * @author Copyright (c) 2026
 */

#pragma once

#include "TestUtils.h"

TestCasePtr test_format_constants_match_the_documented_layout();
TestCasePtr test_format_chunk_count_for_uncompressed_size();
TestCasePtr test_format_chunk_count_matches_the_real_fixture_entry();
TestCasePtr test_format_header_size_grows_by_twelve_per_chunk();
TestCasePtr test_format_header_size_is_zero_for_raw_and_empty();
TestCasePtr test_format_chunk_is_compressed_rule();
TestCasePtr test_format_entry_compressed_when_any_chunk_is_compressed();
TestCasePtr test_format_total_compressed_size_sums_chunks();
TestCasePtr test_format_variant_names();


