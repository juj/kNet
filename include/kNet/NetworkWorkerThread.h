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
#pragma once

/** @file NetworkWorkerThread.h
	@brief The NetworkWorkerThread class. Implements a background thread for responsive
	processing of server and client connections. */

#include "SharedPtr.h"

#include "Lockable.h"
#include "MessageConnection.h"
#include "NetworkServer.h"
#include "Thread.h"

namespace kNet
{

class NetworkWorkerThread
{
public:
	NetworkWorkerThread();

	void AddConnection(Ptr(MessageConnection) connection);
	void RemoveConnection(Ptr(MessageConnection) connection);

	void AddServer(Ptr(NetworkServer) server);
	void RemoveServer(Ptr(NetworkServer) server);

	void StartThread();
	void StopThread();

private:
	Lockable<std::vector<Ptr(MessageConnection)> > connections;
	Lockable<std::vector<Ptr(NetworkServer)> > servers;

	Thread workThread;

	/// The entry point for the work thread, which runs a loop that manages network connections.
	void MainLoop();
};

#ifdef WIN32
DWORD WINAPI NetworkWorkerThreadMain(LPVOID lpParameter);
#endif

} // ~kNet
