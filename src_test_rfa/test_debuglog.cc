/**
 * Testcases for libcommon/DebugLog.h - the logging session and its argv parsing.
 *
 * The headline case is session_writes_a_timestamped_line: it exercises the whole chain,
 * CPPDEBUG -> frontend -> queue -> logger thread -> file, and checks that the line
 * carries both the timestamp and the source location. The rest pins the edges: how the
 * log file is requested, that a second run appends instead of truncating, and that a
 * message with no backend behind it is harmless.
 *
 * @author Copyright (c) 2026
 */

#include "test_debuglog.h"

#include "DebugLog.h"

#include <CpputilsDebug.h>
#include <format.h>

#include <cctype>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

/// Builds the argv a tool would see, then asks DebugLog to interpret it.
std::string parse_log_file( const std::vector<std::string> & args )
{
	std::vector<char *> argv;

	for( const std::string & arg : args ) {
		argv.push_back( const_cast<char *>( arg.c_str() ) );
	}

	return ToolLog::log_file_from_argv( static_cast<int>( argv.size() ), argv.data() );
}

bool parse_debug_flag( const std::vector<std::string> & args )
{
	std::vector<char *> argv;

	for( const std::string & arg : args ) {
		argv.push_back( const_cast<char *>( arg.c_str() ) );
	}

	return ToolLog::debug_flag_from_argv( static_cast<int>( argv.size() ), argv.data() );
}

std::string read_file( const std::string & path )
{
	std::ifstream in( path.c_str(), std::ios::binary );

	if( !in ) {
		return std::string();
	}

	std::ostringstream content;
	content << in.rdbuf();
	return content.str();
}

bool is_digit( char c )
{
	return std::isdigit( static_cast<unsigned char>( c ) ) != 0;
}

/**
 * Checks the documented line shape "[YYYY-MM-DD HH:MM:SS[.mmm]] ".
 *
 * Positional on purpose: searching for a "-" and a ":" would pass on any garbage that
 * happens to contain them, and the whole point of the timestamp is that it is well formed.
 * The fractional part is optional because %S prints it exactly when the time point is
 * finer than seconds - the time point here is cast to milliseconds, so it is present.
 */
bool starts_with_timestamp( const std::string & line )
{
	static const std::size_t digit_positions[] = { 1, 2, 3, 4, 6, 7, 9, 10, 12, 13, 15, 16, 18, 19 };

	if( line.size() < 22 || line[0] != '[' ) {
		return false;
	}

	if( line[5] != '-' || line[8] != '-' || line[11] != ' ' ||
	    line[14] != ':' || line[17] != ':' ) {
		return false;
	}

	for( std::size_t position : digit_positions ) {
		if( !is_digit( line[position] ) ) {
			return false;
		}
	}

	std::size_t position = 20;

	if( position < line.size() && line[position] == '.' ) {
		++position;
		const std::size_t fraction_start = position;

		while( position < line.size() && is_digit( line[position] ) ) {
			++position;
		}

		if( position == fraction_start ) {
			return false;
		}
	}

	if( position >= line.size() || line[position] != ']' ) {
		return false;
	}

	++position;

	return position < line.size() && line[position] == ' ';
}

} // namespace

// ---------------------------------------------------------------------------

TestCasePtr test_debuglog_argv_takes_the_separate_value()
{
	return std::make_shared<TestCaseFuncNoInp>(
		"debuglog_argv_takes_the_separate_value", true, []() {
			return parse_log_file( { "tool.exe", "--log-file", "out.log" } ) == "out.log";
		} );
}

TestCasePtr test_debuglog_argv_takes_the_equals_form()
{
	return std::make_shared<TestCaseFuncNoInp>(
		"debuglog_argv_takes_the_equals_form", true, []() {
			return parse_log_file( { "tool.exe", "--log-file=out.log" } ) == "out.log";
		} );
}

TestCasePtr test_debuglog_argv_absent_means_no_log_file()
{
	// The tools ignored argv entirely before, so an unrelated argument must not be
	// mistaken for a request to log.
	return std::make_shared<TestCaseFuncNoInp>(
		"debuglog_argv_absent_means_no_log_file", true, []() {
			return parse_log_file( { "tool.exe" } ).empty()
			    && parse_log_file( { "tool.exe", "--debug", "something" } ).empty()
			    && parse_log_file( { "tool.exe", "--logfile=out.log" } ).empty();
		} );
}

