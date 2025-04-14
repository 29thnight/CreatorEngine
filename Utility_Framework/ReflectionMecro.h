#pragma once
#include "ReflectionFunction.h"

#pragma region Reflection Macros

constexpr uint32_t PropertyAndMethod = 0;
constexpr uint32_t PropertyOnly = 1;
constexpr uint32_t MethodOnly = 2;
#define meta_default(T) { Meta::Register<T>(); }

#define ReflectionField(T, num) using __Ty = T; \
 static constexpr uint32_t ret_option = num; \
 static const Meta::Type& Reflect()

#define ReflectionFieldInheritance(T, num, Parent) using __Ty = T; \
 using __P_Ty = Parent; \
 static constexpr uint32_t ret_option = num; \
 static const Meta::Type& Reflect()

#define PropertyField static const auto properties = std::to_array
#define MethodField static const auto methods = std::to_array

#define meta_property(member) Meta::MakeProperty(#member, &__Ty::member),
#define meta_enum_property(member) Meta::MakeProperty(#member, &__Ty::member),
#define meta_method(method, ...) Meta::MakeMethod(#method, &__Ty::method, { __VA_ARGS__ }),

#define ReturnReflection(T) \
    if constexpr (ret_option == PropertyAndMethod) \
    { \
        static const Meta::Type type{ #T, properties, methods, nullptr }; \
        return type; \
    } \

#define ReturnReflectionPropertyOnly(T) \
    if constexpr (ret_option == PropertyOnly) \
    { \
        static const Meta::Type type{ #T, properties, {}, nullptr }; \
        return type; \
    } \

#define ReturnReflectionMethodOnly(T) \
    if constexpr (ret_option == MethodOnly) \
    { \
        static const Meta::Type type{ #T, {}, methods, nullptr }; \
        return type; \
    } \

#define ReturnReflectionInheritance(T) \
    if constexpr (ret_option == PropertyAndMethod) \
    { \
        static const Meta::Type type{ #T, properties, methods, &__P_Ty::Reflect() }; \
        return type; \
    } \

#define ReturnReflectionInheritancePropertyOnly(T) \
    if constexpr (ret_option == PropertyOnly) \
    { \
        static const Meta::Type type{ #T, properties, {}, &__P_Ty::Reflect() }; \
        return type; \
    } \

#define ReturnReflectionInheritanceMethodOnly(T) \
    if constexpr (ret_option == MethodOnly) \
    { \
        static const Meta::Type type{ #T, {}, methods, &__P_Ty::Reflect() }; \
        return type; \
    } \

#define AUTO_REGISTER_ENUM(EnumTypeName) \
    static const Meta::EnumAutoRegistrar<EnumTypeName> autoRegistrar_##EnumTypeName;
#pragma endregion
