/**
 * Testcases for cli/UnpackCli.h - the rfaUnpack command line.
 *
 * @author Copyright (c) 2026
 */

#ifndef SRC_TEST_RFA_TEST_RFA_CLI_H_
#define SRC_TEST_RFA_TEST_RFA_CLI_H_

#include "TestUtils.h"

TestCasePtr test_cli_usage_without_arguments();
TestCasePtr test_cli_missing_target_directory_stops_before_loading();
TestCasePtr test_cli_full_extract_writes_every_entry();
TestCasePtr test_cli_index_selects_the_entry_at_that_position();
TestCasePtr test_cli_index_out_of_range_is_reported_but_not_fatal();
TestCasePtr test_cli_list_extracts_full_internal_paths();
TestCasePtr test_cli_list_rejects_a_basename_like_the_original();
TestCasePtr test_cli_f_accepts_a_basename_unlike_the_original();
TestCasePtr test_cli_full_extract_of_the_fh_archive_matches_the_reader();

#endif /* SRC_TEST_RFA_TEST_RFA_CLI_H_ */
