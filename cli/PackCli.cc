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

#include "ThreadSwitch.h"

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

/// Our own extension, not in the original's usage text - which is reproduced verbatim and
/// pinned by the golden, so a new switch must not appear there. Compress with LZO1X-1 instead
/// of the era's LZO1X-999: ~15x faster, ~21% larger, and the shipped 2003 tools cannot read it
/// (finding 27). Only the game can.
const char * const FAST_SWITCH = "--lzo-fast";

struct Options
{
	std::string source_dir;
	std::string base;
	std::string archive;

	bool update = false;
	bool compress = false;
	bool fast = false;

	/// Worker threads asked for: 0 keeps the default, which is the machine's CPU count.
	unsigned threads = 0;

	/// How the --threads switch went, if it was there at all. Keeping the outcome rather than
	/// a `has_threads` flag lets the two failures carry their own message.
	ThreadSwitch thread_switch = ThreadSwitch::NotThreads;

	/// Text that could not be read as a thread count, for the BadValue message.
	std::string bad_threads;

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

		// --threads comes first: it may carry its value as the NEXT token, which must not then
		// be taken for a positional argument - it would become the base folder name.
		unsigned thread_value = options.threads;

		switch( take_thread_switch( args, i, thread_value ) ) {
			case ThreadSwitch::Accepted:
				options.threads = thread_value;
				options.thread_switch = ThreadSwitch::Accepted;
				continue;

			case ThreadSwitch::BadValue:
				options.bad_threads = args[i];
				options.thread_switch = ThreadSwitch::BadValue;
				continue;

			case ThreadSwitch::MissingValue:
				options.thread_switch = ThreadSwitch::MissingValue;
				continue;

			case ThreadSwitch::NotThreads:
				break;
		}

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

		if( lowered == FAST_SWITCH ) {
			options.fast = true;
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

	// On stdout, like every other argument error of this program: stderr carries the
	// --lzo-fast warning and nothing else, so a caller can still tell a warning from a
	// refusal by the stream alone. A caller who asked for a specific count must not get
	// some other count instead, which is why both cases stop the run.
	if( options.thread_switch == ThreadSwitch::MissingValue ) {
		out << "Error! --threads needs a number, but the switch is the last argument\n";
		return 1;
	}

	if( options.thread_switch == ThreadSwitch::BadValue ) {
		out << "Error! --threads needs a number, got '" << options.bad_threads << "'\n";
		return 1;
	}

	if( !options.complete ) {
		out << "ERROR! Not enough command line arguments\n"
		    << USAGE_BLOCK << "\n"
		    << "\n"
		    << BANNER << "\n";
		return 1;
	}

	out << BANNER << "\n";

	if( options.compress && options.fast ) {
		// Finding 27: the 2003-era decoder in RFA Pack 1.7 rejects the match opcodes LZO1X-1
		// emits, so a --lzo-fast archive cannot be read by the shipped tools. The default is
		// the era's own encoder, whose streams they read because those are the streams they
		// wrote themselves (finding 31) - so this warning is about the opt-in path only.
		//
		// It goes to stderr because every captured golden has an empty stderr and a packed
		// stdout; stdout stays a faithful reproduction of the original.
		err << "WARNING! --lzo-fast output is not readable by RFA Pack 1.7 "
		       "(PLAN_rfa_tools.md finding 27). The default encoder is.\n";
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
	write_options.lzo = options.fast ? rfa::LzoVariant::Fast : rfa::LzoVariant::Era;
	write_options.threads = options.threads;

	// Echoed only when the switch was used, so every captured golden keeps its exact stdout.
	// The number printed is the EFFECTIVE one, which is clamped by the policy and the chunk
	// count - for store mode it is always 1, and saying so is more useful than repeating the
	// request back.
	if( options.thread_switch == ThreadSwitch::Accepted ) {
		out << " Threads: " << rfa::RfaWriter::planned_threads( files, write_options ) << "\n";
	}

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
