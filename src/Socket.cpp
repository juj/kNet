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

#include "kNet/Socket.h"
#include "kNet/Network.h"
#include "kNet/NetworkLogging.h"
#include "kNet/EventArray.h"

#include "kNet/DebugMemoryLeakCheck.h"

using namespace std;

#ifdef LINUX
#include <fcntl.h>
#endif

namespace kNet
{

Socket::Socket()
:connectSocket(INVALID_SOCKET),
writeOpen(false),
readOpen(false),
isUdpServerSocket(false)
#ifdef WIN32
,queuedReceiveBuffers(64)
,queuedSendBuffers(64)
#endif
{
	memset(&udpPeerName, 0, sizeof(udpPeerName));
}

Socket::~Socket()
{
}

Socket::Socket(SOCKET connection, const char *address, unsigned short port, SocketTransportLayer transport_, size_t maxSendSize_,
	bool isUdpServerSocket_)
:connectSocket(connection), destinationAddress(address), destinationPort(port), 
transport(transport_), maxSendSize(maxSendSize_),
writeOpen(true), readOpen(true),
isUdpServerSocket(isUdpServerSocket_)
#ifdef WIN32
,queuedReceiveBuffers(64)
,queuedSendBuffers(64)
#endif
{
	SetSendBufferSize(512 * 1024);
	SetReceiveBufferSize(512 * 1024);
}

Socket::Socket(const Socket &rhs)
#ifdef WIN32
:queuedReceiveBuffers(64),
queuedSendBuffers(64)
#endif
{
	*this = rhs;
}

void Socket::operator=(const Socket &rhs)
{
#ifdef WIN32
	assert(queuedReceiveBuffers.Size() == 0);
#endif

	connectSocket = rhs.connectSocket;
	udpPeerName = rhs.udpPeerName;
	destinationAddress = rhs.destinationAddress;
	destinationPort = rhs.destinationPort;
	transport = rhs.transport;
	maxSendSize = rhs.maxSendSize;
	writeOpen = rhs.writeOpen;
	readOpen = rhs.readOpen;
	isUdpServerSocket = rhs.isUdpServerSocket;
}

bool Socket::Connected() const
{
	return connectSocket != INVALID_SOCKET;
}

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

OverlappedTransferBuffer *AllocateOverlappedTransferBuffer(int bytes)
{
	OverlappedTransferBuffer *buffer = new OverlappedTransferBuffer();
	memset(buffer, 0, sizeof(OverlappedTransferBuffer));
	buffer->buffer.buf = new char[bytes];
	buffer->buffer.len = bytes;
#ifdef WIN32
	buffer->overlapped.hEvent = WSACreateEvent();
#endif

	return buffer;
}

void DeleteOverlappedTransferBuffer(OverlappedTransferBuffer *buffer)
{
	if (!buffer)
		return;
	delete buffer->buffer.buf;
#ifdef WIN32
	WSACloseEvent(buffer->overlapped.hEvent);
#endif
	delete buffer;
}

void Socket::SetSendBufferSize(int bytes)
{
	socklen_t len = sizeof(bytes);
	setsockopt(connectSocket, SOL_SOCKET, SO_SNDBUF, (char*)&bytes, len);
}

void Socket::SetReceiveBufferSize(int bytes)
{
	socklen_t len = sizeof(bytes);
	setsockopt(connectSocket, SOL_SOCKET, SO_RCVBUF, (char*)&bytes, len);
}

int Socket::SendBufferSize() const
{
	int bytes = 0;
	socklen_t len = sizeof(bytes);
	getsockopt(connectSocket, SOL_SOCKET, SO_SNDBUF, (char*)&bytes, &len);
	return bytes;
}

int Socket::ReceiveBufferSize() const
{
	int bytes = 0;
	socklen_t len = sizeof(bytes);
	getsockopt(connectSocket, SOL_SOCKET, SO_RCVBUF, (char*)&bytes, &len);
	return bytes;
}

#ifdef WIN32
void Socket::EnqueueNewReceiveBuffer(OverlappedTransferBuffer *buffer)
{
	if (!readOpen || queuedReceiveBuffers.CapacityLeft() == 0)
	{
		DeleteOverlappedTransferBuffer(buffer); // buffer may be a zero pointer, but that is alright.
		return;
	}

	if (!buffer)
	{
		const int receiveBufferSize = 4096;
		buffer = AllocateOverlappedTransferBuffer(receiveBufferSize);
	}

#ifdef WIN32
	WSAResetEvent(buffer->overlapped.hEvent);
#endif

	unsigned long flags = 0;
	int ret;

	if (isUdpServerSocket)
	{
		buffer->fromLen = sizeof(buffer->from);
#ifdef WIN32
		ret = WSARecvFrom(connectSocket, &buffer->buffer, 1, (LPDWORD)&buffer->bytesContains, &flags, (sockaddr*)&buffer->from, &buffer->fromLen, &buffer->overlapped, 0);
#else
		ret = recvfrom(connectSocket, &buffer->buffer.buf, buffer->buffer.len, 0, (sockaddr*)&buffer->from, &buffer->fromLen);
		buffer->bytesContains = ret;
#endif
	}
	else
	{
#ifdef WIN32
		ret = WSARecv(connectSocket, &buffer->buffer, 1, (LPDWORD)&buffer->bytesContains, &flags, &buffer->overlapped, 0);
#else
		ret = recv(connectSocket, &buffer->buffer, buffer->buffer.len, 0);
		buffer->bytesContains = ret;
#endif
	}

	int error = (ret == 0) ? 0 : Network::GetLastError();
#ifdef WIN32
	if (ret == 0 || (ret == KNET_SOCKET_ERROR && error == WSA_IO_PENDING)) // If not 0, ret == SOCKET_ERROR according to MSDN. http://msdn.microsoft.com/en-us/library/ms741688(VS.85).aspx
#else
	if (ret == 0)
#endif
	{
		if (ret == 0 && buffer->bytesContains == 0)
		{
			LOG(LogInfo, "Socket::ReceiveOverlapped: Received 0 bytes from the network. Read connection closed in socket %s.", ToString().c_str());
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
#ifdef WIN32
	else if (error == WSAEDISCON)
	{
		LOG(LogError, "Socket::EnqueueNewReceivebuffer: WSAEDISCON. Connection closed in socket %s.", ToString().c_str());
		readOpen = false;
		DeleteOverlappedTransferBuffer(buffer);
		return;
	}
#endif
	else
	{
		if (error != KNET_EWOULDBLOCK && error != 0)
		{
			LOG(LogError, "Socket::EnqueueNewReceiveBuffer: WSARecv failed in socket %s. Error %s(%d). Closing down socket.", ToString().c_str(), Network::GetErrorString(error).c_str(), error);
			readOpen = false;
			writeOpen = false;
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
	if (!readOpen)
	{
		LOG(LogError, "Trying to read data from a socket that is not open for reading!");
		return 0;
	}

	if (connectSocket == INVALID_SOCKET)
		return 0;

	if (isUdpServerSocket)
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
				LOGNET("recvfrom failed: %s(%u) in socket %s", Network::GetErrorString(error).c_str(), error, ToString().c_str());
			}

			return 0;
		}
		if (numBytesRead > 0)
			LOG(LogData, "recvfrom (%d) in socket %s", numBytesRead, ToString().c_str());

		if (endPoint)
			*endPoint = EndPoint::FromSockAddrIn(from);

		return numBytesRead;
	}

	int ret = recv(connectSocket, dst, maxBytes, 0);

	if (ret > 0)
	{
		LOG(LogData, "Received %d bytes of data from socket 0x%X.", ret, connectSocket);
		return (size_t)ret;
	}
	else if (ret == 0)
	{
		LOG(LogInfo, "Socket::Receive: Received 0 bytes from network. Connection closed in socket %s.", ToString().c_str());
		readOpen = false;
		Disconnect();
		return 0;
	}
	else
	{
		int error = Network::GetLastError();
		if (error != KNET_EWOULDBLOCK && error != 0)
		{
			LOG(LogError, "recv/recvfrom failed in socket %s. Error %s(%d)", ToString().c_str(), Network::GetErrorString(error).c_str(), error);
			readOpen = false;
			writeOpen = false;
			Close();
		}
		return 0;
	}
}

bool Socket::IsOverlappedReceiveReady() const
{
#ifdef WIN32
	if (queuedReceiveBuffers.Size() == 0)
		return false;
	return Event((*queuedReceiveBuffers.Front())->overlapped.hEvent, EventWaitRead).Test();
#else
	EventArray ea;
	ea.AddEvent(Event(connectSocket, EventWaitRead));
	return ea.Wait(0) == 0;
#endif
}

Event Socket::GetOverlappedReceiveEvent()
{
#ifdef WIN32
	/// Prime the receive buffers to the full capacity if they weren't so yet.
	const int capacityLeft = queuedReceiveBuffers.CapacityLeft();
	for(int i = 0; i < capacityLeft; ++i)
		EnqueueNewReceiveBuffer();

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
#ifdef WIN32
	if (readOpen)
	{
		/// Prime the receive buffers to the full capacity if they weren't so yet.
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

		if (receivedData->bytesContains == 0)
		{
			EnqueueNewReceiveBuffer(receivedData);
			if (readOpen)
				LOG(LogInfo, "Socket::ReceiveOverlapped: Received 0 bytes from the network. Read connection closed in socket %s.", ToString().c_str());
			readOpen = false;
			return 0;
		}

//		DumpBuffer("Socket::BeginReceive", receivedData->buffer.buf, receivedData->bytesContains);

		return receivedData;
	}
	else if (error == WSAEDISCON)
	{
		DeleteOverlappedTransferBuffer(receivedData);
		if (readOpen)
			LOG(LogError, "Socket::ReceiveOverlapped: WSAEDISCON. Read connection closed in socket %s.", ToString().c_str());
		readOpen = false;
		return 0;
	}
	else if (error != WSA_IO_INCOMPLETE)
	{
		if (readOpen || writeOpen)
			LOG(LogError, "WSAGetOverlappedResult failed with code %d when reading from an overlapped socket!", error);
		DeleteOverlappedTransferBuffer(receivedData);
		queuedReceiveBuffers.PopFront();
		readOpen = false;
		writeOpen = false;
	}
	return 0;
#else
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
		EnqueueNewReceiveBuffer(buffer);
	else
		DeleteOverlappedTransferBuffer(buffer);
#else
		DeleteOverlappedTransferBuffer(buffer);
#endif
}

void Socket::Disconnect()
{
	if (connectSocket == INVALID_SOCKET)
		return;

	if (transport == SocketOverTCP)
	{
		int result = shutdown(connectSocket, SD_SEND);
		if (result == KNET_SOCKET_ERROR)
		{
			int error = Network::GetLastError();
			LOG(LogError, "Socket::Disconnect(): Socket shutdown(SD_SEND) failed: %s(%u) in socket %s.", Network::GetErrorString(error).c_str(), error, ToString().c_str());
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

	readOpen = false;
	writeOpen = false;

	if (transport == SocketOverTCP)
	{
		int result = shutdown(connectSocket, SD_BOTH);
		if (result == KNET_SOCKET_ERROR)
		{
			int error = Network::GetLastError();
			LOG(LogError, "Socket::Close(): TCP socket shutdown(SD_BOTH) failed: %s(%u) in socket %s.", Network::GetErrorString(error).c_str(), error, ToString().c_str());
		}
		else
		{
			LOG(LogInfo, "Socket::Close(): TCP socket shutdown(SD_BOTH) succeeded on socket %s.", ToString().c_str());
		}
	}

	// Each TCP Socket owns the SOCKET they contain. For UDP server, the same SOCKET is shared by 
	// several Sockets, so closing one would close them all.
	if (!isUdpServerSocket)
		closesocket(connectSocket);

	connectSocket = INVALID_SOCKET;
	destinationAddress = "";
	destinationPort = 0;
/** \todo Use CancelIo to tear-down commited OverlappedTransferBuffers before freeing data. http://msdn.microsoft.com/en-us/library/aa363792(VS.85).aspx
#ifdef WIN32
	while(queuedReceiveBuffers.Size() > 0)
	{
		OverlappedTransferBuffer *b = queuedReceiveBuffers.TakeFront();
		delete b->buffer.buf;
		delete b;
	}
#endif
*/
}

void Socket::SetBlocking(bool isBlocking)
{
	if (connectSocket == INVALID_SOCKET)
		return;

	u_long nonBlocking = (isBlocking == false) ? 1 : 0;
#ifdef WIN32
	ioctlsocket(connectSocket, FIONBIO, &nonBlocking);	
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

	int sendTriesLeft = 100;
	size_t numBytesSent = 0;
	while(numBytesSent < numBytes && sendTriesLeft-- > 0)
	{
		size_t numBytesLeftToSend = (size_t)(numBytes - numBytesSent);
		SetBlocking(true);
		int ret;
		if (transport == SocketOverUDP)
			ret = sendto(connectSocket, data + numBytesSent, (int)numBytesLeftToSend, 0, (sockaddr*)&udpPeerName, sizeof(udpPeerName));
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
			LOG(LogError, "WSAGetOverlappedResult failed with an error code != WSA_IO_INCOMPLETE. (%d)!", error);
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

#ifdef WIN32
	// Clear the event flag so that the completion of WSASend can trigger this and signal us.
	WSAResetEvent(sendBuffer->overlapped.hEvent);

	unsigned long bytesSent = 0;

	int ret;

//	DumpBuffer("Socket::EndSend", sendBuffer->buffer.buf, sendBuffer->buffer.len);

	if (transport == SocketOverUDP)
		ret = WSASendTo(connectSocket, &sendBuffer->buffer, 1, (LPDWORD)&bytesSent, 0, (sockaddr*)&udpPeerName, sizeof(udpPeerName), &sendBuffer->overlapped, 0);
	else
		ret = WSASend(connectSocket, &sendBuffer->buffer, 1, (LPDWORD)&bytesSent, 0, &sendBuffer->overlapped, 0);

	int error = (ret == 0) ? 0 : Network::GetLastError();
	if (ret != 0 && error != WSA_IO_PENDING)
	{
		if (error != KNET_EWOULDBLOCK)
			LOG(LogError, "Socket::EndSend() failed! Error: %s(%d).", Network::GetErrorString(error).c_str(), error);
		DeleteOverlappedTransferBuffer(sendBuffer);
		return false;
	}

	bool success = queuedSendBuffers.Insert(sendBuffer);
	if (!success)
	{
		LOG1(LogError, "queuedSendBuffers.Insert(send); failed!");
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
#ifdef WIN32
	// Set the event flag so as to signal that this buffer is completed immediately.
	WSASetEvent(send->overlapped.hEvent);
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

EndPoint Socket::GetEndPoint() const
{
	return EndPoint::FromSockAddrIn(udpPeerName);
}

unsigned short Socket::LocalPort() const
{
	sockaddr_in addr;
	socklen_t namelen = sizeof(addr);

	int sockRet = getsockname(connectSocket, (sockaddr*)&addr, &namelen); // Note: This works only if family==INETv4
	EndPoint sockName = EndPoint::FromSockAddrIn(addr);
	return sockName.port;
}

std::string Socket::ToString() const
{
	sockaddr_in addr;
	socklen_t namelen = sizeof(addr);
	int peerRet = getpeername(connectSocket, (sockaddr*)&addr, &namelen); // Note: This works only if family==INETv4
	EndPoint peerName = EndPoint::FromSockAddrIn(addr);

	int sockRet = getsockname(connectSocket, (sockaddr*)&addr, &namelen); // Note: This works only if family==INETv4
	EndPoint sockName = EndPoint::FromSockAddrIn(addr);

	char str[256];
	sprintf(str, "%s:%d (%s, connected=%s, maxSendSize=%d, sock: %s, peer: %s)", DestinationAddress(), (unsigned int)DestinationPort(), 
		(transport == SocketOverTCP) ? "TCP" : "UDP", Connected() ? "true" : "false", maxSendSize,
		sockRet == 0 ? sockName.ToString().c_str() : "(-)", peerRet == 0 ? peerName.ToString().c_str() : "(-)");

	return std::string(str);
}

} // ~kNet
