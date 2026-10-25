#pragma once
// 리플렉션 플래그 기반 클래스 (PHASE 18 계열 · 2026-08-18).
//
// 표식이 보장하는 것은 **다형 소멸**이다. 런타임 생성(Meta::Create<Base>, ReflgenRuntime.h)은 reflgen 서술자가
// 만든 T 를 기반 포인터로 넘기고 소비 측은 그것을 기반 포인터로 delete 한다 — 가상 소멸자가 없으면 그 자리에서
// 조용히 잘못된 소멸자가 불린다. (옛 Meta::Type 은 이 표식이 있는 타입에만 createShared/createUnique 를 붙였다 —
// reflgen 도입 P5 에서 타입 표와 함께 걷었다.)
//
// 이력: 원래 Managed::HeapObject였고, ManagedHeap.dll의 mimalloc으로 할당을
// 라우팅하는 클래스 스코프 operator new/delete를 들고 있었다. 그 할당자는
// 실측으로 걷어냈다(지분 0.031% · 씬 로드당 이득 상한 5us — ContainerLibraryDesign
// 결론부). 남은 것은 순수한 타입 시스템 표식이므로 meta로 편입했다.
//
// 의존 0. meta 계층의 순수성(엔진 include 0) 규약을 지킨다.
namespace meta
{
    class polymorphic
    {
    public:
        virtual ~polymorphic() = default;
    };
}
