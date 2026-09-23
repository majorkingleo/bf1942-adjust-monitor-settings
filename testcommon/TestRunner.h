/*
 * TestRunner.h
 *
 * Runs a list of testcases and prints a result table.
 *
 * Mirrors the reporting loop of majorkingleo/cpputilstest's per-component runner
 * (e.g. src_test_cpputilsshared/test_cpputilsshared.cc), but hoisted into the shared
 * harness so each test program does not carry its own copy.
 *
 * @author Copyright (c) 2026
 */

#ifndef TESTCOMMON_TESTRUNNER_H
#define TESTCOMMON_TESTRUNNER_H

#include "TestUtils.h"

#include <string>

/**
 * Run every testcase and report the outcome.
 *
 * Options:
 *   -h, --help               show the help page
 *   -d, --debug              print each testcase name while it runs
 *   -t, --testcase <idx..>   run only the given testcase numbers (1-based)
 *
 * @param component   label used in the banner, e.g. "rfa"
 * @param test_cases  the testcases to run
 * @return 0 when every testcase produced its expected result, 1 otherwise
 */
int run_testcases( int argc,
                   char ** argv,
                   const std::string & component,
                   const TestCases & test_cases );

#endif /* TESTCOMMON_TESTRUNNER_H */
