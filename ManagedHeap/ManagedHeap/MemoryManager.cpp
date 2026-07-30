#include "pch.h"
// mimalloc-override.h가 아니라 mimalloc.h를 쓴다.
//
// override 헤더는 프로세스 전역의 malloc/free를 mimalloc으로 가로채는 기능인데,
// Windows에서는 mimalloc-redirect.dll이 프로세스 진입 시점에 CRT를 후킹하는 방식이라
// EXE 대상으로 설계되어 있다. 이 프로젝트처럼 DLL에서 override를 켜면 로더 초기화와
// 충돌해 DllMain이 실패하고(ERROR_DLL_INIT_FAILED) 프로세스가 기동조차 못 한다.
//
// 여기서 필요한 것은 전역 후킹이 아니라 "mimalloc으로 할당하기"뿐이고,
// 엔진은 모든 할당을 아래 MyAlloc/MyFree로만 경유하므로 mi_ API를 직접 부르면 충분하다.
#include <mimalloc.h>
#include "MemoryManager.h"
#include "gc.h"

// This should be defined when building the DLL project
#define MEMORYMANAGER_EXPORTS

extern "C"
{
    MEMORY_API void* MyAlloc(size_t size)
    {
        return mi_malloc(size);
    }

    MEMORY_API void MyFree(void* ptr)
    {
        mi_free(ptr);
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
