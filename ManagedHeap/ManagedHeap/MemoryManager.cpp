#include "pch.h"
#include "mimalloc-override.h"
#include "MemoryManager.h"
#include "gc.h"

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
    MEMORY_API void* GC_Alloc(size_t size)
    {
        return nullptr;
    }

    MEMORY_API void GC_Initialize()
    {
		GC_init();
    }

    MEMORY_API void GC_Shutdown()
    {
		GC_deinit();
    }

    MEMORY_API void GC_FullCollect()
    {
		GC_gcollect();
    }

    MEMORY_API void GC_IncrementalCollect()
    {
        GC_collect_a_little();
    }
}
