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

/** @file FileTransfer.cpp
	@brief Implements a file transfer application. The server acts as a receiver waiting for a
	       client to connect and send a file. */

#include <iostream>
#include <fstream>
#include <string>
#include <string.h>
#include <utility>

#include "kNet.h"
#include "kNet/DebugMemoryLeakCheck.h"

// If enabled, this sample is used for network transfer profiling and all disk operations are ignored.
//#define NULLTRANSFER

using namespace std;
using namespace kNet;

const unsigned long cFileTransferStartMessage = 30;
const unsigned long cFileTransferFragment = 31;

#ifdef LINUX
#define _stricmp strcasecmp
#endif

struct Fragment
{
	std::vector<char> data;
	size_t fragmentIndex;
};

class NetworkApp : public IMessageHandler, public INetworkServerListener
{
	Network network;

	// Used by the receiver to store partial received data.
	std::map<size_t, Fragment> fragments;

	size_t nextFragment;
	size_t totalFragments;
	size_t bytesReceived;
	std::string filename;
	std::ofstream out;
	tick_t transferStartTick;

	PolledTimer statsPrintTimer;
	static const int printIntervalMSecs = 4000;
public:

	NetworkApp()
	:nextFragment(0),
	totalFragments(0),
	bytesReceived(0)
	{
	}
	/// Called to notify the listener that a new connection has been established.
	void NewConnectionEstablished(MessageConnection *connection)
	{
		connection->RegisterInboundMessageHandler(this);
	}

	void HandleMessage(MessageConnection *source, message_id_t id, const char *data, size_t numBytes)
	{
		switch(id)
		{
		case cFileTransferStartMessage:
		{
			DataDeserializer dd(data, numBytes);
			filename = dd.ReadString();
			size_t fileSize = dd.Read<u32>();
			totalFragments = dd.Read<u32>();
			nextFragment = 0;
			bytesReceived = 0;
			statsPrintTimer.StartMSecs(printIntervalMSecs);
			cout << "Starting receive of file \"" << filename << "\". File size: " << fileSize << "B bytes, which is split into "
				<< totalFragments << " fragments." << endl;
			char str[256];
			sprintf(str, "received_%s", filename.c_str());
			out.open(str, ios::binary | ios::trunc);
			transferStartTick = Clock::Tick();
			break;
		}
		case cFileTransferFragment:
		{
			DataDeserializer dd(data, numBytes);
			size_t fragmentIndex = dd.Read<u32>();
			if (nextFragment == fragmentIndex)
			{
				++nextFragment;
				size_t numDataBytes = dd.BytesLeft();
#ifndef NULLTRANSFER
				out.write(data + dd.BytePos(), numDataBytes);
#endif
				bytesReceived += numDataBytes;
				WriteFinishedFragments();

				LOG(LogVerbose, "Received fragment %d.", fragmentIndex); 

				if (statsPrintTimer.Test())
				{
					const tick_t sendFinishTick = Clock::Tick();
					double timespan = (float)Clock::TimespanToSecondsD(transferStartTick, sendFinishTick);
					LOG(LogUser, "Received fragment %d. Elapsed: %.2f seconds. Bytes received: %d. Transfer rate: %s/sec.", 
						nextFragment-1, (float)timespan, bytesReceived, FormatBytes((size_t)(bytesReceived/timespan)).c_str());
					statsPrintTimer.StartMSecs(printIntervalMSecs);
				}
				
			}
			else // Queue up this fragment.
			{
//				cout << "Queued up received fragment " << fragmentIndex << endl;
				Fragment f;
				size_t numDataBytes = dd.BytesLeft();
#ifndef NULLTRANSFER
				f.data.insert(f.data.end(), data + dd.BytePos(), data + numBytes);
#endif
				f.fragmentIndex = fragmentIndex;
				fragments[fragmentIndex] = f;
				bytesReceived += numDataBytes;
			}
			break;
		}
		default:
			cout << "Received an unknown message with ID 0x" << std::hex << id << "!" << endl;
			break;
		}
	}

	void WriteFinishedFragments()
	{
		std::map<size_t, Fragment>::iterator iter = fragments.find(nextFragment);
		while(iter != fragments.end())
		{
#ifndef NULLTRANSFER
			out.write(&iter->second.data[0], iter->second.data.size());
#endif
			fragments.erase(nextFragment);
			++nextFragment;
			iter = fragments.find(nextFragment);
		}
	}

