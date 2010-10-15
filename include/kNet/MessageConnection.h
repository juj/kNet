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

/** @file MessageConnection.h
	@brief The MessageConnection and ConnectionStatistics classes. */

#include <vector>
#include <map>
#include <utility>
#include <set>

#include "WaitFreeQueue.h"
#include "LockFreePoolAllocator.h"
#include "Lockable.h"
#include "Socket.h"
#include "IMessageHandler.h"
#include "BasicSerializedDataTypes.h"
#include "Datagram.h"
#include "FragmentedTransferManager.h"
#include "NetworkMessage.h"
#include "Event.h"
#include "DataSerializer.h"
#include "DataDeserializer.h"

#include "MaxHeap.h"
#include "Clock.h"
#include "PolledTimer.h"

namespace kNet
{

class MessageConnection;
class UDPMessageConnection;
class TCPMessageConnection;
class NetworkServer;
class Network;
class NetworkWorkerThread;
class FragmentedSendManager;

#ifdef WIN32
struct FragmentedSendManager::FragmentedTransfer;
#endif

/// Stores information about an established MessageConnection.
struct ConnectionStatistics
{
	/// Remembers a ping request that was sent to the other end.
	struct PingTrack
	{
		tick_t pingSentTick;  ///< Timestamp of when the PingRequest was sent.
		tick_t pingReplyTick; ///< If replyReceived==true, contains the timestamp of when PingReply was received as a response.
		unsigned long pingID;      ///< ID of this ping message.
		bool replyReceived;        ///< True of PingReply has already been received for this.
	};
	/// Contains an entry for each recently performed Ping operation, sorted by age (oldest first).
	std::vector<PingTrack> ping;

	/// Remembers both in- and outbound traffic events on the socket.
	struct TrafficTrack
	{
		tick_t tick;          ///< Denotes when this event occurred.
		unsigned long packetsIn;   ///< The number of datagrams in when this event occurred.
		unsigned long packetsOut;  ///< The number of datagrams out when this event occurred.
		unsigned long messagesIn;  ///< The total number of messages the received datagrams contained.
		unsigned long messagesOut; ///< The total number of messages the sent datagrams contained.
		unsigned long bytesIn;     ///< The total number of bytes the received datagrams contained.
		unsigned long bytesOut;    ///< The total number of bytes the sent datagrams contained. 
	};
	/// Contains an entry for each recent traffic event (data in/out) on the connection, sorted by age (oldest first).
	std::vector<TrafficTrack> traffic;

	/// Remembers the send/receive time of a datagram with a certain ID.
	struct DatagramIDTrack
	{
		tick_t tick;
		packet_id_t packetID;
	};
	/// Contains an entry for each recently received packet, sorted by age (oldest first).
	std::vector<DatagramIDTrack> recvPacketIDs;
};

/// Comparison object that sorts the two messages by their priority (higher priority/smaller number first).
class NetworkMessagePriorityCmp
{
public:
	int operator ()(const NetworkMessage *a, const NetworkMessage *b)
	{
		assert(a && b);
		if (a->priority < b->priority) return -1;
		if (b->priority < a->priority) return 1;

		if (a->MessageNumber() < b->MessageNumber()) return 1;
		if (b->MessageNumber() < a->MessageNumber()) return -1;

		return 0;
	}
};

/// Represents the current state of the connection.
enum ConnectionState
{
	ConnectionPending, ///< Waiting for the other end to send an acknowledgement packet to form the connection. No messages may yet be sent or received at this state.
	ConnectionOK,      ///< The connection is bidirectionally open, for both reading and writing. (readOpen=true, writeOpen=true)
	ConnectionDisconnecting, ///< We are closing the connection down. Cannot send any more messages, but can still receive. (readOpen=true, writeOpen=false)
	ConnectionPeerClosed, ///< The other end has closed the connection. No new messages will be received, but can still send messages. (readOpen=false, writeOpen=true)
	ConnectionClosed    ///< The socket is no longer open. A MessageConnection object in this state cannot be reused to open a new connection, but a new connection object must be created.
};

/// Returns a textual representation of a ConnectionState.
std::string ConnectionStateToString(ConnectionState state);

/// Represents a single established network connection. MessageConnection maintains its own worker thread that manages
/// connection control, the scheduling and prioritization of outbound messages, and receiving inbound messages.
class MessageConnection : public RefCountable
{
public:
	virtual ~MessageConnection();

