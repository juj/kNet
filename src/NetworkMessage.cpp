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

/** @file NetworkMessage.cpp
	@brief Represents a serializable network message. */

#include "kNet/DebugMemoryLeakCheck.h"
#include "kNet/NetworkMessage.h"

namespace kNet
{

NetworkMessage::NetworkMessage()
:messageNumber(0),
reliableMessageNumber(0),
sendCount(0),
fragmentIndex(0),
dataCapacity(0),
dataSize(0),
data(0),
contentID(0),
obsolete(false),
priority(0),
transfer(0)
{
}

void NetworkMessage::Resize(size_t newBytes, bool discard)
{
	// Remember how much data is actually being used.
	dataSize = newBytes;

	if (newBytes <= dataCapacity)
		return; // No need to reallocate, we can fit the requested amount of bytes.

	char *newData = new char[newBytes];
	if (!discard)
		memcpy(newData, data, dataCapacity);

	delete[] data;
	data = newData;
	dataCapacity = newBytes;
}

} // ~kNet
