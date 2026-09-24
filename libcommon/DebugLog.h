/**
 * Debug logging for the tools: one frontend, optional file and console backends.
 *
 * The mechanism is the one in examples/lotr_analyzer, documented in
 * .github/skills/debug-logging/. One thing is deliberately different: the reference
 * starts its logger threads in main(), detaches them, and coordinates shutdown with an
 * APP.quit_request flag plus a wait-for-object poll. This session owns the thread and
 * joins it in the destructor instead, so a tool only has to keep one object alive and
 * gets a guaranteed final drain with no global state to remember.
 *
 * Usage:
 *
 *     int main( int argc, char ** argv )
 *     {
 *         try {
 *             ToolLog::Session log( ToolLog::log_file_from_argv( argc, argv ),
 *                                   ToolLog::debug_flag_from_argv( argc, argv ) );
 *             CPPDEBUG( Tools::format( "..." ) );
 *             ...
 *
 * @author Copyright (c) 2026
 */

#ifndef LIBCOMMON_DEBUGLOG_H_
#define LIBCOMMON_DEBUGLOG_H_

#include <atomic>
#include <memory>
#include <string>
#include <thread>

namespace AsyncOut {
	class Debug;
	class Logger;
	class FileLogger;
}

namespace ToolLog {

/**
 * Reads --log-file <path> or --log-file=<path> from argv.
 *
 * Returns an empty string when the option is absent. Throws when the option is present
 * without a value - quietly logging nowhere would be worse than a loud error. Any other
 * argument is ignored, so a tool that never looked at argv before keeps working.
 */
std::string log_file_from_argv( int argc, char ** argv );

/// True when --debug is present, which adds the console backend.
bool debug_flag_from_argv( int argc, char ** argv );

/**
 * Installs the debug frontend as Tools::x_debug and runs the backend loop.
 *
 * The backends are created before anything global is touched, so a log file that cannot
 * be opened throws without leaving a half-installed frontend behind. The destructor
 * stops the thread, drains it, clears Tools::x_debug and only then tears the subscriber
 * list down - in that order, because the publisher holds raw pointers to the backends.
 */
class Session
{
	std::unique_ptr<AsyncOut::Debug>       m_frontend;
	std::unique_ptr<AsyncOut::Logger>      m_console_backend;
	std::unique_ptr<AsyncOut::FileLogger>  m_file_backend;
	std::thread                            m_thread;
	std::atomic<bool>                      m_quit{ false };

public:
	explicit Session( const std::string & log_file = std::string(),
	                  bool console = false );

	~Session();

	Session( const Session & ) = delete;
	Session & operator=( const Session & ) = delete;

private:
	void run();
};

} // namespace ToolLog

#endif /* LIBCOMMON_DEBUGLOG_H_ */
