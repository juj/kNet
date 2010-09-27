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

/** @file UnixEventArray.cpp
	@brief */

#include <cassert>
#include <utility>

#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "kNet/EventArray.h"
#include "kNet/NetworkLogging.h"

using namespace std;

namespace kNet
{

EventArray::EventArray()
:numAdded(0)
{
}

int EventArray::Size() const
{
	return cachedEvents.size();
}

void EventArray::Clear()
{
	FD_ZERO(&readfds);
	FD_ZERO(&writefds);
	nfds = -1;
}

void EventArray::AddEvent(const Event &e)
{
	if (e.IsNull())
	{
		LOGNET("Error: Tried to add a null event to event array at index %d!", numAdded);
		return;
	}
	assert(numAdded < maxEvents);

	switch(e.Type())
	{
	case EventWaitRead:
	case EventWaitSignal:
		FD_SET(e.fd, &readfds);
		nfds = max(nfds, e.fd+1);
		break;
	case EventWaitWrite:
		FD_SET(e.fd, &writefds);
		nfds = max(nfds, e.fd+1);
	default:
		break;
	}

	// No need to add dummy events to select(), but need to add them to the cached events list to keep
	// the indices matching.
	cachedEvents.push_back(e);
}

int EventArray::Wait(int msecs)
{
	if (numAdded == 0)
		return WaitFailed;

	tv.tv_sec = msecs / 1000;
	tv.tv_usec = (msecs - tv.tv_sec * 1000) * 1000;

	// Wait on a read state, since http://linux.die.net/man/2/eventfd :
	// "The file descriptor is readable if the counter has a value greater than 0."
	int ret = select(nfds, &readfds, &writefds, NULL, &tv); // http://linux.die.net/man/2/select
	if (ret == -1)
	{
		LOGNET1("EventArray::Wait: select() failed on an eventfd!");
		return WaitFailed;
	}

	if (ret == 0)
		return WaitTimedOut;

	for(int i = 0; i < cachedEvents.size(); ++i)
		if (cachedEvents[i].Test())
		{
			return i;
		}

	LOGNET1("EventArray::Wait error! No events were set, but select() returned a positive value!");
	return WaitFailed;
}

} // ~kNet
