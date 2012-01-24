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

/** @file DataSerializerTest.cpp
	@brief */

#include <string>
#include <sstream>
#include <iostream>
#include <list>

#include "kNet/DebugMemoryLeakCheck.h"
#include "kNet/BitOps.h"
#include "kNet/DataSerializer.h"
#include "kNet/DataDeserializer.h"

using namespace std;
using namespace kNet;

void PrintStream(const char *stream, int numBits)
{
	for(int i = 0; i < numBits; ++i)
	{
		int idx = i / 8;
		int elem = i % 8;
		bool t = (stream[idx] & (1 << elem)) != 0;
		if (elem == 0 && i != 0)
			cout << " ";
		cout << (t ? '1' : '0');
	}
}

class Action
{
public:
	virtual ~Action() {}

	virtual void Serialize(DataSerializer &dst) = 0;
	virtual void Deserialize(DataDeserializer &src) = 0;

	void Write(DataSerializer &dst)
	{
//		std::cout << "Writing to elemOfs: " << dst.ByteOffset() << ", bitOfs: " << dst.BitOffset()
//			<< "(" << dst.BitsFilled() << "): ";
		int bitsFilled = dst.BitsFilled();
		Serialize(dst);
//		std::cout << "Wrote " << (dst.BitsFilled() - bitsFilled) << " bits." << endl;
	}

	void Read(DataDeserializer &src)
	{
//		std::cout << "Reading from elemOfs: " << src.BytePos() << ", bitOfs: " << src.BitPos()
//			<< "(" << src.BitsReadTotal() << "): ";
		int bitsRead = src.BitsReadTotal();
		Deserialize(src);
//		std::cout << "Read " << (src.BitsReadTotal() - bitsRead) << " bits." << endl;
	}
};

unsigned long randu32()
{
	return (unsigned long)rand() ^ (unsigned long)(rand() << 15) ^ (unsigned long)(rand() << 30);
}

string randstring(int maxlen)
{
	stringstream ss;
	int len = rand() % (maxlen+1);
	for(int i = 0; i < len; ++i)
	{
		unsigned char ch = rand();
		if (ch >= 254 || (ch < 32 && ch != 0x0D && ch != 0x0A && ch != 0x09)) // Retain newlines and tab.
			ch = 0x20;

		ss << (char)ch;
	}
	return ss.str();
}

template<typename T>
class BasicAction : public Action
{
public:
	T value;

	BasicAction()
	{ 
		if (SerializedDataTypeTraits<T>::bitSize <= 32)
			value = (T)(randu32() & LSB(SerializedDataTypeTraits<T>::bitSize));
		else
			value = (T)randu32();
	}
	void Serialize(DataSerializer &dst)
	{ 
		dst.Add<T>(value);
//		cout << "Serialized type " << SerialTypeToReadableString(SerializedDataTypeTraits<T>::type) << ", value: " << value << endl;
	}
	void Deserialize(DataDeserializer &src)
	{ 
		T deserialized = src.Read<T>(); 
//		cout << "Deserialized type " << SerialTypeToReadableString(SerializedDataTypeTraits<T>::type) << ", value: " << deserialized << endl;
		assert(deserialized == value);
	}
};

class StringAction : public Action
{
public:
	string value;
	
	StringAction()
	{
		value = randstring(250);
	}

	void Serialize(DataSerializer &dst)
	{
		dst.Add<string>(value);
//		cout << "Serialized type string, value: " << value << endl;
	}

	void Deserialize(DataDeserializer &src)
	{
		string deserialized = src.Read<string>();
//		cout << "Deserialized type string, value: " << deserialized << endl;
		assert(deserialized == value);
	}
};

class VLE8_16_32_Action : public Action
{
public:
	u32 value;
	
	VLE8_16_32_Action()
	{
		value = randu32();
		if (value > VLE8_16_32::maxValue)
			value = VLE8_16_32::maxValue;
	}

