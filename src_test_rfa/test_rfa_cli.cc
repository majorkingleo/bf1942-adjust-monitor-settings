/**
 * Testcases for cli/UnpackCli.h, driven by the real fixtures in tests/data.
 *
 * What is and is not proven here:
 *
 *  - The CLI *surface* is compared against tests/golden/oracle-cli.json, the verbatim
 *    output of bin/rfaUnpack.orig.exe. Only message presence is asserted, because the
 *    original's line order is not stable under redirection - that is recorded in the
 *    golden's own caveats, and the exit codes it pins are asserted exactly.
 *  - The extracted *bytes* are compared against RfaArchive/PayloadReader. That is
 *    self-consistency, not an oracle; the external evidence for the payloads is Phase 2's
 *    oracle-written golden archives plus the Phase 0 ground truth, which those readers
 *    already pass. What this file adds is that the CLI puts exactly those bytes at exactly
 *    the right paths, and that the selection switches select what they claim to.
 *
 * @author Copyright (c) 2026
 */

#include "test_rfa_cli.h"

#include "PackCli.h"
#include "UnpackCli.h"

#include "rfa/RfaArchive.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace rfa;

namespace {

const char * FH    = "fh/Battle_Of_Pavlov-1942.rfa";
const char * MESH  = "tiny/standardMesh_001.rfa";
const char * PEENE = "tiny/Peenemunde_001.rfa";

/// Same resolution rules as test_rfa_archive.cc: the in-tree build runs from the repo root.
std::string fixture_dir()
{
	std::vector<std::string> candidates;

	if( const char * from_env = std::getenv( "RFA_TEST_DATA_DIR" ) ) {
		if( *from_env ) {
			candidates.push_back( from_env );
		}
	}

	candidates.push_back( "tests/data" );
	candidates.push_back( "../tests/data" );
	candidates.push_back( "../../tests/data" );

	for( const std::string & candidate : candidates ) {
		std::ifstream probe( ( candidate + "/" + FH ).c_str(), std::ios::binary );
		if( probe.good() ) {
			return candidate;
		}
	}

	return std::string();
}

bool fixtures_available()
{
	static const bool available = !fixture_dir().empty();

	if( !available ) {
		std::cout << "[rfa] tests/data was not found relative to the current directory, so "
		             "every fixture-dependent testcase will fail. Run the tests from the "
		             "build directory or set RFA_TEST_DATA_DIR.\n";
	}

	return available;
}

std::string fixture( const char * relative )
{
	const std::string dir = fixture_dir();
	return dir.empty() ? std::string() : ( dir + "/" + relative );
}

/// Runs the CLI with its chatter captured, so a testcase can assert on it.
std::string run( const std::vector<std::string> & args, int & rc )
{
	std::ostringstream out;
	rc = cli::run_unpack( args, out );
	return out.str();
}

/// The same for rfaPack, whose warnings land on a second stream.
std::string run_pack( const std::vector<std::string> & args, int & rc, std::string & err )
{
	std::ostringstream out;
	std::ostringstream errors;
	rc = cli::run_pack( args, out, errors );
	err = errors.str();
	return out.str();
}

/**
 * Where tools/rfa_golden_archives.py put the oracle's archives, or "" if they are missing.
 *
 * The writer testcases have their own copy of this lookup; the CLI needs one too because it
 * asserts on the archive as a FILE, which is the one thing the writer's in-memory comparison
 * cannot see.
 */
std::string golden_root()
{
	std::vector<std::string> candidates;

	if( const char * from_env = std::getenv( "RFA_TEST_DATA_DIR" ) ) {
		if( *from_env ) {
			candidates.push_back( std::string( from_env ) + "/golden" );
		}
	}

	candidates.push_back( "tests/data/golden" );
	candidates.push_back( "../tests/data/golden" );
	candidates.push_back( "../../tests/data/golden" );

	for( const std::string & candidate : candidates ) {
		std::ifstream probe( ( candidate + "/oracle-store.rfa" ).c_str(), std::ios::binary );

		if( probe.good() ) {
			return candidate;
		}
	}

	return std::string();
}

bool write_file( const std::string & path, const std::string & content )
{
	std::ofstream out( path.c_str(), std::ios::binary );

	if( !out ) {
		return false;
	}

	out << content;
	return out.good();
}

bool contains( const std::string & haystack, const std::string & needle )
{
	return haystack.find( needle ) != std::string::npos;
}

std::string basename_of( const std::string & name )
{
	const std::string::size_type slash = name.rfind( '/' );
	return slash == std::string::npos ? name : name.substr( slash + 1 );
}

/// A scratch directory that removes itself, so a failing testcase leaves no litter.
class Scratch
{
public:
	explicit Scratch( const std::string & seed )
	: root_( seed + ".work" )
	{
		std::error_code ec;
		std::filesystem::remove_all( root_, ec );
		std::filesystem::create_directories( root_, ec );
	}

