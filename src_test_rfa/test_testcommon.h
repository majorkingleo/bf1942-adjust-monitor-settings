/**
 * Testcases for testcommon/ itself (TestUtils.h, ColBuilder.h).
 *
 * These exist so `make check` proves the harness works before any product code depends
 * on it: a broken harness silently reporting success would be worse than no harness.
 *
 * @author Copyright (c) 2026
 */

#pragma once

#include "TestUtils.h"

// ColBuilder
TestCasePtr test_colbuilder_empty_renders_nothing();
TestCasePtr test_colbuilder_single_column();
TestCasePtr test_colbuilder_two_columns();
TestCasePtr test_colbuilder_width_follows_widest_cell();
TestCasePtr test_colbuilder_escape_sequences_do_not_inflate_width();
TestCasePtr test_colbuilder_unknown_column_name_is_ignored();
TestCasePtr test_colbuilder_col_lookup();

// TestCaseBase and friends
TestCasePtr test_testcase_bool_expectation_kept_separate_from_result();
TestCasePtr test_testcase_equal_predicate();
TestCasePtr test_testcase_throws_exception_flag();
TestCasePtr test_testcase_onefile_gets_name_from_testcase();
TestCasePtr test_testcase_onefile_removes_stale_file();