	void Serialize(DataSerializer &dst)
	{
		dst.AddVLE<VLE8_16_32>(value);
//		cout << "Serialized type VLE8_16_32, value: " << value << endl;
	}

	void Deserialize(DataDeserializer &src)
	{
		u32 deserialized = src.ReadVLE<VLE8_16_32>();
//		cout << "Deserialized type VLE8_16_32, value: " << deserialized << endl;
		assert(deserialized == value);
	}
};

class VLE8_16_Action : public Action
{
public:
	u16 value;
	
	VLE8_16_Action()
	{
		value = (u16)randu32();
		if (value > VLE8_16::maxValue)
			value = VLE8_16::maxValue;
	}

	void Serialize(DataSerializer &dst)
	{
		dst.AddVLE<VLE8_16>(value);
//		cout << "Serialized type VLE8_16, value: " << value << endl;
	}

	void Deserialize(DataDeserializer &src)
	{
		u16 deserialized = src.ReadVLE<VLE8_16>();
//		cout << "Deserialized type VLE8_16, value: " << deserialized << endl;
		assert(deserialized == value);
	}
};

class VLE16_32_Action : public Action
{
public:
	u32 value;
	
	VLE16_32_Action()
	{
		value = randu32();
		if (value > VLE16_32::maxValue)
			value = VLE16_32::maxValue;
	}

	void Serialize(DataSerializer &dst)
	{
		dst.AddVLE<VLE16_32>(value);
//		cout << "Serialized type VLE16_32, value: " << value << endl;
	}

	void Deserialize(DataDeserializer &src)
	{
		u32 deserialized = src.ReadVLE<VLE16_32>();
//		cout << "Deserialized type VLE16_32, value: " << deserialized << endl;
		assert(deserialized == value);
	}
};

enum FloatCategory
{
	FloatZero,
	FloatNumber,
	FloatInf,
	FloatNegInf,
	FloatSignallingNan,
	FloatQuietNan
};

FloatCategory CategorizeFloat(float val)
{
	u32 v = *(u32*)&val;
	if ((v & 0x7FFFFFFF) == 0)
		return FloatZero;
	bool sign = (v & 0x80000000) != 0;
	u32 biasedExponent = (v & 0x7F800000) >> 23;
	u32 mantissa = v & 0x7FFFFF;
	if (biasedExponent != 0xFF)
		return FloatNumber;

	if (mantissa == 0)
		return sign ? FloatNegInf : FloatInf;

	return ((mantissa & 0x400000) != 0) ? FloatQuietNan : FloatSignallingNan;
}

class MiniFloatAction : public Action
{
public:
	float value;

	MiniFloatAction()
	{
		u32 v = randu32();
		value = *(float*)&v;
	}

	void Serialize(DataSerializer &dst)
	{
		dst.AddMiniFloat(true, 5, 10, 15, value);
	}

	void Deserialize(DataDeserializer &src)
	{
		float readValue = src.ReadMiniFloat(true, 5, 10, 15);
		FloatCategory oldCategory = CategorizeFloat(value);
		FloatCategory newCategory = CategorizeFloat(readValue);
		if (oldCategory != newCategory && !(oldCategory == FloatNumber && abs(value) < 1e-3f && newCategory == FloatZero)
			&& !(oldCategory == FloatNumber && value >= 65536.0f && newCategory == FloatInf)
			&& !(oldCategory == FloatNumber && value <= -65536.0f && newCategory == FloatNegInf))
		{
			std::cout << "Error in Minifloat serialization! Old value: " << value << ", new value: " << readValue << std::endl;
		}
	}
};