	~Scratch()
	{
		std::error_code ec;
		std::filesystem::remove_all( root_, ec );
	}

	Scratch( const Scratch & ) = delete;
	Scratch & operator=( const Scratch & ) = delete;

	const std::string & root() const { return root_; }

	std::string path( const std::string & relative ) const
	{
		return ( std::filesystem::path( root_ ) / relative ).string();
	}

	/// Creates (and returns) a directory below the scratch root.
	std::string subdir( const std::string & name ) const
	{
		const std::string full = path( name );
		std::error_code ec;
		std::filesystem::create_directories( full, ec );
		return full;
	}

private:
	std::string root_;
};

std::vector<unsigned char> read_whole_file( const std::string & path )
{
	std::ifstream in( path.c_str(), std::ios::binary );

	if( !in ) {
		return std::vector<unsigned char>();
	}

	std::ostringstream content;
	content << in.rdbuf();

	const std::string text = content.str();
	return std::vector<unsigned char>( text.begin(), text.end() );
}

/// Every regular file below root, as internal paths with '/' separators, sorted.
std::vector<std::string> tree_files( const std::string & root )
{
	std::vector<std::string> files;

	for( const std::filesystem::directory_entry & entry :
	     std::filesystem::recursive_directory_iterator( root ) ) {

		if( entry.is_regular_file() ) {
			files.push_back( std::filesystem::relative( entry.path(), root ).generic_string() );
		}
	}

	std::sort( files.begin(), files.end() );
	return files;
}

bool write_lines( const std::string & path, const std::vector<std::string> & lines )
{
	std::ofstream out( path.c_str(), std::ios::binary );

	if( !out ) {
		return false;
	}

	for( const std::string & line : lines ) {
		out << line << "\n";
	}

	return out.good();
}

/// Extracts every entry of `relative` and checks the tree against the reader.
bool full_extract_matches_the_reader( const char * relative, const std::string & scratch_seed )
{
	Scratch scratch( scratch_seed );

	const std::string archive_path = fixture( relative );
	const std::string target       = scratch.subdir( "out" );

	int rc = 0;
	const std::string out = run( { "rfaUnpack.exe", archive_path, target }, rc );

	if( rc != 0 ) {
		std::cout << "[rfa] " << relative << " exited " << rc << "\n";
		return false;
	}

	if( !contains( out, "LoadIndex( \"" + archive_path + "\" )" ) ||
	    !contains( out, "unpackedSize_MB: " ) ||
	    !contains( out, "strExtractPath: " + target ) ) {
		return false;
	}

	RfaArchive archive;

	if( !archive.open( archive_path ) ) {
		return false;
	}

	PayloadReader reader( archive_path );

	for( const Entry & entry : archive.entries() ) {

		std::vector<unsigned char> expected;

		if( !reader.read( entry, expected ) ) {
			std::cout << "[rfa] could not read '" << entry.name << "' from " << relative << "\n";
			return false;
		}

		if( read_whole_file( scratch.path( "out/" + entry.name ) ) != expected ) {
			std::cout << "[rfa] extracted bytes differ for '" << entry.name << "'\n";
			return false;
		}
	}

	const std::vector<std::string> written = tree_files( target );

	if( written.size() != archive.entries().size() ) {
		std::cout << "[rfa] " << relative << ": wrote " << written.size()
		          << " files for " << archive.entries().size() << " entries\n";
		return false;
	}

	return true;
}

} // namespace

