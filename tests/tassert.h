/* Copyright 2010 Jukka Jylänki

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License. */

/** @file tassert.h
	@brief */
#pragma once

#include <iostream>
#include <sstream>
#include <string.h>

#include "kNet/NetException.h"

/** Starts a test block that tracks and catches all failures inside.
	To start a test block, call TEST("testname");
	(The last semicolon is optional.)

	For tests to be operational, compile the framework with the "Testrun" configuration, which makes
	all assert()'s to throw. (C++ exceptions must be enabled in the project)

	Each test block must be matched by an ENDTEST(); or ENDTESTEXPECTFAILURE(); statement. */
#define TEST(msg) \
	{ \
		const char *testName = msg; \
		try \
		{ \
			{

/// Ends a test block.
#define ENDTEST() \
			} \
			std::clog << "Success: \"" << testName << "\"." << std::endl; \
		} catch(const std::exception &e) \
		{ \
		std::cerr << " Failed: \"" << testName << "\"" << std::endl << "     " << e.what() << std::endl; \
		} catch(...) \
		{ \
		std::cerr << " Failed: \"" << testName << "\"" << std::endl << "     Unknown exception." << std::endl; \
		} \
	}

/** Ends a test block that is expected to fail. For example, this could be used
	to try out "horrible" things like allocating several terabytes on a custom memory allocator, and check
	that they fail as they should. */
#define ENDTESTEXPECTFAILURE() \
			} \
			std::clog << " Failed: \"" << testName << "\"" << std::endl << "     Expected an exception to be thrown." << std::endl; \
		} catch(const std::exception &e) \
		{ \
		std::cerr << "Success: \"" << testName << "\": Threw an exception as expected, got \"" << e.what() << "\"."<< std::endl; \
		} catch(...) \
		{ \
		std::cerr << "Success: \"" << testName << "\": Threw an exception as expected." << std::endl; \
		} \
	}

/// Equivalent to assert(x == y), but gives out nicer information on failure.
template<typename T>
void assertEqualsTest(const T &x, const T &y, const char *str)
{
	if (x != y)
	{
		std::stringstream ss;
		ss << "Failed expression \"" << str << "\": \"" << x << "\" != \"" << y << "\"";
		char str[512];
		strncpy(str, ss.str().c_str(), 512);
		throw kNet::NetException(str);
	}
}

/** Instead of assert(x == y), you can use assertEquals(x, y), and instead of assert failure "x == y", you'll
	get assert failure "Failed expression x==y: 10 != 14", which is a lot more informative. */
#define assertEquals(x, y) assertEqualsTest(x, y, #x "==" #y);

