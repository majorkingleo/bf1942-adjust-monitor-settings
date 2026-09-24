/**
 * How many CPUs this process may actually use.
 *
 * Deliberately not `std::thread::hardware_concurrency()` on its own. That reports the CPUs the
 * MACHINE has, which is not the same question: on a machine with more than 64 logical
 * processors the Windows API it is built on reports one processor group, and under `taskset`
 * or a cgroup CPU quota the machine has CPUs this process may not touch. Both matter here
 * because the number is used to decide how many worker threads to start.
 *
 * Both platform paths are implemented now, so a Linux port does not have to rediscover the
 * distinction - see CpuCount.cc, which is the only file in this library that includes a
 * platform header.
 *
 * ⚠️ This is not rfa-specific. It lives here because the writer is its only consumer at the
 * moment; if a second one appears it belongs in `libcommon/` (or a `platform/` directory of
 * its own) rather than being duplicated.
 *
 * @author Copyright (c) 2026
 */

#pragma once

namespace rfa {

/// Number of CPUs available to this process. Always at least 1, never 0: callers use it as a
/// thread count, and every fallback in the chain has to end somewhere.
unsigned usable_cpu_count();

} // namespace rfa


