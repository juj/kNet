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

/** @file NetworkWorkerThread.cpp
	@brief */

#include <utility>

#include "kNet/UDPMessageConnection.h"

#include "kNet/DebugMemoryLeakCheck.h"

#include "kNet/NetworkWorkerThread.h"
#include "kNet/NetworkLogging.h"
#include "kNet/Event.h"
#include "kNet/EventArray.h"
#include "kNet/Clock.h"
#include "kNet/Thread.h"

using namespace std;

namespace kNet
{

NetworkWorkerThread::NetworkWorkerThread()
{
}

void NetworkWorkerThread::AddConnection(Ptr(MessageConnection) connection)
{
	workThread.Hold();
	Lockable<std::vector<Ptr(MessageConnection)> >::LockType lock = connections.Acquire();
	lock->push_back(connection);
	LOGNET("Added connection 0x%8X to NetworkWorkerThread.", connection.ptr());
	workThread.Resume();
}

void NetworkWorkerThread::RemoveConnection(Ptr(MessageConnection) connection)
{
	workThread.Hold();

	Lockable<std::vector<Ptr(MessageConnection)> >::LockType lock = connections.Acquire();

	for(size_t i = 0; i < lock->size(); ++i)
		if ((*lock)[i] == connection)
		{
			lock->erase(lock->begin() + i);
			LOGNET("NetworkWorkerThread::RemoveConnection: Connection 0x%8X removed.", connection.ptr());
			workThread.Resume();
			return;
		}
//	LOGNET("NetworkWorkerThread::RemoveConnection called for a nonexisting connection 0x%8X!", connection.ptr());
	workThread.Resume();
}

void NetworkWorkerThread::AddServer(Ptr(NetworkServer) server)
{
	workThread.Hold();
	Lockable<std::vector<Ptr(NetworkServer)> >::LockType lock = servers.Acquire();
	lock->push_back(server);
	LOGNET("Added server 0x%8X to NetworkWorkerThread.", server.ptr());
	workThread.Resume();
}

void NetworkWorkerThread::RemoveServer(Ptr(NetworkServer) server)
{
	workThread.Hold();

//#ifdef VERBOSELOGGING
	PolledTimer timer;
	Lockable<std::vector<Ptr(NetworkServer)> >::LockType lock = servers.Acquire();
	float lockWait = timer.MSecsElapsed();
	LOG(LogWaits, "NetworkWorkerThread::RemoveServer: Waited %f msecs to lock servers list.",
		lockWait);

	for(size_t i = 0; i < lock->size(); ++i)
		if ((*lock)[i] == server)
		{
			lock->erase(lock->begin() + i);
			LOGNET("NetworkWorkerThread::RemoveServer: Server 0x%8X removed.", server.ptr());
			workThread.Resume();
			return;
		}
//	LOGNET("NetworkWorkerThread::RemoveServer called for a nonexisting server 0x%8X!", server.ptr());
	workThread.Resume();
}

void NetworkWorkerThread::StartThread()
{
	workThread.Run(this, &NetworkWorkerThread::MainLoop);
}

void NetworkWorkerThread::StopThread()
{
	workThread.Stop();
}

int NetworkWorkerThread::NumConnections() const
{
	return connections.Acquire()->size();
}

int NetworkWorkerThread::NumServers() const
{
	return servers.Acquire()->size();
}

void NetworkWorkerThread::MainLoop()
{
	std::vector<MessageConnection*> writeWaitConnections;

	EventArray waitEvents;

	// This is an event that is always false and will never be set.
	Event falseEvent = CreateNewEvent(EventWaitDummy);
	assert(!falseEvent.IsNull());
	assert(falseEvent.Test() == false);

	LOGNET("NetworkWorkerThread running main loop.");

	std::vector<MessageConnection*> connectionList;
	std::vector<NetworkServer*> serverList;

	while(!workThread.ShouldQuit())
	{
		workThread.CheckHold();
		if (workThread.ShouldQuit())
			break;
	
		{
			Lockable<std::vector<Ptr(MessageConnection)> >::LockType lock = connections.Acquire();
			connectionList.clear();
			for(size_t i = 0; i < lock->size(); ++i)
				connectionList.push_back((*lock)[i].ptr());
		}
		{
			Lockable<std::vector<Ptr(NetworkServer)> >::LockType serverLock = servers.Acquire();
			serverList.clear();
			for(size_t i = 0; i < serverLock->size(); ++i)
				serverList.push_back((*serverLock)[i].ptr());
		}

		// Inconveniency: Cannot wait for long time periods, since this will call select() or WSAWaitForMultipleObjects,
		// which does not support aborting from the wait if the thread is signalled to interrupt and quit/join. To fix
		// this, should add a custom "interrupt Event" into the WaitArray to wake the thread up when it is supposed to be killed.
		// For now, just sleep only small periods of time at once to make this issue not a problem at application exit time.
		const int maxWaitTime = 50; // msecs. ///\todo Make this a lot larger, like, 2000msecs, once the thread interrupts are handled in Sleep and EventArray::Wait.
		int waitTime = maxWaitTime;

		waitEvents.Clear();
		writeWaitConnections.clear();

		// Next, build the event array that is used for waiting on the sockets.
		// At odd indices we will have socket read events, and at even indices the socket write events.
		// After the events for each connection, we will have the UDP listen sockets for each UDP server connection.
		for(size_t i = 0; i < connectionList.size(); ++i)
		{
			MessageConnection &connection = *connectionList[i];
			UDPMessageConnection *udpConnection = dynamic_cast<UDPMessageConnection*>(&connection);

			connection.UpdateConnection();
			if (connection.GetConnectionState() == ConnectionClosed || !connection.GetSocket() || !connection.GetSocket()->Connected()) // This does not need to be checked each iteration.
				continue;

			// The event that is triggered when data is received on the socket.
			Event readEvent = connection.GetSocket()->GetOverlappedReceiveEvent();
			if (readEvent.IsNull() || connection.GetSocket()->IsUDPSlaveSocket())
				readEvent = falseEvent; // If this socket is not readable, add a false event to skip this event slot.
			waitEvents.AddEvent(readEvent);

			// Determine which event to listen to for sending out data. There are three factors:
			// 1) Is the socket ready for sending (data buffer -wise)?
			// 2) Are there new messages to send?
			// 3) Does the send throttle timer allow us to send data to the socket? (UDP only)

			// If true, this socket is ready to receive new data to be sent.
			bool socketSendReady = connection.GetSocket()->IsOverlappedSendReady() || connection.GetSocket()->GetOverlappedSendEvent().Test();
			// If true, this MessageConnection has new unsent data that needs to be sent out.
			bool socketMessagesAvailable = connection.NumOutboundMessagesPending() > 0 || connection.NewOutboundMessagesEvent().Test();

			if (socketSendReady && socketMessagesAvailable)
			{
				if (connection.GetSocket()->TransportLayer() == SocketOverUDP)
				{
					int msecsLeftUntilWrite = (int)connection.TimeUntilCanSendPacket();
					waitTime = min(waitTime, msecsLeftUntilWrite);
					writeWaitConnections.push_back(&connection);
					waitEvents.AddEvent(falseEvent);
					assert(falseEvent.Test() == false && !falseEvent.IsNull());
				}
				else // TCP socket
					waitEvents.AddEvent(connection.NewOutboundMessagesEvent());
			}
			else if (socketMessagesAvailable) // Here, socketSendReady == false
			{
				Event sendEvent = connection.GetSocket()->GetOverlappedSendEvent();
				if (sendEvent.IsNull())
					waitEvents.AddEvent(falseEvent);
				else
					waitEvents.AddEvent(sendEvent);
			}
			else // Here, socketMessagesAvailable == false
			{
				waitEvents.AddEvent(connection.NewOutboundMessagesEvent());
			}
		}

		// Add all the UDP server listen sockets to the wait event list.
		// For UDP servers, only a single socket is used for receiving data from all clients.
		// In this case, the NetworkServer object handlesg all data reads, but data sends
		// are still managed by the individual MessageConnection objects.
		// For TCP servers, this step is not needed, since each connection has its own independent socket.
		for(size_t i = 0; i < serverList.size(); ++i)
		{
			NetworkServer &server = *serverList[i];

			std::vector<Socket *> &listenSockets = server.ListenSockets();

			for(size_t j = 0; j < listenSockets.size(); ++j)
				if (listenSockets[j]->TransportLayer() == SocketOverUDP)
				{
					Event listenEvent = listenSockets[j]->GetOverlappedReceiveEvent();
					if (listenEvent.IsNull())
						waitEvents.AddEvent(falseEvent);
					else
						waitEvents.AddEvent(listenEvent);
				}
		}

		// If we did not end up adding any wait events to the queue above, the worker thread does not have 
		// any connections to manage. Sleep for a moment, until we get some connections to handle.
		if (waitEvents.Size() == 0)
		{
			Thread::Sleep(maxWaitTime);
			continue;
		}

		// Wait until an event occurs either from the application end or in the socket.
		// When the application wants to send out a message, it is signaled by an event here.
		// Also, when the socket is ready for reading, writing or if it has been closed, it is signaled here.
		int index = waitEvents.Wait(max<int>(1, waitTime));

		if (index >= 0 && index < waitEvents.Size()) // An event was triggered?
		{
			if ((index >> 1) < (int)connectionList.size())
			{
				MessageConnection *connection = connectionList[index>>1];

				// A socket event was raised. We can either read or write.
				if ((index & 1) == 0)
				{
					connection->ReadSocket();
					connection->SendOutPackets();
				}
				else // new outbound messages were received from the application.
					connection->SendOutPackets();
			}
			else // An UDP server received a message.
			{
				int socketIndex = index - connectionList.size() * 2;
				if (serverList.size() > 0)
				{
					NetworkServer &server = *serverList[0]; ///\bug In case of multiple servers, this is not correct!
					std::vector<Socket *> &listenSockets = server.ListenSockets();
					if (socketIndex < (int)listenSockets.size())
						server.ReadUDPSocketData(listenSockets[socketIndex]);
					else
					{
						LOG(LogError, "NetworkWorkerThread::MainLoop: Warning: Cannot find server socket to read from: EventArray::Wait returned index %d (socketIndex %d), but "
							"serverList.size()=%d, connectionList.size()=%d!", index, socketIndex, serverList.size(), connectionList.size());
					}
				}
				else
				{
					LOG(LogError, "NetworkWorkerThread::MainLoop: Warning: EventArray::Wait returned index %d (socketIndex %d), but "
						"serverList.size()=%d, connectionList.size()=%d!", index, socketIndex, serverList.size(), connectionList.size());
				}
			}
		}

		// The UDP send throttle timers are not read through events. The writeWaitConnections list
		// contains a list of UDP connections which are now, or will veru soon (in less than 1msec) be ready for writing. 
		// Poll each and try to send a message.
		for(size_t i = 0; i < writeWaitConnections.size(); ++i)
			writeWaitConnections[i]->SendOutPackets();
	}
	falseEvent.Close();
	LOG(LogInfo, "NetworkWorkerThread quit.");
}

} // ~kNet
