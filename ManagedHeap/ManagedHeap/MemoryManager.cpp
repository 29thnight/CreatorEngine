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

    MEMORY_API void GC_Initialize()
    {
        GC_INIT();
    }

    MEMORY_API void GC_Shutdown()
    {
		GC_deinit();
        GC_win32_free_heap();
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
