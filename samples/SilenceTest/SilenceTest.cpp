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

/** @file SilenceTest.cpp
	@brief This application tests that a connection that does not transfer any messages stays open and
	       does not time out. */

#include <iostream>
#include <fstream>
#include <string>
#include <string.h>
#include <utility>

#include "kNet.h"
#include "kNet/DebugMemoryLeakCheck.h"

using namespace std;
using namespace kNet;

void RunServer(short int port, SocketTransportLayer transport)
{
	Network network;
	NetworkServer *server = network.StartServer(port, transport, 0, true);
	if (!server)
	{
		LOG(LogUser, "Unable to start server!");
	}

	int connections = 0;
	for(;;)
	{
		server->Process();
		NetworkServer::ConnectionMap connectionMap = server->GetConnections();
		connections = max(server->GetConnections().size(), (size_t)connections);
		if (connections > 0 && server->GetConnections().size() == 0)
			break;

		for(NetworkServer::ConnectionMap::iterator iter = connectionMap.begin(); iter != connectionMap.end();
			++iter)
			if (!iter->second->IsPending() && !iter->second->IsReadOpen() && iter->second->IsWriteOpen())
				iter->second->Disconnect();
	}
	LOG(LogUser, "Closing down server.");
	server->Close(2000);
}

void RunClient(const char *address, unsigned short port, SocketTransportLayer transport, float msecsToWait)
{
	Network network;
	Ptr(MessageConnection) connection = network.Connect(address, port, transport, 0);
	if (!connection)
	{
		LOG(LogUser, "Network::Connect failed!");
		return;
	}
	connection->WaitToEstablishConnection();
	if (!connection->Connected())
	{
		LOG(LogUser, "Connection failed, server did not respond!");
		return;
	}

	PolledTimer timer(msecsToWait);
	while(!timer.Test())
	{
		NetworkMessage *msg = connection->ReceiveMessage();
		if (msg)
			connection->FreeMessage(msg);
		else
			Clock::Sleep(10);
	}
	Clock::Sleep((int)msecsToWait);
	connection->Disconnect();
	connection->Close();
}

void PrintUsage()
{
	cout << "Usage: " << endl;
	cout << "       server tcp|udp <port>" << endl;
	cout << "       client tcp|udp <hostname> <port> <msecsToWait>" << endl;
}

BottomMemoryAllocator bma;

int main(int argc, char **argv)
{
	cout << "kNet automatically drop the connection if the peer has not messaged " << endl;
	cout << "anything within a timeout period (currently fixed at 15 seconds)." << endl;
	cout << "This sample implements a client and a server which do not send any messages " << endl;
	cout << "to each other. kNet sends ping-pong messages every 3.5 seconds. These messages " << endl;
	cout << "are used to estimate the RTT, but also serve as keep-alive messages to know " << endl;
	cout << "when a timeout has occurred." << endl << endl;
	cout << "The purpose of this sample is to test that the kNet keep-alive mechanism works " << endl;
	cout << "and the connection does not time out." << endl;

	if (argc < 4)
	{
		PrintUsage();
		return 0;
	}

	EnableMemoryLeakLoggingAtExit();

	kNet::SetLogChannels((LogChannel)(-1) & ~LogObjectAlloc); // Enable all log channels.

	SocketTransportLayer transport = StringToSocketTransportLayer(argv[2]);
	if (transport == InvalidTransportLayer)
	{
		cout << "The second parameter is either 'tcp' or 'udp'!" << endl;
		return 0;
	}

	if (!_stricmp(argv[1], "server"))
	{
		unsigned short port = (unsigned short)atoi(argv[3]);

		RunServer(port, transport);
	}
	else if (!_stricmp(argv[1], "client"))
	{
		unsigned short port = (unsigned short)atoi(argv[4]);
		int msecsToWait = atoi(argv[5]);
		RunClient(argv[3], port, transport, (float)msecsToWait);
	}
	else
	{
		cout << "The second parameter is either 'send' or 'receive'!" << endl;
		return 0;
	}
}
