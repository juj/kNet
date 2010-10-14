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

/** @file UnixEvent.cpp
	@brief */

#include <cassert>

#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/eventfd.h>
#include <errno.h>
#include <string.h>

#include "kNet/Event.h"
#include "kNet/NetworkLogging.h"

namespace kNet
{

Event::Event()
:fd(-1)
{
}

Event::Event(int /*SOCKET*/ fd_, EventWaitType eventType)
:type(eventType), fd(fd_)
{
}

Event CreateNewEvent(EventWaitType type)
{
	Event e;
	e.Create(type);
	assert(e.IsValid());
	return e;
}

void Event::Create(EventWaitType type_)
{
	type = type_;

	// eventfd() reference: see http://linux.die.net/man/2/eventfd
	fd = eventfd(0, 0);
	if (fd == -1)
	{
		LOGNET("Error in Event::Create: %s(%d)!", strerror(errno), errno);
		return;
	}

	int ret = fcntl(fd, F_SETFL, O_NONBLOCK);
	if (ret == -1)
	{
		LOGNET("fcntl call failed: %s(%d)!", strerror(errno), errno);
		return;
	}

	///\todo Remove these, just a bit of immediate-mode testing here.
	assert(Test() == false);
	assert(Test() == false);
	Set();
	assert(Test() == true);
	assert(Test() == true);
	Reset();
	assert(Test() == false);
	assert(Test() == false);
	Set();
	assert(Test() == true);
	assert(Test() == true);
	Reset();
	assert(Test() == false);
	assert(Test() == false);

	///\todo Return success or failure.
}

void Event::Close()
{
	if (fd != -1)
	{
		close(fd);
		fd = -1;
	}
}

bool Event::IsNull() const
{
	return fd == -1;
}

void Event::Reset()
{
	if (IsNull())
	{
		LOGNET("Event::Reset() failed! Tried to reset an uninitialized Event!");
		return;
	}

	eventfd_t val = 0;
	int ret = eventfd_read(fd, &val);
	if (ret == -1 && errno != EAGAIN)
		LOGNET("Event::Reset() eventfd_read() failed: %s(%d)!", strerror(errno), errno);
}

void Event::Set()
{
	if (IsNull())
	{
		LOGNET("Event::Set() failed! Tried to set an uninitialized Event!");
		return;
	}

	int ret = eventfd_write(fd, 1);
	if (ret == -1)
	{
		LOGNET("Event::Set() eventfd_write() failed: %s(%d)!", strerror(errno), errno);
		return;
	}
}

bool Event::Test() const
{
	if (IsNull())
	{
		LOG(LogError, "Event::Test() failed! Tried to test an uninitialized Event!");
		return false;
	}

	if (type == EventWaitSignal)
	{
		eventfd_t val = 0;
		int ret = eventfd_read(fd, &val);
		if (ret == -1 && errno != EAGAIN)
		{
			LOG(LogError, "Event::Test() eventfd_read() failed: %s(%d)!", strerror(errno), errno);
			return false;
		}
		if (val != 0)
		{
			int ret = eventfd_write(fd, 1);
			if (ret == -1)
				LOG(LogError, "Event::Test() eventfd_write() failed: %s(%d)!", strerror(errno), errno);
		}
		return val != 0;
	}
	else
	{
		return Wait(0);
	}
}

/// Returns true if the event was set during this time, or false if timout occurred.
bool Event::Wait(unsigned long msecs) const
{
	if (IsNull())
	{
		LOGNET("Event::Wait() failed! Tried to wait on an uninitialized Event!");
		return false;
	}

	fd_set fds;
	timeval tv;
	tv.tv_sec = msecs / 1000;
	tv.tv_usec = (msecs - tv.tv_sec * 1000) * 1000;

	FD_ZERO(&fds);
	FD_SET(fd, &fds);
	// Wait on a read state, since http://linux.die.net/man/2/eventfd :
	// "The file descriptor is readable if the counter has a value greater than 0."
	int ret = select(fd+1, &fds, NULL, NULL, &tv); // http://linux.die.net/man/2/select
	if (ret == -1)
	{
		LOGNET("Event::Wait: select() failed on an eventfd: %s(%d)!", strerror(errno), errno);
		return false;
	}

	return ret != 0;
}

bool Event::IsValid() const
{
	return !IsNull();
}

} // ~kNet
