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

/** @file MessageCompiler.cpp
	@brief A tool that compiles message XML files into .h files usable from
	       C++ code. */

#include <string>
#include <iostream>

#include "kNet.h"
#include "kNet/DebugMemoryLeakCheck.h"

using namespace std;
using namespace kNet;

int main(int argc, char **argv)
{
	if (argc < 2)
	{
		cout << "No parameters given." << endl;
		return 0;
	}

	EnableMemoryLeakLoggingAtExit();

	SerializedMessageList msg;
	msg.LoadMessagesFromFile(argv[1]);

	cout << "File " << argv[1] << " contains the following structs:" << endl;

	const std::list<SerializedElementDesc> &elements = msg.GetElements();
	for(std::list<SerializedElementDesc>::const_iterator iter = elements.begin();
		iter != elements.end(); ++iter)
	{
		const SerializedElementDesc &elem = *iter;
		
		if (elem.type == SerialStruct)
			if (elem.name.length() == 0)
				cout << "<unnamed struct>" << endl;
			else
				cout << elem.name << endl;
	}

	const std::list<SerializedMessageDesc> &messages = msg.GetMessages();
	for(std::list<SerializedMessageDesc>::const_iterator iter = messages.begin();
		iter != messages.end(); ++iter)
	{
		const SerializedMessageDesc &msg = *iter;
		SerializationStructCompiler compiler;	
		string messageName = compiler.ParseToValidCSymbolName(msg.name.c_str()) + ".h";
		compiler.CompileMessage(msg, messageName.c_str());
	}
}
