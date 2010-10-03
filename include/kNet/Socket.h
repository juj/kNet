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

/** @file Socket.h
	@brief The Socket class. */

#ifdef WIN32

#include "win32/WS2Include.h"
#define KNET_EWOULDBLOCK WSAEWOULDBLOCK
#define KNET_SOCKET_ERROR SOCKET_ERROR
#define KNET_ACCEPT_FAILURE SOCKET_ERROR

namespace kNet
{
typedef int socklen_t;
}

#else

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>
#define INVALID_SOCKET (0)
#define KNET_SOCKET_ERROR (-1)
#define KNET_ACCEPT_FAILURE (-1)
#define KNET_EWOULDBLOCK EWOULDBLOCK
#define closesocket close
#define TIMEVAL timeval
#define SD_SEND SHUT_WR
#define SD_BOTH SHUT_RDWR

namespace kNet
{
typedef unsigned int SOCKET;
}
#endif

#include <vector>
#include <list>

#include "SharedPtr.h"
#include "EndPoint.h"
#include "WaitFreeQueue.h"
#include "Event.h"

namespace kNet
{

/// Identifiers for the possible bottom-level tranport layers.
enum SocketTransportLayer
{
	SocketOverUDP,
	SocketOverTCP
};

typedef int OverlappedTransferTag;

#ifdef WIN32
typedef WSABUF kNetBuffer;
#else
struct kNetBuffer
{
	/// Specifies the number of bytes allocated to buf. This is the maximum amount of bytes that can
	/// be written to buf.
	unsigned long len;

	char *buf;
};
#endif

struct OverlappedTransferBuffer
{
	kNetBuffer buffer;
#ifdef WIN32
	WSAOVERLAPPED overlapped;
#endif

	/// Specifies the number of bytes buffer.buf actually contains.
	int bytesContains;

	sockaddr_in from;
	socklen_t fromLen;
};

/// Represents a low-level network socket.
class Socket : public RefCountable
{
public:
	Socket();

	Socket(SOCKET connection, const char *address, unsigned short port, SocketTransportLayer transport, size_t maxSendSize, bool isUdpServerSocket);

	Socket(const Socket &);
	~Socket();

	void operator=(const Socket &);

	/// Sets the underlying socket send buffer (SO_SNDBUF) size.
	void SetSendBufferSize(int bytes);
	/// Sets the underlying socket receive buffer (SO_RCVBUF) size.
	void SetReceiveBufferSize(int bytes);

	/// Returns the current value for the send buffer of this socket.
	int SendBufferSize() const;
	/// Returns the current value for the receive buffer of this socket.
	int ReceiveBufferSize() const;

	/// Returns true if the connection is both write-open AND read-open.
	bool Connected() const;
	/// Returns true if the connection is open for reading from. This does not mean that there necessarily is new data
	/// to be immediately read - it only means that the socket is open for receiving data.
	bool IsReadOpen() const { return IsOverlappedReceiveReady() || readOpen; }
	/// Returns true if the connection is open for writing to.
	bool IsWriteOpen() const { return writeOpen; }

	/// If this function returns true, this socket represents a UDP server socket instance. For a UDP server,
	/// the data for all clients is received through this same socket, and there are no individual sockets created for
	/// each new connection, like is done with TCP.
	bool IsUDPServerSocket() const { return isUdpServerSocket; }

	/// Performs an immediate write and read close on the socket, without waiting for the connection to gracefully shut down.
	void Close();

	/// Performs a write close operation on the socket, signalling the other end that no more data will be sent. Any data
	/// currently left in the send buffers will be sent over to the other side, but no new data can be sent. The connection
	/// will remain half-open for reading, and Receive() calls may still be made to the socket. When a read returns 0, the
	/// connection will be transitioned to bidirectionally closed state (Connected() will return false).
	void Disconnect();

	/// Sends the given data through the socket. This function may only be called if Socket::IsWriteOpen() returns true. If
	/// the socket is not write-open, calls to this function will fail.
	/// This function is an orthogonal API to the overlapped IO Send routines. Do not mix these API calls when doing
	/// networking, but instead choose one preferred method and consistently use it.
	bool Send(const char *data, size_t numBytes);