// ---------------------------------------------------------------------------

TestCasePtr test_cli_usage_without_arguments()
{
	return std::make_shared<TestCaseFuncNoInp>(
		"cli_usage_without_arguments", true, []() {
			int rc = 0;
			const std::string out = run( { "rfaUnpack.exe" }, rc );

			// The exact banner, trailing space after "Options:" included.
			return rc == 1
			    && contains( out, "|| .RFA UNPACK ||" )
			    && contains( out, " Usage: rfaUnpack.exe <Archive.rfa> [ExtractToPath] [-option]" )
			    && contains( out, " Options: \n" )
			    && contains( out, "   -i[indexToExtract]       -i123   -f[filenameToExtract]    -fObjects/Vehicles/Land/Willy/Objects.con   -l[listFile.lst]         -lC:\\extractFileList.lst" );
		} );
}

TestCasePtr test_cli_missing_target_directory_stops_before_loading()
{
	// The harness deletes this file before the testcase runs, so it is a path that
	// certainly does not exist.
	return std::make_shared<TestCaseFuncOneFile>(
		"cli_missing_target_directory_stops_before_loading",
		[]( const std::string & missing ) {
			if( !fixtures_available() ) {
				return false;
			}

			int rc = 0;
			const std::string out = run( { "rfaUnpack.exe", fixture( PEENE ), missing }, rc );

			// The original resolves the target directory before it touches the archive:
			// that is why no LoadIndex line appears in this scenario in the golden.
			return rc == 1
			    && contains( out, "Error! Directory does not exist: " + missing )
			    && contains( out, "strExtractPath: " + missing )
			    && !contains( out, "LoadIndex(" );
		},
		std::ios::out );
}

TestCasePtr test_cli_full_extract_writes_every_entry()
{
	return std::make_shared<TestCaseFuncOneFile>(
		"cli_full_extract_writes_every_entry",
		[]( const std::string & scratch_file ) {
			if( !fixtures_available() ) {
				return false;
			}

			// Peenemunde is the raw-payload fixture and standardMesh the chunked one, so
			// both payload variants go through the CLI writer.
			return full_extract_matches_the_reader( PEENE, scratch_file + ".peene" )
			    && full_extract_matches_the_reader( MESH, scratch_file + ".mesh" );
		},
		std::ios::out );
}

TestCasePtr test_cli_index_selects_the_entry_at_that_position()
{
	return std::make_shared<TestCaseFuncOneFile>(
		"cli_index_selects_the_entry_at_that_position",
		[]( const std::string & scratch_file ) {
			if( !fixtures_available() ) {
				return false;
			}

			Scratch scratch( scratch_file + ".index" );

			const std::string archive_path = fixture( MESH );
			const std::string target       = scratch.subdir( "out" );

			RfaArchive archive;

			if( !archive.open( archive_path ) || archive.entries().empty() ) {
				return false;
			}

			int rc = 0;
			const std::string out = run( { "rfaUnpack.exe", archive_path, target, "-i0" }, rc );

			if( rc != 0 || !contains( out, " $Extract file index: \"0\" = 0" ) ) {
				return false;
			}

			PayloadReader reader( archive_path );
			std::vector<unsigned char> expected;

			if( !reader.read( archive.entries()[0], expected ) ) {
				return false;
			}

			// -i is a 0-based index into the file table, so entry 0 is what must land.
			const std::vector<std::string> written = tree_files( target );

			return written.size() == 1
			    && written[0] == archive.entries()[0].name
			    && read_whole_file( scratch.path( "out/" + archive.entries()[0].name ) ) == expected;
		},
		std::ios::out );
}