	/// Returns the current connection state.
	ConnectionState GetConnectionState() const;

	/// Returns true if the peer has signalled it will not send any more data (the connection is half-closed or full-closed).
	bool IsReadOpen() const;
	
	/// Returns true if we have signalled not to send any more data (the connection is half-closed or full-closed).
	bool IsWriteOpen() const;

	/// Returns true if the connection is in the ConnectionPending state and waiting for the other end to resolve/establish the connection. 
	/// When this function returns false, the connection may be half-open, bidirectionally open, timed out on ConnectionPending, or closed.
	bool IsPending() const;

	/// Returns true if this socket is connected, i.e. at least half-open in one way.
	bool Connected() const { return IsReadOpen() || IsWriteOpen(); }

	/// Runs a modal processing loop and produces events for all inbound received data.
	void RunModalClient();

	/// Blocks for the given amount of time until the connection has transitioned away from ConnectionPending state.
	/// @param maxMSecstoWait A positive value that indicates the maximum time to wait until returning.
	/// @return If the connection was successfully opened, this function returns true. Otherwise returns false, and
	///         either timeout was encountered and the other end has not acknowledged the connection,
	///         or the connection is in ConnectionClosed state.
	bool WaitToEstablishConnection(int maxMSecsToWait = 500);

	/// Starts a benign disconnect procedure. Transitions ConnectionState to ConnectionDisconnecting. This 
	/// function will block until the given period expires or the other end acknowledges and also closes 
	/// down the connection. Currently no guarantee is given for whether previous reliable messages will 
	/// safely reach the destination. To ensure this, do a manual wait to flush the outbound message queue 
	/// before disconnecting.
	/// @param maxMSecsToWait A positive number that indicates the maximum time to wait for a disconnect
	///                       acknowledgement message until returning.
	///                       If 0 is passed, the function will send the Disconnect message and return immediately.
	/// When this function returns, the connection may either be in ConnectionClosing or ConnectionClosed
	/// state, depending on whether the other end has already acknowledged the disconnection.
	/// \note You may not call this function in middle of StartNewMessage() - EndAndQueueMessage() function calls.
	void Disconnect(int maxMSecsToWait = 500);

	/// Starts a forceful disconnect procedure.
	/// @param maxMSecsToWait If a positive number, Disconnect message will be sent to the peer and if no response
	///                       is received in the given time period, the connection is forcefully closed.
	///                       If 0, no Disconnect message will be sent at all, but the connection is torn down
	///                       and the function returns immediately. The other end will remain hanging and will timeout.
	/// When this function returns, the connection is in ConnectionClosed state.
	/// \note You may not call this function in middle of StartNewMessage() - EndAndQueueMessage() function calls.
	void Close(int maxMSecsToWait = 500);

	/// Stores all the statistics about the current connection.
	Lockable<ConnectionStatistics> stats;

	// There are 3 ways to send messages through a MessageConnection:
	// StartNewMessage/EndAndQueueMessage, SendStruct, and Send. See below.

	/// Start building a new message with the given ID.
	/// @param id The ID for the message you will be sending.
	/// @param numBytes The number of bytes the body of this message will be. This function will pre-allocate the
	///                 NetworkMessage::data field to hold at least that many bytes (Capacity() can also return a larger value).
	///                 This number only needs to be an estimate, since you can later on call NetworkMessage::Reserve()
	///                 to reallocate the message memory. If you pass in the default value 0, no pre-allocation will be performed.
	/// @return The NetworkMessage object that represents the new message to be built. This message is dynamically allocated
	///         from an internal pool of NetworkMessage blocks. For each NetworkMessage pointer obtained, call
	///         EndAndQueueMessage when you have finished building the message to commit the network send and to release the memory.
	///         Alternatively, if after calling StartNewMessage, you decide to abort the network send, free up the NetworkMessage
	///         by calling this->FreeMessage().
	NetworkMessage *StartNewMessage(unsigned long id, size_t numBytes = 0);

