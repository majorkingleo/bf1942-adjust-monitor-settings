/**
 * Minimal test framework for this project.
 *
 * The shape mirrors majorkingleo/cpputilstest (common/TestUtils.h): a testcase is a
 * small object carrying a name and an expected result, so the runner can print a table
 * and report pass/fail without any framework-wide state or macros.
 *
 * Deliberately dependency-free (standard library only). This has to build in the same
 * environments the tools do, including a Cygwin -> mingw cross build, and the upstream
 * harness pulls in ColoredOutput/Arg/OutDebug from cpputils/io, which this project does
 * not currently build.
 *
 * @author Copyright (c) 2026
 */

#ifndef TESTCOMMON_TESTUTILS_H_
#define TESTCOMMON_TESTUTILS_H_

#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <vector>

/**
 * Base class of every testcase.
 *
 * run() returns the observed result, which the runner compares against expected_result.
 * When throws_exception is set the expectation becomes "throws an exception" instead of
 * "returns the expected value".
 */
template<class RESULT>
class TestCaseBase
{
protected:
	std::string name;
	bool throws_exception;
	const RESULT expected_result;

public:
	TestCaseBase( const std::string & name_, const RESULT & expected_result_, bool throws_exception_ = false )
	: name( name_ ),
	  throws_exception( throws_exception_ ),
	  expected_result( expected_result_ )
	{}

	virtual ~TestCaseBase() = default;

	virtual RESULT run() = 0;

	const std::string & getName() const { return name; }
	bool throwsException() const { return throws_exception; }
	RESULT getExpectedResult() const { return expected_result; }
};

/**
 * Feeds one input to one predicate and expects a boolean result.
 */
template<class t_std_string=std::string>
class TestCaseFuncBool : public TestCaseBase<bool>
{
	typedef std::function<bool(const t_std_string & a)> Func;

	Func func;
	const t_std_string input;

public:
	TestCaseFuncBool( const std::string & name,
	                  const t_std_string & input_,
	                  bool expected_result_,
	                  Func func_ )
	: TestCaseBase<bool>( name, expected_result_ ),
	  func( func_ ),
	  input( input_ )
	{}

	bool run() override
	{
		return func( input );
	}
};

/**
 * Compares two values through a caller-supplied predicate.
 */
template<class t_std_string=std::string>
class TestCaseFuncEqual : public TestCaseBase<bool>
{
	typedef std::function<bool(const t_std_string & a, const t_std_string & b)> Func;

	Func func;
	const t_std_string input;
	const t_std_string output;

public:
	TestCaseFuncEqual( const std::string & name,
	                   const t_std_string & input_,
	                   const t_std_string & output_,
	                   Func func_ )
	: TestCaseBase<bool>( name, true ),
	  func( func_ ),
	  input( input_ ),
	  output( output_ )
	{}

	bool run() override
	{
		return func( input, output );
	}
};

/**
 * Calls a nullary predicate.
 */
class TestCaseFuncNoInp : public TestCaseBase<bool>
{
	typedef std::function<bool()> Func;

	Func func;

public:
	TestCaseFuncNoInp( const std::string & name,
	                   bool expected_result_,
	                   Func func_,
	                   bool throws_exception_ = false )
	: TestCaseBase<bool>( name, expected_result_, throws_exception_ ),
	  func( func_ )
	{}

	bool run() override
	{
		return func();
	}
};

/**
 * Hands the testcase its own scratch file name.
 *
 * The file is deleted before run() so each execution starts clean, and the name is
 * derived from the testcase name (".<name>.txt"), matching the upstream harness.
 */
class TestCaseFuncOneFile : public TestCaseBase<bool>
{
	typedef std::function<bool( const std::string & file )> Func;

	Func func;
	std::ios_base::openmode openmode;

public:
	TestCaseFuncOneFile( const std::string & name,
	                     Func func_,
	                     std::ios_base::openmode openmode_ )
	: TestCaseBase<bool>( name, true ),
	  func( func_ ),
	  openmode( openmode_ )
	{}

	bool run() override;
};

typedef std::shared_ptr<TestCaseBase<bool>> TestCasePtr;
typedef std::vector<TestCasePtr> TestCases;

#endif /* TESTCOMMON_TESTUTILS_H_ */