TestCasePtr test_cli_index_out_of_range_is_reported_but_not_fatal()
{
	return std::make_shared<TestCaseFuncOneFile>(
		"cli_index_out_of_range_is_reported_but_not_fatal",
		[]( const std::string & scratch_file ) {
			if( !fixtures_available() ) {
				return false;
			}

			Scratch scratch( scratch_file + ".range" );

			const std::string target = scratch.subdir( "out" );

			int rc = 0;
			const std::string out = run( { "rfaUnpack.exe", fixture( MESH ), target, "-i9999" }, rc );

			// Exit 0 with an error message is the original's behaviour and is pinned by
			// the golden's unpack_index_out_of_range scenario.
			return rc == 0
			    && contains( out, "ERROR! item out of range to extract! 9999" )
			    && contains( out, " $Extract file index: \"9999\" = 9999" )
			    && tree_files( target ).empty();
		},
		std::ios::out );
}

TestCasePtr test_cli_list_extracts_full_internal_paths()
{
	return std::make_shared<TestCaseFuncOneFile>(
		"cli_list_extracts_full_internal_paths",
		[]( const std::string & scratch_file ) {
			if( !fixtures_available() ) {
				return false;
			}

			Scratch scratch( scratch_file + ".list" );

			const std::string archive_path = fixture( FH );
			const std::string target       = scratch.subdir( "out" );

			RfaArchive archive;

			if( !archive.open( archive_path ) || archive.entries().size() < 2 ) {
				return false;
			}

			std::vector<std::string> names;
			names.push_back( archive.entries()[0].name );
			names.push_back( archive.entries()[1].name );

			// The list lives outside the extraction directory, so the file count below is
			// not polluted by it.
			const std::string list = scratch.path( "list.lst" );

			if( !write_lines( list, names ) ) {
				return false;
			}

			int rc = 0;
			const std::string out = run( { "rfaUnpack.exe", archive_path, target, "-l" + list }, rc );

			if( rc != 0 || !contains( out, " $Extract files FileList: " + list ) ) {
				return false;
			}

			PayloadReader reader( archive_path );

			for( const std::string & name : names ) {

				const Entry * entry = archive.find( name );

				if( !entry ) {
					return false;
				}

				std::vector<unsigned char> expected;

				if( !reader.read( *entry, expected ) ) {
					return false;
				}

				if( read_whole_file( scratch.path( "out/" + name ) ) != expected ) {
					return false;
				}

				if( !contains( out, "EXTRACTING BY NAME: " + name ) ) {
					return false;
				}
			}

			return tree_files( target ).size() == names.size();
		},
		std::ios::out );
}

TestCasePtr test_cli_list_rejects_a_basename_like_the_original()
{
	return std::make_shared<TestCaseFuncOneFile>(
		"cli_list_rejects_a_basename_like_the_original",
		[]( const std::string & scratch_file ) {
			if( !fixtures_available() ) {
				return false;
			}

			Scratch scratch( scratch_file + ".base" );

			const std::string archive_path = fixture( FH );
			const std::string target       = scratch.subdir( "out" );

			RfaArchive archive;

			if( !archive.open( archive_path ) || archive.entries().empty() ) {
				return false;
			}

			const std::string basename = basename_of( archive.entries()[0].name );

			// Self-check: if the basename happened to be a full path in this archive the
			// scenario would not be the miss it is meant to be.
			if( archive.find( basename ) != nullptr ) {
				return false;
			}

			const std::string list = scratch.path( "list.lst" );

			if( !write_lines( list, { basename } ) ) {
				return false;
			}

			int rc = 0;
			const std::string out = run( { "rfaUnpack.exe", archive_path, target, "-l" + list }, rc );

			// -l requires the full internal path; the miss is reported, the run continues,
			// and it still exits 0. All three are pinned by the golden.
			return rc == 0
			    && contains( out, "EXTRACTING BY NAME: " + basename )
			    && contains( out, "  ERROR! Could not ExtractByName: " + basename )
			    && contains( out, "\tName Not found!" )
			    && tree_files( target ).empty();
		},
		std::ios::out );
}

