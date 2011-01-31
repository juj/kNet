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

/** @file Network.cpp
	@brief */

#include <string>
#include <sstream>

#include <cassert>

#ifdef LINUX
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#endif

#include "kNet/Network.h"

#include "kNet/DebugMemoryLeakCheck.h"

#include "kNet/TCPMessageConnection.h"
#include "kNet/UDPMessageConnection.h"
#include "kNet/NetworkWorkerThread.h"
#include "kNet/NetworkLogging.h"

namespace kNet
{

const int cMaxTCPSendSize = 256 * 1024;
const int cMaxUDPSendSize = 1400;

std::string Network::GetErrorString(int error)
{
#ifdef WIN32
	void *lpMsgBuf = 0;

	HRESULT hresult = HRESULT_FROM_WIN32(error);
	FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		0, hresult, 0 /*Default language*/, (LPTSTR) &lpMsgBuf, 0, 0);

	// Copy message to C++ -style string, since the data need to be freed before return.
	std::string message = (LPCSTR)lpMsgBuf;

	LocalFree(lpMsgBuf);
	return message;
#else
	return std::string(strerror(error));
#endif
}

int Network::GetLastError()
{
#ifdef WIN32
	return WSAGetLastError();
#else
	return errno;
#endif
}

std::string Network::GetLastErrorString()
{
	return GetErrorString(GetLastError());
}

std::string FormatBytes(size_t numBytes)
{
	char str[256];
	if (numBytes >= 1000 * 1000)
		sprintf(str, "%.3fMB", (float)numBytes / 1024.f / 1024.f);
	else if (numBytes >= 1000)
		sprintf(str, "%.3fKB", (float)numBytes / 1024.f);
	else
		sprintf(str, "%dB", (int)numBytes);
	return std::string(str);
}

Network::Network()
{
#ifdef WIN32
	memset(&wsaData, 0, sizeof(wsaData));
#endif
	Init();
}

Network::~Network()
{
	StopServer();
	DeInit();
}

void PrintLocalIP()
{
    char ac[80];
    if (gethostname(ac, sizeof(ac)) == KNET_SOCKET_ERROR)
	 {
        LOGNET("Error getting local host name!");
        return;
    }
    LOGNET("Host name is %s", ac);

    struct hostent *phe = gethostbyname(ac);
    if (phe == 0) {
        LOGNET("Bad host lookup.");
        return;
    }

    for (int i = 0; phe->h_addr_list[i] != 0; ++i)
	 {
        struct in_addr addr;
        memcpy(&addr, phe->h_addr_list[i], sizeof(struct in_addr));
        LOGNET("Address %d: %s", i, inet_ntoa(addr)); ///\todo inet_ntoa is deprecated! doesn't handle IPv6!
    }
}

void Network::PrintAddrInfo(const addrinfo *ptr)
{
	if (!ptr)
	{
		LOGNET("(Null pointer passed to Network::PrintAddrInfo!)");
		return;
	}

	LOGNET("\tFlags: 0x%x\n", ptr->ai_flags);
	LOGNET("\tFamily: ");
	switch(ptr->ai_family)
	{
	case AF_UNSPEC:
		LOGNET("Unspecified\n");
		break;
	case AF_INET:
		LOGNET("AF_INET (IPv4)\n");
		break;
	case AF_INET6:
		LOGNET("AF_INET6 (IPv6)\n");
		break;
#ifdef WIN32
	case AF_NETBIOS:
		LOGNET("AF_NETBIOS (NetBIOS)\n");
		break;
#endif
	default:
		LOGNET("Other %u\n", ptr->ai_family);
		break;
	}
	LOGNET("\tSocket type: ");
	switch(ptr->ai_socktype)
	{
	case 0:
		LOGNET("Unspecified\n");
		break;
	case SOCK_STREAM:
		LOGNET("SOCK_STREAM (stream)\n");
		break;
	case SOCK_DGRAM:
		LOGNET("SOCK_DGRAM (datagram) \n");
		break;
	case SOCK_RAW:
		LOGNET("SOCK_RAW (raw) \n");
		break;
	case SOCK_RDM:
		LOGNET("SOCK_RDM (reliable message datagram)\n");
		break;
	case SOCK_SEQPACKET:
		LOGNET("SOCK_SEQPACKET (pseudo-stream packet)\n");
		break;
	default:
		LOGNET("Other %u\n", ptr->ai_socktype);
		break;
	}
	LOGNET("\tProtocol: ");
	switch(ptr->ai_protocol)
	{
	case 0:
		LOGNET("Unspecified\n");
		break;
	case IPPROTO_TCP:
		LOGNET("IPPROTO_TCP (TCP)\n");
		break;
	case IPPROTO_UDP:
		LOGNET("IPPROTO_UDP (UDP) \n");
		break;
	default:
		LOGNET("Other %u\n", ptr->ai_protocol);
		break;
	}
	LOGNET("\tLength of this sockaddr: %d\n", ptr->ai_addrlen);
	LOGNET("\tCanonical name: %s\n", ptr->ai_canonname);

	char address[256];
	sprintf(address, "%d.%d.%d.%d",
		(unsigned int)(unsigned char)ptr->ai_addr->sa_data[2], (unsigned int)(unsigned char)ptr->ai_addr->sa_data[3],
		(unsigned int)(unsigned char)ptr->ai_addr->sa_data[4], (unsigned int)(unsigned char)ptr->ai_addr->sa_data[5]);

	LOGNET("Address of this sockaddr: %s.\n", address);

}

