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
#include <cstdlib>
#include "MemoryManager.h"
#include "gc.h"

// This should be defined when building the DLL project
#define MEMORYMANAGER_EXPORTS

extern "C"
{
    // ASan 빌드에서는 mimalloc을 우회해 CRT로 간다(PHASE 9-0).
    //
    // ASan은 CRT의 malloc/free를 가로채 앞뒤에 레드존을 두고, 해제한 블록을
    // 격리(quarantine)에 담아 두었다가 다시 만지면 use-after-free로 잡는다.
    // mi_malloc은 그 가로채기의 바깥이라 ASan에게는 존재하지 않는 메모리다.
    //
    // 그런데 GameObject와 Component는 전부 shared_alloc → MyAllocator →
    // 여기를 지난다. 즉 이 우회가 없으면 ASan을 켜도 컴포넌트 UAF —
    // 이 빌드 구성을 만든 이유 그 자체 — 를 한 건도 잡지 못한 채
    // "무사고"라고 보고하게 된다. 확인하지 못한 것과 확인했고 문제없는 것은 다르다.
    //
    // 대신 이 빌드의 할당 성능은 기준선이 아니다. 진단 전용이므로 상관없다.
#if defined(ENGINE_ASAN)
    MEMORY_API void* MyAlloc(size_t size)
    {
        return std::malloc(size);
    }

    MEMORY_API void MyFree(void* ptr)
    {
        std::free(ptr);
    }
#else
    MEMORY_API void* MyAlloc(size_t size)
    {
        return mi_malloc(size);
    }

    MEMORY_API void MyFree(void* ptr)
    {
        mi_free(ptr);
    }
#endif

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