Action *CreateRandomAction()
{
	switch(rand() % 16)
	{
	case 0: return new BasicAction<bit>();
	case 1: return new BasicAction<u8>();
	case 2: return new BasicAction<s8>();
	case 3: return new BasicAction<u16>();
	case 4: return new BasicAction<s16>();
	case 5: return new BasicAction<u32>();
	case 6: return new BasicAction<s32>();
	case 7: return new BasicAction<u64>();
	case 8: return new BasicAction<s64>();
	case 9: return new BasicAction<float>();
	case 10: return new BasicAction<double>();
	case 11: return new StringAction();
	case 12: return new VLE8_16_Action();
	case 13: return new VLE16_32_Action();
	case 14: return new VLE8_16_32_Action();
	case 15: return new MiniFloatAction();
	default: assert(false); return 0;
	}
}

void RandomizedDataSerializerTest()
{
	std::list<Action*> actions;
	for(int i = 0; i < 100; ++i)
		actions.push_back(CreateRandomAction());

	DataSerializer ds;
	for(std::list<Action*>::iterator iter = actions.begin(); iter != actions.end(); ++iter)
		(*iter)->Write(ds);

	DataDeserializer dd(ds.GetData(), ds.BytesFilled());
	for(std::list<Action*>::iterator iter = actions.begin(); iter != actions.end(); ++iter)
		(*iter)->Read(dd);

	for(std::list<Action*>::iterator iter = actions.begin(); iter != actions.end(); ++iter)
		delete *iter;
}

// Perform a custom stream of data serialization.
void ManualDataSerializerTest()
{
	DataSerializer ds;
	ds.Add<bit>(true);
	ds.Add<bit>(false);

	ds.AddVLE<VLE8_16_32>(0x0DCDCDAB);
	ds.AddVLE<VLE8_16_32>(100000);

	ds.Add<bit>(true);
	ds.Add<bit>(true);
	ds.Add<bit>(true);
	ds.AddString("afeawgfaewgawegawefawfaewf");
	ds.Add<bit>(false);
	ds.Add<bit>(false);
	u32 value = 19;
	ds.AppendBits(value, 5);
	ds.Add<u8>(132);
	ds.Add<u32>(0xABCDEF12);
	ds.AddVLE<VLE8_16>(100);
	ds.AddVLE<VLE8_16>(32000);

	cout << "Bit-exact buffer: ";
	PrintStream(ds.GetData(), ds.BitsFilled());
	cout << endl;

	cout << "Byte-rounded buffer: ";
	PrintStream(ds.GetData(), ds.BytesFilled()*8);
	cout << endl;

	DataDeserializer dd(ds.GetData(), ds.BytesFilled());
	cout << "Bit: " << dd.Read<bit>() << endl;
	cout << "Bit: " << dd.Read<bit>() << endl;

	cout << "vle1: " << std::hex << dd.ReadVLE<VLE8_16_32>() << std::dec << endl;
	cout << "vle2: " << dd.ReadVLE<VLE8_16_32>() << endl;

	cout << "Bit: " << dd.Read<bit>() << endl;
	cout << "Bit: " << dd.Read<bit>() << endl;
	cout << "Bit: " << dd.Read<bit>() << endl;
	cout << "String: " << dd.ReadString() << endl;

	cout << "Bit: " << dd.Read<bit>() << endl;
	cout << "Bit: " << dd.Read<bit>() << endl;

	u32 v = dd.ReadBits(5);
	cout << "5 bits: " << v << endl;
	cout << "u8: " << (unsigned int)dd.Read<u8>() << endl;
	cout << std::hex << "u32: " << dd.Read<u32>() << std::dec << endl;

	cout << "vle1: " << dd.ReadVLE<VLE8_16>() << endl;
	cout << "vle2: " << dd.ReadVLE<VLE8_16>() << endl;
}

void DataSerializerTest()
{
	std::cout << "Running randomized DataSerializerTest." << std::endl;
	for(int i = 0; i < 100; ++i)
		RandomizedDataSerializerTest();
	std::cout << "Running manually written DataSerializerTest." << std::endl;
	ManualDataSerializerTest();
}
