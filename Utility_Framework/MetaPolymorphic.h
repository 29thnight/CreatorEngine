#pragma once
// 리플렉션 플래그 기반 클래스 (PHASE 18 계열 · 2026-08-18).
//
// 이 표식을 상속한 타입에만 리플렉션 레지스트리가 shared/unique 팩토리
// (Meta::Type::createShared / createUnique)를 붙인다. ReflectionMeta.h의
// adapt<T>()가 std::is_base_of_v<meta::polymorphic, T>로 가른다.
//
// 표식이 보장하는 것은 **다형 소멸**이다. createUnique가 넘기는 삭제자는
// void*를 T로 되돌려 delete하고, 소비 측은 그것을 기반 포인터로 들고 다닌다 —
// 가상 소멸자가 없으면 그 자리에서 조용히 잘못된 소멸자가 불린다.
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
