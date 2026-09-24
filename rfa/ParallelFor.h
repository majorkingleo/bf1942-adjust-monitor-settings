/**
 * A tiny work-queue parallel_for.
 *
 * The whole threading policy of this project in twenty lines, shared rather than copied: both
 * the writer and the unpacker need exactly this and nothing more.
 *
 * The `worker` id exists because two pieces of state cannot be shared between threads - the
 * LZO compressor work buffer (448 KB, and the compressor is not reentrant) and the archive
 * read handle behind `PayloadReader`. Each caller gives every worker its own instance and
 * indexes them by that id.
 *
 * Contract for callers: a worker must write its result to a slot indexed by `i`, never append
 * it in completion order. Otherwise the output starts to depend on scheduling, which for this
 * project means archives that stop being byte-identical from one run to the next.
 *
 * @author Copyright (c) 2026
 */

#pragma once

#include <atomic>
#include <cstddef>
#include <thread>
#include <vector>

namespace rfa {

/**
 * Run `fn(i, worker)` for every i in [0, count), across `threads` workers.
 *
 * Runs inline on the calling thread when there is nothing to spread - one worker asked for, or
 * one item to hand out - so the single-threaded path costs nothing, including no thread
 * creation.
 *
 * `Fn` is a template parameter rather than a `std::function`: the callable is a lambda at every
 * call site, and type erasure would buy nothing while adding an indirect call per work item.
 * (C++23's `std::move_only_function` would avoid a copy, not the indirection.)
 *
 * `std::jthread` rather than `std::thread` so the pool joins itself: if handing out work threw,
 * every worker would still be joined by the vector's destructor instead of detaching and
 * racing the return.
 */
template< typename Fn >
void parallel_for( std::size_t count, unsigned threads, const Fn & fn )
{
	if( count == 0 ) {
		return;
	}

	if( threads <= 1 || count == 1 ) {
		for( std::size_t i = 0; i < count; ++i ) {
			fn( i, 0 );
		}
		return;
	}

	std::atomic<std::size_t> next( 0 );

	std::vector<std::jthread> pool;
	pool.reserve( threads );

	for( unsigned worker = 0; worker < threads; ++worker ) {
		pool.emplace_back( [&, worker] {
			for( ;; ) {
				const std::size_t i = next.fetch_add( 1 );
				if( i >= count ) {
					break;
				}
				fn( i, worker );
			}
		} );
	}
}

} // namespace rfa