	void RunReceiver(unsigned short port, SocketTransportLayer transport)
	{
		nextFragment = 0;
		totalFragments = 0xFFFFFFFF;

		// Start the server either in TCP or UDP mode.
		NetworkServer *server = network.StartServer(port, transport, this);
		if (!server)
		{
			cout << "Unable to start server in port " << port << "!" << endl;
			return;
		}

		cout << "Server waiting for connection in port " << port << "." << endl;

		while(server->GetConnections().size() == 0)
		{
			server->Process();
			Clock::Sleep(1);
		}

		Ptr(MessageConnection) clientConnection = server->GetConnections().begin()->second;

		// Stop accepting any further connections.
		server->SetAcceptNewConnections(false);

		clientConnection->WaitToEstablishConnection(10000);

		statsPrintTimer.StartMSecs(printIntervalMSecs);

		transferStartTick = Clock::Tick();

		LOG(LogUser, "Waiting for file receive.");
		while(clientConnection->IsReadOpen())
		{
			server->Process();
			
			Clock::Sleep(1);
			if (statsPrintTimer.Test())
			{
				const tick_t sendFinishTick = Clock::Tick();
				double timespan = (float)Clock::TimespanToSecondsD(transferStartTick, sendFinishTick);
				LOG(LogUser, "Have received %d fragments (+%d out-of-order) (%.2f%%). Elapsed: %.2f seconds. Bytes received: %d. Transfer rate: %s/sec.", 
					nextFragment, fragments.size(), (nextFragment + fragments.size()) * 100.f / totalFragments,
					(float)timespan, bytesReceived, FormatBytes((size_t)(bytesReceived/timespan)).c_str());
				clientConnection->DumpStatus();
				statsPrintTimer.StartMSecs(printIntervalMSecs);
			}
		}
		if (nextFragment == totalFragments)
		{
			LOG(LogUser, "Finished receiving all fragments. File '%s' saved to disk, size: %d bytes. Closing connection.",
				filename.c_str(), bytesReceived);
		}
		else
		{
			LOG(LogUser, "Error: Sender specified the file '%s' to contain %d fragments, but the connection was closed after "
				"receiving %d fragments. Received a partial file of %d bytes.", filename.c_str(), totalFragments, nextFragment);
		}
		clientConnection->Close(15000);
	}

