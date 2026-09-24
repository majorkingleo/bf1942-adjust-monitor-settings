/**
 * rfaUnpack's command line - a reimplementation of RFA Pack 1.7's unpacker.
 *
 * The user-visible contract is tests/golden/oracle-cli.json, captured from
 * bin/rfaUnpack.orig.exe by tools/rfa_golden.py. Two things about that file shape this
 * code:
 *
 *   - Line order within stdout is not stable in the original (the banner goes through a
 *     different stream than the chatter), so the golden is compared on message presence,
 *     not on order. We emit the banner first, which is what a console user sees.
 *   - Exit codes are not a success signal. `-i9999` and an unresolvable name both print an
 *     error and exit 0, and that is pinned, so those paths return 0 here too. Only usage,
 *     argument and I/O failures return 1.
 *
 * @author Copyright (c) 2026
 */

#include "UnpackCli.h"

#include "rfa/RfaArchive.h"

#include <CpputilsDebug.h>
#include <format.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <ostream>
#include <vector>

namespace cli {

namespace {

/// The banner's usage block. The trailing space after "Options:" is in the original.
/// The "Options: " line and the block that follows are printed only when there are no
/// arguments to work with.
const char * const USAGE_BLOCK =
	" Usage: rfaUnpack.exe <Archive.rfa> [ExtractToPath] [-option]\n"
	" Options: \n"
	"   -i[indexToExtract]       -i123   -f[filenameToExtract]    -fObjects/Vehicles/Land/Willy/Objects.con   -l[listFile.lst]         -lC:\\extractFileList.lst";

struct Options
{
	std::string archive;
	std::string extract_path;
	bool        has_extract_path = false;

	std::string index_text;      ///< as written after -i, echoed verbatim
	bool        has_index = false;

	std::string name_text;       ///< -f
	bool        has_name = false;

