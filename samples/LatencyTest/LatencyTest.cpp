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

/** @file LatencyTest.cpp
	@brief Tests the network latency between a server and a client. */

#include <iostream>
#include <string>
#include <cmath>

#include "kNet.h"

using namespace std;
using namespace kNet;

const message_id_t customPingMessageId = 100;
const message_id_t customPingReplyMessageId = 101;

class NetworkApp : public IMessageHandler, public INetworkServerListener
{
	Network network;
	NetworkServer *server;

	tick_t pingSendTime;
	u16 sentPingNumber;
public:
	NetworkApp()	
	{
		sentPingNumber = 0;
	}

	void SendPingMessage(MessageConnection *connection)
	{
		NetworkMessage *msg = connection->StartNewMessage(customPingMessageId, 2);
		msg->priority = 100;
		msg->reliable = false;
		DataSerializer ds(msg->data, msg->Size());
		++sentPingNumber;
		ds.Add<u16>(sentPingNumber);
		connection->EndAndQueueMessage(msg);
		pingSendTime = Clock::Tick();
		cout << "Sent PING_" << sentPingNumber << "." << endl;
	}

	void SendPingReplyMessage(MessageConnection *connection, const char *receivedPingData, size_t receivedPingDataNumBytes)
	{
		DataDeserializer dd(receivedPingData, receivedPingDataNumBytes);
		NetworkMessage *msg = connection->StartNewMessage(customPingReplyMessageId, 2);
		msg->priority = 100;
		msg->reliable = false;
		DataSerializer ds(msg->data, msg->Size());
		u16 pingNumber = dd.Read<u16>();
		ds.Add<u16>(pingNumber);
		connection->EndAndQueueMessage(msg);
		cout << "Received PING_" << pingNumber << ". Sent PONG_" << pingNumber << "." << endl;
	}

	void HandlePingReplyMessage(const char *receivedPingData, size_t receivedPingDataNumBytes)
	{
		DataDeserializer dd(receivedPingData, receivedPingDataNumBytes);
		u16 receivedPingNumber = dd.Read<u16>();
		if (receivedPingNumber == sentPingNumber)
			cout << "Received PONG_" << receivedPingNumber << " in " << Clock::TimespanToMillisecondsF(pingSendTime, Clock::Tick()) << " msecs from sending PING_" << sentPingNumber << "." << std::endl;		
		else
			cout << "Received old PONG_" << receivedPingNumber << endl;
	}

	/// Called to notify the listener that a new connection has been established.
	void NewConnectionEstablished(MessageConnection *connection)
	{
		connection->RegisterInboundMessageHandler(this);
	}

	void HandleMessage(MessageConnection *source, packet_id_t packetId, message_id_t messageId, const char *data, size_t numBytes)
	{
		if (messageId == customPingMessageId)
			SendPingReplyMessage(source, data, numBytes);
		else if (messageId == customPingReplyMessageId)
			HandlePingReplyMessage(data, numBytes);
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

		for(;;)
		{
			server->Process();
			Clock::Sleep(1);
		}
	}

	void RunClient(const char *address, unsigned short port, SocketTransportLayer transport)
	{
		Ptr(MessageConnection) connection = network.Connect(address, port, transport, this);
		if (!connection)
		{
			cout << "Unable to connect to " << address << ":" << port << "." << endl;
			return;
		}

		connection->RegisterInboundMessageHandler(this);

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
		PolledTimer pingSendTimer(2000.f);

		float previousRTT = 0.f;

		for(;;)
		{
			connection->Process();

			Clock::Sleep(1);
			if (pingSendTimer.TriggeredOrNotRunning())
			{
				cout << "kNet-estimated round trip time is " << connection->RoundTripTime() << " msecs." << std::endl; 
				SendPingMessage(connection);
				pingSendTimer.StartMSecs(2000.f);
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

BottomMemoryAllocator bma;

int main(int argc, char **argv)
{
	cout << "This sample measures the latency between two hosts." << endl;
	cout << "kNet runs its own internal ping-pong messaging automatically, and " << endl;
	cout << "allows you to query for the currently estimated RTT by calling " << endl;
	cout << "MessageConnection::RoundTripTime()." << endl << endl;
	cout << "As an example, this sample implements a manual client-side " << endl;
	cout << "latency estimation mechanism. The functionality is very " << endl;
	cout << "similar to how kNet works internally." << endl;

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
