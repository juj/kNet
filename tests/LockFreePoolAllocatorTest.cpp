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

/** @file LockFreePoolAllocatorTest.cpp
	@brief Tests the thread safety and proper operation of the LockFreePoolAllocator structure. */

#include "kNet.h"
#include "kNet/LockFreePoolAllocator.h"
#include "kNet/Thread.h"
#include "tassert.h"
#include "kNet/DebugMemoryLeakCheck.h"

#include <iostream>

using namespace std;
using namespace kNet;

struct Foo : public PoolAllocatable<Foo>
{
    int bar;
};

volatile bool wait = true;

const int numThreads = 20;
LockFreePoolAllocator<Foo> pool;
std::vector<Foo*> foos[numThreads];

void PoolThreadMain(Thread *me, int threadIndex)
{
    while(wait)
    {
        if (me->ShouldQuit())
            return;
    }

	int idx = threadIndex << 16;
    while(!me->ShouldQuit())
    {
        if (rand()%2 || foos[threadIndex].size() == 0)
        {
            if (foos[threadIndex].size() < 100)
            {
                Foo *f = pool.New();
				f->bar = ++idx;
                foos[threadIndex].push_back(f);
            }
        }
        else
        {
            Foo *f = foos[threadIndex].back();
			f->bar = 0;
            foos[threadIndex].pop_back();
            pool.Free(f);
        }
    }
}

template<typename T>
bool VectorsIntersect(const std::vector<T> &a, const std::vector<T> &b)
{
    if (a.size() == 0 || b.size() == 0)
        return false;

    vector<T>::const_iterator iA = a.begin();
    vector<T>::const_iterator iB = b.begin();
    while(iA != a.end() && iB != b.end())
    {
        if (*iA == *iB)
            return true;
        if (*iA < *iB)
            ++iA;
        else
            ++iB;
    }
    return false;
}

void LockFreePoolAllocatorTest()
{
    cout << "Starting LockFreePoolAllocatorTest.";

    Thread testThreads[numThreads];
    wait = true;

    // Fire up all threads.
    for(int i = 0; i < numThreads; ++i)
        testThreads[i].RunFunc(PoolThreadMain, &testThreads[i], i);

    wait = false;
    Thread::Sleep(100);

    for(int i = 0; i < numThreads; ++i)
        testThreads[i].Stop();

    for(int i = 0; i < numThreads; ++i)
        std::sort(foos[i].begin(), foos[i].end());

    for(int i = 0; i < numThreads; ++i)
        for(int j = i+1; j < numThreads; ++j)
            if (VectorsIntersect(foos[i], foos[j]))
            {
                cout << "!!";
                i = numThreads;
                j = numThreads;
                break;
            }    

    for(int i = 0; i < numThreads; ++i)
    {
        for(size_t j = 0; j < foos[i].size(); ++j)
            delete foos[i][j];
        foos[i].clear();
    }
    pool.UnsafeClearAll();
}
