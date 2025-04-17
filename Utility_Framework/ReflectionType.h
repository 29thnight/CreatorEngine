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
			//CORE_ASSERT_MSG(current != end, "VectorIteratorImpl: Invalid iterator");
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

    struct Property
    {
        const char*           name;
        std::string           typeName;
        const Meta::TypeInfo& typeInfo;
        Meta::GetterType      getter;
        Meta::SetterType      setter;
        bool                  isPointer;
        Meta::OffsetType      offset;
		HashedGuid		      typeID;

        //TODO: vector 처리 전용 프로퍼티가 따로 있어야 할거 같음.
        bool                    isVector = false;
        const Meta::TypeInfo&   elementTypeInfo;
        VectorIteratorFunc      createVectorIterator;
        std::string             elementTypeName;
		HashedGuid			    elementTypeID;
		bool                    isElementPointer = false;
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
}
