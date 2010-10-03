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

/** @file Network.h
	@brief The class Network. The root point for creating client and server objects. */

#ifdef LINUX
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#endif

#include "Socket.h"
#include "NetworkServer.h"
#include "MessageConnection.h"

namespace kNet
{

class NetworkWorkerThread;

/// Provides the application an interface for both client and server networking.
class Network
{
public:
	Network();
	~Network();

	static void PrintAddrInfo(const addrinfo *address);

	void PrintHostNameInfo(const char *hostname, const char *port);

	/// Starts a network server that listens to the given local port.
	/// @param serverListener [in] A pointer to the listener object that will be registered to receive notifications
	///	about incoming connections.
	NetworkServer *StartServer(unsigned short port, SocketTransportLayer transport, INetworkServerListener *serverListener);

	/// Starts a network server that listens to multiple local ports.
	/// This version of the function is given a list of pairs (port, UDP|TCP) values
	/// and the server will start listening on each of them.
	NetworkServer *StartServer(const std::vector<std::pair<unsigned short, SocketTransportLayer> > &listenPorts, INetworkServerListener *serverListener);

	void StopServer();

	/// Connects a raw socket (low-level, no MessageConnection abstraction) to the given destination.
	Socket *ConnectSocket(const char *address, unsigned short port, SocketTransportLayer transport);

	/// Closes (==frees) the given Socket object. After calling this function, do not dereference that Socket pointer,
	/// as it is deleted.
	void CloseSocket(Socket *socket);

	void CloseConnection(Ptr(MessageConnection) connection);

	/** Connects to the given address:port using kNet over UDP or TCP. When you are done with the connection,
		free it by letting the refcount go to 0. */
	Ptr(MessageConnection) Connect(const char *address, unsigned short port, SocketTransportLayer transport, IMessageHandler *messageHandler, Datagram *connectMessage = 0);

	/// @return Local machine IP.
	const char *MachineIP() const { return machineIP.c_str(); }

	NetworkWorkerThread *WorkerThread() { return workerThread; }

	/// Returns the error string associated with the given networking error id.
	static std::string GetErrorString(int error);

	/// Returns the error string corresponding to the last error that occurred in the networking library.
	static std::string GetLastErrorString();

	/// Returns the error id corresponding to the last error that occurred in the networking library.
	static int GetLastError();

private:
	std::string machineIP;

	/// Maintains the server-related data structures if this computer
	/// is acting as a server. Otherwise this data is not used.
	Ptr(NetworkServer) server;
	/// Contains all active sockets in the system.
	std::list<Socket> sockets;

	/// Takes the ownership of the given socket, and returns a pointer to the owned one.
	Socket *StoreSocket(const Socket &cp);

	friend class NetworkServer;

	void SendUDPConnectDatagram(Socket &socket, Datagram *connectMessage);

	/// Returns a new UDP socket that is bound to communicating with the given endpoint.
	/// Does NOT send the connection packet.
	/// The returned pointer is owned by this class.
	Socket *ConnectUDP(SOCKET connectSocket, const EndPoint &remoteEndPoint);

	/// Opens a new socket that listens on the given port using the given transport.
	Socket *OpenListenSocket(unsigned short port, SocketTransportLayer transport);

	NetworkWorkerThread *workerThread;

	void Init();
	void DeInit();

#ifdef WIN32
	WSADATA wsaData;
#endif
};

/// Outputs the given number of bytes formatted to KB or MB suffix for readability.
std::string FormatBytes(size_t numBytes);

} // ~kNet
