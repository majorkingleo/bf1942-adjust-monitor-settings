/**
 * See CpuCount.h for why this exists instead of a bare hardware_concurrency() call.
 *
 * @author Copyright (c) 2026
 */

#include "CpuCount.h"

#include <bit>
#include <thread>

#if defined(_WIN32)
	// Both must precede <windows.h>, not follow it. windows.h defines min and max as macros
	// unless NOMINMAX is set, and a macro min would rewrite every std::min in this file into
	// nonsense - the failure looks like a syntax error inside <algorithm>, nowhere near here.
	#ifndef NOMINMAX
		#define NOMINMAX
	#endif
	#ifndef WIN32_LEAN_AND_MEAN
		#define WIN32_LEAN_AND_MEAN
	#endif
	#include <windows.h>
#else
	#include <unistd.h>

	#if defined(__linux__)
		// Conditional on __linux__ because that is the only thing asked for the affinity API
		// below: a platform without <sched.h> should not have to have one.
		#include <sched.h>
	#endif
#endif

/// A feature test, not a platform test. `sched_getaffinity` is a Linux call, and `CPU_ISSET`
/// only exists where glibc exposed the cpu_set_t API - which needs _GNU_SOURCE, set for us by
/// g++. Cygwin, for one, ships <sched.h> with neither, and this is false there.
#if defined(__linux__) && defined(CPU_SETSIZE) && defined(CPU_ISSET)
	#define RFA_HAS_CPU_AFFINITY 1
#else
	#define RFA_HAS_CPU_AFFINITY 0
#endif

namespace rfa {

namespace {

#if RFA_HAS_CPU_AFFINITY

/// Count the CPUs an affinity mask allows. Returns 0 when the call fails, which the caller
/// reads as "no answer" rather than "no CPUs" - the fallbacks behind it are the right answer.
unsigned affinity_cpu_count()
{
	cpu_set_t set;

	if( sched_getaffinity( 0, sizeof( set ), &set ) != 0 ) {
		return 0;
	}

	unsigned count = 0;

	// CPU_ISSET is counted by hand rather than using CPU_COUNT, because CPU_COUNT is a glibc
	// extension while CPU_ISSET is POSIX.1-2008 - and this file is meant to survive the move
	// to a Linux toolchain without a rewrite.
	for( int cpu = 0; cpu < CPU_SETSIZE; ++cpu ) {
		if( CPU_ISSET( cpu, &set ) ) {
			++count;
		}
	}

	return count;
}

#endif

} // namespace

unsigned usable_cpu_count()
{
#if defined(_WIN32)
	// The process's own affinity mask is the honest answer on Windows. It is what
	// `start /affinity` and Task Manager's "Set affinity" narrow, and it is the only reading
	// that stays correct past 64 logical processors, where GetSystemInfo() reports just the
	// current processor group. The mask is a DWORD_PTR, so it cannot describe more than 64
	// CPUs either - which is the same ceiling, not a new restriction.
	DWORD_PTR process_mask = 0;
	DWORD_PTR system_mask = 0;

	if( GetProcessAffinityMask( GetCurrentProcess(), &process_mask, &system_mask )
	    && process_mask != 0 ) {
		// std::popcount rather than a hand-rolled shift loop: C++20 has it, and every target
		// this builds for turns it into a POPCNT instruction.
		return (unsigned)std::popcount( process_mask );
	}

	SYSTEM_INFO info;
	GetSystemInfo( &info );

	if( info.dwNumberOfProcessors > 0 ) {
		return (unsigned)info.dwNumberOfProcessors;
	}
#else
	#if RFA_HAS_CPU_AFFINITY
		// Asked BEFORE sysconf(_SC_NPROCESSORS_ONLN), because the two answer different
		// questions: sysconf counts the machine's online CPUs and ignores both `taskset` and a
		// cgroup CPU quota, while the affinity mask says which CPUs this process may run on.
		{
			const unsigned allowed = affinity_cpu_count();

			if( allowed > 0 ) {
				return allowed;
			}
		}
	#endif

	{
		const long online = sysconf( _SC_NPROCESSORS_ONLN );

		if( online > 0 ) {
			return (unsigned)online;
		}
	}
#endif

	// Portable fallback, and the only path on a platform neither branch above knows.
	const unsigned hardware = std::thread::hardware_concurrency();

	return hardware > 0 ? hardware : 1u;
}

} // namespace rfa
