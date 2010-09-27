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

/** @file HelloClient.cpp
	@brief */

#include "kNet.h"

using namespace kNet;

// Define a MessageID for our a custom message.
const message_id_t cHelloMessageID = 10;

// This object gets called whenever new data is received.
class MessageListener : public IMessageHandler
{
public:
   void HandleMessage(MessageConnection *source, message_id_t id, const char *data, size_t numBytes)
   {
      if (id == cHelloMessageID)
      {
         // Read what we received. 
         DataDeserializer dd(data, numBytes);
         std::cout << "Server says: " << dd.ReadString() << std::endl;
         
         source->Close();
      } 
   }
};

int main(int argc, char **argv)
{
   if (argc < 2)
   {
      std::cout << "Usage: " << argv[0] << " server-ip" << std::endl;
      return 0;
   }
   Network network;
   MessageListener listener;
	const unsigned short cServerPort = 1234;
   Ptr(MessageConnection) connection = network.Connect(argv[1], cServerPort, SocketOverUDP, &listener);

	if (connection)
	{
		// Run the main client loop.
		connection->RunModalClient();
	}
   
   return 0;
}
