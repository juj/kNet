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

/** @file W32Thread.cpp
	@brief */

#include <cassert>
#include <exception>

#include "kNet/Thread.h"
#include "kNet/NetworkLogging.h"
#include "kNet/Clock.h"
#include "kNet/NetException.h"

namespace kNet
{

Thread::Thread()
:threadHandle(NULL),
threadEnabled(false),
invoker(0)
{
}

Thread::~Thread()
{
	Stop();
}

bool Thread::ShouldQuit() const { return threadHandle == NULL || threadEnabled == false; }

bool Thread::IsRunning() const
{ 
	if (threadHandle == NULL)
		return false;

	DWORD exitCode = 0;
	BOOL result = GetExitCodeThread(threadHandle, &exitCode);

	if (result == 0)
	{
		LOGNET("Warning: Received error %d from GetExitCodeThread in Thread::IsRunning!", GetLastError());
		return false;
	}

	return exitCode == STILL_ACTIVE;
}

void Thread::Stop()
{
	if (threadHandle == NULL)
		return;

	// Signal that the thread should quit now.
	threadEnabled = false;

	kNet::Clock::Sleep(10);
	assert(threadHandle != 0);

	int numTries = 100;
	while(numTries-- > 0)
	{
		DWORD exitCode = 0;
		BOOL result = GetExitCodeThread(threadHandle, &exitCode);

		if (result == 0)
		{
			LOGNET("Warning: Received error %d from GetExitCodeThread in Thread::Stop()!", GetLastError());
			break;
		}
		else if (exitCode != STILL_ACTIVE)
		{
			CloseHandle(threadHandle);
			break;
		}
		kNet::Clock::Sleep(50);
	}

	if (threadHandle != NULL)
	{
		TerminateThread(threadHandle, -1);
		CloseHandle(threadHandle);
		LOGNET("Warning: Had to forcibly terminate thread!");
	}

	LOGNET("Thread::Stop() called.");

	threadHandle = NULL;

	delete invoker;
	invoker = 0;
}

DWORD WINAPI ThreadEntryPoint(LPVOID lpParameter)
{
	LOGNET("ThreadEntryPoint: Thread started with param 0x%08X.", lpParameter);

	Thread *thread = reinterpret_cast<Thread*>(lpParameter);
	if (!thread)
	{
		LOGNET("Invalid thread start parameter 0!");
		return -1;
	}
	thread->_ThreadRun();

	return 0;
}

void Thread::_ThreadRun()
{
	try
	{
		if (!threadEnabled)
		{
			LOGNET("ThreadEntryPoint: Thread immediately requested to quit.");
			return;
		}

		invoker->Invoke();
	} catch(NetException &e)
	{
		LOGNET("NetException thrown in thread: %s.", e.what());
	} catch(std::exception &e)
	{
		LOGNET("std::exception thrown in thread: %s.", e.what());
	} catch(...)
	{
		LOGNET("Unknown exception thrown in thread.");
	}
}

void Thread::StartThread()
{
	if (threadHandle != NULL)
		return;

	threadEnabled = true;
	threadHandle = CreateThread(NULL, 0, ThreadEntryPoint, this, 0, NULL);
	if (threadHandle == NULL)
		throw NetException("Failed to create thread!");
	else
		LOGNET("Thread::Run(): Thread created.");
}

void Thread::Sleep(int msecs)
{
	///\todo Allow interruption between sleep.
	Clock::Sleep(msecs);
}

} // ~kNet
