/**
 * rfaPack's command line.
 *
 * Same shape as UnpackCli.h and for the same reason: a library, so the compatibility tests
 * can call the entry point the program calls instead of spawning a process (spawning is
 * ruled out by PLAN_rfa_tools.md finding 19).
 *
 * @author Copyright (c) 2026
 */

#pragma once

#include <iosfwd>
#include <iostream>
#include <string>
#include <vector>

namespace cli {

/**
 * Runs rfaPack.
 *
 * Two streams, because the golden records stdout and stderr apart: everything the original
 * prints goes to @p out, and only the deliberate divergences go to @p err - today just the
 * warning that a `-Compress` archive is not readable by RFA Pack 1.7 (finding 27). Keeping
 * the second stream a parameter rather than reaching for `std::cerr` inside is what lets a
 * testcase assert on it.
 *
 * @param args  argv as the program received it, args[0] included
 * @param out   where the chatter goes; the original's stdout
 * @param err   where the warnings go; the original's stderr, and empty in every golden
 * @return      0 on success, 1 for a usage, argument or I/O failure
 */
int run_pack( const std::vector<std::string> & args, std::ostream & out,
              std::ostream & err = std::cerr );

} // namespace cli


