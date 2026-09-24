/**
 * rfaPack's command line.
 *
 * Same shape as UnpackCli.h and for the same reason: a library, so the compatibility tests
 * can call the entry point the program calls instead of spawning a process (spawning is
 * ruled out by PLAN_rfa_tools.md finding 19).
 *
 * @author Copyright (c) 2026
 */

#ifndef CLI_PACKCLI_H
#define CLI_PACKCLI_H

#include <iosfwd>
#include <string>
#include <vector>

namespace cli {

/**
 * Runs rfaPack.
 *
 * @param args  argv as the program received it, args[0] included
 * @param out   where the chatter goes; the original's stdout
 * @return      0 on success, 1 for a usage, argument or I/O failure
 */
int run_pack( const std::vector<std::string> & args, std::ostream & out );

} // namespace cli

#endif /* CLI_PACKCLI_H */