TestCasePtr test_cli_f_accepts_a_basename_unlike_the_original()
{
	// Deliberate divergence: the shipped build cannot match anything with -f and is
	// recorded as unpack_f_broken in the golden. PLAN_rfa_tools.md Phase 3 asks for real
	// name matching, so -f accepts a full path or a basename here.
	return std::make_shared<TestCaseFuncOneFile>(
		"cli_f_accepts_a_basename_unlike_the_original",
		[]( const std::string & scratch_file ) {
			if( !fixtures_available() ) {
				return false;
			}

			Scratch scratch( scratch_file + ".f" );

			const std::string archive_path = fixture( MESH );
			const std::string target       = scratch.subdir( "out" );

			RfaArchive archive;

			if( !archive.open( archive_path ) || archive.entries().empty() ) {
				return false;
			}

			const std::string basename = basename_of( archive.entries()[0].name );

			int rc = 0;
			const std::string out = run( { "rfaUnpack.exe", archive_path, target, "-f" + basename }, rc );

			if( rc != 0 || !contains( out, " $Extract file name: " + basename ) ) {
				return false;
			}

			if( contains( out, "Could not ExtractByName" ) ) {
				return false;
			}

			PayloadReader reader( archive_path );
			std::vector<unsigned char> expected;

			if( !reader.read( archive.entries()[0], expected ) ) {
				return false;
			}

			return read_whole_file( scratch.path( "out/" + archive.entries()[0].name ) ) == expected;
		},
		std::ios::out );
}

TestCasePtr test_cli_full_extract_of_the_fh_archive_matches_the_reader()
{
	// The largest fixture: 251 entries over 11.49 MB. It is the only case that drives
	// nested directories and multi-chunk entries through the CLI writer.
	return std::make_shared<TestCaseFuncOneFile>(
		"cli_full_extract_of_the_fh_archive_matches_the_reader",
		[]( const std::string & scratch_file ) {
			if( !fixtures_available() ) {
				return false;
			}

			return full_extract_matches_the_reader( FH, scratch_file + ".fh" );
		},
		std::ios::out );
}

// ---------------------------------------------------------------------------
// CLI: rfaPack
//
// The golden's two pack scenarios pin the chatter, and the archives they were captured
// against are gone (they were temp files), so what a testcase can reproduce is the *shape*:
// the same lines, in the same spelling, with the same exit codes. The byte-level claim is
// made separately, against the committed oracle archive, because that is the part that
// matters and the part the golden cannot carry.
//
// One deliberate divergence, recorded here rather than hidden: the golden's `pack_compress`
// has an EMPTY stderr, and ours warns there about finding 27. That is the point of the
// warning - the original fails silently, we do not.
// ---------------------------------------------------------------------------

TestCasePtr test_cli_pack_no_arguments_matches_the_golden()
{
	return std::make_shared<TestCaseFuncNoInp>(
		"cli_pack_no_arguments_matches_the_golden", true, []() {
			int rc = 0;
			std::string err;
			const std::string out = run_pack( { "rfaPack.exe" }, rc, err );

			// The usage block verbatim, trailing spaces included - both of them are in the
			// golden, and a "tidy" edit would silently break drop-in compatibility.
			return rc == 1
			    && contains( out, "ERROR! Not enough command line arguments" )
			    && contains( out, " Usage examples: \n" )
			    && contains( out, "   ProgramName [sourceDir] [PackDirName] [Archive.rfa] [ -u update existing .rfa | -Compress]" )
			    && contains( out, "   RfaPack.exe d:/menu menu menu.rfa\n" )
			    && contains( out, "   RfaPack.exe d:/menu menu menu.rfa -u\n" )
			    && contains( out, "   RfaPack.exe d:/menu menu menu.rfa -Compress\n" )
			    && contains( out, "   RfaPack.exe d:/menu menu menu.rfa -u -Compress" )
			    && contains( out, "-- RFA Pack 1.7 --" )
			    && err.empty();
		} );
}

