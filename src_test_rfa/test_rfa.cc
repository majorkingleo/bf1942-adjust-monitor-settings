/**
 * Test runner for the .rfa tools.
 *
 * One program aggregating every testcase subject, following
 * majorkingleo/cpputilstest's per-component runner layout
 * (e.g. src_test_cpputilsshared/test_cpputilsshared.cc).
 *
 * Phase 1 registers the harness self-tests and the vendored miniLZO codec tests;
 * Phase 2 adds the container model and the reader driven by the real fixtures. Still to
 * come:
 *   test_rfa_archive   writer, determinism across thread counts
 *   test_rfa_cli       CLI compatibility versus bin/*.orig.exe and tests/golden
 *
 * @author Copyright (c) 2026
 */

#include "test_debuglog.h"
#include "test_lzo.h"
#include "test_rfa_archive.h"
#include "test_rfa_cli.h"
#include "test_rfa_format.h"
#include "test_rfa_writer.h"
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

	// --- debug logging (libcommon) ----------------------------------------
	test_cases.push_back( test_debuglog_argv_takes_the_separate_value() );
	test_cases.push_back( test_debuglog_argv_takes_the_equals_form() );
	test_cases.push_back( test_debuglog_argv_absent_means_no_log_file() );
	test_cases.push_back( test_debuglog_argv_without_a_value_is_an_error() );
	test_cases.push_back( test_debuglog_debug_flag_is_opt_in() );
	test_cases.push_back( test_debuglog_strip_options_leaves_the_tool_arguments() );
	test_cases.push_back( test_debuglog_session_writes_a_timestamped_line() );
	test_cases.push_back( test_debuglog_second_session_appends() );
	test_cases.push_back( test_debuglog_session_without_a_backend_writes_nothing() );
	test_cases.push_back( test_debuglog_message_without_a_session_is_harmless() );

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
	test_cases.push_back( test_lzo_codec_available() );
	test_cases.push_back( test_lzo_codec_roundtrip_via_wrapper() );
	test_cases.push_back( test_lzo_codec_detects_size_mismatch() );
	test_cases.push_back( test_lzo_codec_max_compressed_size_is_sufficient() );

	// --- container model --------------------------------------------------
	test_cases.push_back( test_format_constants_match_the_documented_layout() );
	test_cases.push_back( test_format_chunk_count_for_uncompressed_size() );
	test_cases.push_back( test_format_chunk_count_matches_the_real_fixture_entry() );
	test_cases.push_back( test_format_header_size_grows_by_twelve_per_chunk() );
	test_cases.push_back( test_format_header_size_is_zero_for_raw_and_empty() );
	test_cases.push_back( test_format_chunk_is_compressed_rule() );
	test_cases.push_back( test_format_entry_compressed_when_any_chunk_is_compressed() );
	test_cases.push_back( test_format_total_compressed_size_sums_chunks() );
	test_cases.push_back( test_format_variant_names() );

	// --- reader against the real fixtures ---------------------------------
	test_cases.push_back( test_archive_all_fixtures_open_without_problems() );
	test_cases.push_back( test_archive_entry_counts_match_fixtures() );
	test_cases.push_back( test_archive_variants_match_fixtures() );
	test_cases.push_back( test_archive_every_entry_has_consistent_sizes() );
	test_cases.push_back( test_archive_multi_chunk_entry_matches_its_table() );
	test_cases.push_back( test_archive_reader_expands_every_entry_to_declared_size() );
	test_cases.push_back( test_archive_conquest_con_matches_phase0_ground_truth() );
	test_cases.push_back( test_archive_raw_entry_is_read_verbatim() );
	test_cases.push_back( test_archive_find_returns_known_paths() );
	test_cases.push_back( test_archive_corrupt_payload_never_returns_pristine_bytes() );
	test_cases.push_back( test_archive_detects_truncated_file() );
	test_cases.push_back( test_archive_detects_garbage() );
	test_cases.push_back( test_archive_detects_empty_file() );
	test_cases.push_back( test_archive_unreadable_path_fails_cleanly() );

	// --- writer -----------------------------------------------------------
	test_cases.push_back( test_writer_collect_files_names_them_base_slash_relative() );
	test_cases.push_back( test_writer_store_policy_writes_version_0_raw_entries() );
	test_cases.push_back( test_writer_compress_policy_writes_version_1_chunked_entries() );
	test_cases.push_back( test_writer_round_trips_a_tree_through_our_reader() );
	test_cases.push_back( test_writer_multi_chunk_entry_round_trips() );
	test_cases.push_back( test_writer_empty_file_round_trips_in_both_policies() );
	test_cases.push_back( test_writer_orders_entries_like_the_oracle() );
	test_cases.push_back( test_writer_is_deterministic_across_thread_counts() );
	test_cases.push_back( test_writer_missing_source_leaves_no_archive() );
	test_cases.push_back( test_writer_matches_the_golden_store_archive_byte_for_byte() );
	test_cases.push_back( test_writer_compress_archive_is_interchangeable_with_the_oracle() );

	// --- CLI: rfaUnpack ---------------------------------------------------
	test_cases.push_back( test_cli_usage_without_arguments() );
	test_cases.push_back( test_cli_missing_target_directory_stops_before_loading() );
	test_cases.push_back( test_cli_full_extract_writes_every_entry() );
	test_cases.push_back( test_cli_index_selects_the_entry_at_that_position() );
	test_cases.push_back( test_cli_index_out_of_range_is_reported_but_not_fatal() );
	test_cases.push_back( test_cli_list_extracts_full_internal_paths() );
	test_cases.push_back( test_cli_list_rejects_a_basename_like_the_original() );
	test_cases.push_back( test_cli_f_accepts_a_basename_unlike_the_original() );
	test_cases.push_back( test_cli_full_extract_of_the_fh_archive_matches_the_reader() );

	return run_testcases( argc, argv, "rfa", test_cases );
}
