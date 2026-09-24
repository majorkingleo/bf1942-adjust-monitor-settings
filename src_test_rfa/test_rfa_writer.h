/**
 * Testcases for rfa/RfaWriter.h.
 *
 * The headline case is writer_matches_the_oracle_byte_for_byte: it builds the same tree
 * with our writer and with bin/rfaPack.orig.exe and compares the two archives byte for
 * byte. Everything else here is scaffolding around that claim.
 *
 * @author Copyright (c) 2026
 */

#pragma once

#include "TestUtils.h"

TestCasePtr test_writer_collect_files_names_them_base_slash_relative();
TestCasePtr test_writer_store_policy_writes_version_0_raw_entries();
TestCasePtr test_writer_compress_policy_writes_version_1_chunked_entries();
TestCasePtr test_writer_round_trips_a_tree_through_our_reader();
TestCasePtr test_writer_multi_chunk_entry_round_trips();
TestCasePtr test_writer_empty_file_round_trips_in_both_policies();
TestCasePtr test_writer_orders_entries_like_the_oracle();
TestCasePtr test_writer_orders_names_the_way_the_original_folds_them();
TestCasePtr test_writer_is_deterministic_across_thread_counts();
TestCasePtr test_writer_thread_budget_follows_chunk_count_not_file_count();
TestCasePtr test_writer_missing_source_leaves_no_archive();
TestCasePtr test_writer_matches_the_golden_store_archive_byte_for_byte();
TestCasePtr test_writer_compress_archive_is_interchangeable_with_the_oracle();


