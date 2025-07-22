#include "pch.h"
#include <ppl.h>
#include "MemoryManager.h"

using namespace Concurrency;
// This should be defined when building the DLL project
#define MEMORYMANAGER_EXPORTS

extern "C" 
{
    MEMORY_API void* MyAlloc(size_t size) 
    {
        void* ptr = Alloc(size);
        return ptr;
    }

    MEMORY_API void MyFree(void* ptr) 
    {
        Free(ptr);
    }
}
