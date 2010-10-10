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

/** @file NetworkServer.cpp
	@brief */

#include "kNet/Network.h"

#include "kNet/DebugMemoryLeakCheck.h"

#include "kNet/NetworkServer.h"
#include "kNet/TCPMessageConnection.h"
#include "kNet/UDPMessageConnection.h"
#include "kNet/Datagram.h"
#include "kNet/NetworkWorkerThread.h"
#include "kNet/NetworkLogging.h"
#include "kNet/Clock.h"

#include <iostream>

namespace kNet
{

NetworkServer::NetworkServer(Network *owner_, std::vector<Socket *> listenSockets_)
:owner(owner_), listenSockets(listenSockets_), acceptNewConnections(true), networkServerListener(0),
udpConnectionAttempts(64)
{
	assert(owner);
	assert(listenSockets.size() > 0);
}

NetworkServer::~NetworkServer()
{
}

void NetworkServer::RegisterServerListener(INetworkServerListener *listener)
{
	networkServerListener = listener;
}

void NetworkServer::SetAcceptNewConnections(bool acceptNewConnections_)
{
	acceptNewConnections = acceptNewConnections_;
}

void NetworkServer::CloseListenSockets()
{
	assert(owner);

	for(size_t i = 0; i < listenSockets.size(); ++i)
	{
		if (listenSockets[i]->TransportLayer() == SocketOverUDP)
			acceptNewConnections = false; ///\todo At this point, if in UDP mode, we should have destroyed all connections that use this socket!
		else
			owner->CloseSocket(listenSockets[i]); 
	}

	// Now forget all sockets - not getting them back in any way.
	listenSockets.clear();
}

Socket *NetworkServer::AcceptConnections(Socket *listenSocket)
{
	if (!listenSocket || !listenSocket->Connected())
		return 0;

	sockaddr_in clientAddr;
	memset(&clientAddr, 0, sizeof(clientAddr));
	socklen_t addrLen = sizeof(clientAddr);
	SOCKET &listenSock = listenSocket->GetSocketHandle();
	SOCKET acceptSocket = accept(listenSock, (sockaddr*)&clientAddr, &addrLen);
	if (acceptSocket == KNET_ACCEPT_FAILURE)
	{
		int error = Network::GetLastError();
		if (error != KNET_EWOULDBLOCK)
		{
			LOGNET("accept failed: %s(%u)", Network::GetErrorString(error).c_str(), error);
			closesocket(listenSock);
			listenSock = INVALID_SOCKET;
		}
		return 0;
	}
	LOGNET("Accepted incoming connectiond 0x%8X.", acceptSocket);
   
	EndPoint ep = EndPoint::FromSockAddrIn(clientAddr);
	std::string address = ep.IPToString();

	unsigned short port = ep.port;
	LOGNET("Client connected from %s:%d.\n", address.c_str(), (unsigned int)port);

	const size_t maxTcpSendSize = 65536;
	Socket *socket = owner->StoreSocket(Socket(acceptSocket, address.c_str(), port, SocketOverTCP, maxTcpSendSize, false));
	socket->SetBlocking(false);
	socket->SetUDPPeername(clientAddr);

	return socket;
}

void NetworkServer::CleanupDeadConnections()
{
	Lockable<ConnectionMap>::LockType clientsLock = clients.Acquire();

	// Clean up all disconnected/timed out connections.
	ConnectionMap::iterator iter = clientsLock->begin();
	while(iter != clientsLock->end())
	{
		ConnectionMap::iterator next = iter;
		++next;
		if (!iter->second->Connected())
		{
			LOGNET("Client %s disconnected!", iter->second->ToString().c_str());
			if (networkServerListener)
				networkServerListener->ClientDisconnected(iter->second);
			if (iter->second->GetSocket() && iter->second->GetSocket()->TransportLayer() == SocketOverTCP)
				owner->CloseConnection(iter->second);
			clientsLock->erase(iter->first);
		}
		iter = next;
	}
}

void NetworkServer::Process()
{
	CleanupDeadConnections();

	for(size_t i = 0; i < listenSockets.size(); ++i)
	{
		Socket *listen = listenSockets[i];

		if (listen->TransportLayer() == SocketOverTCP)
		{
			// Accept the first inbound connection.
			Socket *client = AcceptConnections(listen);
			if (client)
			{
				if (!client->Connected())
					LOG(LogError, "Warning: Accepted an already closed connection!?");

				LOG(LogInfo, "Client connected from %s.", client->ToString().c_str());

				// Build a message connection on top of the raw socket.
				Ptr(MessageConnection) clientConnection;
				assert(listen->TransportLayer() == SocketOverTCP);
				clientConnection = new TCPMessageConnection(owner, this, client, ConnectionOK);
				assert(owner->WorkerThread());
				owner->WorkerThread()->AddConnection(clientConnection);

				if (networkServerListener)
					networkServerListener->NewConnectionEstablished(clientConnection);

				sockaddr_in sockname;
				socklen_t socknamelen = sizeof(sockname);
				int ret = getpeername(client->GetSocketHandle(), (sockaddr*)&sockname, &socknamelen);
				if (ret != 0)
					LOGNET("getpeername failed for %s!", client->ToString().c_str());

				EndPoint endPoint = EndPoint::FromSockAddrIn(sockname);
				std::cout << "Client connected from " << endPoint.ToString() << std::endl;

				Lockable<ConnectionMap>::LockType clientsLock = clients.Acquire();
				(*clientsLock)[endPoint] = clientConnection;
			}
		}
	}

	// Note that the above loop will only accept one new connection/socket/iteration, so if there are multiple
	// pending new connections, they will only get accepted at a rate of one per each frame.

	// Process a new UDP connection attempt.
	ConnectionAttemptDescriptor *desc = udpConnectionAttempts.Front();
	if (desc)
	{
		ProcessNewUDPConnectionAttempt(desc->listenSocket, desc->peer, (const char *)desc->data.data, desc->data.size);
		udpConnectionAttempts.PopFront();
	}

	// Process all new inbound data for each connection handled by this server.
	Lockable<ConnectionMap>::LockType clientsLock = clients.Acquire();
	for(ConnectionMap::iterator iter = clientsLock->begin(); iter != clientsLock->end(); ++iter)
		iter->second->Process();
}

void NetworkServer::ReadUDPSocketData(Socket *listenSocket)
{
	using namespace std;

	assert(listenSocket);

	OverlappedTransferBuffer *recvData = listenSocket->BeginReceive();
	if (!recvData)
		return; // No datagram available, return.
	if (recvData->bytesContains == 0)
	{
		listenSocket->EndReceive(recvData);
		LOGNET("Received 0 bytes of data in NetworkServer::ReadUDPSocketData!");
		return;
	}
	EndPoint endPoint = EndPoint::FromSockAddrIn(recvData->from); ///\todo Omit this conversion for performance.
	LOG(LogData, "Received a datagram of size %d to socket %s from endPoint %s.", recvData->bytesContains, listenSocket->ToString().c_str(),
		endPoint.ToString().c_str());
	Lockable<ConnectionMap>::LockType clientsLock = clients.Acquire();
	ConnectionMap::iterator iter = clientsLock->find(endPoint); ///\todo HashTable for performance.
	if (iter != clientsLock->end())
	{
		// If the datagram came from a known endpoint, pass it to the connection object that handles that endpoint.
		UDPMessageConnection *udpConnection = dynamic_cast<UDPMessageConnection *>(iter->second.ptr());
		if (!udpConnection)
		{
			LOGNET("Critical! UDP socket data received into a TCP socket!");
		}
		else
			udpConnection->ExtractMessages(recvData->buffer.buf, recvData->bytesContains);
	}
	else
	{
		// The endpoint for this datagram is not known, deserialize it as a new connection attempt packet.
		EnqueueNewUDPConnectionAttempt(listenSocket, endPoint, recvData->buffer.buf, recvData->bytesContains);
	}
	listenSocket->EndReceive(recvData);
}

void NetworkServer::EnqueueNewUDPConnectionAttempt(Socket *listenSocket, const EndPoint &endPoint, const char *data, size_t numBytes)
{
	ConnectionAttemptDescriptor desc;
	desc.data.size = std::min<int>(cDatagramBufferSize, numBytes);
	memcpy(&desc.data.data[0], data, desc.data.size);
	desc.peer = endPoint;
	desc.listenSocket = listenSocket;

	///\todo Check IP banlist.
	///\todo Check that the maximum number of active concurrent connections is not exceeded.

	bool success = udpConnectionAttempts.Insert(desc);
	if (!success)
		LOGNET("Too many connection attempts!");
	else
		LOGNET("Queued new connection attempt from %s.", endPoint.ToString().c_str());
}

bool NetworkServer::ProcessNewUDPConnectionAttempt(Socket *listenSocket, const EndPoint &endPoint, const char *data, size_t numBytes)
{
	LOGNET("New inbound connection attempt from %s with datagram of size %d.", endPoint.ToString().c_str(), numBytes);
	if (!acceptNewConnections)
	{
		LOGNET("Ignored connection attempt since server is set not to accept new connections.");
		return false;
	}

	// Pass the datagram contents to a callback that decides whether this connection is allowed.
	if (networkServerListener)
	{
		bool connectionAccepted = networkServerListener->NewConnectionAttempt(endPoint, data, numBytes);
		if (!connectionAccepted)
		{
			LOGNET("Server listener did not accept the new connection.");
			return false;
		}
	}

	///\todo Check IP banlist.
	///\todo Check that the maximum number of active concurrent connections is not exceeded.

	SOCKET sock = listenSocket->GetSocketHandle();

	// Accept the connection and create a new UDP socket that communicates to that endpoint.
	Socket *socket = owner->ConnectUDP(sock, endPoint);
	if (!socket)
	{
		LOGNET("Network::ConnectUDP failed! Cannot accept new UDP connection.");
		return false;
	}

	UDPMessageConnection *udpConnection = new UDPMessageConnection(owner, this, socket, ConnectionOK);
	Ptr(MessageConnection) connection(udpConnection);
	udpConnection->SetUDPSlaveMode(true);
	{
		Lockable<ConnectionMap>::LockType clientsLock = clients.Acquire();
		(*clientsLock)[endPoint] = connection;
	}

	// Pass the MessageConnection to the main application so it can hook the inbound packet stream.
	if (networkServerListener)
		networkServerListener->NewConnectionEstablished(connection);

	connection->SendPingRequestMessage();

	owner->WorkerThread()->AddConnection(connection);

	LOG(LogInfo, "Accepted new UDP connection.");
	return true;
}

void NetworkServer::BroadcastMessage(const NetworkMessage &msg, MessageConnection *exclude)
{
	Lockable<ConnectionMap>::LockType clientsLock = clients.Acquire();
	for(ConnectionMap::iterator iter = clientsLock->begin(); iter != clientsLock->end(); ++iter)
	{
		MessageConnection *connection = iter->second;
		if (connection == exclude)
			continue;

		SendMessage(msg, *connection);
	}
}

void NetworkServer::BroadcastMessage(unsigned long id, bool reliable, bool inOrder, unsigned long priority, 
                                     unsigned long contentID, const char *data, size_t numBytes,
                                     MessageConnection *exclude)
{
	Lockable<ConnectionMap>::LockType clientsLock = clients.Acquire();
	for(ConnectionMap::iterator iter = clientsLock->begin(); iter != clientsLock->end(); ++iter)
	{
		MessageConnection *connection = iter->second;
		assert(connection);
		if (connection == exclude || !connection->IsWriteOpen())
			continue;

		NetworkMessage *msg = connection->StartNewMessage(id, numBytes);
		msg->reliable = reliable;
		msg->inOrder = inOrder;
		msg->priority = priority;
		msg->contentID = contentID;
		assert(msg->data);
		assert(msg->Size() == numBytes);
		memcpy(msg->data, data, numBytes);
		connection->EndAndQueueMessage(msg);
	}
}

void NetworkServer::SendMessage(const NetworkMessage &msg, MessageConnection &destination)
{
	if (!destination.IsWriteOpen())
		return;

	NetworkMessage *cloned = destination.StartNewMessage(msg.id);
	*cloned = msg;
	destination.EndAndQueueMessage(cloned);
}

void NetworkServer::DisconnectAllClients()
{
	SetAcceptNewConnections(false);

	Lockable<ConnectionMap>::LockType clientsLock = clients.Acquire();
	for(ConnectionMap::iterator iter = clientsLock->begin(); iter != clientsLock->end(); ++iter)
		iter->second->Disconnect(0); // Do not wait for any client.
}

void NetworkServer::Close(int disconnectWaitMilliseconds)
{
	DisconnectAllClients();

	///\todo Re-implement this function to remove the monolithic Sleep here. Instead of this,
	/// wait for the individual connections to finish.
	if (GetConnections().size() > 0)
	{
		Clock::Sleep(disconnectWaitMilliseconds);
		LOG(LogVerbose, "NetworkServer::Close: Waited a fixed period of %d msecs for all connections to disconnect.",
			disconnectWaitMilliseconds);
	}

	Lockable<ConnectionMap>::LockType clientsLock = clients.Acquire();
	for(ConnectionMap::iterator iter = clientsLock->begin(); iter != clientsLock->end(); ++iter)
		iter->second->Close(0); // Do not wait for any client.
}

void NetworkServer::RunModalServer()
{
	assert(this);

	///\todo Loop until StopModalServer() is called.
	for(;;)
	{
		Process();

		///\todo WSACreateEvent/WSAWaitForMultipleEvents for improved responsiveness and performance.
		Clock::Sleep(1);
	}
}

void NetworkServer::ConnectionClosed(MessageConnection *connection)
{
	Lockable<ConnectionMap>::LockType clientsLock = clients.Acquire();
	for(ConnectionMap::iterator iter = clientsLock->begin(); iter != clientsLock->end(); ++iter)
		if (iter->second == connection)
		{
			if (networkServerListener)
				networkServerListener->ClientDisconnected(connection);

			if (connection->GetSocket() && connection->GetSocket()->TransportLayer() == SocketOverTCP)
			{
				owner->CloseSocket(connection->socket);
				connection->socket = 0;
			}

			clientsLock->erase(iter);

			return;
		}

	LOGNET("Unknown MessageConnection passed to NetworkServer::Disconnect!");
}

std::vector<Socket *> &NetworkServer::ListenSockets()
{
	return listenSockets;
}

NetworkServer::ConnectionMap NetworkServer::GetConnections()
{ 
	Lockable<ConnectionMap>::LockType lock = clients.Acquire();
	return *lock;
}

} // ~kNet