TestCasePtr test_debuglog_argv_without_a_value_is_an_error()
{
	// This testcase prints one "Exception from: ..." line to stderr: StderrException
	// reports on construction, before anyone gets to catch it.
	return std::make_shared<TestCaseFuncNoInp>(
		"debuglog_argv_without_a_value_is_an_error",
		false,
		[]() {
			parse_log_file( { "tool.exe", "--log-file" } );
			return false;
		},
		true );
}

TestCasePtr test_debuglog_debug_flag_is_opt_in()
{
	return std::make_shared<TestCaseFuncNoInp>(
		"debuglog_debug_flag_is_opt_in", true, []() {
			return parse_debug_flag( { "tool.exe" } ) == false
			    && parse_debug_flag( { "tool.exe", "--debug" } ) == true;
		} );
}

TestCasePtr test_debuglog_strip_options_leaves_the_tool_arguments()
{
	// rfaUnpack takes positional arguments, so a --log-file path that reached its own
	// parser would be read as the archive.
	return std::make_shared<TestCaseFuncNoInp>(
		"debuglog_strip_options_leaves_the_tool_arguments", true, []() {
			std::vector<char *> argv = {
				const_cast<char *>( "tool.exe" ),
				const_cast<char *>( "--log-file" ),
				const_cast<char *>( "out.log" ),
				const_cast<char *>( "--debug" ),
				const_cast<char *>( "archive.rfa" ),
				const_cast<char *>( "-i0" )
			};

			const std::vector<std::string> stripped =
				ToolLog::strip_options( static_cast<int>( argv.size() ), argv.data() );

			if( stripped.size() != 3 || stripped[0] != "tool.exe" ||
			    stripped[1] != "archive.rfa" || stripped[2] != "-i0" ) {
				return false;
			}

			std::vector<char *> equals = {
				const_cast<char *>( "tool.exe" ),
				const_cast<char *>( "--log-file=out.log" ),
				const_cast<char *>( "archive.rfa" )
			};

			const std::vector<std::string> stripped_equals =
				ToolLog::strip_options( static_cast<int>( equals.size() ), equals.data() );

			return stripped_equals.size() == 2 && stripped_equals[1] == "archive.rfa";
		} );
}

TestCasePtr test_debuglog_session_writes_a_timestamped_line()
{
	// The session is scoped so its destructor runs before the file is read: that is the
	// contract callers rely on, a final drain with nothing left in the queue.
	return std::make_shared<TestCaseFuncOneFile>(
		"debuglog_session_writes_a_timestamped_line",
		[]( const std::string & log_file ) {
			{
				ToolLog::Session session( log_file );
				CPPDEBUG( Tools::format( "hello from %d", 42 ) );
			}

			const std::string content = read_file( log_file );

			return starts_with_timestamp( content )
			    && content.find( "hello from 42" ) != std::string::npos
			    && content.find( "test_debuglog.cc:" ) != std::string::npos;
		},
		std::ios::out );
}

TestCasePtr test_debuglog_second_session_appends()
{
	return std::make_shared<TestCaseFuncOneFile>(
		"debuglog_second_session_appends",
		[]( const std::string & log_file ) {
			{
				ToolLog::Session session( log_file );
				CPPDEBUG( "written by the first run" );
			}

			{
				ToolLog::Session session( log_file );
				CPPDEBUG( "written by the second run" );
			}

			const std::string content = read_file( log_file );

			return content.find( "written by the first run" ) != std::string::npos
			    && content.find( "written by the second run" ) != std::string::npos;
		},
		std::ios::out );
}

TestCasePtr test_debuglog_session_without_a_backend_writes_nothing()
{
	// No log file and no --debug: the frontend has no subscriber at all. Messages must
	// disappear without a crash and without anyone creating a file behind the user's back.
	return std::make_shared<TestCaseFuncOneFile>(
		"debuglog_session_without_a_backend_writes_nothing",
		[]( const std::string & log_file ) {
			{
				ToolLog::Session session;
				CPPDEBUG( "nobody is subscribed" );
			}

			std::error_code ec;
			return !std::filesystem::exists( log_file, ec );
		},
		std::ios::out );
}

TestCasePtr test_debuglog_message_without_a_session_is_harmless()
{
	// Tools::x_debug is NULL before the first session and again after the last one; the
	// CPPDEBUG macro null-checks it, so this must simply do nothing.
	return std::make_shared<TestCaseFuncNoInp>(
		"debuglog_message_without_a_session_is_harmless", true, []() {
			CPPDEBUG( "no session installed" );
			return true;
		} );
}
