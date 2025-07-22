#pragma once

#ifdef MANAGEDHEAP_EXPORTS
#define MEMORY_API __declspec(dllexport)
#else
#define MEMORY_API __declspec(dllimport)
#endif

extern "C" {
    MEMORY_API void* MyAlloc(size_t size);
    MEMORY_API void MyFree(void* ptr);
}
