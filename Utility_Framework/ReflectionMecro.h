#pragma once
#include "ReflectionYml.h"
#include "ReflectionImGuiHelper.h"

#pragma region Reflection Macros
#define EXPAND(x) x

#define meta_default(T) { Meta::Register<T>(); }

#define ReflectionField(T) using __Ty = T; \
 static const Meta::Type& Reflect()

#define ReflectionFieldInheritance(T, Parent) using __Ty = T; \
 using __P_Ty = Parent; \
 static const Meta::Type& Reflect()

#define PropertyField static const auto properties = std::to_array
#define MethodField static const auto methods = std::to_array

#define meta_property(member) Meta::MakeProperty(#member, &__Ty::member),
#define meta_enum_property(member) Meta::MakeProperty(#member, &__Ty::member),
#define meta_method(method, ...) Meta::MakeMethod(#method, &__Ty::method, { __VA_ARGS__ }),

#define PropertyAndMethod \
    static const Meta::Type type{ type_name.c_str(), properties, methods, nullptr, TypeTrait::GUIDCreator::GetTypeID<__Ty>() }; \
    return type; \

#define PropertyOnly \
    static const Meta::Type type{ type_name.c_str(), properties, {}, nullptr, TypeTrait::GUIDCreator::GetTypeID<__Ty>() }; \
    return type; \

#define MethodOnly \
    static const Meta::Type type{ type_name.c_str(), {}, methods, nullptr, TypeTrait::GUIDCreator::GetTypeID<__Ty>() }; \
    return type; \

#define PropertyAndMethodInheritance \
    static const Meta::Type type{ type_name.c_str(), properties, methods, &__P_Ty::Reflect(), TypeTrait::GUIDCreator::GetTypeID<__Ty>() }; \
    return type; \

#define PropertyOnlyInheritance \
    static const Meta::Type type{ type_name.c_str(), properties, {}, &__P_Ty::Reflect(), TypeTrait::GUIDCreator::GetTypeID<__Ty>() }; \
    return type; \

#define MethodOnlyInheritance \
    static const Meta::Type type{ type_name.c_str(), {}, methods, &__P_Ty::Reflect(), TypeTrait::GUIDCreator::GetTypeID<__Ty>() }; \
    return type; \

#define FieldEnd(T, Mecro) \
        std::string type_name = #T;\
        EXPAND(Mecro) \

#define REFLECTION_REGISTER() void RegisterReflect()

#define AUTO_REGISTER_ENUM(EnumTypeName) \
    static const Meta::EnumAutoRegistrar<EnumTypeName> autoRegistrar_##EnumTypeName;

#define AUTO_REGISTER_CLASS(ClassTypeName) \
    Meta::Register<ClassTypeName>();

#define GENERATED_BODY(T) \
    T() \
    { \
        m_name = #T; \
        m_typeID = TypeTrait::GUIDCreator::GetTypeID<T>(); \
    }\
    virtual ~T() = default; \
    
#pragma endregion
