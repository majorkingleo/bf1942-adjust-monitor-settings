/**
 * class for printing debugging messages async to stdout
 * @author Copyright (c) 2023 Martin Oberzalek
 */
#pragma once
#include <OutDebug.h>
#include <ColoredOutput.h>
#include <FastDelivery.h>
#include <variant>
#include <mutex>
#include <semaphore>
#include <chrono>

namespace AsyncOut {

struct Data
{
	const char 							   *file;
	unsigned 								line;
	const char 							   *function;
	std::variant<std::string,std::wstring> 	message;
	Tools::ColoredOutput::Color 			color;
	std::wstring 							prefix;	
	std::chrono::utc_clock::time_point 		when = std::chrono::utc_clock::now();
};

class Logger : public Tools::FastDelivery::PublisherNode<Data>, public Tools::OutDebug
{
	std::list<value_type> 	messages;
	std::mutex 				m_messages;
	std::binary_semaphore 	m_worktodo{0};
public:

	// called async from Debug publisher.
	void deliver( const value_type & msg );

	virtual void log();

	// wait for data
	void wait();
	void wait_for( std::chrono::steady_clock::duration timeout );	

protected:
	std::list<value_type> popAll();
};

class Debug : public Tools::OutDebug, public Tools::FastDelivery::Publisher<Data,Logger>
{
public:
	Debug( Tools::ColoredOutput::Color color = Tools::ColoredOutput::BRIGHT_YELLOW );

#ifdef __cpp_lib_string_view
  virtual void add( const char *file, unsigned line, const char *function, const std::string_view & s ) override;
  virtual void add( const char *file, unsigned line, const char *function, const std::wstring_view & s ) override;
#else
  virtual void add( const char *file, unsigned line, const char *function, const std::string & s ) override;
  virtual void add( const char *file, unsigned line, const char *function, const std::wstring & s ) override;
#endif

};

} // namespace AsyncOut
