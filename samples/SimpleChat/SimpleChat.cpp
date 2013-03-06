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

/** @file SimpleChat.cpp
	@brief */

#include <iostream>
#include <string>

#include "kNet.h"
#include "kNet/DebugMemoryLeakCheck.h"

//#undef _MSC_VER

using namespace std;
using namespace kNet;

const unsigned long cChatMessageID = 42;

class NetworkApp : public INetworkServerListener, public IMessageHandler
{
	Network network;
	// If we are the server, this pointer will be nonzero.
	NetworkServer *server;
	// If we are the client, this pointer will be nonzero.
	Ptr(MessageConnection) connection;

	// Will contain the chat message as it is being typed on the command line.
	std::string inputText;

public:
	NetworkApp()
	:server(0)
	{
	}

	// The server must implement this message.
	bool NewConnectionAttempt(const EndPoint &/*endPoint*/, Datagram &/*datagram*/)
	{
		// Allow the client to connect.
		return true;
	}

	/// Called to notify the listener that a new connection has been established.
	void NewConnectionEstablished(MessageConnection *connection)
	{
		cout << "New connection from " << connection->ToString() << "." << endl;
		connection->RegisterInboundMessageHandler(this);
	}

	void HandleMessage(MessageConnection *source, packet_id_t /*packetId*/, message_id_t messageId, const char *data, size_t numBytes)
	{
		if (messageId == cChatMessageID)
			OnChatMessageReceived(source, data, numBytes);
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

		while(server->GetConnections().size() == 0)
		{
			server->Process();
			Clock::Sleep(100);
		}

		Ptr(MessageConnection) clientConnection = server->GetConnections().begin()->second;

		// Stop accepting any further connections.
//		server->SetAcceptNewConnections(false);

		RunChat(clientConnection);
	}

	void RunClient(const char *address, unsigned short port, SocketTransportLayer transport)
	{
		connection = network.Connect(address, port, transport, 0);
		if (!connection)
		{
			cout << "Unable to connect to " << address << ":" << port << "." << endl;
			return;
		}

		cout << "Waiting for connection.." << endl;
		bool success = connection->WaitToEstablishConnection();
		if (!success)
		{
			cout << "Failed to connect to server!" << endl;
			return;
		}

		cout << "Connected to " << connection->GetSocket()->ToString() << "." << endl;

		RunChat(connection);
	}

	void OnChatMessageReceived(MessageConnection *connection, const char *message, int numBytes)
	{
		char str[1025] = {};
		strncpy(str, message, numBytes-1); // '-1' to take into account the null byte in the message.
		// Note: We can't assume the sender sent the null byte in the message, it might be an ill-crafted message!
		if (inputText.length() > 0)
			cout << endl;
		cout << connection->RemoteEndPoint().ToString() << " says: " << str << endl;
		if (inputText.length() > 0)
			cout << "> " << inputText;

		// If we are the server, make the received message go to all other connected clients as well.
		if (server)
			server->BroadcastMessage(cChatMessageID, true, true, 100, 0, message, numBytes, connection);
	}

	void SendChatMessage(const char *message)
	{
		// Always remember to sanitize data at both ends.
		const unsigned long cMaxMsgSize = 1024;
		if (strlen(message) >= cMaxMsgSize)
			throw NetException("Tried to send too large chat message!");

		const size_t messageLength = strlen(message)+1; // Add one to the length to guarantee a null byte to the stream.
		if (server)
			server->BroadcastMessage(cChatMessageID, true, true, 100, 0, message, messageLength);
		else if (connection && connection->IsWriteOpen())
			connection->SendMessage(cChatMessageID, true, true, 100, 0, message, messageLength);
	}

#ifdef _MSC_VER // On windows, poll for new messages to send from the console instead of blocking.
	void Win32PeekConsoleInput()
	{
		// Get the standard input handle.
		HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
		if (hStdin == INVALID_HANDLE_VALUE)
			throw std::exception("Failed to get stdin handle!");

		// Wait for the events.  
		INPUT_RECORD irInBuf[128];
		DWORD numRead = 0;
		if (!PeekConsoleInput(hStdin, irInBuf, 1, &numRead))
			throw std::exception("PeekConsoleInput failed!"); 
		if (numRead > 0)
		{
			if (!ReadConsoleInput(hStdin, irInBuf, 128, &numRead))
				throw std::exception("ReadConsoleInput failed!"); 
			for (DWORD i = 0; i < numRead; i++) 
				if (irInBuf[i].EventType == KEY_EVENT && irInBuf[i].Event.KeyEvent.bKeyDown == TRUE)
					if (irInBuf[i].Event.KeyEvent.wVirtualKeyCode == VK_RETURN) // Enter key
					{
						if (inputText == "exit" || inputText == "quit" || inputText == "q")
						{
							if (connection)
								connection->Close();
							if (server)
								server->Close(500);
						}
						else if (inputText.length() > 0)
							SendChatMessage(inputText.c_str());
						inputText = "";
						cout << endl;
					}
					else
					{
						char str[2] = {};
						str[0] = irInBuf[i].Event.KeyEvent.uChar.AsciiChar;
						if (str[0] >= 32 && str[0] <= 127)
						{
							if (inputText.length() == 0)
								cout << "> ";
							cout << str[0]; // Echo the character we inputted back to console (yes, there might be a flag for this)
							inputText += std::string(str);
						}
					}
		}
	}
#endif

