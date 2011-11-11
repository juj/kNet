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

/** @file SpeedTest.cpp
	@brief */

#include <iostream>
#include <string>

#include "kNet.h"
#include "kNet/DebugMemoryLeakCheck.h"

using namespace std;
using namespace kNet;

const unsigned long cDataMessage = 50;

class NetworkApp : public IMessageHandler, public INetworkServerListener
{
	Network network;
	NetworkServer *server;
	size_t flowRate;
	Ptr(MessageConnection) client;
public:

	/// Called to notify the listener that a new connection has been established.
	void NewConnectionEstablished(MessageConnection *connection)
	{
		client = connection;
		connection->RegisterInboundMessageHandler(this);

		NetworkMessage *msg = connection->StartNewMessage(cDataMessage);
		msg->priority = 100;
		msg->reliable = true;
		connection->EndAndQueueMessage(msg);
	}

	void HandleMessage(MessageConnection *source, message_id_t id, const char *data, size_t numBytes)
	{
		switch(id)
		{
		case cDataMessage:
			LOG(LogVerbose, "Received a message of size %d bytes and ID %d.", numBytes, id);
			break;
		default:
			LOG(LogUser, "Received an unknown message with ID 0x%X!", id);
			break;
		}
	}

	void RunServer(unsigned short port, SocketTransportLayer transport, size_t flowRate_)
	{
		flowRate = flowRate_;
		client = 0;

		// Start the server either in TCP or UDP mode.
		server = network.StartServer(port, transport, this, true);
		if (!server)
		{
			cout << "Unable to start server in port " << port << "!" << endl;
			return;
		}

		cout << "Server waiting for connection in port " << port << "." << endl;

		PolledTimer statsPrintTimer(2000.f);

		for(;;)
		{
			server->Process();

			Clock::Sleep(1);

			if (statsPrintTimer.Test())
			{
				if (client)
				{
					if (client->GetConnectionState() == ConnectionClosed || 
						client->GetConnectionState() == ConnectionPeerClosed)
						break;
					client->DumpStatus();
				}
				statsPrintTimer.StartMSecs(2000.f);
			}
		}
	}

	void RunClient(const char *address, unsigned short port, SocketTransportLayer transport, 
		size_t numMessagesToSend, size_t messageSize, bool reliable, size_t flowRate)
	{
		Ptr(MessageConnection) connection = network.Connect(address, port, transport, this);
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

		RunSpeedTest(connection, numMessagesToSend, messageSize, reliable);
	}

	void SendMessage(MessageConnection *connection, size_t size, bool reliable)
	{
		NetworkMessage *msg = connection->StartNewMessage(cDataMessage, size);
		msg->priority = 100;
		msg->reliable = reliable;
		// The message we send out here will be 'size' bytes long. The contents will
		// be all garbage, since we do not initialize msg->data to anything. But
		// in this sample, we are not interested in the message contents.
		connection->EndAndQueueMessage(msg);
	}

	void RunSpeedTest(MessageConnection *connection, size_t numMessagesToSend, size_t messageSize, bool reliable)
	{
		PolledTimer statsPrintTimer(2000.f);

		cout << "Starting speed test." << endl;
		while(numMessagesToSend > 0 || connection->NumOutboundMessagesPending() > 0)
		{
			connection->Process();

			// Keep the outbound queue full with data.
			while(connection->NumOutboundMessagesPending() < 1000 && numMessagesToSend > 0)
			{
				SendMessage(connection, messageSize, reliable);
				--numMessagesToSend;
			}

			Clock::Sleep(10);

			if (statsPrintTimer.Test())
			{
				connection->DumpStatus();
				statsPrintTimer.StartMSecs(2000.f);
			}
		}

		cout << "Finished speed test." << endl;
	}
};

void PrintUsage()
{
	cout << "Usage: " << endl;
	cout << "       server tcp|udp port" << endl;
	cout << "       client tcp <hostname> <port> <numMessages> <messageSize>" << endl;
	cout << "       client udp <hostname> <port> <numMessages> <messageSize> <reliable> [flowRate]" << endl;
}

int main(int argc, char **argv)
{
	if (argc < 4)
	{
		PrintUsage();
		return 0;
	}

	EnableMemoryLeakLoggingAtExit();

	SocketTransportLayer transport = StringToSocketTransportLayer(argv[2]);
	if (transport == InvalidTransportLayer)
	{
		cout << "The second parameter is either 'tcp' or 'udp'!" << endl;
		return 0;
	}
	NetworkApp app;
	if (!_stricmp(argv[1], "server"))
	{
		unsigned short port = atoi(argv[3]);

		size_t flowRate = (transport == SocketOverUDP && argc >= 5) ? atoi(argv[4]) : 0;

		app.RunServer(port, transport, flowRate);
	}
	else if (!_stricmp(argv[1], "client"))
	{
		if ((transport == SocketOverTCP && argc < 7) || (transport == SocketOverUDP && argc < 8))
		{
			PrintUsage();
			return 0;
		}

		const char *hostname = argv[3];
		unsigned short port = atoi(argv[4]);
		size_t numMessages = atoi(argv[5]);
		size_t messageSize = atoi(argv[6]);
		bool reliable = true;

		if (transport == SocketOverUDP && !_stricmp(argv[7], "false"))
			reliable = false;

		size_t flowRate = (argc >= 9) ? atoi(argv[8]) : 0;

		app.RunClient(hostname, port, transport, numMessages, messageSize, reliable, flowRate);
	}
	else
	{
		cout << "The second parameter is either 'server' or 'client'!" << endl;
		return 0;
	}
}
