#pragma once
#include "MetaAlias.h"
#include <memory>

// 외부 서술 훅 전방 선언(HasReflection이 판별에 쓴다) — 정의는 MetaSchema.h.
namespace meta
{
    template<class T> struct of;
}

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

    // CT9: 서술 보유 판별 — in-class reflect() 레시피 또는 외부 서술
    // meta::of<T> 특수화(전방 선언으로 충분 — 미특수화 primary는 불완전 타입이라
    // requires가 false로 떨어진다). "런타임 Type을 얻을 수 있는가" = 이 하나다.
    // 정본 콘셉트는 meta::reflectable(MetaSchema.h) — 여기는 하위 계층용 별해.
    template<typename T>
    concept HasReflection = requires { T::reflect(); }
        || requires { ::meta::of<T>::value; };
}
