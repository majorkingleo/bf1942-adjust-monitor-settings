/**
 * Debug logging for the tools.
 *
 * @author Copyright (c) 2026
 */

#include "DebugLog.h"

#include "AsyncFileLogger.h"
#include "AsyncOutDebug.h"

#include <CpputilsDebug.h>
#include <stderr_exception.h>

#include <chrono>

using namespace Tools;

namespace ToolLog {

namespace {

const char * const LOG_FILE_OPTION = "--log-file";
const char * const DEBUG_OPTION    = "--debug";

/// How long the logger thread sleeps when nothing arrives. Same value the reference uses.
constexpr std::chrono::milliseconds IDLE_TIMEOUT{ 200 };

/// The file is flushed on a timer, not per line.
constexpr std::chrono::seconds FLUSH_INTERVAL{ 3 };

} // namespace

std::string log_file_from_argv( int argc, char ** argv )
{
	for( int i = 1; i < argc; ++i ) {

		const std::string arg = argv[i] ? argv[i] : "";

		if( arg == LOG_FILE_OPTION ) {

			if( i + 1 >= argc || !argv[i+1] || !*argv[i+1] ) {
				throw STDERR_EXCEPTION( std::string( LOG_FILE_OPTION ) + " needs a path" );
			}

			return argv[i + 1];
		}

		const std::string with_equal = std::string( LOG_FILE_OPTION ) + "=";

		if( arg.rfind( with_equal, 0 ) == 0 ) {

			const std::string value = arg.substr( with_equal.size() );

			if( value.empty() ) {
				throw STDERR_EXCEPTION( std::string( LOG_FILE_OPTION ) + " needs a path" );
			}

			return value;
		}
	}

	return std::string();
}

bool debug_flag_from_argv( int argc, char ** argv )
{
	for( int i = 1; i < argc; ++i ) {
		if( argv[i] && std::string( argv[i] ) == DEBUG_OPTION ) {
			return true;
		}
	}

	return false;
}

Session::Session( const std::string & log_file, bool console )
{
	// Backends first: if the log file cannot be opened this throws while nothing is
	// installed yet, so Tools::x_debug can never end up pointing at a dead object.
	if( !log_file.empty() ) {
		m_file_backend = std::make_unique<AsyncOut::FileLogger>( log_file );
	}

	if( console ) {
		m_console_backend = std::make_unique<AsyncOut::Logger>();
	}

	m_frontend = std::make_unique<AsyncOut::Debug>();

	if( m_file_backend ) {
		m_frontend->subscribe( m_file_backend.get() );
	}

	if( m_console_backend ) {
		m_frontend->subscribe( m_console_backend.get() );
	}

	// From here on CPPDEBUG has somewhere to go.
	Tools::x_debug = m_frontend.get();

	if( m_file_backend || m_console_backend ) {
		m_thread = std::thread( [this]() { run(); } );
	}
}

Session::~Session()
{
	m_quit = true;

	if( m_thread.joinable() ) {
		m_thread.join();
	}

	// Stop any further CPPDEBUG before the subscriber list is dismantled.
	Tools::x_debug = nullptr;

	// Destroying a backend unsubscribes it (~PublisherNode) and closes the log file,
	// which flushes it. The frontend must outlive both.
	m_file_backend.reset();
	m_console_backend.reset();

	m_frontend.reset();
}

void Session::run()
{
	// Every backend's semaphore is released by every message, so waking up on one of
	// them is enough to know that something arrived.
	AsyncOut::Logger * const primary = m_file_backend
	                                   ? static_cast<AsyncOut::Logger *>( m_file_backend.get() )
	                                   : m_console_backend.get();

	auto next_flush = std::chrono::steady_clock::now() + FLUSH_INTERVAL;

	while( !m_quit ) {

		if( m_console_backend ) {
			m_console_backend->log();
		}

		if( m_file_backend ) {
			m_file_backend->log();
		}

		if( std::chrono::steady_clock::now() > next_flush ) {

			if( m_file_backend ) {
				m_file_backend->flush();
			}

			next_flush = std::chrono::steady_clock::now() + FLUSH_INTERVAL;
		}

		if( primary ) {
			primary->wait_for( IDLE_TIMEOUT );
		} else {
			std::this_thread::sleep_for( IDLE_TIMEOUT );
		}
	}

	// Final drain, while the backends are still alive.
	if( m_console_backend ) {
		m_console_backend->log();
	}

	if( m_file_backend ) {
		m_file_backend->log();
	}
}

} // namespace ToolLog
