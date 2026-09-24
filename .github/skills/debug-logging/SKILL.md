---
name: debug-logging
description: 'Add debug messages to a cpputils-based C++ tool and route them into a log file. USE FOR: instrumenting code with CPPDEBUG / Tools::x_debug, choosing between the synchronous OutDebug frontend and the asynchronous AsyncOut one, adding a timestamped file backend (AsyncOut::FileLogger), wiring the logger thread into main(), adding cpputils/io to an autotools build. DO NOT USE FOR: the .rfa archive format or its tools (use rfa-unpack / bf1942-standalone-map); a throwaway printf that never leaves the console.'
---

# Debug messages and the log file

Analysis of `examples/lotr_analyzer`, which is the working reference. Two layers, deliberately
separate: how a message is **emitted**, and how it is **delivered** to a destination.

```
CPPDEBUG( Tools::format( ... ) )            <- call site, cpputils/io/CpputilsDebug.h
        |
        v   Tools::x_debug->add( file, line, function, message )
   Tools::Debug                             <- abstract interface, 4 add() overloads
        |
        +-- Tools::OutDebug                 -> prints straight to std::cout (sync, no threads)
        |
        +-- AsyncOut::Debug                 -> wraps into Data, calls distribute() (async)
                    |
                    v   Tools::FastDelivery::Publisher<Data, Logger>   (fan-out)
              +-----+-------+
              |             |
     AsyncOut::Logger   AsyncOut::FileLogger
     std::cout, colour  ofstream, timestamps
```

## When to use

- **Sync `Tools::OutDebug`** — single-threaded tool, or a short-lived helper. Prints immediately,
  correct `file:line`, no threads, no shutdown dance.
- **Async `AsyncOut::Debug`** — anything with worker threads, or when the same messages must go to
  console *and* a file. The worker never touches `std::cout`/the file; it only locks a list.

## Step 1 — emit a message

```cpp
#include <CpputilsDebug.h>

CPPDEBUG( Tools::format( "mail from '%s' to '%s' subject '%s'", m.from.data, m.to.data, m.subject.data ) );
CPPDEBUG( Tools::wformat( L"Converted body_text_html to plain text: %s", body_text_html ) );
CPPDEBUG( "connected to SMTP server" );     // no formatting needed
```

- Message text comes from `Tools::format` / `Tools::wformat` (cpputilsformat), printf-style.
- The file/line come from the macro (`__FILE__`, `__LINE__`, `__PRETTY_FUNCTION__`). Never add them
  to the message text yourself.
- The macro is a bare `if` statement, not `do { } while(0)` — `if (c) CPPDEBUG(x); else ...` has a
  dangling-else bug. Always brace the surrounding `if`.
- With `NDEBUG` the macro expands to **nothing**. A release build has no debug output at all.

## Step 2 — install a frontend

```cpp
auto log_frontend = new AsyncOut::Debug();   // or: new Tools::OutDebug()
Tools::x_debug = log_frontend;               // must happen before any CPPDEBUG can run
```

`Tools::x_debug` is a plain global (`Tools::Debug*`, initialised to `NULL`). The macro null-checks
it, so calls before installation are **silently dropped, not a crash** — if a log looks like it
starts mid-run, this is usually why.

## Step 3 — the file backend

Copy [AsyncOutDebug.h](./assets/AsyncOutDebug.h), [AsyncOutDebug.cc](./assets/AsyncOutDebug.cc),
[AsyncFileLogger.h](./assets/AsyncFileLogger.h), [AsyncFileLogger.cc](./assets/AsyncFileLogger.cc).

`FileLogger` derives from `AsyncOut::Logger` and overrides only `log()`. It inherits the queue, the
mutex, the semaphore and `popAll()`, so a new backend (rotation, syslog, JSON) is one `log()`.

What it already does:

| Behaviour | Detail |
|---|---|
| Open mode | `std::ios::out \| std::ios::app` — appends, never truncates across restarts |
| Open failure | throws `STDERR_EXCEPTION( Tools::format( "cannot open log file '%s'", filename ) )` |
| Line format | `[YYYY-MM-DD HH:MM:SS.mmm] basename:line message` — `%S` prints the fractional part whenever the time point is finer than seconds |
| Timestamp | `Data.when` is stamped **at the call site**, so order and time are production order, not consumption order |
| Timezone | `TZ` env var via `std::chrono::locate_zone`, else `std::chrono::current_zone()` |
| Wide messages | `Utf8Util::wStringToUtf8` — the **file is always UTF-8**, unlike the console path which goes through `DetectLocale::w2out` and follows the console locale |
| Flushing | public `flush()`; the thread calls it on a timer, not per line |

## Step 4 — wire the thread into `main()`

Full snippet: [main-wiring.cc](./assets/main-wiring.cc). The shape is:

```cpp
if( !cfg.LogFile.value.empty() ) {
    std::thread( [&]( auto log_frontend ) {
        auto wait_for_object = APP.get_wait_for_object_handle();   // keeps "logger alive" true

        try {
            AsyncOut::FileLogger backend( cfg.LogFile.value );
            log_frontend->subscribe( &backend );

            do {
                backend.log();                                       // drain the queue
                backend.wait_for( std::chrono::milliseconds(200) );  // then idle up to 200 ms
                if( /* 3 s elapsed */ ) backend.flush();
            } while( !APP.quit_request );

            backend.log();                                           // final drain
        } catch( std::exception & err ) { std::cerr << err.what() << std::endl; }
    }, log_frontend ).detach();
}
```

