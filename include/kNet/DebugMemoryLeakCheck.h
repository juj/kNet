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
#pragma once

/** @file DebugMemoryLeakCheck.h
	@brief Provides overloads of operators new and delete for tracking memory leaks. */

#if defined (WIN32) && defined(_DEBUG) && defined(KNET_MEMORY_LEAK_CHECK)

#include <new>
#include <crtdbg.h>

#ifndef _CRTDBG_MAP_ALLOC
#define _CRTDBG_MAP_ALLOC
#endif

__forceinline static void *operator new(size_t size, const char *file, int line)
{
	return _malloc_dbg(size, _NORMAL_BLOCK, file, line);
}

__forceinline static void *operator new[](size_t size, const char *file, int line)
{
	return _malloc_dbg(size, _NORMAL_BLOCK, file, line);
}

__forceinline static void operator delete(void *ptr, const char *, int)
{
	_free_dbg(ptr, _NORMAL_BLOCK);
}

__forceinline static void operator delete[](void *ptr, const char *, int)
{
	_free_dbg(ptr, _NORMAL_BLOCK);
}

#define new new (__FILE__, __LINE__)

#endif

