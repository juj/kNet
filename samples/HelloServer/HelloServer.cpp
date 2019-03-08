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
const message_id_t cRegisterMessageID = 11;
const message_id_t cGeneralMessageID = 12;

class UserContext
{
public:
	explicit UserContext(MessageConnection* conn): messageConnection(conn)
	{
	}

	~UserContext()
	{
		KNET_LOG(LogUser, "Client '%s' is being destroyed" , userName.c_str());
	}

	void OnPeerData(packet_id_t pid, message_id_t mid, const char* data, size_t len)
	{
		MessageConnection* conn = messageConnection;
		if (mid == cRegisterMessageID)
		{
            DataDeserializer dd(data, len);

			userName = dd.ReadString();
			KNET_LOG(LogUser, "Client '%s' connected from %s.", userName.c_str(), conn->ToString().c_str());

			const int maxBytesCount = 256;
			NetworkMessage* msg = conn->StartNewMessage(cGeneralMessageID, maxBytesCount);
			msg->reliable = true;
			DataSerializer ds(msg->data, maxBytesCount);
			ds.AddString("Hello, " + userName + "! What can I do for you?");
			conn->EndAndQueueMessage(msg, ds.BytesFilled());
		}
		else
        {
            DataDeserializer dd(data, len);
			KNET_LOG(LogUser, "Get message from %s: %s.", userName.c_str(), dd.ReadString().c_str());
		}
	}

private:
	MessageConnection* messageConnection;
	std::string userName;
};


// This object gets called for notifications on new network connection events.
class ServerListener : public INetworkServerListener, public IMessageHandler
{
public:
	void HandleMessage(MessageConnection* source, packet_id_t packetId, message_id_t messageId, const char* data,
					   size_t numBytes)
	{
		UserContext* uc = source->GetUserContext<UserContext>();
		uc->OnPeerData(packetId, messageId, data, numBytes);
	}

	void NewConnectionEstablished(MessageConnection *connection)
	{
		UserContext* uc = new UserContext(connection);
		connection->SetUserContext(uc);
		connection->RegisterInboundMessageHandler(this);

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
		KNET_LOG(LogUser, "Client connected from %s.", connection->ToString().c_str());
	}

	void ClientDisconnected(MessageConnection *connection)
	{
		connection->Disconnect();
		UserContext* uc = connection->GetUserContext<UserContext>();
		delete uc;
	}
};

BottomMemoryAllocator bma;

int main()
{
	EnableMemoryLeakLoggingAtExit();

	Network network;
	ServerListener listener;

	kNet::SetLogChannels(LogUser | LogInfo | LogError);

	KNET_LOG(LogUser, "Starting server.");
	// Start listening on a port.
	const unsigned short cServerPort = 1234;
	NetworkServer *server = network.StartServer(cServerPort, SocketOverUDP, &listener, true);

	if (server)
	{
		KNET_LOG(LogUser, "Waiting for incoming connections.");
		// Run the main server loop.
		// This never returns since we don't call NetworkServer::Stop(), but for this example, it doesn't matter.  
		server->RunModalServer();
	}
}
