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

/** @file LatencyTest.cpp
	@brief Tests the network latency between a server and a client. */

#include <iostream>
#include <string>
#include <cmath>

#include "kNet.h"

#ifdef LINUX
#define _stricmp strcasecmp
#endif

using namespace std;
using namespace kNet;

class NetworkApp : public IMessageHandler, public INetworkServerListener
{
	Network network;
	NetworkServer *server;
public:

	/// Called to notify the listener that a new connection has been established.
	void NewConnectionEstablished(MessageConnection *connection)
	{
		connection->RegisterInboundMessageHandler(this);

		NetworkMessage *msg = connection->StartNewMessage(100);
		msg->priority = 100;
		msg->reliable = true;
		connection->EndAndQueueMessage(msg);
	}

	void HandleMessage(MessageConnection *source, message_id_t id, const char *data, size_t numBytes)
	{
		cout << "Received a message with ID 0x" << std::hex << id << "!" << endl;
	}

	void RunServer(unsigned short port, SocketTransportLayer transport)
	{
		// Start the server either in TCP or UDP mode.
		server = network.StartServer(port, transport, this, true);
		if (!server)
		{
			cout << "Unable to start server in port " << port << "!" << endl;
			return;
		}

		cout << "Server waiting for connection in port " << port << "." << endl;

		server->RunModalServer();
	}

	void RunClient(const char *address, unsigned short port, SocketTransportLayer transport)
	{
		MessageConnection *connection = network.Connect(address, port, transport, this);
		if (!connection)
		{
			cout << "Unable to connect to " << address << ":" << port << "." << endl;
			return;
		}

		cout << "Waiting for connection.." << endl;
		while(connection->GetConnectionState() == ConnectionPending)
			Clock::Sleep(100);

		if (connection->GetConnectionState() != ConnectionOK)
		{
			cout << "Failed to connect to server!" << endl;
			return;
		}

		cout << "Connected to " << connection->ToString() << "." << endl;

		RunLatencyTest(connection);
	}

	void RunLatencyTest(MessageConnection *connection)
	{
		PolledTimer disconnectTimer(30 * 1000.f);

		float previousPing = 0.f;
		float previousRTT = 0.f;

		while(!disconnectTimer.Test())
		{
			connection->Process();

			Clock::Sleep(10);

			// Print latency statistics whenever it has changed.
			if (fabs(previousPing - connection->Latency()) > 1e-3f || 
				fabs(previousRTT - connection->RoundTripTime()) > 1e-3f)
			{
				previousPing = connection->Latency();
				previousRTT = connection->RoundTripTime();

				connection->DumpStatus();
			}
		}

		connection->Disconnect();
		connection->RunModalClient();
	}
};

void PrintUsage()
{
	cout << "Usage: " << endl;
	cout << "       server tcp|udp port" << endl;
	cout << "       client tcp|udp hostname port" << endl;
}

int main(int argc, char **argv)
{
	if (argc < 4)
	{
		PrintUsage();
		return 0;
	}

	EnableMemoryLeakLoggingAtExit();

	SocketTransportLayer transport = SocketOverUDP;
	if (!_stricmp(argv[2], "tcp"))
		transport = SocketOverTCP;
	else if (!!_stricmp(argv[2], "udp"))
	{
		cout << "The third parameter is either 'tcp' or 'udp'!" << endl;
		return 0;
	}
	NetworkApp app;
	if (!_stricmp(argv[1], "server"))
	{
		unsigned short port = atoi(argv[3]);

		app.RunServer(port, transport);
	}
	else if (!_stricmp(argv[1], "client"))
	{
		if (argc < 5)
		{
			PrintUsage();
			return 0;
		}

		const char *hostname = argv[3];
		unsigned short port = atoi(argv[4]);

		app.RunClient(hostname, port, transport);
	}
	else
		cout << "The second parameter is either 'server' or 'client'!" << endl;

	return 0;
}