	void RunChat(Ptr(MessageConnection) connection)
	{
#ifdef _MSC_VER
		// Get the standard input handle.
		HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
		if (hStdin == INVALID_HANDLE_VALUE)
			throw std::exception("Failed to get stdin handle!");

		// Save the current input mode, to be restored on exit. 
		DWORD oldInputMode;
		if (!GetConsoleMode(hStdin, &oldInputMode)) 
			throw std::exception("GetConsoleMode failed!"); 
 
		// Enable the window and mouse input events.  
		if (!SetConsoleMode(hStdin, ENABLE_WINDOW_INPUT)) 
			throw std::exception("SetConsoleMode failed!");
#endif

		cout << "Chat running. Type messages and send them by pressing Enter. Type 'q'/'exit'/'quit' to disconnect." << endl;

		while((server && server->GetConnections().size() > 0) || (connection && connection->IsReadOpen()))
		{
			if (server)
				server->Process();

			if (!connection->IsReadOpen())
				connection->Disconnect();

			for(int i = 0; i < 100; ++i) // Process a maximum of 100 messages at one go.
			{
				NetworkMessage *msg = connection->ReceiveMessage(50);
				if (!msg)
					break;

				if (msg->id == cChatMessageID && msg->Size() > 0)
					OnChatMessageReceived(connection, &msg->data[0], msg->Size());
				connection->FreeMessage(msg);
			}

#ifdef _MSC_VER
			Win32PeekConsoleInput();
#else
			char inputText[256] = {};
			cout << "Enter a text to say: ";
			cin.getline(inputText, 255, '\n');
			if (strlen(inputText) > 0)
			{
				if (!strcmp(inputText, "quit") || !strcmp(inputText, "exit") || !strcmp(inputText, "q"))
					connection->Close();
				else
					SendChatMessage(inputText);
			}
#endif
			// Nothing in the above sleeps or blocks to wait for any events. Sleep() to keep the CPU use down.
			kNet::Clock::Sleep(1);
		}
		cout << "Disconnected." << endl;
	}
};

void PrintUsage()
{
	cout << "Usage: " << endl;
	cout << "       tcp|udp server port" << endl;
	cout << "       tcp|udp client hostname port" << endl;
}

BottomMemoryAllocator bma;

int main(int argc, char **argv)
{
	if (argc < 4)
	{
		PrintUsage();
		return 0;
	}

	kNet::SetLogChannels(LogUser | LogInfo | LogError);

	EnableMemoryLeakLoggingAtExit();

	SocketTransportLayer transport = StringToSocketTransportLayer(argv[1]);
	if (transport == InvalidTransportLayer)
	{
		cout << "The first parameter is either 'tcp' or 'udp'!" << endl;
		return 0;
	}
	NetworkApp app;
	if (!_stricmp(argv[2], "server"))
	{
		unsigned short port = (unsigned short)atoi(argv[3]);

		app.RunServer(port, transport);
	}
	else if (!_stricmp(argv[2], "client"))
	{
		if (argc < 5)
		{
			PrintUsage();
			return 0;
		}

		unsigned short port = (unsigned short)atoi(argv[4]);
		app.RunClient(argv[3], port, transport);
	}
	else
	{
		cout << "The second parameter is either 'server' or 'client'!" << endl;
		return 0;
	}
}