	/// Finishes building the message and submits it to the outbound send queue.
	/// @param msg The message to send. After calling this function, this pointer should be considered freed and may not be
	///            dereferenced or passed to any other member function of this MessageConnection. Only pass in here 
	///            NetworkMessage pointers obtained by a call to StartNewMessage() of the same MessageConnection instance.
	/// @param numBytes Specify here the number of actual bytes you filled in into the msg.data field. A size of 0
	///                 is valid, and can be used in cases the message ID itself is the whole message. Passing in the default 
	///                 value of this parameter will use the size value that was specified in the call to StartNewMessage().
	/// @param internalQueue If true, specifies that this message was submitted from the network worker thread and not the application
	///                 thread. Pass in the value 'false' here in the client application, or there is a chance of a race condition.
	void EndAndQueueMessage(NetworkMessage *msg, size_t numBytes = (size_t)(-1), bool internalQueue = false);

	/// This is a conveniency function to access the above StartNewMessage/EndAndQueueMessage pair. The performance of this
	/// function call is not as good, since a memcpy of the message will need to be made. For performance-critical messages,
	/// it is better to craft the message directly into the buffer area provided by StartNewMessage.
	void SendMessage(unsigned long id, bool reliable, bool inOrder, unsigned long priority, unsigned long contentID, 
	                 const char *data, size_t numBytes);

	/// Sends a message using a serializable structure.
	template<typename SerializableData>
	void SendStruct(const SerializableData &data, unsigned long id, bool inOrder, 
		bool reliable, unsigned long priority, unsigned long contentID = 0);

	/// Sends a message using a compiled message structure.
	template<typename SerializableMessage>
	void Send(const SerializableMessage &data, unsigned long contentID = 0);

	enum PacketSendResult
	{
		PacketSendOK,
		PacketSendSocketClosed,
		PacketSendSocketFull,
		PacketSendNoMessages,
		PacketSendThrottled   ///< Cannot send just yet, throttle timer is in effect.
	};

	/// Serializes several messages into a single UDP/TCP packet and sends it out to the wire.
	virtual PacketSendResult SendOutPacket() = 0;

	/// Sends out as many packets at one go as is allowed by the current send rate of the connection.
	virtual void SendOutPackets() = 0;

	/// Returns how many milliseconds need to be waited before this socket can try sending data the next time.
	virtual unsigned long TimeUntilCanSendPacket() const = 0;

	void UpdateConnection();
	virtual void DoUpdateConnection() {}

	/// Stops all outbound sends until ResumeOutboundSends is called. Use if you need to guarantee that some messages be sent in the same datagram.
	/// Do not stop outbound sends for long periods, or the other end may time out the connection.
	void PauseOutboundSends();

	/// Resumes sending of outbound messages.
	void ResumeOutboundSends();

	size_t NumInboundMessagesPending() const { return inboundMessageQueue.Size(); }

	/// Returns the number of messages in the outbound queue that are pending to be sent.
	size_t NumOutboundMessagesPending() const { return outboundQueue.Size() + outboundAcceptQueue.Size(); }

	/// Marks that the peer has closed the connection and will not send any more application-level data.
	void SetPeerClosed();

	/// Returns the underlying raw socket.
	Socket *GetSocket() { return socket; }

	/// Returns an object that identifies the endpoint this connection is connected to.
	EndPoint GetEndPoint() const;

	/// Sets an upper limit to the data send rate for this connection.
	/// The default is not to have an upper limit at all.
	/// @param numBytesPerSec The upper limit for the number of bytes to send per second. This limit includes the message header
	///                       bytes as well and not just the payload. Set to 0 to force no limit.
	/// @param numDatagramsPerSec The maximum number of datagrams (UDP packets) to send per second. Set to 0 to force no limit.
	///                       If the connection is operating on top of TCP, this field has no effect.
	///\todo Implement.
	void SetMaximumDataSendRate(int numBytesPerSec, int numDatagramsPerSec);

	/// Registers a new listener object for the events of this connection.
	void RegisterInboundMessageHandler(IMessageHandler *handler) { inboundMessageHandler = handler; }

