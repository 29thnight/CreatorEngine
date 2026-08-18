#pragma once
// CT11-c: MetaAlias.h 흡수 은퇴 — 소비자가 이 파일 하나뿐이라 "공유 별칭
// 모음"의 존재 이유가 소멸했다. 사강 2종(TypeInfo·OffsetType — CT10의
// typeInfo/offset 필드 삭제 동반 사망)은 버리고 생존 4알리아스만 여기로.
// TypeTrait.h는 종전 MetaAlias 경유 전이 include였던 것을 직접 문다(HashedGuid).
#include "TypeTrait.h"
#include <functional>
#include <span>
#include <any>
#include <vector>
#include <memory>

// 외부 서술 훅 전방 선언(HasReflection이 판별에 쓴다) — 정의는 MetaSchema.h.
namespace meta
{
    template<class T> struct of;
}

namespace Meta
{
    struct MethodParameter;

    using SetterType = std::function<void(void* instance, std::any value)>;
    using Invoker = std::function<std::any(void* instance, const std::vector<std::any>& args)>;
    using MethodParameterContainer = std::vector<MethodParameter>;
    template<typename T>
    using View = std::span<T>;

    // IVectorIterator/VectorIteratorImpl/VectorIteratorFunc는 CT10 감사에서
    // 삭제 — createVectorIterator 필드가 전 코퍼스에서 호출 0건이었다(레거시
    // 직렬화 워크 전속, CT7에서 소비자 소멸).

    struct EnumType;

    // CT10 감사 수축: getter(any 게터)·isPointer·offset·typeInfo·벡터 메타
    // 6종(isVector·elementTypeInfo·createVectorIterator·elementTypeName·
    // elementTypeID·isElementPointer)은 소비 0으로 판명되어 삭제. 남은 것은
    // 이름 기반 소비자(콘솔 필드 설정·DrawMethods·프리팹 시딩)가 실제로 읽는
    // 필드뿐이다: name·typeName·setter·typeID + 인스펙터 속성 꼬리.
    struct Property
    {
        const char*           name{};
        std::string           typeName{};
        Meta::SetterType      setter{};
		HashedGuid		      typeID;

        // CT6-b: meta::field<>.with(...) 속성의 런타임 투영 — 어댑터(meta::adapt)가
        // member_info의 range/displayName을 여기 채우고 인스펙터가 소비한다.
        // 꼬리에 기본값으로 추가 — 기존 위치 지정 집합체 초기화는 영향받지 않는다.
        bool                    hasRange = false;
        float                   rangeMin = 0.0f;
        float                   rangeMax = 0.0f;
        const char*             displayName = nullptr;

        // 열거형 점검(8-17): 프로퍼티가 자기 enum 표를 직접 소유한다 —
        // MakeEnumPropertyImpl이 create_enum_type<EnumT>()의 함수-로컬 static
        // 정본을 연결한다(프로그램 수명). 이름 키 EnumRegistry 조회가 전부
        // 이 포인터로 대체되어 등록소·AUTO_REGISTER_ENUM이 함께 은퇴했다.
        const EnumType*         enumType = nullptr;
    };

    struct MethodParameter
    {
        std::string     name;
        std::string     typeName;
        HashedGuid		typeID;
    };

    struct Method
    {
        const char*              name;
        Invoker                  invoker;
        MethodParameterContainer parameters;
    };

    // CT11: 팩토리 접합 — FactoryRegistry(typeID 해시 조회 싱글턴)가 Type
    // 필드로 접혔다. 소비자 전원이 이미 Type*를 쥔 채 생성을 요청하므로
    // 조회 자체가 0회가 된다. adapt<T>()가 채우며, meta::polymorphic 파생만
    // createShared를 갖는 규약은 구 FactoryRegistry::Register<T> 그대로다.
    using CreateFn = void* (*)();
    using CreateSharedFn = std::shared_ptr<void>(*)();

    // K2 스테이지 A: GameObject::m_components가 고유 소유(std::unique_ptr)로
    // 바뀌며 createShared와 나란히 필요해졌다 — 반환은 void*형 삭제자를 든
    // unique_ptr<void,...>다(타입 소거 이유는 createShared와 같다: Type은
    // 런타임 값이라 템플릿 T를 다시 알 수 없다). 삭제자는 adapt<T>()가 T를
    // 캡처해 채운다 — meta::polymorphic 파생만 갖는 규약은 createShared와 동일.
    using CreateUniqueFn = std::unique_ptr<void, void(*)(void*)>(*)();

    struct Type
    {
        std::string            name{};
        View<const Property>   properties{};
        View<const Method>     methods{};
        const Type*            parent{ nullptr };
		HashedGuid             typeID{};
        CreateFn               create{ nullptr };
        CreateSharedFn         createShared{ nullptr };
        CreateUniqueFn         createUnique{ nullptr };
    };

    struct EnumValue 
    {
        const char* name;
        int         value;
    };

    struct EnumType 
    {
        const char*           name;
        View<const EnumValue> values;
    };

    template<typename T, std::size_t N> 
    using MetaContainer = std::array<T, N>;

    template<std::size_t N> 
    using MetaProperties = MetaContainer<Meta::Property, N>;

    template<std::size_t N> 
    using MetaMethods = MetaContainer<Meta::Method, N>;

    // CT9: 서술 보유 판별 — in-class reflect() 레시피 또는 외부 서술
    // meta::of<T> 특수화(전방 선언으로 충분 — 미특수화 primary는 불완전 타입이라
    // requires가 false로 떨어진다). "런타임 Type을 얻을 수 있는가" = 이 하나다.
    // 정본 콘셉트는 meta::reflectable(MetaSchema.h) — 여기는 하위 계층용 별해.
    template<typename T>
    concept HasReflection = requires { T::reflect(); }
        || requires { ::meta::of<T>::value; };
}