void Network::PrintHostNameInfo(const char *hostname, const char *port)
{
	addrinfo hints;

	//--------------------------------
	// Setup the hints address info structure
	// which is passed to the getaddrinfo() function
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	//--------------------------------
	// Call getaddrinfo(). If the call succeeds,
	// the result variable will hold a linked list
	// of addrinfo structures containing response
	// information
	addrinfo *result = NULL;
	unsigned long dwRetval = (unsigned long)getaddrinfo(hostname, port, &hints, &result);
	if (dwRetval != 0)
	{
		LOGNET("getaddrinfo failed with error: %d\n", dwRetval);
		return;
	}

	LOGNET("getaddrinfo returned success\n");

	int i = 1;

	// Retrieve each address and print out the hex bytes
	for (addrinfo *ptr = result; ptr != NULL; ptr = ptr->ai_next)
	{
		LOGNET("getaddrinfo response %d\n", i++);
		PrintAddrInfo(ptr);
	}

	freeaddrinfo(result);

	PrintLocalIP();
}

void Network::Init()
{
#ifdef WIN32
	// Initialize Winsock
	int result = WSAStartup(MAKEWORD(2,2), &wsaData);
	if (result != 0)
	{
		LOGNET("WSAStartup failed: %s(%d)", GetErrorString(result).c_str(), result);
		return;
	}
#endif

	char str[256];
	gethostname(str, 256);
	machineIP = str;
	LOGNET("gethostname returned %s", str);
}

NetworkWorkerThread *Network::GetOrCreateWorkerThread()
{
	static const int maxConnectionsPerThread = 1;

	// Find an existing thread with sufficiently low load.
	for(size_t i = 0; i < workerThreads.size(); ++i)
		if (workerThreads[i]->NumConnections() + workerThreads[i]->NumServers() < maxConnectionsPerThread)
			return workerThreads[i];

	// No appropriate thread found. Create a new one.
	NetworkWorkerThread *workerThread = new NetworkWorkerThread();
	workerThread->StartThread();
	workerThreads.push_back(workerThread);
	LOG(LogInfo, "Created a new NetworkWorkerThread. There are now %d worker threads.", workerThreads.size());
	return workerThread;
}

void Network::AssignConnectionToWorkerThread(Ptr(MessageConnection) connection)
{
	GetOrCreateWorkerThread()->AddConnection(connection);
}

NetworkServer *Network::StartServer(unsigned short port, SocketTransportLayer transport, INetworkServerListener *serverListener, bool allowAddressReuse)
{
	Socket *listenSock = OpenListenSocket(port, transport, allowAddressReuse);
	if (listenSock == 0)
	{
		LOGNET("Failed to start server. Could not open listen port to %d using %s.", (unsigned int)port, 
			transport == SocketOverTCP ? "TCP" : "UDP");
		return 0;
	}

	std::vector<Socket *> listenSockets;
	listenSockets.push_back(listenSock);

	server = new NetworkServer(this, listenSockets);
	server->RegisterServerListener(serverListener);

	GetOrCreateWorkerThread()->AddServer(server);

	LOGNET("Server up (%s). Waiting for client to connect...", listenSock->ToString().c_str());

	return server;
}

