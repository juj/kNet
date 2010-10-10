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

/** @file VLETest.cpp
	@brief */

#include <string>

#include "kNet/DebugMemoryLeakCheck.h"
#include "kNet/DataSerializer.h"
#include "kNet/DataDeserializer.h"

using namespace std;
using namespace kNet;

template<typename VLEType>
void TestVLE(unsigned long step1 = 1, unsigned long step2 = 1, unsigned long stepmod = 10000)
{
	for(unsigned long i = 0; i < VLEType::maxValue; i += step1 + ((step2*i) % stepmod))
	{
		DataSerializer ds;

		ds.AddVLE<VLEType>(i);
		ds.AddVLE<VLEType>((i+1)%VLEType::maxValue);
		ds.AddVLE<VLEType>((i*5145324+2)%VLEType::maxValue);

		DataDeserializer dd(ds.GetData(), ds.BytesFilled());
		unsigned long val1 = dd.ReadVLE<VLEType>();
		unsigned long val2 = dd.ReadVLE<VLEType>();
		unsigned long val3 = dd.ReadVLE<VLEType>();
		assert(val1 == i);
		assert(val2 == (i+1)%VLEType::maxValue);
		assert(val3 == (i*5145324+2)%VLEType::maxValue);
	}
}

void VLETest()
{
//	TestVLE<VLE8_16_32>(1, 100, 100000);
	TestVLE<VLE8_16>();
//	TestVLE<VLE8_32>(1,1000);
//	TestVLE<VLE16_32>(1,1000);
}
