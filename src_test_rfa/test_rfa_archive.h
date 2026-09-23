/**
 * Testcases for rfa/RfaArchive.h, driven by the real fixtures in tests/data.
 *
 * @author Copyright (c) 2026
 */

#ifndef SRC_TEST_RFA_TEST_RFA_ARCHIVE_H_
#define SRC_TEST_RFA_TEST_RFA_ARCHIVE_H_

#include "TestUtils.h"

TestCasePtr test_archive_all_fixtures_open_without_problems();
TestCasePtr test_archive_entry_counts_match_fixtures();
TestCasePtr test_archive_variants_match_fixtures();
TestCasePtr test_archive_every_entry_has_consistent_sizes();
TestCasePtr test_archive_multi_chunk_entry_matches_its_table();
TestCasePtr test_archive_reader_expands_every_entry_to_declared_size();
TestCasePtr test_archive_raw_entry_is_read_verbatim();
TestCasePtr test_archive_find_returns_known_paths();
TestCasePtr test_archive_conquest_con_matches_phase0_ground_truth();
TestCasePtr test_archive_corrupt_payload_never_returns_pristine_bytes();
TestCasePtr test_archive_detects_truncated_file();
TestCasePtr test_archive_detects_garbage();
TestCasePtr test_archive_detects_empty_file();
TestCasePtr test_archive_unreadable_path_fails_cleanly();

#endif /* SRC_TEST_RFA_TEST_RFA_ARCHIVE_H_ */
