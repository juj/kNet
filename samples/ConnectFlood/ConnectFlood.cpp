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

/** @file ConnectFlood.cpp
	@brief A program that hammers the given server with multiple concurrent connections
		to test how it behaves. */

#include <iostream>
#include <string>

#include "kNet.h"

using namespace std;
using namespace kNet;

#ifdef LINUX
#define _stricmp strcasecmp
#endif

class NetworkApp : public IMessageHandler
{
	Network network;
public:
	void HandleMessage(MessageConnection *,message_id_t,const char *,size_t)
	{
	}

	void RunClient(const char *address, unsigned short port, SocketTransportLayer transport, int numConcurrentConnections, int numTotalConnections)
	{
		cout << "Starting connection flood.";

		std::vector<MessageConnection*> connections;
		int numConnectionAttempts = 0;
		while(numConnectionAttempts < numTotalConnections && connections.size() > 0)
		{
			// Start new connections.
			while((int)connections.size() < numConcurrentConnections && numConnectionAttempts < numTotalConnections)
			{
				++numConnectionAttempts;
				MessageConnection *connection = network.Connect(address, port, transport, this);
				if (connection)
					connections.push_back(connection);
				else
					break;
			}

			// Disconnect any established connections.
			for(int i = 0; i < (int)connections.size(); ++i)
			{
				if (connections[i]->GetConnectionState() == ConnectionOK ||
					connections[i]->GetConnectionState() == ConnectionClosed)
				{
					connections[i]->Close();
					connections.erase(connections.begin() + i);
					--i;
				}
			}

			Clock::Sleep(1);
		}

		cout << "Finished connection flood." << endl;
	}
};

void PrintUsage()
{
	cout << "Usage: " << endl;
	cout << "       tcp|udp <hostname> <port> <numConcurrentConnections> <numTotalConnections>" << endl;
}

int main(int argc, char **argv)
{
	if (argc < 6)
	{
		PrintUsage();
		return 0;
	}

	EnableMemoryLeakLoggingAtExit();

	SocketTransportLayer transport = SocketOverUDP;
	if (!_stricmp(argv[1], "tcp"))
		transport = SocketOverTCP;
	else if (!!_stricmp(argv[1], "udp"))
	{
		cout << "The second parameter is either 'tcp' or 'udp'!" << endl;
		return 0;
	}
	NetworkApp app;

	const char *hostname = argv[2];
	unsigned short port = atoi(argv[3]);
	int numConcurrentConnections = atoi(argv[4]);
	int numTotalConnections = atoi(argv[5]);

	app.RunClient(hostname, port, transport, numConcurrentConnections, numTotalConnections);
}