	void RunSender(const char *address, unsigned short port, SocketTransportLayer transport, const char *filename)
	{
		FILE *handle = fopen(filename, "rb");
		if (!handle)
		{
			LOG(LogUser, "Failed to open file %s!", filename);
			return;
		}
		fseek(handle, 0, SEEK_END);
		long fileSize = ftell(handle);
		if (fileSize <= 0)
		{
			fclose(handle);
			LOG(LogUser, "File %s has zero size!", filename);
			return;
		}

		fseek(handle, 0, SEEK_SET);
		std::vector<char> tempData;

		Ptr(MessageConnection) connection = network.Connect(address, port, transport, this);
		if (!connection)
		{
			cout << "Unable to connect to " << address << ":" << port << "." << endl;
			return;
		}

		// We have nothing better to do while waiting for the connection to build up, so wait modally.
		if (!connection->WaitToEstablishConnection(10000))
		{
			cout << "Failed to connect to server!" << endl;
			return;
		}

		cout << "Connected to " << connection->GetSocket()->ToString() << "." << endl;

		transferStartTick = Clock::Tick();

		NetworkMessage *msg = connection->StartNewMessage(cFileTransferStartMessage, 2048);
		DataSerializer ds(msg->data, 2048);
		const size_t fragmentSize = 450;
		const size_t numFragments = (fileSize + fragmentSize - 1) / fragmentSize;
		ds.AddString(filename);
		ds.Add<u32>(fileSize);
		ds.Add<u32>(numFragments);
		msg->priority = 100;
		msg->reliable = true;
		msg->inOrder = true;
		connection->EndAndQueueMessage(msg, ds.BytesFilled());

		size_t nextFragment = 0;
		size_t bytesSent = 0;

		LOG(LogUser, "Starting file transfer. File size: %dB, number of fragments: %d.",
			fileSize, numFragments);
		statsPrintTimer.StartMSecs(printIntervalMSecs);

		while(connection->IsWriteOpen())
		{
			connection->Process();
			Clock::Sleep(1); // A simple throttle on the send loop to avoid using 100% CPU.

			// Add new data fragments into the queue.
			const int outboundMsgQueueSize = 200;
			int i = 100;
			while(i-- > 0 && connection->IsWriteOpen() && connection->NumOutboundMessagesPending() < outboundMsgQueueSize && bytesSent < fileSize)
			{
				// File payload data bytes in this message.
				const size_t bytesInThisFragment = min((int)fragmentSize, (int)(fileSize - bytesSent));

				NetworkMessage *msg = connection->StartNewMessage(cFileTransferFragment, bytesInThisFragment+4);
				msg->priority = 100;
				msg->reliable = true;
				msg->inOrder = true;

				DataSerializer ds(msg->data, msg->Size());
				ds.Add<u32>(nextFragment++);

#ifndef NULLTRANSFER
				size_t read = fread(ds.GetData() + ds.BytesFilled(), sizeof(char), bytesInThisFragment, handle);
#else
				size_t read = bytesInThisFragment;
#endif
				if (read < bytesInThisFragment)
				{
					LOG(LogUser, "Failed to read file!");
					connection->Close(0);
				}
				
				connection->EndAndQueueMessage(msg);
				bytesSent += bytesInThisFragment;
			}

			// If we've put out all file fragments to the network, close the connection down.
			if (connection->IsWriteOpen() && bytesSent >= fileSize && connection->NumOutboundMessagesPending() == 0)
			{
				LOG(LogUser, "All data sent. Disconnecting.");
				connection->Disconnect(15000);
			}
			
			if (statsPrintTimer.Test())
			{
				const tick_t sendFinishTick = Clock::Tick();
				double timespan = (float)Clock::TimespanToSecondsD(transferStartTick, sendFinishTick);
				LOG(LogUser, "Sending fragment %d. Elapsed: %.2f seconds. Bytes sent: %d. Transfer rate: %s/sec.", 
					nextFragment-1, (float)timespan, bytesSent, FormatBytes((size_t)(bytesSent/timespan)).c_str());
				connection->DumpStatus();
				statsPrintTimer.StartMSecs(printIntervalMSecs);
			}
		}
		
		LOG(LogUser, "Waiting for peer to acknowledge all received data.");
		while((connection->NumOutboundMessagesPending() > 0 && connection->IsWriteOpen()) || connection->IsReadOpen())
		{
			connection->Process();
			Clock::Sleep(1);

			if (statsPrintTimer.TriggeredOrNotRunning())
			{
				connection->DumpStatus();
				statsPrintTimer.StartMSecs(printIntervalMSecs);
			}
		}

		const tick_t sendFinishTick = Clock::Tick();
		double timespan = (float)Clock::TimespanToSecondsD(transferStartTick, sendFinishTick);
		connection->DumpStatus();
		LOG(LogUser, "File transfer finished in %.2f seconds. Bytes sent: %d. Transfer rate: %s/sec. Closing connection.", 
			(float)timespan, bytesSent, FormatBytes((size_t)(bytesSent/timespan)).c_str());

		connection->Close(15000);
		fclose(handle);
	}
};

void PrintUsage()
{
	cout << "Usage: " << endl;
	cout << "       receive tcp|udp <port>" << endl;
	cout << "       send tcp|udp <hostname> <port> <filename>" << endl;
}

int main(int argc, char **argv)
{
	if (argc < 4)
	{
		PrintUsage();
		return 0;
	}

	EnableMemoryLeakLoggingAtExit();

//	kNet::SetLogChannels((LogChannel)(-1) & ~(LogObjectAlloc | LogVerbose)); // Enable all log channels.
	kNet::SetLogChannels(LogUser);

	SocketTransportLayer transport = SocketOverUDP;
	if (!_stricmp(argv[2], "tcp"))
		transport = SocketOverTCP;
	else if (!!_stricmp(argv[2], "udp"))
	{
		cout << "The second parameter is either 'tcp' or 'udp'!" << endl;
		return 0;
	}
	NetworkApp app;
	if (!_stricmp(argv[1], "receive"))
	{
		unsigned short port = atoi(argv[3]);

		app.RunReceiver(port, transport);
	}
	else if (!_stricmp(argv[1], "send"))
	{
		if (argc < 6)
		{
			PrintUsage();
			return 0;
		}

		unsigned short port = atoi(argv[4]);
		app.RunSender(argv[3], port, transport, argv[5]);
	}
	else
	{
		cout << "The second parameter is either 'send' or 'receive'!" << endl;
		return 0;
	}
}
