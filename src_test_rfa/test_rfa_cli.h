/**
 * Testcases for cli/UnpackCli.h and cli/PackCli.h - the two command lines.
 *
 * @author Copyright (c) 2026
 */

#pragma once

#include "TestUtils.h"

// rfaUnpack
TestCasePtr test_cli_usage_without_arguments();
TestCasePtr test_cli_missing_target_directory_stops_before_loading();
TestCasePtr test_cli_full_extract_writes_every_entry();
TestCasePtr test_cli_index_selects_the_entry_at_that_position();
TestCasePtr test_cli_index_out_of_range_is_reported_but_not_fatal();
TestCasePtr test_cli_list_extracts_full_internal_paths();
TestCasePtr test_cli_list_rejects_a_basename_like_the_original();
TestCasePtr test_cli_f_accepts_a_basename_unlike_the_original();
TestCasePtr test_cli_full_extract_of_the_fh_archive_matches_the_reader();

// rfaPack
TestCasePtr test_cli_pack_no_arguments_matches_the_golden();
TestCasePtr test_cli_pack_plain_matches_the_golden();
TestCasePtr test_cli_pack_compress_matches_the_golden();
TestCasePtr test_cli_pack_lzo_fast_warns_and_produces_a_larger_archive();
TestCasePtr test_cli_pack_switch_matching_is_case_insensitive();
TestCasePtr test_cli_pack_update_refuses_and_writes_nothing();
TestCasePtr test_cli_pack_missing_source_directory_is_reported();
TestCasePtr test_cli_pack_store_output_is_byte_identical_to_the_oracle_archive();


