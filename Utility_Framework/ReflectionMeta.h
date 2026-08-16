#pragma once
// 컴파일타임 리플렉션 코어 (PHASE 18 CT4).
//
// 표기 한 곳(멤버 포인터 + 문자열화 이름)에서 두 세계를 모두 공급한다:
//   1. CtReflect() — constexpr 메타(멤버 포인터 튜플). typed 방문(ForEachMember)이
//      멤버를 실제 타입 T&로 본다 — CT6에서 직렬화·인스펙터가 이 계약으로 옮겨
//      가면 any/function 이중 타입소거가 소멸한다.
//   2. Reflect() — 어댑터(AdaptReflect)가 같은 메타에서 기존 Meta::Type 런타임
//      테이블을 생성한다. 소비자(직렬화·인스펙터·팩토리) 무변경으로 타입을
//      타입 단위 원자 이전하기 위한 과도기 다리이며 CT7에서 소멸한다.
//
// 설계 제약 셋 (ReflectionRedesignPlan CT4):
//   - CtReflect는 static 데이터가 아니라 **함수**다. 매크로가 클래스 상단(멤버
//     선언 앞)에 놓이는 기존 관례를 지키려면 멤버 포인터 형성이 complete-class
//     문맥(함수 본문)에서 일어나야 한다 — static 데이터 멤버 초기치는 뒤에
//     선언된 멤버를 못 본다.
//   - 이름은 NTTP source_location 추출이 아니라 **매크로 문자열화**가 정본이다.
//     MSVC의 멤버 포인터 NTTP 시그니처 표기는 버전 민감하고, 매크로 경로는
//     현행 generated.h와 같은 이름(#member)을 컴파일러 무관하게 보장한다 —
//     직렬화 키 = 멤버명이므로 이것이 자산 호환의 전제다.
//   - typeID는 아직 구 정본(GUIDCreator)이다. 골든 diff 0이 메타/어댑터의
//     동등성을 증명하려면 정체성 교체(FNV-1a 64)는 별도 슬라이스여야 한다.
#include "ReflectionFunction.h"
#include <tuple>
#include <array>
#include <type_traits>

namespace Meta
{
    template<auto Ptr>
    struct CtMember
    {
        const char* name;
        static constexpr auto pointer = Ptr;
    };

    template<class Owner, class Parent, class... Members>
    struct CtTypeMeta
    {
        using OwnerType = Owner;
        using ParentType = Parent;
        const char* typeName;
        std::tuple<Members...> members;
    };

    template<class Owner, class Parent = void, class... Members>
    constexpr auto MakeCtMeta(const char* typeName, Members... ms)
    {
        return CtTypeMeta<Owner, Parent, Members...>{ typeName, std::tuple<Members...>{ ms... } };
    }

    template<class T>
    concept HasCtReflect = requires { T::CtReflect(); };

    // typed 방문 — 부모 메타 보유 시 부모 먼저(직렬화의 부모-우선 순회와 동일
    // 의미론). 부모가 레거시(CtReflect 없음)면 부모 몫은 어댑터 Type::parent
    // 체인이 담당하므로 여기서는 건너뛴다.
    template<class T, class F>
    void ForEachMember(T& obj, F&& f)
    {
        constexpr auto meta = T::CtReflect();
        using MetaT = std::remove_cv_t<decltype(meta)>;

        if constexpr (HasCtReflect<typename MetaT::ParentType>)
        {
            ForEachMember(static_cast<typename MetaT::ParentType&>(obj), f);
        }

        std::apply([&](const auto&... ms)
        {
            (f(ms.name, obj.*(ms.pointer)), ...);
        }, meta.members);
    }

    // 어댑터: constexpr 메타 → 기존 Meta::Type 테이블. MakeProperty를 그대로
    // 쓰므로(같은 이름·같은 멤버 포인터·같은 순서) 산출 Property는 generated.h
    // 경로와 바이트 동등하다 — 골든 diff 0이 그 증명이다.
    template<class T>
    const Type& AdaptReflect()
    {
        static constexpr auto meta = T::CtReflect();
        using MetaT = std::remove_cv_t<decltype(meta)>;

        static const auto properties = std::apply([](const auto&... ms)
        {
            return std::to_array({ MakeProperty(ms.name, ms.pointer)... });
        }, meta.members);

        const Type* parent = nullptr;
        if constexpr (!std::is_void_v<typename MetaT::ParentType>)
        {
            parent = &MetaT::ParentType::Reflect();
        }

        static const Type type{ meta.typeName, properties, {}, parent,
            TypeTrait::GUIDCreator::GetTypeID<T>() };
        return type;
    }
}

// 멤버 표기 — 이름은 문자열화로 고정(= 현행 YAML 필드명 = 자산 호환).
#define ct_property(m) Meta::CtMember<&__Ty::m>{ #m }

// generated.h + ReflectX + [[Serializable]]/[[Property]] 어노테이션 일습을
// 대체하는 단일 표기. 매크로 위치는 기존 ReflectX와 같은 자리(클래스 상단)면
// 된다 — CtReflect가 함수라 멤버 선언 순서에 구애받지 않는다.
#define ReflectionMetaField(T, ...) \
public: \
    using __Ty = T; \
    static constexpr auto CtReflect() { return Meta::MakeCtMeta<T>( #T, __VA_ARGS__ ); } \
    static const Meta::Type& Reflect() { return Meta::AdaptReflect<T>(); }

#define ReflectionMetaFieldInheritance(T, Parent, ...) \
public: \
    using __Ty = T; \
    static constexpr auto CtReflect() { return Meta::MakeCtMeta<T, Parent>( #T, __VA_ARGS__ ); } \
    static const Meta::Type& Reflect() { return Meta::AdaptReflect<T>(); }