	/// Fetches all newly received messages waiting in the inbound queue, and passes each of these
	/// to the message handler registered using RegisterInboundMessageHandler.
	/// Call this function periodically to receive new data from the network if you are using the Observer pattern.
	/// Alternatively, use the immediate-mode ReceiveMessage function to receive messages directly one at a time.
	/// @param maxMessageToProcess If the inbound queue contains more than this amount of new messages,
	///                            the processing loop will return to give processing time to other parts of the application.
	///                            If 0 is passed, messages are processed until the queue is empty.
	/// \note It is important to have a non-zero limit in maxMessagesToProcess (unless you're sure what you are doing), since
	///       otherwise an attacker might affect the performance of the application main loop by sending messages so fast that
	///       the queue never has time to exhaust, thus giving an infinite loop in practice.
	void Process(int maxMessagesToProcess = 100);
	
	/// Waits for at most the given amount of time, and returns immediately when a new message is received for processing.
	/// @param maxMSecsToWait If 0, the call will wait indefinitely until a message is received or the connection transitions to
	///                       closing state.
	///                       If a positive value is passed, at most that many milliseconds is waited for a new message to be received.
	void WaitForMessage(int maxMSecsToWait);

	/// Returns the next message in the inbound queue. This is an alternative API to RegisterInboundMessageHandler/Process.
	/// \note When using this function to receive messages, remember to call FreeMessage for each NetworkMessage returned, or you
	/// will have a major size memory leak, fast.
	/// @param maxMSecsToWait If a negative number, the call will not wait at all if there are no new messages to process, but 
	///                       returns 0 immediately.
	///                       If 0, the call will wait indefinitely until a message is received or the connection transitions to
	///                       closing state.
	///                       If a positive value is passed, at most that many milliseconds is waited for a new message to be received.
	/// @return A newly allocated object to the received message, or 0 if the queue was empty and no messages were received during
	///         the wait period, or if the connection transitioned to closing state. When you are finished reading the message,
	///         call FreeMessage for the returned pointer.
	NetworkMessage *ReceiveMessage(int maxMSecsToWait = -1);

	/// Frees up a NetworkMessage struct when it is no longer needed. [Called by both worker and main thread]
	/// You need to call this for each message that you received from a call to ReceiveMessage.
	void FreeMessage(NetworkMessage *msg);
	
	/// Returns the estimated RTT of the connection, in milliseconds. RTT is the time taken to communicate a message from client->host->client.
	float RoundTripTime() const { return rtt; }

	/// Returns the estimated delay time from this connection to host, in milliseconds.
	float Latency() const { return rtt / 2.f; }

	/// Returns the number of milliseconds since we last received data from the socket.
	float LastHeardTime() const { return Clock::TicksToMillisecondsF(Clock::TicksInBetween(Clock::Tick(), lastHeardTime)); }

	float PacketsInPerSec() const { return packetsInPerSec; }
	float PacketsOutPerSec() const { return packetsOutPerSec; }
	float MsgsInPerSec() const { return msgsInPerSec; }
	float MsgsOutPerSec() const { return msgsOutPerSec; }
	float BytesInPerSec() const { return bytesInPerSec; }
	float BytesOutPerSec() const { return bytesOutPerSec; }

	/// Returns a single-line message describing the connection state.
	std::string ToString() const;

	/// Dumps a long multi-line status message of this connection state to stdout.
	void DumpStatus() const;

	virtual void DumpConnectionStatus() const {}

	/// Posted when the application has pushed us some messages to handle.
	Event NewOutboundMessagesEvent() const;

	/// Specifies the result of a Socket read activity.
	enum SocketReadResult
	{
		SocketReadOK,        ///< All data was read from the socket and it is empty for now.
		SocketReadError,     ///< An error occurred - probably the connection is dead.
		SocketReadThrottled, ///< There was so much data to read that we need to pause and make room for sends as well.
	};

	/// Reads all the new bytes available in the socket. [used internally by worker thread]
	/// This data will be read into the connection's internal data queue, where it will be 
	/// parsed to messages.
	/// @param bytesRead [out] This field will get the number of bytes successfully read.
	/// @return The return code of the operation.
	virtual SocketReadResult ReadSocket(size_t &bytesRead) = 0;