TestCasePtr test_cli_pack_plain_matches_the_golden()
{
	// The golden's `pack_plain`: one source file, a directory-qualified archive path, no
	// switches.
	return std::make_shared<TestCaseFuncOneFile>(
		"cli_pack_plain_matches_the_golden",
		[]( const std::string & scratch_file ) {
			Scratch work( scratch_file + ".plain" );

			const std::string src     = work.subdir( "src" );
			const std::string archive = work.path( "plain.rfa" );

			if( !write_file( src + "/one.txt", "Game.setNumberOfTickets 1 115\r\n" ) ) {
				std::cout << "[rfa] could not create the source file\n";
				return false;
			}

			int rc = 0;
			std::string err;
			const std::string out =
				run_pack( { "rfaPack.exe", src, "menu", archive }, rc, err );

			if( !err.empty() ) {
				std::cout << "[rfa] unexpected stderr: " << err << "\n";
			}

			return rc == 0
			    && contains( out, "TotalFiles: 1" )
			    && contains( out, "  Uncompressed .rfa size (mb): 0" )
			    && contains( out, "tmpDestDirLoc: " +
			                     std::filesystem::path( archive ).parent_path().string() )
			    && contains( out, " TimeTaken: " )
			    && contains( out, "-- RFA Pack 1.7 --" )
			    && contains( out, " UseCompression: 0" )
			    && contains( out, " Update & Append: 0" )
			    && err.empty()
			    && std::filesystem::is_regular_file( archive );
		},
		std::ios::out );
}

TestCasePtr test_cli_pack_compress_matches_the_golden_and_warns()
{
	// The golden's `pack_compress`. Same lines as `pack_plain` with ` UseCompression: 1` -
	// and the one place where we depart from the original on purpose: it says nothing about
	// the archive it just wrote being unreadable by the tool family it belongs to
	// (finding 27), we say so on stderr.
	return std::make_shared<TestCaseFuncOneFile>(
		"cli_pack_compress_matches_the_golden_and_warns",
		[]( const std::string & scratch_file ) {
			Scratch work( scratch_file + ".compress" );

			const std::string src     = work.subdir( "src" );
			const std::string archive = work.path( "comp.rfa" );

			if( !write_file( src + "/one.txt", "Game.setNumberOfTickets 1 115\r\n" ) ) {
				return false;
			}

			int rc = 0;
			std::string err;
			const std::string out =
				run_pack( { "rfaPack.exe", src, "menu", archive, "-Compress" }, rc, err );

			return rc == 0
			    && contains( out, "TotalFiles: 1" )
			    && contains( out, "  Uncompressed .rfa size (mb): 0" )
			    && contains( out, "-- RFA Pack 1.7 --" )
			    && contains( out, " UseCompression: 1" )
			    && contains( out, " Update & Append: 0" )
			    && std::filesystem::is_regular_file( archive )
			    // the golden has an empty stderr here; the warning is the divergence
			    && contains( err, "WARNING! -Compress output is not readable by RFA Pack 1.7" );
		},
		std::ios::out );
}

TestCasePtr test_cli_pack_switch_matching_is_case_insensitive()
{
	// The documented spellings are -u and -Compress. Accepting -COMPRESS and -U costs
	// nothing and cannot break a caller that uses the documented form, so both are pinned
	// here: a case-only typo must not silently pack in the wrong mode.
	return std::make_shared<TestCaseFuncOneFile>(
		"cli_pack_switch_matching_is_case_insensitive",
		[]( const std::string & scratch_file ) {
			Scratch work( scratch_file + ".case" );

			const std::string src = work.subdir( "src" );

			if( !write_file( src + "/one.txt", "x" ) ) {
				return false;
			}

			int rc             = 0;
			std::string err;
			const std::string upper = work.path( "upper.rfa" );

			const std::string out =
				run_pack( { "rfaPack.exe", src, "menu", upper, "-COMPRESS" }, rc, err );

			// -COMPRESS is honoured...
			if( rc != 0 || !contains( out, " UseCompression: 1" ) ) {
				return false;
			}

			// ...and -U reaches the same refusal as -u rather than being ignored.
			const std::string update = work.path( "update.rfa" );

			const std::string out_u =
				run_pack( { "rfaPack.exe", src, "menu", update, "-U" }, rc, err );

			return rc == 1
			    && contains( out_u, " Update & Append: 1" )
			    && contains( out_u, "Error! -u is not implemented yet" )
			    && !std::filesystem::exists( update );
		},
		std::ios::out );
}