	/// Starts the sending of new data. After having filled the data to send to the OverlappedTransferBuffer that is
	/// returned here, commit the send by calling EndSend. If you have called BeginSend, but decide not to send any data,
	/// call AbortSend instead (otherwise memory will leak).
	/// @return A transfer buffer where the data to send is to be filled in. If no new data can be sent at this time,
	///         this function returns 0.
	OverlappedTransferBuffer *BeginSend();
	/// Finishes and queues up the given transfer that was created with a call to BeginSend.
	/// @return True if send succeeded, false otherwise. In either case, the ownership of the passed buffer send
	///         is taken by this Socket and may not be accessed anymore. Discard the pointer after calling this function.
	bool EndSend(OverlappedTransferBuffer *send);
	/// Cancels the sending of data. Call this function if you first call BeginSend, but decide you don't want to send the data.
	/// This frees the given buffer, do not dereference it after calling this function.
	void AbortSend(OverlappedTransferBuffer *send);

#ifdef WIN32
	/// Returns the number of sends in the send queue.
	int NumOverlappedSendsInProgress() const { return queuedSendBuffers.Size(); }
	/// Returns the maximum number of sends that can be queued up simultaneously.
	int MaxOverlappedSendsInProgress() const { return queuedSendBuffers.Capacity(); }
#endif
	/// Returns true if it is possible to send out new data. In this case, a call to BeginSend will succeed.
	bool IsOverlappedSendReady();
	/// Returns the event object that should be waited on to receive a notification when it is possible to send out new data.
	Event GetOverlappedSendEvent();

	/// Waits for the max given amount of time for new data to be received from the socket.
	/// @return True if new data was received, false if the timeout period elapsed.
	bool WaitForData(int msecs);

	/// Reads in data from the socket. If there is no data available, this function will not block, but will immediately
	/// return 0.
	/// This function issues an immediate recv() call to the socket and is not compatible with the Overlapped Transfer API
	/// above. Do not mix the use of these two APIs, but pick one method to use and stay with it.
	/// @param endPoint [out] If the socket is an UDP socket that is not bound to an address, this will contain the source address.
	/// @return The number of bytes that were successfully read.
	size_t Receive(char *dst, size_t maxBytes, EndPoint *endPoint = 0);

	/// Call to receive new data from the socket.
	/// @return A buffer that contains the data, or 0 if no new data was available. When you are finished reading the buffer, call
	///         EndReceive to free up the buffer, or memory will leak.
	OverlappedTransferBuffer *BeginReceive();
	/// Finishes a read operation on the socket. Frees the given buffer to be re-queued for a future socket read operation.
	void EndReceive(OverlappedTransferBuffer *buffer);
#ifdef WIN32
	/// Returns the number of receive buffers that have been queued for the socket.
	int NumOverlappedReceivesInProgress() const { return queuedReceiveBuffers.Size(); }
	/// Returns the maximum number of receive buffers that can be queued for the socket.
	int MaxOverlappedReceivesInProgress() const { return queuedReceiveBuffers.Capacity(); }
#endif
	/// Returns true if there is new data to be read in. In that case, BeginReceive() will not return 0.
	bool IsOverlappedReceiveReady() const;
	/// Returns the event object that will be notified whenever data is available to be read from the socket.
	Event GetOverlappedReceiveEvent();

	/// Returns which transport layer the connection is using. This value is either SocketOverUDP or SocketOverTCP.
	SocketTransportLayer TransportLayer() const { return transport; }

	/// Returns the maximum amount of bytes that can be sent through to the network in one call. If you try sending
	/// more data than specified by this value, the result is undefined.
	size_t MaxSendSize() const { return maxSendSize; }

	const char *DestinationAddress() const { return destinationAddress.c_str(); }
	unsigned short DestinationPort() const { return destinationPort; }

	/// Returns the local port that this socket is bound to.
	unsigned short LocalPort() const;

	/// Returns the EndPoint this socket is connected to.
	EndPoint GetEndPoint() const;

	/// Returns a human-readable representation of this socket, specifying the peer address and port this socket is
	/// connected to.
	std::string ToString() const;

	void SetUDPPeername(const sockaddr_in &peer) { udpPeerName = peer; }

	/// Sets the socket to blocking or nonblocking state.
	void SetBlocking(bool isBlocking);

	SOCKET &GetSocketHandle() { return connectSocket; }

private:
	SOCKET connectSocket;
	sockaddr_in udpPeerName;
	std::string destinationAddress;
	unsigned short destinationPort;
	SocketTransportLayer transport;
	size_t maxSendSize;
	bool writeOpen;
	bool readOpen;

	/// UDP server sockets operate in unbound (destination, or peer) mode, since the same socket is used
	/// to send and receive data to and from all client addresses. UDP client sockets
	/// and TCP sockets are always bound to a peer.
	bool isUdpServerSocket;

#ifdef WIN32
	WaitFreeQueue<OverlappedTransferBuffer*> queuedReceiveBuffers;
	WaitFreeQueue<OverlappedTransferBuffer*> queuedSendBuffers;

	void EnqueueNewReceiveBuffer(OverlappedTransferBuffer *buffer = 0);
#endif
};

} // ~kNet
