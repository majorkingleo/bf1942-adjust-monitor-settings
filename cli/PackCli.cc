/**
 * rfaPack's command line - a reimplementation of RFA Pack 1.7's packer.
 *
 * Contract: tests/golden/oracle-cli.json, captured from bin/rfaPack.orig.exe.
 *
 *   rfaPack.exe <sourceDir> <baseFolderName> <archive.rfa> [-u] [-Compress]
 *
 * The writer underneath is the one Phase 2 proved byte-identical to the oracle for store
 * mode, so what this file adds is the argument handling, the walk order and the policy
 * selection - plus the chatter, which is what the captured scenarios actually pin.
 *
 * `-u` is not implemented yet. It is a larger, separate piece of work: §2.5 of the plan
 * established that it is a fresh pack with the target archive's policy plus carried-over
 * entries, with three deliberate divergences (D6, D7, D8) that still need a decision. Until
 * then the switch is recognised, echoed, and refused rather than silently ignored - a user
 * who asked for an update must not get a plain pack instead.
 *
 * @author Copyright (c) 2026
 */

#include "PackCli.h"

#include "rfa/RfaWriter.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <ostream>
#include <string>
#include <vector>

namespace cli {

namespace {

/// Printed on every run, exactly once. The golden has it as the last line of the usage
/// block and as a line of its own for a successful pack.
const char * const BANNER = "-- RFA Pack 1.7 --";

/// Trailing space after "Usage examples:" is in the original.
const char * const USAGE_BLOCK =
	" Usage examples: \n"
	"   ProgramName [sourceDir] [PackDirName] [Archive.rfa] [ -u update existing .rfa | -Compress]\n"
	"   RfaPack.exe d:/menu menu menu.rfa\n"
	"   RfaPack.exe d:/menu menu menu.rfa -u\n"
	"   RfaPack.exe d:/menu menu menu.rfa -Compress\n"
	"   RfaPack.exe d:/menu menu menu.rfa -u -Compress";

const char * const UPDATE_SWITCH   = "-u";
const char * const COMPRESS_SWITCH = "-compress";

struct Options
{
	std::string source_dir;
	std::string base;
	std::string archive;

	bool update = false;
	bool compress = false;

	/// The three positional arguments are all required; the switches are not.
	bool complete = false;
};

std::string lower( const std::string & text )
{
	std::string lowered = text;

	for( char & c : lowered ) {
		if( c >= 'A' && c <= 'Z' ) {
			c = static_cast<char>( c - 'A' + 'a' );
		}
	}

	return lowered;
}

Options parse( const std::vector<std::string> & args )
{
	Options options;
	std::vector<std::string> positional;

	for( std::size_t i = 1; i < args.size(); ++i ) {

		const std::string lowered = lower( args[i] );

		// Switches are matched case-insensitively. The documented spellings are -u and
		// -Compress; accepting -compress as well costs nothing and cannot break a caller
		// that uses the documented form.
		if( lowered == UPDATE_SWITCH ) {
			options.update = true;
			continue;
		}

		if( lowered == COMPRESS_SWITCH ) {
			options.compress = true;
			continue;
		}

		positional.push_back( args[i] );
	}

	if( positional.size() > 0 ) {
		options.source_dir = positional[0];
	}

	if( positional.size() > 1 ) {
		options.base = positional[1];
	}

	if( positional.size() > 2 ) {
		options.archive = positional[2];
	}

	options.complete = positional.size() >= 3;

	return options;
}

/**
 * Where the original says it stages the archive.
 *
 * The golden only covers an absolute archive path, where this is that path's directory. A
 * bare file name has no directory to report, and "." is the reading that stays truthful
 * about where the archive lands. Unpinned by any captured scenario.
 */
std::string destination_directory( const std::string & archive )
{
	const std::filesystem::path parent = std::filesystem::path( archive ).parent_path();

	return parent.empty() ? std::string( "." ) : parent.string();
}

std::uint64_t total_bytes( const std::vector<rfa::SourceFile> & files )
{
	std::uint64_t total = 0;

	for( const rfa::SourceFile & file : files ) {
		total += file.size;
	}

	return total;
}

} // namespace

int run_pack( const std::vector<std::string> & args, std::ostream & out, std::ostream & err )
{
	const Options options = parse( args );

	if( !options.complete ) {
		out << "ERROR! Not enough command line arguments\n"
		    << USAGE_BLOCK << "\n"
		    << "\n"
		    << BANNER << "\n";
		return 1;
	}

	out << BANNER << "\n";

	if( options.compress ) {
		// Finding 27: the 2003-era decoder in RFA Pack 1.7 rejects the match opcodes miniLZO
		// emits, so a -Compress archive we write cannot be read by the shipped tools - and in
		// all likelihood not by the game either. The switch still works and the archive is
		// valid by our own reader, but producing something the target cannot read must not
		// happen silently. This goes to stderr because every captured golden has an empty
		// stderr and a packed stdout; stdout stays a faithful reproduction.
		err << "WARNING! -Compress output is not readable by RFA Pack 1.7 "
		       "(PLAN_rfa_tools.md finding 27). Use store mode unless the target is ours.\n";
	}

	if( options.update ) {
		out << " UseCompression: " << ( options.compress ? 1 : 0 ) << "\n"
		    << " Update & Append: 1\n"
		    << "Error! -u is not implemented yet\n";
		return 1;
	}

	std::vector<rfa::SourceFile> files;
	std::string error;

	if( !rfa::RfaWriter::collect_files( options.source_dir, options.base, files, &error ) ) {
		out << "Error! " << error << "\n";
		return 1;
	}

	out << "TotalFiles: " << files.size() << "\n";
	out << "  Uncompressed .rfa size (mb): " << ( total_bytes( files ) / ( 1024 * 1024 ) ) << "\n";
	out << "tmpDestDirLoc: " << destination_directory( options.archive ) << "\n";

	rfa::WriteOptions write_options;
	write_options.policy = options.compress ? rfa::CompressionPolicy::Compress
	                                        : rfa::CompressionPolicy::Store;

	const auto started = std::chrono::steady_clock::now();

	rfa::RfaWriter writer;
	rfa::WriteResult result;

	const bool written = writer.write( files, options.archive, write_options, &result, &error );

	const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now() - started ).count();

	out << " TimeTaken: " << elapsed << "\n";

	// The original parses and echoes both switches even when they cannot take effect - that
	// is how §2.5 established that -Compress is ignored during -u. Keeping the echo means a
	// caller can see what was understood.
	out << " UseCompression: " << ( options.compress ? 1 : 0 ) << "\n";
	out << " Update & Append: " << ( options.update ? 1 : 0 ) << "\n";

	if( !written ) {
		out << "Error! " << error << "\n";
		return 1;
	}

	return 0;
}

} // namespace cli