TestCasePtr test_cli_pack_update_refuses_and_writes_nothing()
{
	// -u is not implemented (§2.5: it is a fresh pack plus carried-over entries, with three
	// divergences D6-D8 still undecided). Until it is, the switch is recognised, echoed and
	// refused: a caller who asked for an update must not receive a plain pack instead. The
	// archive must not exist afterwards, or a caller cannot tell the two apart.
	return std::make_shared<TestCaseFuncOneFile>(
		"cli_pack_update_refuses_and_writes_nothing",
		[]( const std::string & scratch_file ) {
			Scratch work( scratch_file + ".update" );

			const std::string src     = work.subdir( "src" );
			const std::string archive = work.path( "updated.rfa" );

			if( !write_file( src + "/one.txt", "x" ) ||
			    !write_file( src + "/two.txt", "y" ) ) {
				return false;
			}

			int rc = 0;
			std::string err;
			const std::string out =
				run_pack( { "rfaPack.exe", src, "menu", archive, "-u" }, rc, err );

			if( rc != 1 ) {
				std::cout << "[rfa] -u exited " << rc << " instead of refusing\n";
			}

			return rc == 1
			    && contains( out, " Update & Append: 1" )
			    && contains( out, "Error! -u is not implemented yet" )
			    && !std::filesystem::exists( archive );
		},
		std::ios::out );
}

TestCasePtr test_cli_pack_missing_source_directory_is_reported()
{
	return std::make_shared<TestCaseFuncOneFile>(
		"cli_pack_missing_source_directory_is_reported",
		[]( const std::string & scratch_file ) {
			Scratch work( scratch_file + ".missing" );

			const std::string missing = work.path( "no_such_dir" );
			const std::string archive = work.path( "out.rfa" );

			int rc = 0;
			std::string err;
			const std::string out =
				run_pack( { "rfaPack.exe", missing, "menu", archive }, rc, err );

			// A directory that is not there is a failure, and it must not leave an archive
			// behind that looks like a successful pack of an empty tree.
			return rc == 1
			    && contains( out, "Error! '" + missing + "' is not a directory" )
			    && !std::filesystem::exists( archive );
		},
		std::ios::out );
}

TestCasePtr test_cli_pack_store_output_is_byte_identical_to_the_oracle_archive()
{
	// The strongest assertion available to this file, and the reason the CLI exists as a
	// library: driving the command line - argument parsing, the walk, the policy, the write
	// - must produce bin\rfaPack.orig.exe's own bytes for the committed golden tree, not
	// merely for the writer underneath it. Found by reading the file the CLI actually
	// created, which is the one thing the writer's in-memory comparison cannot cover.
	return std::make_shared<TestCaseFuncOneFile>(
		"cli_pack_store_output_is_byte_identical_to_the_oracle_archive",
		[]( const std::string & scratch_file ) {
			const std::string root = golden_root();

			if( root.empty() ) {
				std::cout << "[rfa] tests/data/golden not found; run "
				             "tools/rfa_golden_archives.py\n";
				return false;
			}

			Scratch work( scratch_file + ".golden" );

			const std::string archive = work.path( "ours-store.rfa" );

			int rc = 0;
			std::string err;
			const std::string out =
				run_pack( { "rfaPack.exe", root + "/tree", "menu", archive }, rc, err );

			if( rc != 0 ) {
				std::cout << "[rfa] the CLI exited " << rc << ":\n" << out << "\n" << err << "\n";
				return false;
			}

			const std::vector<unsigned char> ours   = read_whole_file( archive );
			const std::vector<unsigned char> theirs = read_whole_file( root + "/oracle-store.rfa" );

			if( ours.empty() || theirs.empty() ) {
				std::cout << "[rfa] could not read one of the archives\n";
				return false;
			}

			if( ours != theirs ) {
				std::cout << "[rfa] store archive differs: ours " << ours.size()
				          << " B, the oracle " << theirs.size() << " B\n";
				return false;
			}

			return contains( out, "TotalFiles: " )
			    && contains( out, " UseCompression: 0" )
			    && err.empty();
		},
		std::ios::out );
}