NetworkServer *Network::StartServer(const std::vector<std::pair<unsigned short, SocketTransportLayer> > &listenPorts, 
	INetworkServerListener *serverListener, bool allowAddressReuse)
{
	if (listenPorts.size() == 0)
	{
		LOGNET("Failed to start server, since you did not provide a list of ports to listen to in Network::StartServer()!");
		return 0;
	}

	std::vector<Socket *> listenSockets;

	for(size_t i = 0; i < listenPorts.size(); ++i)
	{
		Socket *listenSock = OpenListenSocket(listenPorts[i].first, listenPorts[i].second, allowAddressReuse);
		if (listenSock)
			listenSockets.push_back(listenSock);
	}

	if (listenSockets.size() == 0)
	{
		LOGNET("Failed to start server. No ports to listen to!");
		return 0;
	}

	server = new NetworkServer(this, listenSockets);
	server->RegisterServerListener(serverListener);

	GetOrCreateWorkerThread()->AddServer(server);

	LOGNET("Server up and listening on the following ports: ");
	{
	std::stringstream ss;
	ss << "UDP ";
	for(size_t i = 0; i < listenSockets.size(); ++i)
		if (listenSockets[i]->TransportLayer() == SocketOverUDP)
			ss << listenSockets[i]->LocalPort() << " ";
	LOGNET(ss.str().c_str());
	}
	{
		std::stringstream ss;
		ss << "TCP ";
		for(size_t i = 0; i < listenSockets.size(); ++i)
			if (listenSockets[i]->TransportLayer() == SocketOverTCP)
				ss << listenSockets[i]->LocalPort() << " ";
		LOGNET(ss.str().c_str());
	}

	return server;
}

void Network::StopServer()
{
	for(size_t i = 0; i < workerThreads.size(); ++i)
		workerThreads[i]->RemoveServer(server);

	///\todo This is a forceful stop. Perhaps have a benign teardown as well?
	server = 0;
	LOG(LogVerbose, "Network::StopServer: Deinitialized NetworkServer.");
}

void Network::CloseSocket(Socket *socket)
{
	if (!socket)
	{
		LOGNETVERBOSE("Network::CloseSocket() called with a null socket pointer!");
		return;
	}

	for(std::list<Socket>::iterator iter = sockets.begin(); iter != sockets.end(); ++iter)
		if (&*iter == socket)
		{
			socket->Close();
			// The Socket pointers MessageConnection objects have are pointers to this list,
			// so after calling this function with a Socket pointer, the Socket is deleted for good.
			sockets.erase(iter);
			LOGNET("Network::CloseSocket: Closed socket 0x%08X!", socket);
			return;
		}
	LOGNET("Network::CloseSocket: Tried to close a nonexisting socket 0x%08X!", socket);
}

void Network::CloseConnection(Ptr(MessageConnection) connection)
{
	LOG(LogVerbose, "Network::CloseConnection: Closing down connection 0x%X.", connection.ptr());
	if (!connection)
		return;

	for(size_t i = 0; i < workerThreads.size(); ++i)
		workerThreads[i]->RemoveConnection(connection);

	CloseSocket(connection->socket);
	connection->socket = 0;
}

void Network::DeInit()
{
	LOG(LogVerbose, "Network::DeInit: Closing down network worker thread.");
	PolledTimer timer;

	for(size_t i = 0; i < workerThreads.size(); ++i)
	{
		workerThreads[i]->StopThread();
		delete workerThreads[i];
	}
	workerThreads.clear();

	while(sockets.size() > 0)
	{
		sockets.front().Close();
		sockets.pop_front();
	}
#ifdef WIN32
	WSACleanup();
#endif
	LOG(LogWaits, "Network::DeInit: Deinitialized kNet Network object, took %f msecs.", timer.MSecsElapsed());
}

