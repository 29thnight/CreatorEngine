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
#include <atomic>
#include "MemoryManager.h"

namespace
{
    // C0 계측 누계. relaxed면 충분하다 — 서로 다른 카운터 사이의 순서를 보장할
    // 필요가 없고, 읽는 쪽(MyHeapStats)도 "그 순간의 근사"를 원하지 정확한
    // 스냅샷을 원하지 않는다. 정확한 스냅샷을 만들려면 할당 경로에 락이 필요한데
    // 그건 재려는 대상 자체를 왜곡한다.
    std::atomic<size_t> g_allocCount{ 0 };
    std::atomic<size_t> g_freeCount{ 0 };
    std::atomic<size_t> g_totalBytes{ 0 };
}

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
        g_allocCount.fetch_add(1, std::memory_order_relaxed);
        g_totalBytes.fetch_add(size, std::memory_order_relaxed);
        return std::malloc(size);
    }

    MEMORY_API void MyFree(void* ptr)
    {
        if (nullptr != ptr) { g_freeCount.fetch_add(1, std::memory_order_relaxed); }
        std::free(ptr);
    }
#else
    MEMORY_API void* MyAlloc(size_t size)
    {
        g_allocCount.fetch_add(1, std::memory_order_relaxed);
        g_totalBytes.fetch_add(size, std::memory_order_relaxed);
        return mi_malloc(size);
    }

    MEMORY_API void MyFree(void* ptr)
    {
        if (nullptr != ptr) { g_freeCount.fetch_add(1, std::memory_order_relaxed); }
        mi_free(ptr);
    }
#endif

    MEMORY_API void MyHeapStats(size_t* outAllocCount, size_t* outFreeCount, size_t* outTotalBytes)
    {
        if (nullptr != outAllocCount) { *outAllocCount = g_allocCount.load(std::memory_order_relaxed); }
        if (nullptr != outFreeCount)  { *outFreeCount  = g_freeCount.load(std::memory_order_relaxed); }
        if (nullptr != outTotalBytes) { *outTotalBytes = g_totalBytes.load(std::memory_order_relaxed); }
    }

    MEMORY_API void MyHeapStatsReset()
    {
        g_allocCount.store(0, std::memory_order_relaxed);
        g_freeCount.store(0, std::memory_order_relaxed);
        g_totalBytes.store(0, std::memory_order_relaxed);
    }
}
