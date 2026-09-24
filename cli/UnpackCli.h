/**
 * rfaUnpack's command line.
 *
 * Lives in a library rather than in the program so the compatibility tests can drive it
 * directly. Spawning the binary from a test was already ruled out in Phase 2
 * (PLAN_rfa_tools.md finding 19): std::system goes through cmd.exe, which reads `/` as a
 * switch and resolves relative paths against its own working directory. Calling the same
 * function the program calls is both hermetic and faster.
 *
 * Everything goes to one stream. The original writes nothing to stderr - every scenario
 * captured in tests/golden/oracle-cli.json has an empty stderr - so there is no second
 * stream to model.
 *
 * @author Copyright (c) 2026
 */

#ifndef CLI_UNPACKCLI_H
#define CLI_UNPACKCLI_H

#include <iosfwd>
#include <string>
#include <vector>

namespace cli {

/**
 * Runs rfaUnpack.
 *
 * @param args  argv as the program received it, args[0] included
 * @param out   where the chatter goes; the original's stdout
 * @return      0 on success, 1 for a usage, argument or I/O failure. Selection misses
 *              (`-i` out of range, a name that cannot be resolved) exit 0, because the
 *              golden file pins the original's exit code to 0 for exactly those cases.
 */
int run_unpack( const std::vector<std::string> & args, std::ostream & out );

} // namespace cli

#endif /* CLI_UNPACKCLI_H */
