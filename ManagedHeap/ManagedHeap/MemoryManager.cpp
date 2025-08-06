#include "pch.h"
#include "mimalloc-override.h"
#include "MemoryManager.h"

// This should be defined when building the DLL project
#define MEMORYMANAGER_EXPORTS

extern "C" 
{
    MEMORY_API void* MyAlloc(size_t size) 
    {
        void* ptr = malloc(size);
        return ptr;
    }

    MEMORY_API void MyFree(void* ptr) 
    {
        free(ptr);
    }
}
