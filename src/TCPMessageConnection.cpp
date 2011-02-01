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

/** @file TCPMessageConnection.cpp
	@brief */

#include "kNet/TCPMessageConnection.h"

#include "kNet/DebugMemoryLeakCheck.h"

#include "kNet/NetworkLogging.h"
#include "kNet/DataSerializer.h"
#include "kNet/DataDeserializer.h"
#include "kNet/VLEPacker.h"
#include "kNet/NetException.h"

namespace kNet
{

/// The maximum size for a TCP message.
static const u32 cMaxTCPMessageSize = 1024 * 1024;

TCPMessageConnection::TCPMessageConnection(Network *owner, NetworkServer *ownerServer, Socket *socket, ConnectionState startingState)
:MessageConnection(owner, ownerServer, socket, startingState),
tcpInboundSocketData(2 * 1024 * 1024)
{
}

MessageConnection::SocketReadResult TCPMessageConnection::ReadSocket(size_t &totalBytesRead)
{
	if (!socket || !socket->IsReadOpen())
		return SocketReadError;

	using namespace std;

	totalBytesRead = 0;

	const int arbitraryInboundMessageCapacityLimit = 1024; ///\todo Estimate a proper limit some way better.
	if (inboundMessageQueue.CapacityLeft() < arbitraryInboundMessageCapacityLimit) 
	{
		LOG(LogVerbose, "TCPMessageConnection::ReadSocket: Read throttled! Application cannot consume data fast enough.");
		return SocketReadThrottled; // Can't read in new data, since the client app can't process it so fast.
	}

	const size_t maxBytesToRead = 256 * 1024; // Only pull in this much at one iteration.

	// Pump the socket's receiving end until it's empty or can't process any more for now.
	while(totalBytesRead < maxBytesToRead)
	{
		// The ring buffer does not handle discontinuous wrap-arounds at the end of the array. When we get to the end,
		// we must compact the space. If no free space at the end, we must compact. Also, compact if we have an opportunity to do it with low cost.
		if (tcpInboundSocketData.ContiguousFreeBytesLeft() < 4096 ||
			(tcpInboundSocketData.Size() <= 32 && tcpInboundSocketData.StartIndex() != 0))
		{		
			tcpInboundSocketData.Compact();
			if (tcpInboundSocketData.ContiguousFreeBytesLeft() == 0)
			{
				LOG(LogError, "Inbound TCP ring buffer full! Cannot receive more data!");
				break;
			}
		}

		// We might potentially fetch too much data we can't handle at this point. If so, don't try to read.
		if (tcpInboundSocketData.ContiguousFreeBytesLeft() < 4096) ///\todo Parameterize.
		{
			LOG(LogError, "tcpInboundSocketData.ContiguousFreeBytesLeft() < 4096!");
			break;
		}

		OverlappedTransferBuffer *buffer = socket->BeginReceive();
		if (!buffer)
			break; // Nothing to receive.

		LOG(LogData, "TCPMessageConnection::ReadSocket: Received %d bytes from the network from peer %s.", 
			buffer->bytesContains, socket->ToString().c_str());

		assert((size_t)buffer->bytesContains <= (size_t)tcpInboundSocketData.ContiguousFreeBytesLeft());
		memcpy(tcpInboundSocketData.End(), buffer->buffer.buf, buffer->bytesContains);
		tcpInboundSocketData.Inserted(buffer->bytesContains);

		totalBytesRead += buffer->bytesContains;
		socket->EndReceive(buffer);
	}

	// Update statistics about the connection.
	if (totalBytesRead > 0)
	{
		lastHeardTime = Clock::Tick();
		AddInboundStats(totalBytesRead, 1, 0);
	}

	// Finally, try to parse any bytes we received to complete messages. Any bytes consisting a partial
	// message will be left into the tcpInboundSocketData partial buffer to wait for more bytes to be received later.
	ExtractMessages();

	if (totalBytesRead >= maxBytesToRead)
		return SocketReadThrottled;
	else
		return SocketReadOK;
}

template<typename T>
bool ContainerUniqueAndNoNullElements(const std::vector<T> &cont)
{
	for(size_t i = 0; i < cont.size(); ++i)
		for(size_t j = i+1; j < cont.size(); ++j)
			if (cont[i] == cont[j] || cont[i] == 0)
				return false;
	return true;
}

/// Packs several messages from the outbound priority queue into a single packet and sends it out the wire.
/// @return False if the send was a failure and sending should not be tried again at this time, true otherwise.
MessageConnection::PacketSendResult TCPMessageConnection::SendOutPacket()
{
	if (bOutboundSendsPaused || outboundQueue.Size() == 0)
		return PacketSendNoMessages;

	if (!socket || !socket->IsWriteOpen())
	{
		LOGNETVERBOSE("TCPMessageConnection::SendOutPacket: Socket is not write open %p!", socket);
		if (connectionState == ConnectionOK) ///\todo This is slightly annoying to manually update the state here,
			connectionState = ConnectionPeerClosed; /// reorganize to be able to have this automatically apply.
		if (connectionState == ConnectionDisconnecting)
			connectionState = ConnectionClosed;
		return PacketSendSocketClosed;
	}

	///\todo For TCP sends, minSendSize==1 sounds like a valid amount to use in all cases. Consider removing this
	/// completely so we do not need to handle unnecessary cases?
	const size_t minSendSize = 1; 
	const size_t maxSendSize = socket->MaxSendSize();
	// Push out all the pending data to the socket.
	assert(ContainerUniqueAndNoNullElements(serializedMessages));
	assert(ContainerUniqueAndNoNullElements(outboundQueue));
	serializedMessages.clear(); // 'serializedMessages' is a temporary data structure used only by this member function.
	OverlappedTransferBuffer *overlappedTransfer = socket->BeginSend();
	if (!overlappedTransfer)
	{
		LOGNET("TCPMessageConnection::SendOutPacket: Starting an overlapped send failed!");
		return PacketSendSocketClosed;
	}

	int numMessagesPacked = 0;
	DataSerializer writer(overlappedTransfer->buffer.buf, overlappedTransfer->buffer.len);
	while(outboundQueue.Size() > 0)
	{
		NetworkMessage *msg = *outboundQueue.Front();

		if (msg->obsolete)
		{
			FreeMessage(msg);
			outboundQueue.PopFront();
			continue;
		}
		const int encodedMsgIdLength = VLE8_16_32::GetEncodedBitLength(msg->id) / 8;
		const size_t messageContentSize = msg->dataSize + encodedMsgIdLength; // 1 byte: Message ID. X bytes: Content.
		const int encodedMsgSizeLength = VLE8_16_32::GetEncodedBitLength(messageContentSize) / 8;
		const size_t totalMessageSize = messageContentSize + encodedMsgSizeLength; // 2 bytes: Content length. X bytes: Content.
		// If this message won't fit into the buffer, send out all previously gathered messages.
		if (writer.BytesFilled() + totalMessageSize >= maxSendSize)
			break;

		writer.AddVLE<VLE8_16_32>(messageContentSize);
		writer.AddVLE<VLE8_16_32>(msg->id);
		if (msg->dataSize > 0)
			writer.AddAlignedByteArray(msg->data, msg->dataSize);
		++numMessagesPacked;

		serializedMessages.push_back(msg);
		assert(*outboundQueue.Front() == msg);
		outboundQueue.PopFront();
	}
	assert(ContainerUniqueAndNoNullElements(serializedMessages));

	if (writer.BytesFilled() == 0 && outboundQueue.Size() > 0)
		LOG(LogError, "Failed to send any messages to socket %s! (Probably next message was too big to fit in the buffer).", socket->ToString().c_str());

	if (writer.BytesFilled() >= minSendSize)
	{
		overlappedTransfer->buffer.len = writer.BytesFilled();
		bool success = socket->EndSend(overlappedTransfer);

		if (!success) // If we failed to send, put all the messages back into the outbound queue to wait for the next send round.
		{
			for(size_t i = 0; i < serializedMessages.size(); ++i)
				outboundQueue.InsertWithResize(serializedMessages[i]);
			assert(ContainerUniqueAndNoNullElements(outboundQueue));

			LOG(LogError, "TCPMessageConnection::SendOutPacket() failed: Could not initiate overlapped transfer!");

			return PacketSendSocketFull;
		}

		LOG(LogData, "TCPMessageConnection::SendOutPacket: Sent %d bytes (%d messages) to peer %s.", writer.BytesFilled(), serializedMessages.size(), socket->ToString().c_str());
		AddOutboundStats(writer.BytesFilled(), 0, numMessagesPacked);

		// The messages in serializedMessages array are now in the TCP driver to handle. It will guarantee
		// delivery if possible, so we can free the messages already.
		for(size_t i = 0; i < serializedMessages.size(); ++i)
			FreeMessage(serializedMessages[i]);

		// Thread-safely clear the eventMsgsOutAvailable event if we don't have any messages to process.
		if (NumOutboundMessagesPending() == 0)
			eventMsgsOutAvailable.Reset();
		if (NumOutboundMessagesPending() > 0)
			eventMsgsOutAvailable.Set();
			
		return PacketSendOK;
	}
	else // Not enough bytes to send out. Put all the messages back in the queue.
	{
		LOG(LogVerbose, "TCPMessageConnection::SendOutPacket(). Not enough bytes to send out (%d).", writer.BytesFilled());

		for(size_t i = 0; i < serializedMessages.size(); ++i)
			outboundQueue.InsertWithResize(serializedMessages[i]);
		assert(ContainerUniqueAndNoNullElements(outboundQueue));

		socket->AbortSend(overlappedTransfer);
		return PacketSendNoMessages;
	}
}

void TCPMessageConnection::SendOutPackets()
{
	if (!socket || !socket->IsWriteOpen() || !socket->IsOverlappedSendReady())
		return;

	PacketSendResult result = PacketSendOK;
	int maxSends = 50; // Place an arbitrary limit to how many packets we will send at a time.
	while(result == PacketSendOK && TimeUntilCanSendPacket() == 0 && maxSends-- > 0)
		result = SendOutPacket();
}

void TCPMessageConnection::ExtractMessages()
{
	try
	{
		size_t numMessagesReceived = 0;
		for(;;)
		{
			if (tcpInboundSocketData.Size() == 0) // No new packets in yet.
				break;

			DataDeserializer reader(tcpInboundSocketData.Begin(), tcpInboundSocketData.Size());
			u32 messageSize = reader.ReadVLE<VLE8_16_32>();
			if (messageSize == DataDeserializer::VLEReadError)
				break; // The packet hasn't yet been streamed in.

			if (messageSize == 0 || messageSize > cMaxTCPMessageSize)
			{
				LOGNET("Received an invalid message size %d! Closing connection!", messageSize);
				if (socket)
					socket->Close();
				connectionState = ConnectionClosed;
				throw NetException("Malformed TCP data! Received an invalid message size!");
			}

			if (reader.BytesLeft() < messageSize)
				break; // We haven't yet received the whole message, have to abort parsing for now and wait for the whole message.

			HandleInboundMessage(0, reader.CurrentData(), messageSize);
			reader.SkipBytes(messageSize);

			assert(reader.BitPos() == 0);
			u32 bytesConsumed = reader.BytePos();

			// Erase the bytes we just processed from the ring buffer.
			tcpInboundSocketData.Consumed(bytesConsumed);

			++numMessagesReceived;
		}
		AddInboundStats(0, 0, numMessagesReceived);
	} catch(const NetException &e)
	{
		LOG(LogError, "TCPMessageConnection::ExtractMessages() caught a networking exception: \"%s\"!", e.what());
	} catch(const std::exception &e)
	{
		LOG(LogError, "TCPMessageConnection::ExtractMessages() caught a std::exception: \"%s\"!", e.what());
	} catch(...)
	{
		LOG(LogError, "TCPMessageConnection::ExtractMessages() caught an unknown exception!");
	}
}

void TCPMessageConnection::PerformDisconnection()
{
	if (socket)
		socket->Disconnect();
}

unsigned long TCPMessageConnection::TimeUntilCanSendPacket() const
{
	// For TCPMessageConnection, this throttling logic is not used. Perhaps will not be used ever, as the
	// TCP driver does all send throttling already.
	return 0;
}

} // ~kNet
