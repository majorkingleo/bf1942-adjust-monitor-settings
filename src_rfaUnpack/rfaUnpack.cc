/**
 * rfaUnpack - extract files from a Battlefield 1942 .rfa archive.
 *
 * A thin wrapper. The command line itself is in cli/UnpackCli.cc so that the
 * compatibility tests can call it directly instead of spawning this binary.
 *
 * @author Copyright (c) 2026
 */

#include "UnpackCli.h"

#include "DebugLog.h"

#include <exception>
#include <iostream>
#include <string>
#include <vector>

int main( int argc, char ** argv )
{
	try {
		// Install the debug frontend first, so the CPPDEBUG calls inside the CLI have
		// somewhere to go. ToolLog strips its own switches from the argument list, which
		// matters here: --log-file <path> would otherwise look like the archive argument.
		const ToolLog::Session log( ToolLog::log_file_from_argv( argc, argv ),
		                            ToolLog::debug_flag_from_argv( argc, argv ) );

		const std::vector<std::string> args = ToolLog::strip_options( argc, argv );

		return cli::run_unpack( args, std::cout );

	} catch( const std::exception & error ) {
		// A log file that cannot be opened, or anything unexpected from the CLI. Report it
		// and fail: letting it escape would reach std::terminate with no message at all.
		std::cerr << error.what() << std::endl;
		return 1;
	}
}
