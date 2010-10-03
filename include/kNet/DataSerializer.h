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

/** @file DataSerializer.h
	@brief The class \ref DataSerializer DataSerializer. Stores POD data to bit streams. */

#include <vector>
#include <cassert>

#include "kNet/SharedPtr.h"

#include "kNet/MessageListParser.h"
#include "kNet/SerializedDataIterator.h"
#include "kNet/VLEPacker.h"

namespace kNet
{

struct SerializedMessage : public RefCountable
{
	std::vector<char> data;
};

/// DataSerializer is a helper class that can be used to serialize data types to a stream of raw bits 
/// suitable for disk storage or network transfer.
class DataSerializer
{
public:
	/// Instantiates a new DataSerializer that maintains its own buffer space for serialization.
	/// The size limit for the data to serialize can be specified as an optional parameter.
	///\todo Support dynamic resizing.
	explicit DataSerializer(size_t maxBytes_ = 128 * 1024);
	
	/// Instantiates a new DataSerializer that maintains its own buffer space for serialization and serializes using a template.
	///\todo Support dynamic resizing.
	DataSerializer(size_t maxBytes_, const SerializedMessageDesc *msgTemplate);

	/// Instantiates a new DataSerializer that writes its data into the given fixed-size buffer.
	DataSerializer(char *data_, size_t maxBytes_);

	/// Instantiates a new DataSerializer that writes its data into the given fixed-size buffer, using a message template.
	DataSerializer(char *data_, size_t maxBytes_, const SerializedMessageDesc *msgTemplate);

	/// Instantiates a new DataSerializer that writes to the given vector. 
	/// @param maxBytes The maximum number of bytes that the message can take up space.
	explicit DataSerializer(std::vector<char> &data, size_t maxBytes);

	/// Instantiates a new DataSerializer that writes to the given vector, using a message template. 
	/// @param maxBytes The maximum number of bytes that the message can take up space.
	DataSerializer(std::vector<char> &data, size_t maxBytes, const SerializedMessageDesc *msgTemplate);

	/// Appends a single element of the passed type.
	template<typename T>
	void Add(const T &value);

	template<typename VLEType>
	void AddVLE(u32 value);

	/// Appends the given number of bits to the stream.
	/// @param value The variable where the bits are taken from. The bits are read from the LSB first, towards the MSB end of the value.
	/// @param amount The number of bits to read, in the range [1, 32].
	void AppendBits(u32 value, int amount);

	/// Adds a given string as length-prepended (not zero-padded). In the message template, use a
	/// parameter of type 's8' with dynamicCount field set to e.g. 8.
	void AddString(const char *str);

	/// See \ref void DataSerializer::AddString(const char *str); "".
	void AddString(const std::string &str) { AddString(str.c_str()); }

	/// Appends the given amount of elements from the passed array.
	template<typename T>
	void AddArray(const T *data, u32 count);

	/// Adds an array of bytes to the stream. The contents in the stream must be byte-aligned when calling
	/// this function. A serialization template may not be used when calling this function.
	void AddAlignedByteArray(const void *data, u32 numBytes);

	/// Sets the number of instances in a varying element.
	void SetVaryingElemSize(u32 count);

	void ResetFill();

	char *GetData() const { return data; }
	
	/// Advances the stream pointer the given number of bytes. Use this method if you have used an external method
	/// of filling data to the stream.
	void SkipNumBytes(size_t numBytes);

	/// @return The number of bytes filled so far. Partial bits at the end are rounded up to constitute a full byte.
	size_t BytesFilled() const { return elemOfs + ((bitOfs != 0) ? 1 : 0); }

	/// @return The number of bits filled so far.
	size_t BitsFilled() const { return elemOfs * 8 + bitOfs; }

	/// @return The total capacity of the buffer we are filling into.
	size_t Capacity() const { return maxBytes; }

private:
	void AppendByte(u8 byte);
	void AppendUnalignedByte(u8 byte);
	void AppendAlignedByte(u8 byte);

	/// Iterator that iterates a template that specifies the elements that are present in the message.
	Ptr(SerializedDataIterator) iter;

	/// Points to the beginning of the data buffer we are filling.
	char *data;

	/// The number of bytes in data.
	size_t maxBytes;

	/// The actual data so far.
	Ptr(SerializedMessage) messageData;
	/// The current element we're filling in the data buffer.
	size_t elemOfs;
	/// The current bit of the element we're filling in the data buffer, [0, 7].
	int bitOfs; 
};

template<typename T>
void DataSerializer::Add(const T &value)
{
#ifdef _DEBUG
	if (iter)
	{
		BasicSerializedDataType nextExpectedType = iter->NextElementType();
		BasicSerializedDataType currentFilledType = SerializedDataTypeTraits<T>::type;
		assert(nextExpectedType == currentFilledType);
	}
#endif
	const u8 *data = reinterpret_cast<const u8*>(&value);

	for(int i = 0; i < sizeof(value); ++i)
		AppendByte(data[i]);

	if (iter)
		iter->ProceedToNextVariable();
}

template<>
void DataSerializer::Add<bit>(const bit &value);

template<typename VLEType>
void DataSerializer::AddVLE(u32 value)
{
	assert(!iter); // Can't use with a template.

	u32 encoded = VLEType::Encode(value);
	int numBits = VLEType::GetEncodedBitLength(value);

	///\todo To support other than 1/2/4-byte VLE's, convert to using AddBits().
	if (numBits == 8) Add<u8>((u8)encoded);
	else if (numBits == 16) Add<u16>((u16)encoded);
	else if (numBits == 32) Add<u32>(encoded);
	else
	{
		assert(false && "N/I numBits count! todo.. Write AddBits()");
	}
}

/// Appends the given amount of elements from the passed array.
template<typename T>
void DataSerializer::AddArray(const T *data, u32 count)
{
	for(u32 i = 0; i < count; ++i)
		Add<T>(data[i]); 

	// If the user added an empty array, we have to manually walk to the next variable.
	if (count == 0 && iter)
		iter->ProceedToNextVariable();
}

/// Sums up the sizes of each element of an array.
template<typename T> size_t SumArray(const T &data, size_t numElems)
{
   size_t size = 0;
   for(size_t i = 0; i < numElems; ++i)
      size += data[i].Size();
   return size;
}

} // ~kNet