	SocketReadResult ReadSocket() { size_t ignored = 0; return ReadSocket(ignored); }

	/// Sets the worker thread object that will handle this connection.
	void SetWorkerThread(NetworkWorkerThread *thread) { workerThread = thread; }

protected:
	/// The Network object inside which this MessageConnection lives. [main thread]
	Network *owner;
	/// If this MessageConnection represents a client connection on the server side, this gives the owner. [main thread]
	NetworkServer *ownerServer;
	/// Stores the thread that manages the background processing of this connection. The same thread can manage multiple
	/// connections and servers, and not just this one.
	NetworkWorkerThread *workerThread;

	/// A queue populated by the main thread to give out messages to the MessageConnection work thread to process.
	/// [produced by main thread, consumed by worker thread]
	WaitFreeQueue<NetworkMessage*> outboundAcceptQueue;

	/// A queue populated by the networking thread to hold all the incoming messages until the application can process them.
	/// [produced by worker thread, consumed by main thread]
	WaitFreeQueue<NetworkMessage*> inboundMessageQueue;

	/// A priority queue to maintain in order all the messages that are going out the pipe.
	///\todo Make the choice of which of the following structures to use a runtime option.
//	MaxHeap<NetworkMessage*, NetworkMessagePriorityCmp> outboundQueue;
	WaitFreeQueue<NetworkMessage*> outboundQueue;

	/// Tracks all the message sends that are fragmented. [worker thread]
	Lockable<FragmentedSendManager> fragmentedSends;

	/// Tracks all the receives of fragmented messages and helps reconstruct the original messages from fragments. [worker thread]
	FragmentedReceiveManager fragmentedReceives;

	/// Allocations of NetworkMessage structures go through a pool to avoid dynamic new/delete calls when sending messages.
	LockFreePoolAllocator<NetworkMessage> messagePool;

	float rtt; ///< The currently estimated round-trip time, in milliseconds.

	PolledTimer pingTimer;
	PolledTimer statsRefreshTimer;

	void HandleInboundMessage(packet_id_t packetID, const char *data, size_t numBytes);

	/// Allocates a new NetworkMessage struct. [both worker and main thread]
	NetworkMessage *AllocateNewMessage();

	// Ping/RTT management operations:
	void SendPingRequestMessage();
	void HandlePingRequestMessage(const char *data, size_t numBytes);
	void HandlePingReplyMessage(const char *data, size_t numBytes);

	// Frees all internal dynamically allocated message data.
	void FreeMessageData();

	/// Checks if the connection has been silent too long and has now timed out.
	void DetectConnectionTimeOut();

	/// Refreshes RTT and other connection related statistics.
	void ComputeStats();
	
	/// Adds a new entry for outbound data statistics.
	void AddOutboundStats(unsigned long numBytes, unsigned long numPackets, unsigned long numMessages);

	/// Adds a new entry for inbound data statistics.
	void AddInboundStats(unsigned long numBytes, unsigned long numPackets, unsigned long numMessages);

	/// Pulls in all new messages from the main thread to the worker thread side and admits them to the send priority queue. [worker thread only]
	void AcceptOutboundMessages();

	/// Starts the socket-specific disconnection procedure.
	virtual void PerformDisconnection() = 0;

	/// The object that receives notifications of all received data.
	IMessageHandler *inboundMessageHandler;

	/// The underlying socket on top of which this connection operates.
	Socket *socket;

	/// Specifies the current connection state.
	ConnectionState connectionState;

	/// If true, all sends to the socket are on hold, until ResumeOutboundSends() is called.
	bool bOutboundSendsPaused;

	friend class NetworkServer;
	friend class Network;

	/// Posted when the application has pushed us some messages to handle.
	Event eventMsgsOutAvailable;

	void operator=(const MessageConnection &); ///< Noncopyable, N/I.
	MessageConnection(const MessageConnection &); ///< Noncopyable, N/I.

