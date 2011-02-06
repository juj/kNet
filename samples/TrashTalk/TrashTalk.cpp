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

/** @file TrashTalk.cpp
	@brief Opens a raw Socket connection to a server and sends random bytes to it. */

#include <iostream>
#include <string>
#include <vector>
#include <ctime>

#include "kNet.h"

#ifdef UNIX
#define _stricmp strcasecmp
#endif

using namespace std;
using namespace kNet;

class NetworkApp : public IMessageHandler
{
	Network network;
public:
	void HandleMessage(MessageConnection *,message_id_t,const char *,size_t)
	{
	}

	void RunClient(const char *address, unsigned short port, SocketTransportLayer transport, int numMessages, int messageSize)
	{
		srand((unsigned int)time(NULL));

		cout << "Connecting to server. ";

		// Note: Here we don't build a proper MessageConnection, instead just work on a low-level abstraction layer
		// to be able to send raw data.

		Socket *socket = network.ConnectSocket(address, port, transport);
		if (!socket)
		{
			cout << "Failed to connect!" << endl;
			return;
		}

		for(int i = 0; i < numMessages; ++i)
		{
			std::vector<char> data;
			while((int)data.size() < messageSize)
				data.push_back(rand() & 0xFF);

			socket->Send(&data[0], data.size());

			Clock::Sleep((rand() % 100) + 1);
		}
		socket->Disconnect();
		socket->Close();
		network.DeleteSocket(socket);

		cout << "Finished sending data." << endl;
	}
};

void PrintUsage()
{
	cout << "Usage: " << endl;
	cout << "       tcp|udp <hostname> <port> <numMessages> <messageSize>" << endl;
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
	int numMessages = atoi(argv[4]);
	int messageSize = atoi(argv[5]);

	app.RunClient(hostname, port, transport, numMessages, messageSize);
}