Four ordering rules, all load-bearing:

1. `Tools::x_debug` is assigned **before** the config is read and before any worker starts.
2. `backend` is a **stack local of the lambda**; `subscribe()` keeps only a pointer to it. The
   final `backend.log()` must be inside that scope — `~PublisherNode()` unsubscribes on the way out,
   and anything logged after that goes nowhere.
3. Because the thread is `detach()`ed, the exit path must set a quit flag and then **wait for the
   drain**: `APP.quit_request = true; while( APP.has_active_wait_for_objects() ) sleep(100ms);`
   The `WaitForObject` token from `get_wait_for_object_handle()` is what reports "still running".
4. Both backends subscribe to the **same** frontend, so file and console are fed independently;
   either can be absent without touching the other.

Whether the log file is enabled is a **config decision**, not a CLI flag (in the reference:
`ConfigSectionGlobal` with `CONFIG_SIMPLE_DECLARE_STR( LogFile )` from `~/.lotr-analyzer.ini`).
Empty path ⇒ no thread at all. The console backend is behind `-debug`.

## Step 5 — build wiring

The example builds a fourth cpputils library for this; this repo currently does not have it. Add:

```make
AM_CPPFLAGS += -I$(top_srcdir)/cpputils/io

noinst_LIBRARIES += cpputils/io/libcpputilsio.a

cpputils_io_libcpputilsio_a_SOURCES = \
		cpputils/io/ColoredOutput.h \
		cpputils/io/ColoredOutput.cc \
		cpputils/io/CpputilsDebug.h \
		cpputils/io/CpputilsDebug.cc \
		cpputils/io/DetectLocale.h \
		cpputils/io/DetectLocale.cc \
		cpputils/io/OutDebug.h \
		cpputils/io/OutDebug.cc
```

and put `cpputils/io/libcpputilsio.a` into the `LDADD` of every target that logs, plus

```make
LIBS += -liconv
```

`read_file.cc` implements `ReadFile::convert` with `iconv`, and `DetectLocale` calls it to
translate between the console encoding and UTF-8. Once `DetectLocale` is pulled in — and
`OutDebug` pulls it in — the link needs it. Without it:
`undefined reference to `iconv_open'`. On the Cygwin mingw-w64 sysroot `libiconv.a`
exists, and with `-static` the linker prefers it over `libiconv.dll.a`. Requirements:

- **C++20 or newer.** `std::binary_semaphore`, `std::chrono::utc_clock`, `std::chrono::locate_zone`
  and `std::format` are all used. (The reference uses `-std=gnu++23`, this repo `-std=c++20`.)
- `-static` on mingw, otherwise the binary wants `libstdc++-6.dll` at run time.
- `tools_config.h` must stay in the repo root: `cpputils/io/DetectLocale.h` includes it as
  `"../../tools_config.h"`, and if that file defines `DISABLE_CPPUTILS_READFILE`, `DetectLocale` —
  and with it all of `OutDebug` — compiles to nothing.
- `locate_zone`/`current_zone` need a timezone database on the target.

## Pitfalls

| # | Trap | Consequence |
|---|---|---|
| 1 | `NDEBUG` defined | `CPPDEBUG` expands to nothing — "my debug output disappeared in the release build" |
| 2 | messages emitted before `x_debug` is set | dropped silently, the macro only null-checks |
| 3 | **`FastDelivery::Publisher::distribute()` skips a node it cannot CAS** | with concurrent producers, messages are **lost**. Measured: 2 producer threads, 400 messages, slow subscriber → **200 received, 200 dropped**. Serialise the fan-out if every line matters |
| 4 | `unsubscribe()` does not exist in the cpputils revision the reference pins (`3918380`) | there you can only unregister by destroying the node; this repo's cpputils (`4efaca0`) added `unsubscribe()` and a proper skip path |
| 5 | backend destroyed while messages are still in flight | `subscribe()` stores a raw pointer; only `~PublisherNode()` unregisters |
| 6 | `detach()` without the quit/wait handshake | the last queued lines die with the process |
| 7 | expecting `file:line` in the file backend | it prints basename + line, `prefix` is console-only, and `function` is captured in `Data` but never printed by either backend |
| 8 | assuming a whole-second timestamp | `%S` prints the fractional part when the time point is finer than seconds, so the real line is `[2026-09-24 15:00:18.491]` and its width varies |
| 9 | `-std=c++20` missing | `<semaphore>`, `utc_clock`, `std::format` unavailable |

## Files

- [assets/AsyncOutDebug.h](./assets/AsyncOutDebug.h), [assets/AsyncOutDebug.cc](./assets/AsyncOutDebug.cc) — frontend `AsyncOut::Debug` + backend base `AsyncOut::Logger`
- [assets/AsyncFileLogger.h](./assets/AsyncFileLogger.h), [assets/AsyncFileLogger.cc](./assets/AsyncFileLogger.cc) — the file backend
- [assets/main-wiring.cc](./assets/main-wiring.cc) — the two thread blocks plus shutdown
- [references/internals.md](./references/internals.md) — data flow, why each design choice, and where the seams are