	std::string list_path;       ///< -l
	bool        has_list = false;
};

bool looks_like_switch( const std::string & token )
{
	return token.size() >= 2 && token[0] == '-';
}

Options parse( const std::vector<std::string> & args )
{
	Options options;
	std::vector<std::string> positional;

	for( std::size_t i = 1; i < args.size(); ++i ) {

		const std::string & token = args[i];

		if( !looks_like_switch( token ) ) {
			positional.push_back( token );
			continue;
		}

		// Switches take their value attached, not separated: -i123, -lC:\list.lst.
		const std::string value = token.substr( 2 );

		switch( token[1] ) {
			case 'i': options.index_text = value; options.has_index = true; break;
			case 'f': options.name_text  = value; options.has_name  = true; break;
			case 'l': options.list_path  = value; options.has_list  = true; break;
			default:  break;   // unknown switches are ignored, as the original does
		}
	}

	if( !positional.empty() ) {
		options.archive = positional[0];
	}

	if( positional.size() > 1 ) {
		options.extract_path = positional[1];
		options.has_extract_path = true;
	}

	return options;
}

/// The original documents ExtractToPath as optional but the golden never exercises the
/// omitted case. "." is the reading that keeps the tool usable; anything else would mean
/// refusing to run.
std::string target_directory( const Options & options )
{
	if( options.has_extract_path && !options.extract_path.empty() ) {
		return options.extract_path;
	}

	return ".";
}

bool write_entry( rfa::PayloadReader & reader,
                  const std::string & target,
                  const rfa::Entry & entry,
                  std::string * error )
{
	std::vector<unsigned char> data;

	if( !reader.read( entry, data, error ) ) {
		return false;
	}

	const std::filesystem::path destination = std::filesystem::path( target ) / entry.name;

	std::error_code ec;
	std::filesystem::create_directories( destination.parent_path(), ec );

	std::ofstream file( destination.string().c_str(), std::ios::binary | std::ios::trunc );

	if( !file ) {
		if( error ) {
			*error = "cannot open '" + destination.string() + "'";
		}
		return false;
	}

	if( !data.empty() ) {
		file.write( reinterpret_cast<const char *>( data.data() ),
		            static_cast<std::streamsize>( data.size() ) );
	}

	if( !file ) {
		if( error ) {
			*error = "cannot write '" + destination.string() + "'";
		}
		return false;
	}

	return true;
}

/// Last path component, for the -f fallback.
std::string basename_of( const std::string & name )
{
	const std::string::size_type slash = name.rfind( '/' );

	return slash == std::string::npos ? name : name.substr( slash + 1 );
}

const rfa::Entry * find_by_basename( const rfa::RfaArchive & archive, const std::string & name )
{
	for( const rfa::Entry & entry : archive.entries() ) {
		if( basename_of( entry.name ) == name ) {
			return &entry;   // first hit in table order: deterministic
		}
	}

	return nullptr;
}

/**
 * Resolves one name and extracts it, reporting exactly what the original reports.
 *
 * @param allow_basename  -f accepts a bare file name, -l does not. The original's -l
 *                        requires the full internal path and reports "Name Not found!"
 *                        for a basename, which is pinned by the golden.
 */
bool extract_by_name( std::ostream & out,
                      rfa::PayloadReader & reader,
                      const std::string & target,
                      const rfa::RfaArchive & archive,
                      const std::string & name,
                      bool allow_basename )
{
	out << "EXTRACTING BY NAME: " << name << "\n";

	const rfa::Entry * entry = archive.find( name );

	if( !entry && allow_basename ) {
		entry = find_by_basename( archive, name );
	}

	if( !entry ) {
		out << "  ERROR! Could not ExtractByName: " << name << "\n"
		    << "\tName Not found!\n";
		return false;
	}

	std::string error;

	if( !write_entry( reader, target, *entry, &error ) ) {
		out << "  ERROR! Could not extract: " << name << "\n"
		    << "\t" << error << "\n";
		return false;
	}

	return true;
}

} // namespace

int run_unpack( const std::vector<std::string> & args, std::ostream & out )
{
	const Options options = parse( args );

	out << "|| .RFA UNPACK ||\n";

	if( options.archive.empty() ) {
		out << USAGE_BLOCK << "\n";
		return 1;
	}

	const std::string target = target_directory( options );

	out << "strExtractPath: " << target << "\n";

	// The directory is checked before the archive is touched: the oracle reports a missing
	// target without ever printing LoadIndex, so it clearly resolves the path first. Every
	// extract target must already exist.
	if( !std::filesystem::is_directory( target ) ) {
		out << "Error! Directory does not exist: " << target << "\n";
		return 1;
	}

	out << "LoadIndex( \"" << options.archive << "\" )\n";

	rfa::RfaArchive archive;
	std::string error;

	if( !archive.open( options.archive, &error ) ) {
		out << "Error! Cannot open archive: " << options.archive << "\n"
		    << "\t" << error << "\n";
		return 1;
	}

	// The original prints two heap addresses here - a debug leftover, and process layout
	// rather than behaviour (the golden normalises them to {ADDR}). We print our own two
	// pointers so the line keeps its shape for anything that greps for the prefix.
	out << "rfa_file_name: " << static_cast<const void *>( archive.path().c_str() )
	    << " <- " << static_cast<const void *>( &archive ) << "\n";

	// Recoverable oddities stay out of stdout: the original has no such channel, and the
	// golden would not match. They go to the debug log instead.
	for( const std::string & problem : archive.problems() ) {
		CPPDEBUG( Tools::format( "problem in '%s': %s", options.archive.c_str(), problem.c_str() ) );
	}

	rfa::PayloadReader reader( options.archive );

	if( !reader.good() ) {
		out << "Error! Cannot open archive: " << options.archive << "\n";
		return 1;
	}

	if( options.has_index ) {

		const int index = std::atoi( options.index_text.c_str() );

		out << " $Extract file index: \"" << options.index_text << "\" = " << index << "\n";

		if( index < 0 || static_cast<std::size_t>( index ) >= archive.entries().size() ) {
			out << "ERROR! item out of range to extract! " << index << "\n";
			return 0;   // pinned: a selection miss is not a failure exit
		}

		CPPDEBUG( Tools::format( "extracting index %d: '%s'", index, archive.entries()[index].name.c_str() ) );

		if( !write_entry( reader, target, archive.entries()[index], &error ) ) {
			out << "  ERROR! Could not extract: " << archive.entries()[index].name << "\n"
			    << "\t" << error << "\n";
			return 1;
		}

		return 0;
	}

	if( options.has_list ) {

		out << " $Extract files FileList: " << options.list_path << "\n";

		std::ifstream list( options.list_path.c_str() );

		if( !list ) {
			out << "Error! Cannot open file list: " << options.list_path << "\n";
			return 1;
		}

		// The original splits the list on whitespace, which is why a name containing a
		// space is unreachable through -l even though it exists in the archive.
		std::string name;

		while( list >> name ) {
			// A name that cannot be resolved is reported and the run continues: pinned by
			// the golden, which extracts the good entries of a list that also has a bad one.
			extract_by_name( out, reader, target, archive, name, false );
		}

		return 0;
	}

	if( options.has_name ) {

		out << " $Extract file name: " << options.name_text << "\n";

		// Deliberate divergence from the captured behaviour: the shipped build cannot match
		// anything with -f, reporting "Name Not found!" even for files that exist. We try
		// the full internal path first and fall back to the basename, which is what the
		// switch promises. PLAN_rfa_tools.md Phase 3 records this as intentional.
		extract_by_name( out, reader, target, archive, options.name_text, true );

		return 0;
	}

	out << "unpackedSize_MB: " << ( archive.total_uncompressed_size() / ( 1024 * 1024 ) ) << "\n";

	std::size_t extracted = 0;
	std::size_t failed = 0;

	for( const rfa::Entry & entry : archive.entries() ) {

		if( write_entry( reader, target, entry, &error ) ) {
			++extracted;
			continue;
		}

		++failed;
		out << "  ERROR! Could not extract: " << entry.name << "\n"
		    << "\t" << error << "\n";
	}

	CPPDEBUG( Tools::format( "extracted %d of %d entries into '%s'",
	                         static_cast<int>( extracted ),
	                         static_cast<int>( archive.entries().size() ),
	                         target.c_str() ) );

	// The original exits 0 even when it wrote nothing. A payload that cannot be read is a
	// real failure, so it is reported as one.
	return failed == 0 ? 0 : 1;
}

} // namespace cli
