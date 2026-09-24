/**
 * Testcases for libcommon/DebugLog.h.
 *
 * @author Copyright (c) 2026
 */

#ifndef SRC_TEST_RFA_TEST_DEBUGLOG_H_
#define SRC_TEST_RFA_TEST_DEBUGLOG_H_

#include "TestUtils.h"

TestCasePtr test_debuglog_argv_takes_the_separate_value();
TestCasePtr test_debuglog_argv_takes_the_equals_form();
TestCasePtr test_debuglog_argv_absent_means_no_log_file();
TestCasePtr test_debuglog_argv_without_a_value_is_an_error();
TestCasePtr test_debuglog_unopenable_log_file_throws();
TestCasePtr test_debuglog_debug_flag_is_opt_in();
TestCasePtr test_debuglog_strip_options_leaves_the_tool_arguments();
TestCasePtr test_debuglog_session_writes_a_timestamped_line();
TestCasePtr test_debuglog_second_session_appends();
TestCasePtr test_debuglog_session_without_a_backend_writes_nothing();
TestCasePtr test_debuglog_message_without_a_session_is_harmless();

#endif /* SRC_TEST_RFA_TEST_DEBUGLOG_H_ */
