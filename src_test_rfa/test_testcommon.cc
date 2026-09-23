/**
 * Testcases for testcommon/ itself.
 *
 * @author Copyright (c) 2026
 */

#include "test_testcommon.h"

#include "ColBuilder.h"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

using namespace std::string_literals;

// ---------------------------------------------------------------------------
// ColBuilder
// ---------------------------------------------------------------------------

TestCasePtr test_colbuilder_empty_renders_nothing()
{
	return std::make_shared<TestCaseFuncNoInp>(
		"colbuilder_empty_renders_nothing", true, []() {
			ColBuilder col;
			return col.toString().empty();
		} );
}

TestCasePtr test_colbuilder_single_column()
{
	return std::make_shared<TestCaseFuncNoInp>(
		"colbuilder_single_column", true, []() {
			ColBuilder col;
			const int idx = col.addCol( "A" );
			col.addColData( idx, "1" );

			return col.toString() == "A\n-\n1\n"s;
		} );
}

TestCasePtr test_colbuilder_two_columns()
{
	return std::make_shared<TestCaseFuncNoInp>(
		"colbuilder_two_columns", true, []() {
			ColBuilder col;
			const int a = col.addCol( "A" );
			const int b = col.addCol( "B" );
			col.addColData( a, "1" );
			col.addColData( b, "2" );

			return col.toString() == "A | B\n-----\n1 | 2\n"s;
		} );
}

TestCasePtr test_colbuilder_width_follows_widest_cell()
{
	return std::make_shared<TestCaseFuncNoInp>(
		"colbuilder_width_follows_widest_cell", true, []() {
			ColBuilder col;
			const int idx = col.addCol( "Name" );
			col.addColData( idx, "x" );

			// column is as wide as its header, so the short cell is right-aligned
			return col.toString() == "Name\n----\n   x\n"s;
		} );
}

TestCasePtr test_colbuilder_escape_sequences_do_not_inflate_width()
{
	return std::make_shared<TestCaseFuncNoInp>(
		"colbuilder_escape_sequences_do_not_inflate_width", true, []() {
			ColBuilder col;
			const int idx = col.addCol( "A" );
			col.addColData( idx, "\x1b[31mred\x1b[0m" );

			// "red" is 3 visible characters, so the separator must be 3 dashes even
			// though the raw string is much longer
			return col.toString() == "  A\n---\n\x1b[31mred\x1b[0m\n"s;
		} );
}

TestCasePtr test_colbuilder_unknown_column_name_is_ignored()
{
	return std::make_shared<TestCaseFuncNoInp>(
		"colbuilder_unknown_column_name_is_ignored", true, []() {
			ColBuilder col;
			col.addCol( "A" );
			col.addColData( "no_such_column", "1" ); // must not throw or add a row

			return col.getMaxNumOfRows() == 0 && col.toString() == "A\n-\n"s;
		} );
}

TestCasePtr test_colbuilder_col_lookup()
{
	return std::make_shared<TestCaseFuncNoInp>(
		"colbuilder_col_lookup", true, []() {
			ColBuilder col;
			col.addCol( "A" );
			col.addCol( "B" );

			return col.haveCol( "B" ) && !col.haveCol( "C" )
			    && col.getColByName( "B" ) == 1
			    && col.getColByName( "C" ) == -1
			    && col.getNumOfCols() == 2;
		} );
}

// ---------------------------------------------------------------------------
// TestCaseBase and friends
// ---------------------------------------------------------------------------

TestCasePtr test_testcase_bool_expectation_kept_separate_from_result()
{
	// The framework must record the *expectation* without forcing the observation to
	// match it - that separation is what lets the runner report failures.
	return std::make_shared<TestCaseFuncNoInp>(
		"testcase_bool_expectation_kept_separate_from_result", true, []() {
			auto test = std::make_shared<TestCaseFuncBool<std::string>>(
				"case", "abc", false, []( const std::string & input ) { return input == "abc"; } );

			return test->getExpectedResult() == false && test->run() == true;
		} );
}

TestCasePtr test_testcase_equal_predicate()
{
	return std::make_shared<TestCaseFuncNoInp>(
		"testcase_equal_predicate", true, []() {
			auto test = std::make_shared<TestCaseFuncEqual<>>(
				"case", "abc", "abc", []( const std::string & a, const std::string & b ) { return a == b; } );

			return test->run() && test->getExpectedResult() && !test->throwsException();
		} );
}

TestCasePtr test_testcase_throws_exception_flag()
{
	return std::make_shared<TestCaseFuncNoInp>(
		"testcase_throws_exception_flag", true, []() {
			auto test = std::make_shared<TestCaseFuncNoInp>(
				"case",
				true,
				[]() -> bool { throw std::runtime_error( "boom" ); },
				true );

			if( !test->throwsException() ) {
				return false;
			}

			try {
				test->run();
			} catch( const std::runtime_error & ) {
				return true;
			}

			return false;
		} );
}

TestCasePtr test_testcase_onefile_gets_name_from_testcase()
{
	return std::make_shared<TestCaseFuncNoInp>(
		"testcase_onefile_gets_name_from_testcase", true, []() {
			std::string seen;

			auto test = std::make_shared<TestCaseFuncOneFile>(
				"onefile_gets_name_from_testcase",
				[&seen]( const std::string & file ) { seen = file; return true; },
				std::ios::out );

			test->run();

			return seen == ".onefile_gets_name_from_testcase.txt";
		} );
}

TestCasePtr test_testcase_onefile_removes_stale_file()
{
	// A previous run's leftovers must not influence the next run.
	return std::make_shared<TestCaseFuncNoInp>(
		"testcase_onefile_removes_stale_file", true, []() {
			const std::string file = ".onefile_removes_stale_file.txt";

			{
				std::ofstream out( file, std::ios::binary );
				out << "stale content that is much longer";
			}

			auto test = std::make_shared<TestCaseFuncOneFile>(
				"onefile_removes_stale_file",
				[]( const std::string & f ) {
					std::ofstream out( f, std::ios::binary );
					out << "hello";
					out.close();

					return std::filesystem::exists( f ) && std::filesystem::file_size( f ) == 5;
				},
				std::ios::out );

			return test->run();
		} );
}
