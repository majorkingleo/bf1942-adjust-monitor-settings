/**
 * rfaPack - build a Battlefield 1942 .rfa archive from a directory tree.
 *
 * A thin wrapper. The command line itself is in cli/PackCli.cc so that the compatibility
 * tests can call it directly instead of spawning this binary.
 *
 * @author Copyright (c) 2026
 */

#include "PackCli.h"

#include "DebugLog.h"

#include <exception>
#include <iostream>
#include <string>
#include <vector>

int main( int argc, char ** argv )
{
	try {
		// Install the debug frontend first, so the CPPDEBUG calls inside the CLI and the
		// writer have somewhere to go. ToolLog strips its own switches from the argument
		// list, which matters here: --log-file <path> would otherwise look positional.
		const ToolLog::Session log( ToolLog::log_file_from_argv( argc, argv ),
		                            ToolLog::debug_flag_from_argv( argc, argv ) );

		const std::vector<std::string> args = ToolLog::strip_options( argc, argv );

		return cli::run_pack( args, std::cout );

	} catch( const std::exception & error ) {
		// A log file that cannot be opened, or anything unexpected from the CLI. Report it
		// and fail: letting it escape would reach std::terminate with no message at all.
		std::cerr << error.what() << std::endl;
		return 1;
	}
}
