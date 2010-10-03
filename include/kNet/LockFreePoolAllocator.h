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

/** @file LockFreePoolAllocator.h
	@brief The PoolAllocatable<T> and LockFreePoolAllocator<T> template classes. */

#include <iostream>
#include "Atomics.h"

namespace kNet
{

template<typename T>
struct PoolAllocatable
{
	T *next;
};

/// T must implement PoolAllocatable.
template<typename T>
class LockFreePoolAllocator
{
public:
	LockFreePoolAllocator()
	:root(0)
	{
	}

	~LockFreePoolAllocator()
	{
		UnsafeClearAll();
	}

	/// Allocates a new object of type T and returns a pointer to it.
	/// Call Free() to deallocate the object.
	T *New()
	{
		if (!root)
		{
			T *n = new T();
			return n;
		}

		T *allocated;
		T *newRoot;

		do
		{
			allocated = root;
			newRoot = root->next;
		} while(CmpXChgPointer((void**)&root, newRoot, allocated) == false);

		assert(allocated != root);
		return allocated;
	}

	void Free(T *ptr)
	{
		if (!ptr)
			return;

		assert(ptr != root);
		T *top;

		do
		{
			top = root;
			ptr->next = top;
		} while(CmpXChgPointer((void**)&root, ptr, top) == false);
	}

	/// Deallocates all cached unused nodes in this pool. Thread-safe and lock-free. If you are manually tearing
	/// down the pool and there are no other threads accessing this, you may call the even faster version
	/// UnsafeClearAll(), which ignores compare-and-swap updates.
	void ClearAll()
	{
		assert(!DebugHasCycle());
		while(root)
		{
			T *node = New();
			delete node;
		}
	}

	/// A fast method to free all items allocated in the pool.
	/// Not thread-safe, only call when you can guarantee there are no threads calling New() or Free().
	void UnsafeClearAll()
	{
		assert(!DebugHasCycle());
		T *node = root;
		while(node)
		{
			T *next = node->next;
			delete node;
			node = next;
		}
		root = 0;
	}

	/// A debugging function that checks whether the underlying linked list has a cycle or not. Not thread-safe!
	bool DebugHasCycle()
	{
		T *n1 = root;
		if (!n1)
			return false;
		T *n2 = n1->next;
		while(n2 != 0)
		{
			if (n1 == n2)
				return true;

			n1 = n1->next;
			n2 = n2->next;
			if (n2)
				n2 = n2->next;
		}
		return false;
	}

	/// A debugging function that prints out all the objects in the pool. Not thread-safe!
	void DumpPool()
	{
		using namespace std;

		T *node = root;
		cout << "Root: 0x" << ios::hex << root << ".Next: " << ios::hex << (root ? root->next : 0) << std::endl;
		int size = 0;
		if (node)
			node = node->next;
		while(node)
		{
			cout << "Node: 0x" << ios::hex << node << ".Next: " << ios::hex << (node ? node->next : 0) << std::endl;
			node = node->next;
			++size;
		}
		cout << "Total: " << size << " elements." << endl;
	}

private:
	T * volatile root;
};

} // ~kNet