	tick_t lastHeardTime; ///< The tick since last successful receive from the socket.	
	float packetsInPerSec; ///< The average number of datagrams we are receiving/second.
	float packetsOutPerSec; ///< The average number of datagrams we are sending/second.
	float msgsInPerSec; ///< The average number of kNet messages we are receiving/second.
	float msgsOutPerSec; ///< The average number of kNet messages we are sending/second.
	float bytesInPerSec; ///< The average number of bytes we are receiving/second. This includes kNet headers.
	float bytesOutPerSec; ///< The average number of bytes we are sending/second. This includes kNet headers.

	/// A running number attached to each outbound message (not present in network stream) to 
	/// break ties when deducing which message should come before which.
	unsigned long outboundMessageNumberCounter;

	/// A running number that is assigned to each outbound reliable message. This is used to
	/// enforce proper ordering of ordered messages.
	unsigned long outboundReliableMessageNumberCounter;

	/// A (messageID, contentID) pair.
	typedef std::pair<u32, u32> MsgContentIDPair;

	typedef std::map<MsgContentIDPair, std::pair<packet_id_t, tick_t> > ContentIDReceiveTrack;

	/// Each (messageID, contentID) pair has a packetID "stamp" associated to them to track 
	/// and decimate out-of-order received obsoleted messages.
	ContentIDReceiveTrack inboundContentIDStamps;

	typedef std::map<MsgContentIDPair, NetworkMessage*> ContentIDSendTrack;

	ContentIDSendTrack outboundContentIDMessages;

	void CheckAndSaveOutboundMessageWithContentID(NetworkMessage *msg);

	void ClearOutboundMessageWithContentID(NetworkMessage *msg);

	/// Checks whether the given (messageID, contentID)-pair is already out-of-date and obsoleted
	/// by a newer packet and should not be processed.
	/// @return True if the packet should be processed (there was no superceding record), and
	///         false if the packet is old and should be discarded.
	bool CheckAndSaveContentIDStamp(u32 messageID, u32 contentID, packet_id_t packetID);

	void SplitAndQueueMessage(NetworkMessage *message, bool internalQueue, size_t maxFragmentSize);

	static const unsigned long MsgIdPingRequest = 0;
	static const unsigned long MsgIdPingReply = 1;
	static const unsigned long MsgIdFlowControlRequest = 2;
	static const unsigned long MsgIdPacketAck = 3;
	static const unsigned long MsgIdDisconnect = 0x3FFFFFFF;
	static const unsigned long MsgIdDisconnectAck = 0x3FFFFFFE;

	/// Private ctor - MessageConnections are instantiated by Network and NetworkServer classes.
	explicit MessageConnection(Network *owner, NetworkServer *ownerServer, Socket *socket, ConnectionState startingState);

	void StartWorkerThread();
	void StopWorkerThread();

	virtual void Initialize() {}

	virtual bool HandleMessage(packet_id_t /*packetID*/, u32 /*messageID*/, const char * /*data*/, size_t /*numBytes*/) { return false; }
};

template<typename SerializableData>
void MessageConnection::SendStruct(const SerializableData &data, unsigned long id, bool inOrder, 
		bool reliable, unsigned long priority, unsigned long contentID)
{
	const size_t dataSize = data.Size();

	NetworkMessage *msg = StartNewMessage(id, dataSize);

	if (dataSize > 0)
	{
		DataSerializer mb(msg->data, dataSize);
		data.SerializeTo(mb);
		assert(mb.BytesFilled() == dataSize); // The SerializableData::Size() estimate must be exact!
	}

	msg->id = id;
	msg->contentID = contentID;
	msg->inOrder = inOrder;
	msg->priority = priority;
	msg->reliable = reliable;

	EndAndQueueMessage(msg);
}

template<typename SerializableMessage>
void MessageConnection::Send(const SerializableMessage &data, unsigned long contentID)
{
	const size_t dataSize = data.Size();

	NetworkMessage *msg = StartNewMessage(data.MessageID(), dataSize);

	if (dataSize > 0)
	{
		DataSerializer mb(msg->data, dataSize);
		data.SerializeTo(mb);
		assert(mb.BytesFilled() == dataSize);
	}

	msg->id = data.MessageID();
	msg->contentID = contentID;
	msg->inOrder = data.inOrder;
	msg->priority = data.priority;
	msg->reliable = data.reliable;

	EndAndQueueMessage(msg);
}

} // ~kNet
