/*
 * TestRunner.cc
 *
 * @author Copyright (c) 2026
 */

#include "TestRunner.h"

#include "ColBuilder.h"

#include <iostream>
#include <set>
#include <sstream>
#include <string>

namespace {

std::string help_text( const std::string & component, const char * argv0 )
{
	std::ostringstream out;
	out << "Test runner for " << component << "\n"
	    << "\n"
	    << "Usage: " << ( argv0 ? argv0 : component.c_str() ) << " [options]\n"
	    << "\n"
	    << "Options:\n"
	    << "  -h, --help               show this page\n"
	    << "  -d, --debug              print each testcase name while it runs\n"
	    << "  -t, --testcase <idx..>   run only these testcases (1-based numbers\n"
	    << "                           as listed in the Idx column below)\n"
	    << "\n"
	    << "Exit status is 0 when every testcase matched its expected result.\n";
	return out.str();
}

/**
 * Returns false when the command line could not be understood.
 */
bool parse_args( int argc,
                 char ** argv,
                 bool & help,
                 bool & debug,
                 std::set<int> & only,
                 const std::string & component )
{
	for( int i = 1; i < argc; ++i ) {
		const std::string token = argv[i];

		if( token == "-h" || token == "--help" ) {
			help = true;
			continue;
		}

		if( token == "-d" || token == "--debug" ) {
			debug = true;
			continue;
		}

		if( token == "-t" || token == "--testcase" ) {
			// consume every following token that looks like a number
			while( i + 1 < argc ) {
				const std::string next = argv[i + 1];
				try {
					const std::size_t consumed = 0;
					const int value = std::stoi( next, nullptr, 10 );
					(void)consumed;
					only.insert( value );
				} catch( ... ) {
					break;
				}
				++i;
			}
			continue;
		}

		// -t5 / --testcase=5 style
		if( token.rfind( "-t", 0 ) == 0 || token.rfind( "--testcase=", 0 ) == 0 ) {
			const std::string value = token.rfind( "--testcase=", 0 ) == 0
			                        ? token.substr( std::string( "--testcase=" ).size() )
			                        : token.substr( 2 );
			try {
				only.insert( std::stoi( value, nullptr, 10 ) );
			} catch( ... ) {
				std::cout << "invalid testcase number: " << value << "\n\n";
				std::cout << help_text( component, argv[0] ) << "\n";
				return false;
			}
			continue;
		}

		std::cout << "unknown option: " << token << "\n\n";
		std::cout << help_text( component, argv[0] ) << "\n";
		return false;
	}

	return true;
}

} // namespace

int run_testcases( int argc,
                   char ** argv,
                   const std::string & component,
                   const TestCases & test_cases )
{
	bool help = false;
	bool debug = false;
	std::set<int> only;

	if( !parse_args( argc, argv, help, debug, only, component ) ) {
		return 1;
	}

	if( help ) {
		std::cout << help_text( component, argv[0] ) << "\n";
		return 0;
	}

	ColBuilder col;
	const int COL_IDX      = col.addCol( "Idx" );
	const int COL_NAME     = col.addCol( "Test" );
	const int COL_RESULT   = col.addCol( "Result" );
	const int COL_EXPECTED = col.addCol( "Expected" );
	const int COL_TEST_RES = col.addCol( "Test Result" );

	int idx = 0;
	bool something_failed = false;
	int ran = 0;

	for( const auto & test : test_cases ) {
		++idx;

		if( !only.empty() && only.count( idx ) == 0 ) {
			continue;
		}

		++ran;

		if( debug ) {
			std::cout << "run test: " << test->getName() << std::endl;
		}

		std::string expected = "true";
		if( !test->getExpectedResult() ) {
			expected = "false";
		}
		if( test->throwsException() ) {
			expected = "exception";
		}

		std::string result;

		try {
			result = test->run() ? "true" : "false";
		} catch( ... ) {
			result = "exception";
		}

		const bool passed = result == expected;
		if( !passed ) {
			something_failed = true;
		}

		std::ostringstream idx_text;
		idx_text << idx;

		col.addColData( COL_IDX, idx_text.str() );
		col.addColData( COL_NAME, test->getName() );
		col.addColData( COL_RESULT, result );
		col.addColData( COL_EXPECTED, expected );
		col.addColData( COL_TEST_RES, passed ? "succeeded" : "failed" );
	}

	std::cout << "\n" << component << " testcases\n\n";
	std::cout << col.toString() << std::endl;

	if( ran == 0 ) {
		std::cout << "no testcase was selected\n";
		return 1;
	}

	if( something_failed ) {
		std::cout << "Complete result: FAILED!!" << std::endl;
		return 1;
	}

	std::cout << "Complete result: succeeded (" << ran << " testcases)" << std::endl;
	return 0;
}
