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

/** @file Socket.cpp
	@brief */

#include <string>
#include <cassert>
#include <utility>

#include "kNet/Network.h"

#include "kNet/DebugMemoryLeakCheck.h"

#include "kNet/Socket.h"
#include "kNet/NetworkLogging.h"
#include "kNet/EventArray.h"

using namespace std;

#ifdef LINUX
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#endif

#ifdef WIN32
const int numConcurrentReceiveBuffers = 4;
const int numConcurrentSendBuffers = 4;
#endif

namespace kNet
{

std::string SocketTransportLayerToString(SocketTransportLayer transport)
{
	switch(transport)
	{
	case SocketOverUDP: return "UDP";
	case SocketOverTCP: return "TCP";
	default:
		{
			std::stringstream ss;
			ss << "Invalid SocketTransportLayer (" << (int)transport << ")!";
			return ss.str();
		}
	}
}

std::string SocketTypeToString(SocketType type)
{
	switch(type)
	{
	case ServerListenSocket: return "Server listen socket";
	case ServerClientSocket: return "Server client socket";
	case ClientSocket: return "Client socket";
	default:
		{
			std::stringstream ss;
			ss << "Invalid SocketType (" << (int)type << ")!";
			return ss.str();
		}
	}
}

Socket::Socket()
:connectSocket(INVALID_SOCKET),
writeOpen(false),
readOpen(false),
transport(InvalidTransportLayer),
type(InvalidSocketType)
#ifdef WIN32
,queuedReceiveBuffers(numConcurrentReceiveBuffers)
,queuedSendBuffers(numConcurrentSendBuffers)
#endif
{
	localEndPoint.Reset();
	remoteEndPoint.Reset();
}

Socket::~Socket()
{
	// We cannot Close() the socket here, since the same underlying BSD SOCKET handle can be shared
	// between multiple Socket objects. We rely instead to non-RAII behavior and manually remembering to 
	// Close().
#ifdef WIN32
	FreeOverlappedTransferBuffers();
#endif
}

Socket::Socket(SOCKET connection, const EndPoint &localEndPoint_, const char *localHostName_,
	const EndPoint &remoteEndPoint_, const char *remoteHostName_, 
	SocketTransportLayer transport_, SocketType type_, size_t maxSendSize_)
:connectSocket(connection), localEndPoint(localEndPoint_), localHostName(localHostName_),
remoteEndPoint(remoteEndPoint_), remoteHostName(remoteHostName_), 
transport(transport_), type(type_), maxSendSize(maxSendSize_),
writeOpen(true), readOpen(true)
#ifdef WIN32
,queuedReceiveBuffers(numConcurrentReceiveBuffers)
,queuedSendBuffers(numConcurrentSendBuffers)
#endif
{
	SetSendBufferSize(512 * 1024);
	SetReceiveBufferSize(512 * 1024);
	udpPeerAddress = remoteEndPoint.ToSockAddrIn();
}

Socket::Socket(const Socket &rhs)
#ifdef WIN32
:queuedReceiveBuffers(numConcurrentReceiveBuffers)
,queuedSendBuffers(numConcurrentSendBuffers)
#endif
{
	*this = rhs;
}

Socket &Socket::operator=(const Socket &rhs)
{
#ifdef WIN32
	// We would be losing data if Socket is copied from old to new when data is present in the receive buffers,
	// since the receive buffers are not moved or copied to the new Socket.
	assert(queuedReceiveBuffers.Size() == 0);
#endif

	if (this == &rhs)
		return *this;

	connectSocket = rhs.connectSocket;
	localEndPoint = rhs.localEndPoint;
	localHostName = rhs.localHostName;
	remoteEndPoint = rhs.remoteEndPoint;
	remoteHostName = rhs.remoteHostName;
	transport = rhs.transport;
	type = rhs.type;
	maxSendSize = rhs.maxSendSize;
	writeOpen = rhs.writeOpen;
	readOpen = rhs.readOpen;
	udpPeerAddress = rhs.udpPeerAddress;

	return *this;
}

bool Socket::Connected() const
{
	return connectSocket != INVALID_SOCKET;
}
/*
bool Socket::WaitForData(int msecs)
{
	if (!readOpen)
		return false;

	fd_set readSet;
	FD_ZERO(&readSet);
	FD_SET(connectSocket, &readSet);
	TIMEVAL tv = { msecs / 1000, (msecs % 1000) * 1000 };
	int ret = select(0, &readSet, NULL, NULL, &tv);
	if (ret == KNET_SOCKET_ERROR || ret == 0)
		return false;
	else
		return true;
}
*/
OverlappedTransferBuffer *AllocateOverlappedTransferBuffer(int bytes)
{
	OverlappedTransferBuffer *buffer = new OverlappedTransferBuffer;
	memset(buffer, 0, sizeof(OverlappedTransferBuffer));
	buffer->buffer.buf = new char[bytes];
	buffer->buffer.len = bytes;
#ifdef WIN32
	buffer->overlapped.hEvent = WSACreateEvent();
	if (buffer->overlapped.hEvent == WSA_INVALID_EVENT)
	{
		LOG(LogError, "Socket.cpp:AllocateOverlappedTransferBuffer: WSACreateEvent failed!");
		delete[] buffer->buffer.buf;
		delete buffer;
		return 0;
	}
#endif

	return buffer;
}

void DeleteOverlappedTransferBuffer(OverlappedTransferBuffer *buffer)
{
	if (!buffer)
		return;
	delete[] buffer->buffer.buf;
#ifdef WIN32
	BOOL success = WSACloseEvent(buffer->overlapped.hEvent);
	if (success == FALSE)
		LOG(LogError, "Socket.cpp:DeleteOverlappedTransferBuffer: WSACloseEvent failed!");
	buffer->overlapped.hEvent = WSA_INVALID_EVENT;
#endif
	delete buffer;
}

void Socket::SetSendBufferSize(int bytes)
{
	socklen_t len = sizeof(bytes);
	if (setsockopt(connectSocket, SOL_SOCKET, SO_SNDBUF, (char*)&bytes, len))
		LOG(LogError, "Socket::SetSendBufferSize: setsockopt failed with error %s(%d)!", Network::GetLastErrorString().c_str(), Network::GetLastError());
}

void Socket::SetReceiveBufferSize(int bytes)
{
	socklen_t len = sizeof(bytes);
	if (setsockopt(connectSocket, SOL_SOCKET, SO_RCVBUF, (char*)&bytes, len))
		LOG(LogError, "Socket::SetReceiveBufferSize: setsockopt failed with error %s(%d)!", Network::GetLastErrorString().c_str(), Network::GetLastError());
}

int Socket::SendBufferSize() const
{
	int bytes = 0;
	socklen_t len = sizeof(bytes);
	if (getsockopt(connectSocket, SOL_SOCKET, SO_SNDBUF, (char*)&bytes, &len))
	{
		LOG(LogError, "Socket::SendBufferSize: getsockopt failed with error %s(%d)!", Network::GetLastErrorString().c_str(), Network::GetLastError());
		return 0;
	}
	return bytes;
}

int Socket::ReceiveBufferSize() const
{
	int bytes = 0;
	socklen_t len = sizeof(bytes);
	if (getsockopt(connectSocket, SOL_SOCKET, SO_RCVBUF, (char*)&bytes, &len))
	{
		LOG(LogError, "Socket::ReceiveBufferSize: getsockopt failed with error %s(%d)!", Network::GetLastErrorString().c_str(), Network::GetLastError());
		return 0;
	}

	return bytes;
}

#ifdef WIN32
void Socket::EnqueueNewReceiveBuffer(OverlappedTransferBuffer *buffer)
{
	if (!readOpen || queuedReceiveBuffers.CapacityLeft() == 0 || IsUDPSlaveSocket())
	{
		DeleteOverlappedTransferBuffer(buffer); // buffer may be a zero pointer, but that is alright.
		return;
	}

	if (!buffer)
	{
		const int receiveBufferSize = 4096;
		buffer = AllocateOverlappedTransferBuffer(receiveBufferSize);
		if (!buffer)
		{
			LOG(LogError, "Socket::EnqueueNewReceiveBuffer: Call to AllocateOverlappedTransferBuffer failed!");
			return;
		}
	}

	if (WSAResetEvent(buffer->overlapped.hEvent) != TRUE)
		LOG(LogError, "Socket::EnqueueNewReceiveBuffer: WSAResetEvent failed!");

	unsigned long flags = 0;
	int ret;

	if (IsUDPServerSocket())
	{
		buffer->fromLen = sizeof(buffer->from);
		ret = WSARecvFrom(connectSocket, &buffer->buffer, 1, (LPDWORD)&buffer->bytesContains, &flags, (sockaddr*)&buffer->from, &buffer->fromLen, &buffer->overlapped, 0);
	}
	else
	{
		ret = WSARecv(connectSocket, &buffer->buffer, 1, (LPDWORD)&buffer->bytesContains, &flags, &buffer->overlapped, 0);
	}

	int error = (ret == 0) ? 0 : Network::GetLastError();
	if (ret == 0 || (ret == SOCKET_ERROR && error == WSA_IO_PENDING)) // If ret is not 0, ret == SOCKET_ERROR according to MSDN. http://msdn.microsoft.com/en-us/library/ms741688(VS.85).aspx
	{
		if (ret == 0 && buffer->bytesContains == 0)
		{
			LOG(LogInfo, "Socket::EnqueueNewReceiveBuffer: Received 0 bytes from the network. Read connection closed in socket %s.", ToString().c_str());
			readOpen = false;
			DeleteOverlappedTransferBuffer(buffer);
			return;
		}
		// Return value 0: The operation completed and we have received new data. Push it to a queue for the user to receive.
		// WSA_IO_PENDING: The operation was successfully enqueued.
		bool success = queuedReceiveBuffers.Insert(buffer);
		if (!success)
		{
			LOG(LogError, "Socket::EnqueueNewReceiveBuffer: queuedReceiveBuffers.Insert(buffer); failed!");
			DeleteOverlappedTransferBuffer(buffer);
		}
	}
	else if (error == WSAEDISCON)
	{
		LOG(LogError, "Socket::EnqueueNewReceivebuffer: WSAEDISCON. Connection closed in socket %s.", ToString().c_str());
		readOpen = false;
		///\todo Should do writeOpen = false; here as well?
		DeleteOverlappedTransferBuffer(buffer);
		return;
	}
	else
	{
		if (error != WSAEWOULDBLOCK && error != 0)
		{
			LOG(LogError, "Socket::EnqueueNewReceiveBuffer: WSARecv failed in socket %s. Error %s(%d). Closing down socket.", ToString().c_str(), Network::GetErrorString(error).c_str(), error);
			readOpen = false;
			writeOpen = false;
			if (IsUDPServerSocket())
				LOG(LogError, "Socket::EnqueueNewReceiveBuffer: Closed UDP server socket!");
			Close();
		}

		LOG(LogError, "Socket::EnqueueNewReceiveBuffer: WSARecv for overlapped socket failed! Error code: %d.", error);
		DeleteOverlappedTransferBuffer(buffer);
		return;
	}
}
#endif

void DumpBuffer(const char *description, const char *data, int size)
{
	printf("%s (%d bytes): ", description, size);
	for(int i = 0; i < min(size, 50); ++i)
		printf("%02X ", (unsigned int)(unsigned char)data[i]);
	printf("\n");
}

size_t Socket::Receive(char *dst, size_t maxBytes, EndPoint *endPoint)
{
	assert(dst);
	assert(maxBytes > 0);
	if (maxBytes == 0)
		return 0;

	if (connectSocket == INVALID_SOCKET)
		return 0;

	if (!readOpen)
		return 0;

	if (IsUDPSlaveSocket()) // UDP slave sockets are never read directly. Instead the UDP server socket reads all client data.
		return 0; // So, if we accidentally come here to read a UDP slave socket, act as if it never received any data.

	if (IsUDPServerSocket())
	{
		sockaddr_in from;
		socklen_t fromLen = sizeof(from);
		int numBytesRead = recvfrom(connectSocket, dst, maxBytes, 0, (sockaddr*)&from, &fromLen);
		if (numBytesRead == KNET_SOCKET_ERROR)
		{
			int error = Network::GetLastError();
			if (error != KNET_EWOULDBLOCK && error != 0)
			{
				///\todo Mark UDP server and client sockets separately. For a server socket, we cannot Close() here,
				/// but for client sockets it is safe.
				LOG(LogError, "Socket::Receive: recvfrom failed: %s(%u) in socket %s", Network::GetErrorString(error).c_str(), error, ToString().c_str());
			}

			return 0;
		}
		if (numBytesRead > 0)
			LOG(LogData, "recvfrom (%d) in socket %s", numBytesRead, ToString().c_str());

		if (endPoint)
			*endPoint = EndPoint::FromSockAddrIn(from);

		return numBytesRead;
	}

	// If we reach here, this socket is a tcp connection socket (server->client or client->server), or a udp client->server socket.

	int ret = recv(connectSocket, dst, maxBytes, 0);

	if (ret > 0)
	{
		LOG(LogData, "Received %d bytes of data from socket 0x%X.", ret, connectSocket);
		return (size_t)ret;
	}
	else if (ret == 0)
	{
		LOG(LogInfo, "Socket::Receive: Received 0 bytes from network. Read-connection closed to socket %s.", ToString().c_str());
		readOpen = false;
//		Disconnect();
		return 0;
	}
	else
	{
		int error = Network::GetLastError();
		if (error != KNET_EWOULDBLOCK && error != 0)
		{
			LOG(LogError, "Socket::Receive: recv failed in socket %s. Error %s(%d)", ToString().c_str(), Network::GetErrorString(error).c_str(), error);
			readOpen = false;
			writeOpen = false;
			Close();
		}
		return 0;
	}
}

bool Socket::IsOverlappedReceiveReady() const
{
	if (IsUDPSlaveSocket()) // UDP slave sockets are never read directly. For these sockets, act as if they're never ready for reads.
		return false;

#ifdef WIN32
	if (queuedReceiveBuffers.Size() == 0)
		return false;
	return Event((*queuedReceiveBuffers.Front())->overlapped.hEvent, EventWaitRead).Test();
#else
	if (!readOpen)
		return false;

	EventArray ea;
	ea.AddEvent(Event(connectSocket, EventWaitRead));
	return ea.Wait(0) == 0;
#endif
}

Event Socket::GetOverlappedReceiveEvent()
{
	if (IsUDPSlaveSocket()) // UDP slave sockets are never read directly. For these sockets, return a null event that never fires.
		return Event();

#ifdef WIN32
	if (readOpen)
	{
		/// Prime the receive buffers to the full capacity if they weren't so yet.
		const int capacityLeft = queuedReceiveBuffers.CapacityLeft();
		for(int i = 0; i < capacityLeft; ++i)
			EnqueueNewReceiveBuffer();
	}

	if (queuedReceiveBuffers.Size() == 0)
		return Event();

	OverlappedTransferBuffer *receivedData = *queuedReceiveBuffers.Front();
	assert(receivedData);

	return Event(receivedData->overlapped.hEvent, EventWaitRead);
#else
	return Event(connectSocket, EventWaitRead);
#endif
}

Event Socket::GetOverlappedSendEvent()
{
	if (!writeOpen)
		return Event();

#ifdef WIN32
	if (queuedSendBuffers.Size() == 0)
		return Event();

	OverlappedTransferBuffer *sentData = *queuedSendBuffers.Front();
	assert(sentData);

	return Event(sentData->overlapped.hEvent, EventWaitWrite);
#else
	return Event(connectSocket, EventWaitWrite);
#endif
}

OverlappedTransferBuffer *Socket::BeginReceive()
{
    // UDP 'slave socket' is a socket descriptor on the server side that is a copy of the single UDP server listen socket.
    // The slave sockets don't receive data directly, but the server socket is used instead to receive data for them.
    if (IsUDPSlaveSocket())
    {
#ifdef WIN32
        assert(queuedReceiveBuffers.Size() == 0); // We shouldn't ever have queued a single receive buffer for this Socket.
#endif
        return 0; // If we happen to come here, act as if the socket never received any data.
    }

#ifdef WIN32
	if (readOpen)
	{
		// Insert new empty receive buffers to the Overlapped Transfer receive queue until we have a full capacity queue primed.
		const int capacityLeft = queuedReceiveBuffers.CapacityLeft();
		for(int i = 0; i < capacityLeft; ++i)
			EnqueueNewReceiveBuffer();
	}

	if (queuedReceiveBuffers.Size() == 0)
		return 0;
	
	OverlappedTransferBuffer *receivedData = *queuedReceiveBuffers.Front();
	DWORD flags = 0;
	BOOL ret = WSAGetOverlappedResult(connectSocket, &receivedData->overlapped, (LPDWORD)&receivedData->bytesContains, FALSE, &flags);
	int error = (ret == TRUE) ? 0 : Network::GetLastError();
	if (ret == TRUE)
	{
		queuedReceiveBuffers.PopFront();

		// Successfully receiving zero bytes with overlapped sockets means the same as recv() of 0 bytes,
		// peer has closed the write connection (but can still recv() until we also close the write connection).
		if (receivedData->bytesContains == 0)
		{
			DeleteOverlappedTransferBuffer(receivedData);
			if (readOpen)
				LOG(LogInfo, "Socket::BeginReceive: Received 0 bytes from the network. Read connection closed in socket %s.", ToString().c_str());
			readOpen = false;
			if (IsUDPServerSocket())
				LOG(LogError, "Socket::BeginReceive: UDP server socket transitioned to readOpen==false!");
			return 0;
		}

//		DumpBuffer("Socket::BeginReceive", receivedData->buffer.buf, receivedData->bytesContains);

		return receivedData;
	}
	else if (error == WSAEDISCON)
	{
		queuedReceiveBuffers.PopFront();
		DeleteOverlappedTransferBuffer(receivedData);
		if (readOpen || writeOpen)
			LOG(LogError, "Socket::BeginReceive: WSAEDISCON. Bidirectionally closing connection in socket %s.", ToString().c_str());
		if (IsUDPServerSocket())
			LOG(LogError, "Socket::BeginReceive: Closed UDP server socket!");
		readOpen = false;
		writeOpen = false;
		// Close(); ///\todo Should call this?
		return 0;
	}
	else if (error != WSA_IO_INCOMPLETE)
	{
		queuedReceiveBuffers.PopFront();
		if (readOpen || writeOpen)
            if (!(IsUDPServerSocket() && error == 10054)) // If we are running both UDP server and client on localhost, we can receive 10054 (Peer closed connection) on the server side, in which case, we ignore this error print.
			    LOG(LogError, "Socket::BeginReceive: WSAGetOverlappedResult failed with code %d when reading from an overlapped socket! Reason: %s.", error, Network::GetErrorString(error).c_str());
		DeleteOverlappedTransferBuffer(receivedData);
		// Mark this socket closed, unless the read error was on a UDP server socket, in which case we must ignore
		// the read error on this buffer (an error on a single client connection cannot shut down the whole server!)
		if (!IsUDPServerSocket() && (readOpen || writeOpen))
		{
			readOpen = false;
			writeOpen = false;
			LOG(LogError, "Socket::BeginReceive: Closed socket due to read error!");
			// Close(); ///\todo Should call this?
		}
	}
	return 0;
#else
	if (!readOpen)
		return 0;

	const int receiveBufferSize = 4096;
	OverlappedTransferBuffer *buffer = AllocateOverlappedTransferBuffer(receiveBufferSize);
	EndPoint source;
	buffer->bytesContains = Receive(buffer->buffer.buf, buffer->buffer.len, &source);
	if (buffer->bytesContains > 0)
	{
		buffer->fromLen = sizeof(buffer->from);
		buffer->from = source.ToSockAddrIn();
		return buffer;
	}
	else
	{
		// Did not get any data. Delete the buffer immediately.
		DeleteOverlappedTransferBuffer(buffer);
		return 0;
	}
#endif
}

void Socket::EndReceive(OverlappedTransferBuffer *buffer)
{
#ifdef WIN32
	if (readOpen)
	{
		EnqueueNewReceiveBuffer(buffer);
		return;
	}
#endif

	DeleteOverlappedTransferBuffer(buffer);
}

void Socket::Disconnect()
{
	if (connectSocket == INVALID_SOCKET)
		return;

	LOG(LogVerbose, "Socket::Disconnect(), this: 0x%X.", this);

	if (transport == SocketOverTCP)
	{
		int result = shutdown(connectSocket, SD_SEND);
		if (result == KNET_SOCKET_ERROR)
		{
			int error = Network::GetLastError();
			LOG(LogError, "Socket::Disconnect(): TCP socket shutdown(SD_SEND) failed: %s(%u) in socket %s.", Network::GetErrorString(error).c_str(), error, ToString().c_str());
		}
		else
		{
			LOG(LogInfo, "Socket::Disconnect(): TCP socket shutdown(SD_SEND) succeeded on socket %s.", ToString().c_str());
		}
	}

	writeOpen = false;
}

void Socket::Close()
{
	if (connectSocket == INVALID_SOCKET)
		return;

	LOG(LogInfo, "Socket::Close(): Closing socket %s.", ToString().c_str());

	if (!IsUDPSlaveSocket())
	{
		// For UDP client sockets on the server side, the same SOCKET object is shared by 
		// all other client sockets and the UDP server socket. For those sockets, the UDP server socket is the owner
		// of the socket object, so the clients cannot close their sockets, as closing one would close them all.

		int result = shutdown(connectSocket, SD_BOTH);
		if (result == KNET_SOCKET_ERROR)
		{
			int error = Network::GetLastError();
			LOG(LogError, "Socket::Close(): Socket shutdown(SD_BOTH) failed: %s(%u) in socket %s.", Network::GetErrorString(error).c_str(), error, ToString().c_str());
		}
		else
		{
			LOG(LogInfo, "Socket::Close(): Socket shutdown(SD_BOTH) succeeded on socket %s.", ToString().c_str());
		}

		result = closesocket(connectSocket);
		if (result == KNET_SOCKET_ERROR)
		{
			int error = Network::GetLastError();
			LOG(LogError, "Socket::Close(): closesocket() failed: %s(%u) in socket %s.", Network::GetErrorString(error).c_str(), error, ToString().c_str());
		}
	}

	connectSocket = INVALID_SOCKET;
	localEndPoint.Reset();
	localHostName = "";
	remoteEndPoint.Reset();
	remoteHostName = "";
	transport = InvalidTransportLayer;
	type = InvalidSocketType;
	readOpen = false;
	writeOpen = false;

#ifdef WIN32
	FreeOverlappedTransferBuffers();
#endif
}

#ifdef WIN32
void Socket::FreeOverlappedTransferBuffers()
{
	LOG(LogVerbose, "Socket::FreeOverlappedTransferBuffers(), this: 0x%X.", this);
/// \todo Use CancelIo to tear-down commited OverlappedTransferBuffers before freeing data. http://msdn.microsoft.com/en-us/library/aa363792(VS.85).aspx
	while(queuedReceiveBuffers.Size() > 0)
		DeleteOverlappedTransferBuffer(queuedReceiveBuffers.TakeFront());

	while(queuedSendBuffers.Size() > 0)
		DeleteOverlappedTransferBuffer(queuedSendBuffers.TakeFront());
}
#endif

void Socket::SetBlocking(bool isBlocking)
{
	if (connectSocket == INVALID_SOCKET)
		return;

	u_long nonBlocking = (isBlocking == false) ? 1 : 0;
#ifdef WIN32
	if (ioctlsocket(connectSocket, FIONBIO, &nonBlocking))
		LOG(LogError, "Socket::SetBlocking: ioctlsocket failed with error %s(%d)!", Network::GetLastErrorString().c_str(), Network::GetLastError());
#else
	int flags = fcntl(connectSocket, F_GETFL, 0);
	fcntl(connectSocket, F_SETFL, flags | O_NONBLOCK);
#endif
}

/// @return True on success, false otherwise.
bool Socket::Send(const char *data, size_t numBytes)
{
	if (!writeOpen)
	{
		LOG(LogError, "Trying to send data to a socket that is not open for writing!");
		return false;
	}

	if (connectSocket == INVALID_SOCKET)
	{
		LOG(LogError, "Trying to send a datagram to INVALID_SOCKET!");
		return false;
	}

	// Server listen sockets are *not* for sending data. They only accept incoming connections.
	if (type == ServerListenSocket)
	{
		LOG(LogError, "Trying to send data through a server listen socket!");
		return false;
	}

	int sendTriesLeft = 100;
	size_t numBytesSent = 0;
	while(numBytesSent < numBytes && sendTriesLeft-- > 0)
	{
		size_t numBytesLeftToSend = (size_t)(numBytes - numBytesSent);
		SetBlocking(true);
		int ret;
		if (transport == SocketOverUDP)
			ret = sendto(connectSocket, data + numBytesSent, (int)numBytesLeftToSend, 0, (sockaddr*)&udpPeerAddress, sizeof(udpPeerAddress));
		else
			ret = send(connectSocket, data + numBytesSent, (int)numBytesLeftToSend, 0);

		LOG(LogData, "Sent data to socket 0x%X.", connectSocket);
//		DumpBuffer("Socket::Send", data + numBytesSent, numBytesLeftToSend);

		if (ret == KNET_SOCKET_ERROR)
		{
			int error = Network::GetLastError();
			if (error != KNET_EWOULDBLOCK)
			{
				if (error != 0)
					Close();
				LOGNET("Failed to send %d bytes over socket %s. Error %s(%u)", numBytes, ToString().c_str(), Network::GetErrorString(error).c_str(), error);
			}
			SetBlocking(false);
			return false;
		}
		SetBlocking(false);
		LOG(LogData, "Sent %d bytes of data to socket %s.", ret, ToString().c_str());
		numBytesSent += ret;

#ifdef WIN32 ///\todo
		if (numBytesSent < numBytes)
		{
			FD_SET writeSocketSet;
			FD_SET errorSocketSet;
			FD_ZERO(&writeSocketSet);
			FD_ZERO(&errorSocketSet);
			FD_SET(connectSocket, &writeSocketSet);
			FD_SET(connectSocket, &errorSocketSet);
			timeval timeout = { 5, 0 }; 
			int ret = select(0, 0, &writeSocketSet, &errorSocketSet, &timeout);
			if (ret == 0 || ret == KNET_SOCKET_ERROR)
			{
				LOGNET("Waiting for socket %s to become write-ready failed!", ToString().c_str());
				// If we did manage to send any bytes through, the stream is now out of sync,
				// tear it down.
				if (numBytesSent > 0)
					Close();
				return false;
			}
		}
#endif
	}

	if (sendTriesLeft <= 0)
	{
		if (numBytesSent > 0)
		{
			LOG(LogError, "Could not send %d bytes to socket %s. Achieved %d bytes, stream has now lost bytes, closing connection!", numBytes, 
				ToString().c_str(), numBytesSent);
			Close();
		}
		else
			LOG(LogError, "Could not send %d bytes to socket %s.", numBytes, ToString().c_str());
		return false;
	}

	return true;
}

bool Socket::IsOverlappedSendReady()
{
	if (!writeOpen)
		return false;

#ifdef WIN32
	if (queuedSendBuffers.CapacityLeft() > 0)
		return true;

	OverlappedTransferBuffer *sentData = *queuedSendBuffers.Front();
	DWORD flags = 0;
	BOOL ret = WSAGetOverlappedResult(connectSocket, &sentData->overlapped, (LPDWORD)&sentData->bytesContains, 
		FALSE, &flags);
	return ret == TRUE;
#else
	EventArray ea;
	ea.AddEvent(Event(connectSocket, EventWaitWrite));
	return ea.Wait(0) == 0;
#endif
}

OverlappedTransferBuffer *Socket::BeginSend()
{
	if (!writeOpen)
		return 0;

#ifdef WIN32
	if (queuedSendBuffers.Size() > 0)
	{
		OverlappedTransferBuffer *sentData = *queuedSendBuffers.Front();
		DWORD flags = 0;
		BOOL ret = WSAGetOverlappedResult(connectSocket, &sentData->overlapped, (LPDWORD)&sentData->bytesContains, 
			(queuedSendBuffers.CapacityLeft() == 0) ? TRUE : FALSE, &flags);
		int error = (ret == TRUE) ? 0 : Network::GetLastError();
		if (ret == TRUE)
		{		
			queuedSendBuffers.PopFront();
			sentData->buffer.len = maxSendSize; // This is the number of bytes that the client is allowed to fill.
			return sentData;
		}
		if (ret == FALSE && error != WSA_IO_INCOMPLETE)
		{
			LOG(LogError, "Socket::BeginSend: WSAGetOverlappedResult failed with an error %s, code %d != WSA_IO_INCOMPLETE!", 
				Network::GetErrorString(error).c_str(), error);
			writeOpen = false;
			return 0;
		}
	}

	if (queuedSendBuffers.CapacityLeft() == 0)
		return 0;
#endif

	OverlappedTransferBuffer *transfer = AllocateOverlappedTransferBuffer(maxSendSize);
	return transfer; ///\todo In debug mode - track this pointer.
}

bool Socket::EndSend(OverlappedTransferBuffer *sendBuffer)
{
	assert(sendBuffer);
	if (!sendBuffer)
		return false;

#ifdef WIN32
	// Clear the event flag so that the completion of WSASend can trigger this and signal us.
	WSAResetEvent(sendBuffer->overlapped.hEvent);

	unsigned long bytesSent = 0;

	int ret;

//	DumpBuffer("Socket::EndSend", sendBuffer->buffer.buf, sendBuffer->buffer.len);

	if (transport == SocketOverUDP)
		ret = WSASendTo(connectSocket, &sendBuffer->buffer, 1, (LPDWORD)&bytesSent, 0, (sockaddr*)&udpPeerAddress, sizeof(udpPeerAddress), &sendBuffer->overlapped, 0);
	else
		ret = WSASend(connectSocket, &sendBuffer->buffer, 1, (LPDWORD)&bytesSent, 0, &sendBuffer->overlapped, 0);

	int error = (ret == 0) ? 0 : Network::GetLastError();
	if (ret != 0 && error != WSA_IO_PENDING)
	{
		if (error != KNET_EWOULDBLOCK)
		{
			LOG(LogError, "Socket::EndSend() failed! Error: %s(%d).", Network::GetErrorString(error).c_str(), error);
			if (!IsUDPServerSocket())
				writeOpen = false;
		}
		DeleteOverlappedTransferBuffer(sendBuffer);
		return false;
	}

	bool success = queuedSendBuffers.Insert(sendBuffer);
	if (!success)
	{
		LOG(LogError, "queuedSendBuffers.Insert(send); failed!");
		///\todo WARNING: Deleting a buffer that is submitted to WSASend. This crashes. The alternative
		/// is to leak. Refactor so that the above queuedSendBuffers.Insert is tried for success before calling WSASend.
		DeleteOverlappedTransferBuffer(sendBuffer);
		return false;
	}
	return true;

#elif LINUX
	unsigned long bytesSent = 0;

	int ret;

//	DumpBuffer("Socket::EndSend", sendBuffer->buffer.buf, sendBuffer->buffer.len);

	if (transport == SocketOverUDP)
		ret = sendto(connectSocket, sendBuffer->buffer.buf, sendBuffer->buffer.len, 0, (sockaddr*)&udpPeerName, sizeof(udpPeerName));
	else
		ret = send(connectSocket, sendBuffer->buffer.buf, sendBuffer->buffer.len, 0);

	int error = (ret == sendBuffer->buffer.len) ? 0 : Network::GetLastError();

	if (ret > 0)
	{
		if (ret < sendBuffer->buffer.len)
		{
			LOG(LogError, "Socket::EndSend: Warning! Managed to only partially send out %d bytes out of %d bytes in the buffer!",
				ret, sendBuffer->buffer.len);
			return false;
		}
		else
		{
			LOG(LogData, "Socket::EndSend: Sent out %d bytes to socket %s.",
				ret, ToString().c_str());
			return true;
		}
	}

	if (error && error != KNET_EWOULDBLOCK)
		LOG(LogError, "Socket::EndSend() failed! Error: %s(%d).", Network::GetErrorString(error).c_str(), error);

	DeleteOverlappedTransferBuffer(sendBuffer);

	return false;
#endif
}

void Socket::AbortSend(OverlappedTransferBuffer *send)
{
	if (!writeOpen)
	{
		DeleteOverlappedTransferBuffer(send);
		return;
	}

#ifdef WIN32
	// Set the event flag so as to signal that this buffer is completed immediately.
	if (WSASetEvent(send->overlapped.hEvent) != TRUE)
	{
		LOG(LogError, "Socket::AbortSend: WSASetEvent failed!");
		DeleteOverlappedTransferBuffer(send);
		return;
	}
	bool success = queuedSendBuffers.Insert(send);
	if (!success)
	{
		LOG(LogError, "queuedSendBuffers.Insert(send); failed! AbortOverlappedSend");
		DeleteOverlappedTransferBuffer(send);
	}
#else
	DeleteOverlappedTransferBuffer(send);
#endif
}
/*
unsigned short Socket::LocalPort() const
{
	sockaddr_in addr;
	socklen_t namelen = sizeof(addr);

	int sockRet = getsockname(connectSocket, (sockaddr*)&addr, &namelen); // Note: This works only if family==INETv4
	EndPoint sockName = EndPoint::FromSockAddrIn(addr);
	return sockName.port;
}
*/
std::string Socket::ToString() const
{
	sockaddr_in addr;
	socklen_t namelen = sizeof(addr);
	int peerRet = getpeername(connectSocket, (sockaddr*)&addr, &namelen); // Note: This works only if family==INETv4
	EndPoint peerName = EndPoint::FromSockAddrIn(addr);

	int sockRet = getsockname(connectSocket, (sockaddr*)&addr, &namelen); // Note: This works only if family==INETv4
	EndPoint sockName = EndPoint::FromSockAddrIn(addr);

	char str[256];
	sprintf(str, "%s:%d (%s, connected=%s, readOpen: %s, writeOpen: %s, maxSendSize=%d, sock: %s, peer: %s, socket: %d, this: 0x%p)", 
		DestinationAddress(), (unsigned int)DestinationPort(), 
		(transport == SocketOverTCP) ? "TCP" : (IsUDPServerSocket() ? "UDP server" : (IsUDPSlaveSocket() ? "UDP Slave" : "UDP")), 
		Connected() ? "true" : "false", readOpen ? "true" : "false", writeOpen ? "true" : "false",
		maxSendSize, sockRet == 0 ? sockName.ToString().c_str() : "(-)", 
		peerRet == 0 ? peerName.ToString().c_str() : "(-)", (int)connectSocket,
		this);

	return std::string(str);
}

void Socket::SetNaglesAlgorithmEnabled(bool enabled)
{
	if (connectSocket == INVALID_SOCKET)
	{
		LOG(LogError, "Socket::SetNaglesAlgorithmEnabled called for invalid socket object!");
		return;
	}
	if (transport != SocketOverTCP)
	{
		LOG(LogError, "Calling Socket::SetNaglesAlgorithmEnabled is only valid for TCP sockets!");
		return;
	}

#ifdef WIN32
	BOOL nagleEnabled = enabled ? TRUE : FALSE;
	int ret = setsockopt(connectSocket, IPPROTO_TCP, TCP_NODELAY, (const char *)&nagleEnabled, sizeof(nagleEnabled));
#else
	int nagleEnabled = enabled ? 1 : 0;
	int ret = setsockopt(connectSocket, IPPROTO_TCP, TCP_NODELAY, &nagleEnabled, sizeof(nagleEnabled));
#endif
	if (ret != 0)
		LOG(LogError, "Setting TCP_NODELAY=%s for socket %d failed. Reason: %s.",
			enabled ? "true" : "false", (int)connectSocket, Network::GetLastErrorString().c_str());
}

} // ~kNet