Socket *Network::OpenListenSocket(unsigned short port, SocketTransportLayer transport, bool allowAddressReuse)
{
	addrinfo *result = NULL;
	addrinfo *ptr = NULL;
	addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_flags = AI_PASSIVE;
	hints.ai_socktype = (transport == SocketOverTCP) ? SOCK_STREAM : SOCK_DGRAM;
	hints.ai_protocol = (transport == SocketOverTCP) ? IPPROTO_TCP : IPPROTO_UDP;

	char strPort[256];
	sprintf(strPort, "%d", (unsigned int)port);

	int ret = getaddrinfo(NULL, strPort, &hints, &result);
	if (ret != 0)
	{
		LOGNET("getaddrinfo failed: %s(%d)", GetErrorString(ret).c_str(), ret);
		return 0;
	}

	SOCKET listenSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
	LOGNET("Network::OpenListenSocket: Created listenSocket 0x%8X.", listenSocket);

	if (listenSocket == INVALID_SOCKET)
	{
		int error = GetLastError();
		LOGNET("Error at socket(): %s(%u)", GetErrorString(error).c_str(), error);
		freeaddrinfo(result);
		return 0;
	}

	if (allowAddressReuse)
	{
		// Allow other sockets to be bound to this address after this. 
		// (Possibly unsecure, only enable for development purposes - to avoid having to wait for the server listen socket 
		//  to time out if the server crashes.)
#ifdef WIN32
		BOOL val = TRUE;
		ret = setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, (const char *)&val, sizeof(val));
#else
		int val = 1;
		ret = setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
#endif
		if (ret != 0)
		{
			int error = GetLastError();
			LOG(LogError, "setsockopt to SO_REUSEADDR failed: %s(%d)", GetErrorString(error).c_str(), error);
		}
	}

	// Setup the listening socket - bind it to a port.
	// If we are setting up a TCP socket, the socket will be only for listening and accepting incoming connections.
	// If we are setting up an UDP socket, all connection initialization and data transfers will be managed through this socket.
	ret = bind(listenSocket, result->ai_addr, (int)result->ai_addrlen);
	if (ret == KNET_SOCKET_ERROR)
	{
		int error = GetLastError();
		LOGNET("bind failed: %s(%u) when trying to bind to port %d with transport %s", 
			GetErrorString(error).c_str(), error, port, transport == SocketOverTCP ? "TCP" : "UDP");
		closesocket(listenSocket);
		freeaddrinfo(result);
		return 0;
	}

	freeaddrinfo(result);

	// For a reliable TCP socket, start the server with a call to listen().
	if (transport == SocketOverTCP)
	{
		// Transition the bound socket to a listening state.
		ret = listen(listenSocket, SOMAXCONN);
		if (ret == KNET_SOCKET_ERROR)
		{
			int error = GetLastError();
			LOGNET("Error at listen(): %s(%u)", GetErrorString(error).c_str(), error);
			closesocket(listenSocket);
			return 0;
		}
	}

	const size_t maxSendSize = (transport == SocketOverTCP ? cMaxTCPSendSize : cMaxUDPSendSize);
	sockets.push_back(Socket(listenSocket, "", port, transport, maxSendSize, (transport == SocketOverUDP) ? true : false));
	Socket *listenSock = &sockets.back();
	listenSock->SetBlocking(false);

	return listenSock;
}

Socket *Network::ConnectSocket(const char *address, unsigned short port, SocketTransportLayer transport)
{
	addrinfo *result = NULL;
	addrinfo *ptr = NULL;
	addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = (transport == SocketOverTCP) ? SOCK_STREAM : SOCK_DGRAM;
	hints.ai_protocol = (transport == SocketOverTCP) ? IPPROTO_TCP : IPPROTO_UDP;

	char strPort[256];
	sprintf(strPort, "%d", (unsigned int)port);
	int ret = getaddrinfo(address, strPort, &hints, &result);
	if (ret != 0)
	{
		LOGNET("Network::Connect: getaddrinfo failed: %s(%d)", GetErrorString(ret).c_str(), ret);
		return 0;
	}

#ifdef WIN32
	SOCKET connectSocket = WSASocket(result->ai_family, result->ai_socktype, result->ai_protocol,
		NULL, 0, WSA_FLAG_OVERLAPPED);
#else
	SOCKET connectSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
	LOGNET("Created connectSocket 0x%8X.", connectSocket);
#endif
	if (connectSocket == INVALID_SOCKET)
	{
		int error = GetLastError();
		LOGNET("Network::Connect: Error at socket(): %s(%u)", GetErrorString(error).c_str(), error);
		freeaddrinfo(result);
		return 0;
	}

	// Connect to server.
#ifdef WIN32
	ret = WSAConnect(connectSocket, result->ai_addr, (int)result->ai_addrlen, 0, 0, 0, 0);
#else
	ret = connect(connectSocket, result->ai_addr, (int)result->ai_addrlen);
#endif

	if (ret == KNET_SOCKET_ERROR)
	{
		closesocket(connectSocket);
		connectSocket = INVALID_SOCKET;
	}

	freeaddrinfo(result);

	if (connectSocket == INVALID_SOCKET)
	{
		LOGNET("Unable to connect to server!");
		return 0;
	}

	sockaddr_in peername;
	socklen_t peernamelen = sizeof(peername);
	getpeername(connectSocket, (sockaddr*)&peername, &peernamelen); ///\todo Check return value.

	Socket socket(connectSocket, address, port, transport, (transport == SocketOverTCP) ? cMaxTCPSendSize : cMaxUDPSendSize, false);
	socket.SetUDPPeername(peername);

	socket.SetBlocking(false);
	sockets.push_back(socket);

	Socket *sock = &sockets.back();

	return sock;
}

