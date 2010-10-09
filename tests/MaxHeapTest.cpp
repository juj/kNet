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

/** @file MaxHeapTest.cpp
	@brief */

#include "kNet/MaxHeap.h"
#include "tassert.h"

void MaxHeapTest()
{
	using namespace kNet;

	TEST("MaxHeap Insert")
	MaxHeap<int> heap;
	assert(heap.Size() == 0);
	heap.Insert(1);
	assert(heap.Size() == 1);
	assert(heap.Front() == 1);
	heap.Insert(15);
	assert(heap.Size() == 2);
	assert(heap.Front() == 15);
	heap.Insert(3);
	assert(heap.Size() == 3);
	assert(heap.Front() == 15);
	heap.Insert(3);
	assert(heap.Front() == 15);
	assert(heap.Size() == 4);
	assert(heap.Search(15) == 0);
	assert(heap.Search(91) == -1);
	assert(heap.Search(3) > 0);
	assert(heap.Search(1) > 0);
	ENDTEST()
}
