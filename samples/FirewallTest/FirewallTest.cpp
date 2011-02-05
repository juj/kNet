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

/** @file FirewallTest.cpp
	@brief Implements a server that listens on multiple TCP or UDP ports and a client that scans
	       a connection to that server by finding a free port. */

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <ctime>

#include "kNet.h"

using namespace std;
using namespace kNet;

#ifdef LINUX
#define _stricmp strcasecmp
#endif

class NetworkApp : public IMessageHandler, public INetworkServerListener
{
	Network network;
	NetworkServer *server;
	int numMessagesReceived;

public:
	NetworkApp()
	:server(0),
	numMessagesReceived(0)
	{
		srand((unsigned int)time(NULL));
	}

	void NewConnectionEstablished(MessageConnection *connection)
	{
		connection->RegisterInboundMessageHandler(this);
	}

	void HandleMessage(MessageConnection *connection, message_id_t id, const char *msg, size_t size)
	{
		cout << "Received a message with ID " << id << " and size " << size << "." << endl;
		switch(id)
		{
		case 123:
			SendRandomMessage(connection, 456, size);
			++numMessagesReceived;
			break;
		case 456:
			++numMessagesReceived;
			break;
		default:
			break;
		}
	}

	void RunServer(std::vector< std::pair<unsigned short, SocketTransportLayer> > ports)
	{
		server = network.StartServer(ports, this, true);
		if (server)
			server->RunModalServer();
	}

	void SendRandomMessage(MessageConnection *connection, int id, int size)
	{
		NetworkMessage *msg = connection->StartNewMessage(id, size);
		for(int i = 0; i < (int)msg->Size(); ++i)
			msg->data[i] = rand() & 0xFF;
		msg->reliable = true;
		connection->EndAndQueueMessage(msg);
	}

	bool ClientTestMessaging(MessageConnection *connection)
	{
		numMessagesReceived = 0;
		int numMessagesSent = 0;
		for(int size = 500; size <= 2000; size += 500)
		{
			SendRandomMessage(connection, 123, size);
			++numMessagesSent;
		}

		// We expect to get a response to each of the message sent above.
		PolledTimer waitTimer(15000.f);
		while(!waitTimer.Test() && connection->Connected() && numMessagesReceived < numMessagesSent)
		{
			connection->Process();
			Clock::Sleep(1);
		}

		return numMessagesReceived == numMessagesSent && numMessagesReceived > 0;
	}

	void RunClient(const char *address, std::vector< std::pair<unsigned short, SocketTransportLayer> > ports)
	{
		std::ofstream out("resultlog.txt");
		out << "Probing connection to address " << address << endl;

		const int numConnectionAttempts = 1;
		for(size_t i = 0; i < ports.size(); ++i)
			for(int j = 0; j < numConnectionAttempts; ++j)
			{
				std::stringstream result;
				result << "Connection to port " << ((ports[i].second == SocketOverTCP) ? "TCP" : "UDP") 
						<< "/" << ports[i].first << " ";

				Ptr(MessageConnection) connection = network.Connect(address, ports[i].first, ports[i].second, this);
				if (connection)
				{
					PolledTimer waitTimer;
					waitTimer.StartMSecs(5000.f);
					while(connection->GetConnectionState() == ConnectionPending && !waitTimer.Test())
					{
						connection->Process();
						Clock::Sleep(1);
					}
					if (connection->GetConnectionState() == ConnectionOK)
					{
						bool success = ClientTestMessaging(connection);
						if (success)
						{
							j = numConnectionAttempts;
							result << "successful and data passed ok." << endl;
						}
						else
							result << "successful, but messaging failed." << endl;
					}
					else
						 result << "failed." << endl;
				}
				else result << "failed." << endl;

				out << result.str();
				cout << result.str();

				if (connection)
					connection->Disconnect();
				out.flush();
		}
	}
};

std::vector<std::pair<unsigned short, SocketTransportLayer> >
	LoadPortsList(const char *filename)
{
	std::vector<std::pair<unsigned short, SocketTransportLayer> > ports;

	std::ifstream in(filename);
	while(!in.eof() && in.good())
	{
		unsigned short port;
		std::string transport;
		in >> port;
		in >> transport;
		if (transport == "UDP")
			ports.push_back(std::make_pair(port, SocketOverUDP));
		if (transport == "TCP")
			ports.push_back(std::make_pair(port, SocketOverTCP));
	}
	return ports;
}

int main(int argc, char **argv)
{
	EnableMemoryLeakLoggingAtExit();

	// See http://technet.microsoft.com/en-us/library/cc959828.aspx
	// and http://technet.microsoft.com/en-us/library/cc959829.aspx
	// and http://technet.microsoft.com/en-us/library/cc959833.aspx

	std::vector< std::pair<unsigned short, SocketTransportLayer> > ports =
		LoadPortsList("ports.txt");

	NetworkApp app;

	if (argc < 2)
		return 0;

	const char *serverOrClient = argv[1];

	if (!_stricmp(serverOrClient, "server"))
		app.RunServer(ports);
	else if (argc >= 3)
	{
		const char *hostname = argv[2];
		app.RunClient(hostname, ports);
	}
}
