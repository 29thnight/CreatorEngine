#pragma once

#ifdef MANAGEDHEAP_EXPORTS
#define MEMORY_API __declspec(dllexport)
#else
#define MEMORY_API __declspec(dllimport)
#endif

extern "C" {
    MEMORY_API void*	MyAlloc(size_t size);
    MEMORY_API void		MyFree(void* ptr);

    // 이 힙을 지나간 할당의 누계 (ContainerLibraryDesign C0).
    //
    // 왜 mi_stats_print가 아닌가: 그것은 MI_STAT 빌드 옵션에 의존하고, 무엇보다
    // 이 프로세스에는 힙이 둘이다. 전역 operator new 오버라이드가 없어
    // std::vector·std::string의 버퍼는 CRT 힙으로 가고, 여기(mimalloc)를 지나는
    // 것은 Managed::HeapObject 파생뿐이다. C0가 답하려는 질문은 "그 둘의 비율"
    // 이므로 양쪽을 같은 잣대(건수·바이트)로 세어야 한다 — CRT 쪽은
    // _CrtMemCheckpoint가, 이쪽은 이 함수가 맡는다.
    //
    // 비용은 할당당 relaxed 원자 연산 둘, 해제당 하나다. MyAlloc 자체가 이미
    // DLL 경계를 넘는 호출이라 상대 비용은 무시할 수준이고, 그래서 빌드 구성과
    // 무관하게 항상 켜 둔다 — Release에서도 재야 진짜 수치가 나온다.
    //
    // 널 포인터를 넘긴 출력 인자는 건너뛴다.
    MEMORY_API void		MyHeapStats(size_t* outAllocCount, size_t* outFreeCount, size_t* outTotalBytes);
    MEMORY_API void		MyHeapStatsReset();
}
