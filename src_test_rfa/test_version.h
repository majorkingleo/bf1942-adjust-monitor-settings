/**
 * Testcases for libcommon/ToolVersion.h - the -v / --version switch and the compiled-in stamp.
 *
 * @author Copyright (c) 2026
 */

#pragma once

#include "TestUtils.h"

TestCasePtr test_version_recognises_only_the_version_spellings();
TestCasePtr test_version_text_names_the_program_the_commit_and_the_origin();
TestCasePtr test_version_stamp_is_well_formed();
