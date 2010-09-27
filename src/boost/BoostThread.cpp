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

/** @file BoostThread.cpp
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
{
}

Thread::~Thread()
{
	Stop();
}

bool Thread::ShouldQuit() const
{ 
	boost::this_thread::interruption_point();
	return thread.interruption_requested();
}

bool Thread::IsRunning() const
{ 
	return thread.get_id() != boost::thread::id();
}

void Thread::Stop()
{
	thread.interrupt();
	thread.join();
}

void Thread::StartThread()
{
	if (IsRunning())
		Stop();

	thread = boost::thread(boost::ref(*invoker));
}

} // ~kNet
