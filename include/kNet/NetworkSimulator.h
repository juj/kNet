/* Copyright The kNet Project.

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

/** @file NetworkSimulator.h
	@brief The NetworkSimulator class, which enables different network conditions testing. */

#include "kNetFwd.h"
#include "PolledTimer.h"
#include <vector>

namespace kNet
{

/// A NetworkSimulator is attached to MessageConnections to add in an intermediate layer for
/// network conditions testing.
class NetworkSimulator
{
public:
	NetworkSimulator();
	~NetworkSimulator();

	/// If false, the network simulator is not being used.
	/// By default, this is always false.
	bool enabled;

	/// Specifies the percentage of messages to drop. This is in the range [0.0, 1.0]. Default: 0.
	float packetLossRate;

	/// Specifies a constant delay to add to each packet (msecs). Default: 0.
	float constantPacketSendDelay;

	/// Specifies an amount of uniformly random delay to add to each packet (msecs), [0, uniformRandomPacketSendDelay].  Default: 0.
	float uniformRandomPacketSendDelay;

	void SubmitSendBuffer(OverlappedTransferBuffer *buffer);

	/// Runs a polled update tick on the network simulator. Transfers all expired data.
	void Process();

	/// Discards and frees all currently queued messages.
	void Free();

private:
	struct QueuedBuffer
	{
		OverlappedTransferBuffer *buffer;

		/// Stores how long to delay this buffer until transfer.
		PolledTimer timeUntilTransfer;
	};
	std::vector<QueuedBuffer> queuedBuffers;

	MessageConnection *owner;

	friend class MessageConnection;
};

} // ~kNet

