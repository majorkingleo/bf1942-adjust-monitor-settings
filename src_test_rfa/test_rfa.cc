/**
 * Test runner for the .rfa tools.
 *
 * One program aggregating every testcase subject, following
 * majorkingleo/cpputilstest's per-component runner layout
 * (e.g. src_test_cpputilsshared/test_cpputilsshared.cc).
 *
 * Phase 1 registers the harness self-tests and the vendored miniLZO codec tests. The
 * remaining rfa subjects land in Phase 2/3 as they gain real cases:
 *   test_rfa_format    container parse/serialize, both payload variants and the chunk table
 *   test_rfa_archive   reader/writer, -u update, determinism across thread counts
 *   test_rfa_cli       CLI compatibility versus bin/*.orig.exe and tests/golden
 *
 * @author Copyright (c) 2026
 */

#include "test_lzo.h"
#include "test_testcommon.h"

#include "TestRunner.h"

#include <memory>

int main( int argc, char ** argv )
{
	TestCases test_cases;

	// --- testcommon -------------------------------------------------------
	test_cases.push_back( test_colbuilder_empty_renders_nothing() );
	test_cases.push_back( test_colbuilder_single_column() );
	test_cases.push_back( test_colbuilder_two_columns() );
	test_cases.push_back( test_colbuilder_width_follows_widest_cell() );
	test_cases.push_back( test_colbuilder_escape_sequences_do_not_inflate_width() );
	test_cases.push_back( test_colbuilder_unknown_column_name_is_ignored() );
	test_cases.push_back( test_colbuilder_col_lookup() );

	test_cases.push_back( test_testcase_bool_expectation_kept_separate_from_result() );
	test_cases.push_back( test_testcase_equal_predicate() );
	test_cases.push_back( test_testcase_throws_exception_flag() );
	test_cases.push_back( test_testcase_onefile_gets_name_from_testcase() );
	test_cases.push_back( test_testcase_onefile_removes_stale_file() );

	// --- lzo (vendored miniLZO) -------------------------------------------
	test_cases.push_back( test_lzo_init() );
	test_cases.push_back( test_lzo_roundtrip_empty() );
	test_cases.push_back( test_lzo_roundtrip_single_byte() );
	test_cases.push_back( test_lzo_roundtrip_random_incompressible() );
	test_cases.push_back( test_lzo_roundtrip_highly_compressible() );
	test_cases.push_back( test_lzo_roundtrip_at_chunk_boundaries() );
	test_cases.push_back( test_lzo_compressible_input_actually_shrinks() );
	test_cases.push_back( test_lzo_single_run_encoding_matches_format_doc() );
	test_cases.push_back( test_lzo_decompressing_truncated_data_fails_cleanly() );

	return run_testcases( argc, argv, "rfa", test_cases );
}
