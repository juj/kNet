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

/** @file DataSerializer.cpp
	@brief */

#include <cstring>

#include "kNet/DebugMemoryLeakCheck.h"

#include "kNet/DataSerializer.h"
#include "kNet/BitOps.h"

namespace kNet
{

DataSerializer::DataSerializer(size_t maxBytes_)
{
	assert(maxBytes_ > 0);

	maxBytes = maxBytes_;
	messageData = new SerializedMessage();
	messageData->data.resize(maxBytes);
	data = &messageData->data[0];

	ResetFill();
}

DataSerializer::DataSerializer(size_t maxBytes_, const SerializedMessageDesc *msgTemplate)
{
	assert(maxBytes_ > 0);
	assert(msgTemplate != 0);

	iter = new SerializedDataIterator(*msgTemplate);

	maxBytes = maxBytes_;
	messageData = new SerializedMessage();
	messageData->data.resize(maxBytes);
	data = &messageData->data[0];

	ResetFill();
}

DataSerializer::DataSerializer(char *data_, size_t maxBytes_)
:data(data_), maxBytes(maxBytes_)
{
	ResetFill();
}

DataSerializer::DataSerializer(char *data_, size_t maxBytes_, const SerializedMessageDesc *msgTemplate)
:data(data_), maxBytes(maxBytes_)
{
	assert(msgTemplate != 0);

	iter = new SerializedDataIterator(*msgTemplate);
	ResetFill();
}

DataSerializer::DataSerializer(std::vector<char> &data_, size_t maxBytes_)
{
	if (data_.size() < maxBytes_)
		data_.resize(maxBytes_);
	if (data_.size() == 0 || maxBytes_ == 0)
		throw NetException("Cannot instantiate a DataSerializer object to a zero-sized std::vector-based buffer!");
	data = &data_[0];
	maxBytes = maxBytes_;

	ResetFill();
}

DataSerializer::DataSerializer(std::vector<char> &data_, size_t maxBytes_, const SerializedMessageDesc *msgTemplate)
{
	if (data_.size() < maxBytes_)
		data_.resize(maxBytes_);
	if (data_.size() == 0 || maxBytes_ == 0)
		throw NetException("Cannot instantiate a DataSerializer object to a zero-sized std::vector-based buffer!");
	data = &data_[0];
	maxBytes = maxBytes_;

	if (!msgTemplate)
		throw NetException("Null message template cannot be passed in to DataSerializer ctor!");
	iter = new SerializedDataIterator(*msgTemplate);

	ResetFill();
}

void DataSerializer::AppendByte(u8 byte)
{
	if (bitOfs == 0)
		AppendAlignedByte(byte);
	else
		AppendUnalignedByte(byte);
}

void DataSerializer::AppendUnalignedByte(u8 byte)
{
	// The current partial byte can hold (8-bitOfs) bits.
	data[elemOfs] = (data[elemOfs] & LSB(bitOfs)) | ((byte & LSB(8-bitOfs)) << bitOfs);
	// The next byte can hold full 8 bits, but we only need to add bitOfs bits.
	data[++elemOfs] = byte >> (8-bitOfs);
}

void DataSerializer::AppendAlignedByte(u8 byte)
{
	assert(bitOfs == 0);

	assert(elemOfs < maxBytes);
	data[elemOfs++] = byte;
}

void DataSerializer::AppendBits(u32 value, int amount)
{
	const u8 *bytes = reinterpret_cast<const u8*>(&value);
	while(amount >= 8)
	{
		AppendByte(*bytes);
		++bytes;
		amount -= 8;
	}

	u8 remainder = *bytes & LSB(amount);

	data[elemOfs] = (data[elemOfs] & LSB(bitOfs)) | ((remainder & LSB(8-bitOfs)) << bitOfs);
	if (bitOfs + amount >= 8)
		data[++elemOfs] = remainder >> (8-bitOfs);

	bitOfs = (bitOfs + amount) & 7;
}

void DataSerializer::ResetFill()
{
	if (iter)
		iter->ResetTraversal();
	elemOfs = 0;
	bitOfs = 0;
}

void DataSerializer::AddAlignedByteArray(const void *srcData, u32 numBytes)
{
	assert(bitOfs == 0);
	assert(!iter);
	if (elemOfs + numBytes > maxBytes)
		throw NetException("DataSerializer::AddAlignedByteArray: Attempted to write past the array end buffer!");

	memcpy(&data[elemOfs], srcData, numBytes);
	elemOfs += numBytes;
}

u32 DataSerializer::AddUnsignedFixedPoint(int numIntegerBits, int numDecimalBits, float value)
{
	assert(numIntegerBits >= 0);
	assert(numDecimalBits > 0);
	assert(numIntegerBits + numDecimalBits <= 32);
	const float maxVal = (float)(1 << numIntegerBits);
	const u32 maxBitPattern = (1 << (numIntegerBits + numDecimalBits)) - 1; // All ones - the largest value we can send.
	u32 outVal = value <= 0 ? 0 : (value >= maxVal ? maxBitPattern : (u32)(value * (float)(1 << numDecimalBits)));
	assert(outVal <= maxBitPattern);
	AppendBits(outVal, numIntegerBits + numDecimalBits);
	return outVal;
}

u32 DataSerializer::AddSignedFixedPoint(int numIntegerBits, int numDecimalBits, float value)
{
	// Adding a [-k, k-1] range -> remap to unsigned [0, 2k-1] range and send that instead.
	return AddUnsignedFixedPoint(numIntegerBits, numDecimalBits, value + (float)(1 << (numIntegerBits-1)));
}

static inline float ClampF(float val, float minVal, float maxVal) { return val <= minVal ? minVal : (val >= maxVal ? maxVal : val); }

void DataSerializer::AddQuantizedFloat(float minRange, float maxRange, int numBits, float value)
{
	u32 outVal = (u32)((ClampF(value, minRange, maxRange) - minRange) * (float)((1 << numBits)-1) / (maxRange - minRange));
	AppendBits(outVal, numBits);
}

#define PI ((float)3.1415926535897932384626433832795028841971693993751058209749445923078164062862089986280348253421170679)

void DataSerializer::AddNormalizedVector2D(float x, float y, int numBits)
{
	// Call atan2() to get the aimed angle of the 2D vector in the range [-PI, PI], then quantize the 1D result to the desired precision.
	AddQuantizedFloat(-PI, PI, numBits, atan2(y, x));
}

int DataSerializer::AddVector2D(float x, float y, int magnitudeIntegerBits, int magnitudeDecimalBits, int directionBits)
{
	// Compute the length of the vector. Use a fixed-point representation to store the length.
	float length = sqrt(x*x+y*y);
	u32 bitVal = AddUnsignedFixedPoint(magnitudeIntegerBits, magnitudeDecimalBits, length);

	// If length == 0, don't need to send the angle, as it's a zero vector.
	if (bitVal != 0)
	{
		// Call atan2() to get the aimed angle of the 2D vector in the range [-PI, PI], then quantize the 1D result to the desired precision.
		float angle = atan2(y, x);
		AddQuantizedFloat(-PI, PI, directionBits, atan2(y, x));
		return magnitudeIntegerBits + magnitudeDecimalBits + directionBits;
	}
	else
		return magnitudeIntegerBits + magnitudeDecimalBits;
}

void DataSerializer::AddNormalizedVector3D(float x, float y, float z, int numBitsYaw, int numBitsPitch)
{
	// Convert to spherical coordinates. We assume that the vector (x,y,z) has been normalized beforehand.
	float azimuth = atan2(x, z); // The 'yaw'
	float inclination = asin(-y); // The 'pitch'

	AddQuantizedFloat(-PI, PI, numBitsYaw, azimuth);
	AddQuantizedFloat(-PI/2, PI/2, numBitsPitch, inclination);
}

int DataSerializer::AddVector3D(float x, float y, float z, int numBitsYaw, int numBitsPitch, int magnitudeIntegerBits, int magnitudeDecimalBits)
{
	float length = sqrt(x*x + y*y + z*z);

	u32 bitVal = AddUnsignedFixedPoint(magnitudeIntegerBits, magnitudeDecimalBits, length);

	if (bitVal != 0)
	{
		// The written length was not zero. Send the spherical angles as well.
		float azimuth = atan2(x, z);
		float inclination = asin(-y / length);

		AddQuantizedFloat(-PI, PI, numBitsYaw, azimuth);
		AddQuantizedFloat(-PI/2, PI/2, numBitsPitch, inclination);
		return magnitudeIntegerBits + magnitudeDecimalBits + numBitsYaw + numBitsPitch;
	}
	else // The vector is (0,0,0). Don't send spherical angles as they're redundant.
		return magnitudeIntegerBits + magnitudeDecimalBits;
}

void DataSerializer::AddArithmeticEncoded(int numBits, int val1, int max1, int val2, int max2)
{
	assert(max1 * max2 < (1 << numBits));
	assert(val1 >= 0);
	assert(val1 < max1);
	assert(val2 >= 0);
	assert(val2 < max2);
	AppendBits(val1 * max2 + val2, numBits);
}

void DataSerializer::AddArithmeticEncoded(int numBits, int val1, int max1, int val2, int max2, int val3, int max3)
{
	assert(max1 * max2 * max3 < (1 << numBits));
	assert(val1 >= 0);
	assert(val1 < max1);
	assert(val2 >= 0);
	assert(val2 < max2);
	assert(val3 >= 0);
	assert(val3 < max3);
	AppendBits((val1 * max2 + val2) * max3 + val3, numBits);
}

void DataSerializer::AddArithmeticEncoded(int numBits, int val1, int max1, int val2, int max2, int val3, int max3, int val4, int max4)
{
	assert(max1 * max2 * max3 * max4 < (1 << numBits));
	assert(val1 >= 0);
	assert(val1 < max1);
	assert(val2 >= 0);
	assert(val2 < max2);
	assert(val3 >= 0);
	assert(val3 < max3);
	assert(val4 >= 0);
	assert(val4 < max4);
	AppendBits(((val1 * max2 + val2) * max3 + val3) * max4 + val4, numBits);
}

void DataSerializer::AddArithmeticEncoded(int numBits, int val1, int max1, int val2, int max2, int val3, int max3, int val4, int max4, int val5, int max5)
{
	assert(max1 * max2 * max3 * max4 * max5 < (1 << numBits));
	assert(val1 >= 0);
	assert(val1 < max1);
	assert(val2 >= 0);
	assert(val2 < max2);
	assert(val3 >= 0);
	assert(val3 < max3);
	assert(val4 >= 0);
	assert(val4 < max4);
	assert(val5 >= 0);
	assert(val5 < max5);
	AppendBits((((val1 * max2 + val2) * max3 + val3) * max4 + val4) * max5 + val5, numBits);
}

/// Requires a template to be present to use this.
void DataSerializer::SetVaryingElemSize(u32 count)
{
	assert(iter.ptr() != 0);
	assert(iter->NextElementDesc() != 0);

	AppendBits(count, iter->NextElementDesc()->count);

	iter->SetVaryingElemSize(count);
}

template<>
void DataSerializer::Add<char*>(char * const & value)
{
	AddString(value);
}

template<>
void DataSerializer::Add<const char*>(const char * const & value)
{
	AddString(value);
}

template<>
void DataSerializer::Add<std::string>(const std::string &value)
{
	AddString(value);
}

template<>
void DataSerializer::Add<bit>(const bit &value)
{
	u8 val = (value != 0) ? 1 : 0;
	AppendBits(val, 1);
}

void DataSerializer::AddString(const char *str)
{
	size_t len = strlen(str);
	if (iter)
		SetVaryingElemSize(len);
	else
		Add<u8>(len);

	AddArray<s8>((const s8*)str, len);
}

void DataSerializer::SkipNumBytes(size_t numBytes)
{
	elemOfs += numBytes;
	if (elemOfs + (bitOfs ? 1 : 0) > maxBytes)
		throw NetException("DataSerializer::SkipNumBytes: Attempted to travel past the end of the array!");
}

} // ~kNet