Ptr(MessageConnection) Network::Connect(const char *address, unsigned short port, 
	SocketTransportLayer transport, IMessageHandler *messageHandler, Datagram *connectMessage)
{
	Socket *socket = ConnectSocket(address, port, transport);
	if (!socket)
		return 0;

	if (transport == SocketOverUDP)
	{
		SendUDPConnectDatagram(*socket, connectMessage);
		LOGNET("Network::Connect: Sent a UDP Connection Start datagram to to %s.", socket->ToString().c_str());
	}
	else
		LOGNET("Network::Connect: Connected a TCP socket to %s.", socket->ToString().c_str());

	Ptr(MessageConnection) connection;
	if (transport == SocketOverTCP)
		connection = new TCPMessageConnection(this, 0, socket, ConnectionOK);
	else
		connection = new UDPMessageConnection(this, 0, socket, ConnectionPending);

	connection->RegisterInboundMessageHandler(messageHandler);
	GetOrCreateWorkerThread()->AddConnection(connection);

	return connection;
}

Socket *Network::ConnectUDP(SOCKET connectSocket, const EndPoint &remoteEndPoint)
{
	sockaddr_in remoteAddr = remoteEndPoint.ToSockAddrIn();

	///\todo Not IPv6-capable.
	char strIp[256];
	sprintf(strIp, "%d.%d.%d.%d", remoteEndPoint.ip[0], remoteEndPoint.ip[1], remoteEndPoint.ip[2], remoteEndPoint.ip[3]);

	sockets.push_back(Socket(connectSocket, strIp, remoteEndPoint.port, SocketOverUDP, cMaxUDPSendSize, false));
	Socket *socket = &sockets.back();
	socket->SetBlocking(false);
	socket->SetUDPPeername(remoteEndPoint.ToSockAddrIn());

	LOGNET("Network::ConnectUDP: Connected an UDP socket to %s.", socket->ToString().c_str());
	return socket;
}

Socket *Network::StoreSocket(const Socket &cp)
{
	sockets.push_back(cp);
	return &sockets.back();
}

void Network::SendUDPConnectDatagram(Socket &socket, Datagram *connectMessage)
{
	OverlappedTransferBuffer *sendData = socket.BeginSend();
	if (!sendData)
	{
		LOG(LogError, "Network::SendUDPConnectDatagram: socket.BeginSend failed! Cannot send UDP connection datagram!");
	}
	if (connectMessage)
	{
		///\todo Craft the proper connection attempt datagram.
		sendData->buffer.len = std::min<int>(connectMessage->size, sendData->buffer.len);
		memcpy(sendData->buffer.buf, connectMessage->data, sendData->buffer.len);
		LOG(LogVerbose, "Network::SendUDPConnectDatagram: Sending UDP connect message of size %d.", sendData->buffer.len);
	}
	else
	{
		///\todo Craft the proper connection attempt datagram.
		sendData->buffer.len = std::min<int>(8, sendData->buffer.len);
		memset(sendData->buffer.buf, 0, sendData->buffer.len);
		LOG(LogVerbose, "Network::SendUDPConnectDatagram: Sending null UDP connect message of size %d.", sendData->buffer.len);
	}
	socket.EndSend(sendData);
}

} // ~kNet
