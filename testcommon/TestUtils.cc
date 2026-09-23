/**
 * Testcase helpers that need a translation unit of their own.
 *
 * @author Copyright (c) 2026
 */

#include "TestUtils.h"

#include <cstdio>
#include <filesystem>

bool TestCaseFuncOneFile::run()
{
	std::string file_name = "." + name + ".txt";

	std::error_code ec;
	std::filesystem::remove( file_name, ec );

	const bool result = func( file_name );

	// The scratch file is an implementation detail; do not leave litter in the repo root.
	std::filesystem::remove( file_name, ec );

	return result;
}
