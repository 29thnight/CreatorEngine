#pragma once
#include "MetaAlias.h"
#include <memory>

namespace Meta
{
    struct IVectorIterator
    {
        virtual bool IsValid() const = 0;
        virtual void* Get() const = 0;
        virtual void Next() = 0;
        virtual ~IVectorIterator() = default;
    };

    template<typename T>
    struct VectorIteratorImpl : public IVectorIterator
    {
        using Iter = typename std::vector<T>::iterator;

        Iter current;
        Iter end;

        VectorIteratorImpl(Iter begin, Iter end)
            : current(begin), end(end)
        {
        }

        bool IsValid() const override { return current != end; }
        void* Get() const override
        {
            if constexpr (std::is_pointer_v<T>)
                return *current;
            else if constexpr (is_shared_ptr_v<T>)
                return current->get();
            else
                return const_cast<void*>(static_cast<const void*>(&(*current)));
        }
        void Next() override { ++current; }
    };

	using VectorIteratorFunc = std::function<std::unique_ptr<IVectorIterator>(void* instance)>;

    struct EnumType;

    struct Property
    {
        const char*           name{};
        std::string           typeName{};
        const Meta::TypeInfo& typeInfo;
        Meta::GetterType      getter{};
        Meta::SetterType      setter{};
        bool                  isPointer{};
        Meta::OffsetType      offset{};
		HashedGuid		      typeID;

        //TODO: vector ó�� ���� ������Ƽ�� ���� �־�� �Ұ� ����.
        bool                    isVector = false;
        const Meta::TypeInfo&   elementTypeInfo;
        VectorIteratorFunc      createVectorIterator;
        std::string             elementTypeName;
		HashedGuid			    elementTypeID;
		bool                    isElementPointer = false;

        // CT6-b: meta::member<>(...) 속성의 런타임 투영 — 어댑터(meta::adapt)가
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
        const TypeInfo& typeInfo;
        HashedGuid		typeID;
    };

    struct Method
    {
        const char*              name;
        Invoker                  invoker;
        MethodParameterContainer parameters;
    };

    struct Type
    {
        std::string            name{};
        View<const Property>   properties{};
        View<const Method>     methods{};
        const Type*            parent{ nullptr };
		HashedGuid             typeID{};
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

    template<typename T>
    concept HasReflect = requires
    {
        { T::Reflect() } -> std::same_as<const Type&>;
    };

    // CT4-d: 신형(P2996 유사) 서술 보유 — consteval describe() 하나가 표기의
    // 전부다. 런타임 Type은 Meta::TypeOf<T>()가 어댑터로 공급한다.
    template<typename T>
    concept HasDescribe = requires
    {
        T::describe();
    };

    // "런타임 Type 테이블을 얻을 수 있는 타입" — 레거시/신형 무관 판별.
    // 컴파일타임 분기(벡터 매퍼·중첩 프로퍼티 자동 등록)는 HasReflect가 아니라
    // 이것을 물어야 한다: 신형 타입은 Reflect() 멤버가 없다.
    template<typename T>
    concept HasRuntimeType = HasReflect<T> || HasDescribe<T>;
}
