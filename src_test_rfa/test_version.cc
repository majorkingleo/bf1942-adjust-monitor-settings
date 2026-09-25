/**
 * Testcases for libcommon/ToolVersion.h.
 *
 * What is and is not proven here:
 *
 *  - the header's own promises are pinned: which spellings count as a request, that a typo does
 *    not, and what the printed block contains.
 *  - the generated stamp is checked for SHAPE (seven hex digits, an ISO date) and for being the
 *    one that gets printed.
 *
 *  - what is NOT proven is that the stamp names the commit the test runs in. A testcase may not
 *    run a subprocess here - `std::system` goes through cmd.exe, which mangles both forward-slash
 *    and absolute paths (AGENTS.md) - so there is no `git rev-parse` to compare against. The
 *    build is what guarantees it: the version_stamp.h rule regenerates the header on every make,
 *    and `rfaPack --version` in the build directory is the check a human runs.
 *
 * @author Copyright (c) 2026
 */

#include "test_version.h"

#include "ToolVersion.h"

// The generated header, straight from the include path: this is the only testcase that reads what
// make produced rather than what the library decided.
#include "version_stamp.h"

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

using namespace ToolVersion;

namespace {

bool contains( const std::string & haystack, const std::string & needle )
{
	return haystack.find( needle ) != std::string::npos;
}

bool all_hex( const std::string & text )
{
	for( const char c : text ) {
		const bool digit = ( c >= '0' && c <= '9' );
		const bool hex   = ( c >= 'a' && c <= 'f' ) || ( c >= 'A' && c <= 'F' );

		if( !digit && !hex ) {
			return false;
		}
	}

	return true;
}

bool all_digits( const std::string & text )
{
	for( const char c : text ) {
		if( c < '0' || c > '9' ) {
			return false;
		}
	}

	return true;
}

} // namespace

TestCasePtr test_version_recognises_only_the_version_spellings()
{
	return std::make_shared<TestCaseFuncNoInp>(
		"version_recognises_only_the_version_spellings", true, []() {
			const std::vector<std::string> requests = {
				"-v", "-V", "--version", "--VERSION", "--Version",
			};

			for( const std::string & token : requests ) {
				if( !is_version_switch( token ) ) {
					std::cout << "[rfa] '" << token << "' should be a version request\n";
					return false;
				}
			}

			// Typos, and the switches these tools already own: none of them may be mistaken for
			// a request, because the caller would get a version block instead of its work.
			const std::vector<std::string> others = {
				"-version", "-vv", "-Vv", "v", "version", "--versio", "--version-x", "--version=",
				"-i0", "-i", "-f", "-l", "-u", "-Compress", "--lzo-fast", "--threads", "-j", "0",
			};

			for( const std::string & token : others ) {
				if( is_version_switch( token ) ) {
					std::cout << "[rfa] '" << token << "' was taken for a version request\n";
					return false;
				}
			}

			// And the argument-list form has to find it wherever it sits.
			if( !requested( { "rfaPack.exe", "menu", "-v" } )
			    || !requested( { "--version" } )
			    || requested( { "rfaPack.exe", "-Compress" } )
			    || requested( {} ) ) {
				std::cout << "[rfa] requested() does not agree with is_version_switch()\n";
				return false;
			}

			return true;
		} );
}

TestCasePtr test_version_text_names_the_program_the_commit_and_the_origin()
{
	return std::make_shared<TestCaseFuncNoInp>(
		"version_text_names_the_program_the_commit_and_the_origin", true, []() {
			const std::string text = ToolVersion::text( "rfaPack" );

			// First line: the program that was asked, then the package version. The name comes
			// from the caller, so two tools of the same build do not report the same name.
			const std::string first = text.substr( 0, text.find( '\n' ) );

			if( first != std::string( "rfaPack " ) + VERSION ) {
				std::cout << "[rfa] unexpected first line: '" << first << "'\n";
				return false;
			}

			if( !contains( text, URL ) || !contains( text, AUTHOR ) || !contains( text, "commit" ) ) {
				std::cout << "[rfa] the block is missing something:\n" << text;
				return false;
			}

			// Four lines: name/version, commit, repo, author.
			if( std::count( text.begin(), text.end(), '\n' ) != 4 ) {
				std::cout << "[rfa] expected four lines:\n" << text;
				return false;
			}

			if( ToolVersion::text( "rfaUnpack" ).compare( 0, 9, "rfaUnpack" ) != 0 ) {
				std::cout << "[rfa] the program name is not taken from the caller\n";
				return false;
			}

			return true;
		} );
}

TestCasePtr test_version_stamp_is_well_formed()
{
	return std::make_shared<TestCaseFuncNoInp>(
		"version_stamp_is_well_formed", true, []() {
			const std::string commit = TOOL_VERSION_COMMIT;
			const std::string date   = TOOL_VERSION_DATE;

			// "unknown" is the answer of a tree without .git, which is legitimate: a release
			// tarball has no commit to name. Anything else has to look like what git produces.
			if( commit != "unknown" ) {
				if( commit.size() < 7 || commit.size() > 40 || !all_hex( commit ) ) {
					std::cout << "[rfa] commit '" << commit << "' is not a git hash\n";
					return false;
				}
			}

			if( date != "unknown" ) {
				if( date.size() != 10 || date[4] != '-' || date[7] != '-'
				    || !all_digits( date.substr( 0, 4 ) )
				    || !all_digits( date.substr( 5, 2 ) )
				    || !all_digits( date.substr( 8, 2 ) ) ) {
					std::cout << "[rfa] date '" << date << "' is not YYYY-MM-DD\n";
					return false;
				}
			}

			// Whatever the stamp says, the printed block has to carry it - that is the whole
			// point of generating it.
			const std::string text = ToolVersion::text( "x" );

			if( !contains( text, commit ) || !contains( text, date ) ) {
				std::cout << "[rfa] the stamp is not what gets printed:\n" << text;
				return false;
			}

			return true;
		} );
}
