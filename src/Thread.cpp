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

/** @file Thread.cpp
	@brief Implements platform-generic Thread functions. */

#include "kNet/Event.h" ///\todo Investigate the inclusion chain of these two files. Is this #include necessary?
#include "kNet/NetworkLogging.h"
#include "kNet/Thread.h"
#include "kNet/PolledTimer.h"

namespace kNet
{

/// Suspends the thread until 'Resume()' is called. Call this function from the main thread.
void Thread::Hold()
{
	if (threadHoldEvent.Test())
		return;

	threadResumeEvent.Reset();
	threadHoldEvent.Reset();
	threadHoldEventAcked.Reset();

	threadHoldEvent.Set();

	PolledTimer timer;
	while(IsRunning())
	{
		bool success = threadHoldEventAcked.Wait(1000);
		if (success)
			break;
	}
	LOG(LogWaits, "Thread::Hold: Took %f msecs.", timer.MSecsElapsed());
}

/// Resumes the thread that is being held.
void Thread::Resume()
{
	threadResumeEvent.Set();
	threadHoldEvent.Reset();
	threadHoldEventAcked.Reset();
}

void Thread::CheckHold()
{
	if (threadHoldEvent.Test())
	{
		LOG(LogVerbose, "Thread::CheckHold(): suspending thread. this: %p.", this);

		PolledTimer timer;
		while(!ShouldQuit())
		{
			threadHoldEventAcked.Set();
			bool success = threadResumeEvent.Wait(1000);
			if (success)
				break;
		}
		LOG(LogWaits, "Thread::CheckHold: Slept for %f msecs.", timer.MSecsElapsed());
		threadHoldEventAcked.Reset();
	}
}

} // ~kNet
