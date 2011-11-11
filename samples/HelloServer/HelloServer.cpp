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

/** @file HelloServer.cpp
	@brief */

#include "kNet.h"
#include "kNet/DebugMemoryLeakCheck.h"

using namespace kNet;

// Define a MessageID for our a custom message.
const message_id_t cHelloMessageID = 10;

// This object gets called for notifications on new network connection events.
class ServerListener : public INetworkServerListener
{
public:
	void NewConnectionEstablished(MessageConnection *connection)
	{
		const int maxMsgBytes = 256;
		// Start building a new message.
		NetworkMessage *msg = connection->StartNewMessage(cHelloMessageID, maxMsgBytes);
		msg->reliable = true;

		// Create a DataSerializer object with a buffer of 256 bytes.
		DataSerializer ds(msg->data, maxMsgBytes);
		// Add a message string.
		ds.AddString(std::string("Hello! You are connecting from ") + connection->RemoteEndPoint().ToString());
		// Push the message out to the client.
		connection->EndAndQueueMessage(msg, ds.BytesFilled());
		LOG(LogUser, "Client connected from %s.", connection->ToString().c_str());
	}

	void ClientDisconnected(MessageConnection *connection)
	{
		connection->Disconnect();
	}
};

int main()
{
	EnableMemoryLeakLoggingAtExit();

	Network network;
	ServerListener listener;

	kNet::SetLogChannels(LogUser | LogInfo | LogError);

	LOG(LogUser, "Starting server.");
	// Start listening on a port.
	const unsigned short cServerPort = 1234;
	NetworkServer *server = network.StartServer(cServerPort, SocketOverUDP, &listener, true);

	if (server)
	{
		LOG(LogUser, "Waiting for incoming connections.");
		// Run the main server loop.
		// This never returns since we don't call NetworkServer::Stop(), but for this example, it doesn't matter.  
		server->RunModalServer();
	}
}
