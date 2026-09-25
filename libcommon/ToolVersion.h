/**
 * Where a tool came from: the `-v` / `--version` switch.
 *
 * Answers the question a bug report starts with - which build is this? - with the commit the
 * binary was compiled from, the repository it lives in and the author. All three are compiled in,
 * so it works from a copy of the .exe with no source tree anywhere near it, which is exactly the
 * situation in the repository that consumes these programs.
 *
 * The commit comes from a generated header (tools/make_version_stamp.sh, wired up by the
 * version_stamp.h rule in Makefile.am), so it is refreshed by make, not by configure.
 *
 * @author Copyright (c) 2026
 */

#pragma once

#include <iosfwd>
#include <string>
#include <vector>

namespace ToolVersion {

/**
 * Where this project lives, and who wrote it.
 *
 * Constants rather than something git is asked for at runtime: a source tree without a remote has
 * to give the same answer, and the answer must not change with the checkout.
 */
extern const char * const URL;
extern const char * const AUTHOR;

/**
 * True for `-v`, `-V`, `--version` and `--VERSION`.
 *
 * The long form is matched case-insensitively, like the other switches of these tools. Anything
 * else is not a request - in particular `-version` (one dash) and `-vv`, which are the spellings a
 * typo produces. Note that `-v` was NOT a switch before: rfaUnpack ignored unknown switches and
 * rfaPack read them as positional arguments, so a caller that used to pass it as a no-op now gets
 * the version block and no work done.
 */
bool is_version_switch( const std::string & token );

/// True when any argument of `args` requests the version.
bool requested( const std::vector<std::string> & args );

/// The block a version request prints, for `program`: name, version, commit, repo, author.
std::string text( const char * program );

/// Prints text( program ) to `out`.
void print( std::ostream & out, const char * program );

} // namespace ToolVersion
