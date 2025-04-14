#pragma once
#include "MetaAlias.h"

namespace Meta
{
    struct Property
    {
        const char*           name;
        std::string           typeName;
        const Meta::TypeInfo& typeInfo;
        Meta::GetterType      getter;
        Meta::SetterType      setter;
        bool                  isPointer;
        Meta::OffsetType      offset;
    };

    struct MethodParameter
    {
        std::string     name;
        std::string     typeName;
        const TypeInfo& typeInfo;
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
