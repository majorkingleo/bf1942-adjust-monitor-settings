/*
 * Wiring a frontend plus one backend thread, extracted from
 * examples/lotr_analyzer/src/main.cc (main(), right after the config file
 * is read and before any worker thread starts).
 *
 * Two things matter here and neither is obvious:
 *   - Tools::x_debug must be installed BEFORE anything can call CPPDEBUG.
 *   - `backend` is a stack local of the thread lambda, and Publisher::subscribe()
 *     stores only a pointer to it. ~PublisherNode() unsubscribes, so the final
 *     backend.log() must run before the lambda returns.
 */

auto log_frontend = new AsyncOut::Debug();
Tools::x_debug = log_frontend;

const ConfigSectionGlobal & cfg_global = Configfile2::get(ConfigSectionGlobal::KEY);

// --- backend 1: the log file. Opt-in: no LogFile configured, no thread. ---
if( !cfg_global.LogFile.value.empty() ) {
	std::thread( [&cfg_global,&co]( auto log_frontend ) {

		// keeps a flag true while this thread lives, so the main path can wait
		// for the last drain via APP.has_active_wait_for_objects()
		auto wait_for_object = APP.get_wait_for_object_handle();

		try {

			AsyncOut::FileLogger backend( cfg_global.LogFile.value );
			log_frontend->subscribe(&backend);

			const auto flush_interval = 3s;

			auto next_flush = std::chrono::steady_clock::now() + flush_interval;

			do {
				backend.log();                                          // drain what is queued
				backend.wait_for( std::chrono::milliseconds(200) );     // then idle up to 200 ms

				if( std::chrono::steady_clock::now() > next_flush ) {
					backend.flush();
					next_flush = std::chrono::steady_clock::now() + flush_interval;
				}
			} while( !APP.quit_request );

			backend.log();   // final drain, still inside the scope that owns `backend`

		} catch( std::exception & err ) {
			std::cerr << co.bad("error in file logger thread: ") << err.what() << std::endl;
		}

	}, log_frontend ).detach();
}

// --- backend 2: the console, only with -debug. Same frontend, so both are fed. ---
if( o_debug.getState() ) {
	std::thread( [&co]( auto log_frontend ) {

		auto wait_for_object = APP.get_wait_for_object_handle();

		try {
			AsyncOut::Logger backend;
			log_frontend->subscribe(&backend);

			do {
				backend.log();
				backend.wait_for( std::chrono::milliseconds(200) );
			} while( !APP.quit_request );

			backend.log();

		} catch( std::exception & err ) {
			std::cerr << co.bad("error in debug logger thread: ") << err.what() << std::endl;
		}
	}, log_frontend ).detach();
}

// --- shutdown: ask, then wait for the detached threads to drain ---
APP.quit_request = true;
std::this_thread::yield();

while( APP.has_active_wait_for_objects() ) {
	std::this_thread::sleep_for(100ms);
}
