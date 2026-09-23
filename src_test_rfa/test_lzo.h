/**
 * Testcases for the vendored miniLZO codec.
 *
 * Phase 2 will wrap this in rfa::LzoCodec; these tests pin the codec itself, including
 * the encoding facts the RFA format depends on, so a mis-vendored or wrong-version LZO
 * shows up here rather than as corrupt archives later.
 *
 * @author Copyright (c) 2026
 */

#ifndef SRC_TEST_RFA_TEST_LZO_H_
#define SRC_TEST_RFA_TEST_LZO_H_

#include "TestUtils.h"

TestCasePtr test_lzo_init();
TestCasePtr test_lzo_roundtrip_empty();
TestCasePtr test_lzo_roundtrip_single_byte();
TestCasePtr test_lzo_roundtrip_random_incompressible();
TestCasePtr test_lzo_roundtrip_highly_compressible();
TestCasePtr test_lzo_roundtrip_at_chunk_boundaries();
TestCasePtr test_lzo_compressible_input_actually_shrinks();
TestCasePtr test_lzo_single_run_encoding_matches_format_doc();
TestCasePtr test_lzo_decompressing_truncated_data_fails_cleanly();

// rfa::LzoCodec - the wrapper the library actually uses
TestCasePtr test_lzo_codec_roundtrip_via_wrapper();
TestCasePtr test_lzo_codec_detects_size_mismatch();
TestCasePtr test_lzo_codec_max_compressed_size_is_sufficient();
TestCasePtr test_lzo_codec_available();

#endif /* SRC_TEST_RFA_TEST_LZO_H_ */
