/**
 * The one switch both command lines accept that the original does not: `--threads N`.
 *
 * Shared instead of written twice, because the part that is easy to get wrong is not the number
 * - it is where the value comes from. A separated value has to be consumed here, or the next
 * token is taken for a positional argument: in rfaPack that silently becomes the base folder
 * name, in rfaUnpack the extract directory. Getting that wrong in one of the two programs is
 * exactly the kind of divergence this file exists to prevent.
 *
 * Accepted spellings: `--threads N`, `--threads=N`, `-j N`, `-jN`. The long form is matched
 * case-insensitively, like rfaPack's other switches.
 *
 * `--threads 0` means "use the default" - the CPU count for packing, a third of it for
 * unpacking. It is NOT the same as `--threads 1`, which forces a serial run and is what the
 * benchmarks use as their baseline.
 *
 * @author Copyright (c) 2026
 */

#pragma once

#include <charconv>
#include <string>
#include <system_error>
#include <vector>

namespace cli {

/// What `take_thread_switch` found.
enum class ThreadSwitch
{
	NotThreads,   ///< not a thread switch: the caller handles the token as it normally would
	Accepted,     ///< `value` is set, and `index` has been advanced past any separated value
	BadValue,     ///< it WAS a thread switch, but `args[index]` is not a usable count: abort
	MissingValue  ///< it WAS a thread switch and it is the LAST argument: there is no count
};

/**
 * Parse a thread count.
 *
 * Rejects anything that is not a plain non-negative number that fits in an unsigned, so a
 * negative value or a word is an error rather than a silent fallback to the default - a caller
 * that asked for four threads and silently got twenty-four would never find out.
 */
inline bool parse_thread_count( const std::string & text, unsigned * value )
{
	if( text.empty() ) {
		return false;
	}

	unsigned parsed = 0;

	const std::from_chars_result result =
		std::from_chars( text.data(), text.data() + text.size(), parsed );

	// `from_chars` neither skips whitespace nor accepts a sign, which is what we want.
	if( result.ec != std::errc() || result.ptr != text.data() + text.size() ) {
		return false;
	}

	*value = parsed;
	return true;
}

/**
 * Look at `args[index]` and, if it is a thread switch, consume it.
 *
 * On Accepted, `index` has been advanced past a separated value, so the caller's loop must not
 * process that token again. On BadValue, `args[index]` is the token to name in the error message.
 * The two failures are separated because they deserve different sentences: a word after the
 * switch is a typo, while a switch at the very end of the command line has no count at all, and
 * reporting that one as `got '--threads'` would name the wrong token.
 */
inline ThreadSwitch take_thread_switch( const std::vector<std::string> & args,
                                        std::size_t & index,
                                        unsigned & value )
{
	std::string lowered = args[index];

	for( char & c : lowered ) {
		if( c >= 'A' && c <= 'Z' ) {
			c = static_cast<char>( c - 'A' + 'a' );
		}
	}

	const std::string long_switch = "--threads";

	std::string text;

	if( lowered == long_switch || lowered == "-j" ) {
		if( index + 1 >= args.size() ) {
			return ThreadSwitch::MissingValue;   // the switch is there, its value is not
		}

		text = args[++index];
	} else if( lowered.rfind( long_switch + "=", 0 ) == 0 ) {
		text = args[index].substr( long_switch.size() + 1 );
	} else if( lowered.size() > 2 && lowered.rfind( "-j", 0 ) == 0 ) {
		text = args[index].substr( 2 );
	} else {
		return ThreadSwitch::NotThreads;
	}

	if( !parse_thread_count( text, &value ) ) {
		return ThreadSwitch::BadValue;
	}

	return ThreadSwitch::Accepted;
}

} // namespace cli
