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

/** @file MessageListParser.cpp
	@brief */

///\todo Currently disabled since TinyXML dependency is not resolved.
#if 0

#include <tinyxml.h>

#include "kNet/NetworkLogging.h"
#include "kNet/MessageListParser.h"

#include "kNet/DebugMemoryLeakCheck.h"

#define NUMELEMS(x) (sizeof(x)/sizeof(x[0]))

namespace
{
	const char *data[] = { "", "bit", "u8", "s8", "u16", "s16", "u32", "s32", "u64", "s64", "float", "double", "struct" };
	const size_t typeSizes[] = { -1, -1, 1, 1, 2, 2, 4, 4, 8, 8, 4, 8, -1 }; ///< -1 here denotes 'does not apply'.
}

namespace kNet
{

BasicSerializedDataType StringToSerialType(const char *type)
{
	for(int i = 0; i < NUMELEMS(data); ++i)
		if (!strcmp(type, data[i]))
			return (BasicSerializedDataType)i;

	return SerialInvalid;
}

const char *SerialTypeToString(BasicSerializedDataType type)
{
	assert(type >= SerialInvalid);
	assert(type < NumSerialTypes); 
	return data[type];
}

size_t SerialTypeSize(BasicSerializedDataType type)
{
	assert(type >= SerialInvalid);
	assert(type < NumSerialTypes); 
	return typeSizes[type];	
}

SerializedElementDesc *SerializedMessageList::ParseNode(TiXmlElement *node, SerializedElementDesc *parentNode)
{
	elements.push_back(SerializedElementDesc());
	SerializedElementDesc *elem = &elements.back();
	elem->parent = parentNode;

	if (!strcmp(node->Value(), "message"))
	{
		elem->count = 1;
		elem->varyingCount = false;
		elem->type = SerialStruct;
	}
	else
	{
		// Cannot have both static count and dynamic count!
		assert(!node->Attribute("count") || !node->Attribute("varyingCount")); ///\todo Convert assert() to error checking.

		if (node->Attribute("count"))
		{
			node->QueryIntAttribute("count", &elem->count);
			elem->varyingCount = false;
		}
		else if (node->Attribute("dynamicCount"))
		{
			node->QueryIntAttribute("dynamicCount", &elem->count);
			elem->varyingCount = true;
		}
		else
		{
			elem->count = 1;
			elem->varyingCount = false;
		}

		elem->type = StringToSerialType(node->Value());
	}

	elem->name = node->Attribute("name") ? node->Attribute("name") : "";

	// If this node is a structure, parse all its members.
	if (elem->type == SerialStruct)
	{
		TiXmlElement *child = node->FirstChildElement();
		while(child)
		{
			SerializedElementDesc *childElem = ParseNode(child, elem);
			elem->elements.push_back(childElem);

			child = child->NextSiblingElement();
		}
	}

	return elem;
}

bool ParseBool(const char *str)
{
	if (!str)
		return false;

	if (!stricmp(str, "true") || !stricmp(str, "1"))
		return true;
	else
		return false;
}

void SerializedMessageList::ParseMessages(TiXmlElement *root)
{
	TiXmlElement *node = root->FirstChildElement("message");
	while(node)
	{
		SerializedMessageDesc desc;
		int success = node->QueryIntAttribute("id", (int*)&desc.id);
		if (success == TIXML_NO_ATTRIBUTE)
		{
			LOG(LogError, "Error parsing message attribute 'id' as int!");
			node = node->NextSiblingElement("message");
			continue; 
		}
		success = node->QueryIntAttribute("priority", (int*)&desc.priority);
		if (success == TIXML_NO_ATTRIBUTE)
			desc.priority = 0x7FFFFFFF; // If priority not specified, use default.
		if (node->Attribute("name"))
			desc.name = node->Attribute("name");
		desc.reliable = ParseBool(node->Attribute("reliable"));
		desc.inOrder = ParseBool(node->Attribute("inOrder"));
		desc.data = ParseNode(node, 0);

		// Work a slight convenience - if there is a single struct inside a single struct inside a single struct - jump straight through to the data.

		messages.push_back(desc);

		node = node->NextSiblingElement("message");
	}
}

void SerializedMessageList::ParseStructs(TiXmlElement *root)
{
	TiXmlElement *node = root->FirstChildElement("struct");
	while(node)
	{
		ParseNode(node, 0);

		node = node->NextSiblingElement("struct");
	}
}

void SerializedMessageList::LoadMessagesFromFile(const char *filename)
{
	TiXmlDocument doc(filename);
	bool success = doc.LoadFile();
	if (!success)
	{
		LOG(LogError, "TiXmlDocument open failed on filename %s!", filename);
		return;
	}

	TiXmlElement *xmlRoot = doc.FirstChildElement();

	ParseStructs(xmlRoot);
	ParseMessages(xmlRoot);
}


const SerializedMessageDesc *SerializedMessageList::FindMessageByID(u32 id)
{
	for(std::list<SerializedMessageDesc>::iterator iter = messages.begin();
		iter != messages.end(); ++iter)
		if (iter->id == id)
			return &*iter;

	return 0;
}

const SerializedMessageDesc *SerializedMessageList::FindMessageByName(const char *name)
{
	for(std::list<SerializedMessageDesc>::iterator iter = messages.begin();
		iter != messages.end(); ++iter)
		if (iter->name == name)
			return &*iter;

	return 0;
}

} // ~kNet

#endif
