#pragma once
#include "ReflectionFunction.h"

#pragma region Reflection Macros
#define EXPAND(x) x

#define meta_default(T) { Meta::Register<T>(); }

// 일반 Reflection Macros
#define ReflectionField(T) public: \
 using __Ty = T; \
 static const Meta::Type& Reflect()

#define ReflectionFieldInheritance(T, Parent) using __Ty = T; \
 using __P_Ty = Parent; \
 static const Meta::Type& Reflect()

// 스크립트 Reflection Macros
#define ReflectionScriptField(T) public: \
 using __Ty = T; \
 virtual const Meta::Type& ScriptReflect() override

#define ReflectionScriptFieldInheritance(T, Parent) using __Ty = T; \
 using __P_Ty = Parent; \
 virtual const Meta::Type& ScriptReflect() override

#define ScriptFieldDefault() \
virtual const Meta::Type& ScriptReflect()\
{\
    static const Meta::Type type{};\
    return type;\
};

//공통 Reflection Prop and Method Macros
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
//일반 Reflection Inheritance Macros
#define PropertyAndMethodInheritance \
    static const Meta::Type type{ type_name.c_str(), properties, methods, &__P_Ty::Reflect(), TypeTrait::GUIDCreator::GetTypeID<__Ty>() }; \
    return type; \

#define PropertyOnlyInheritance \
    static const Meta::Type type{ type_name.c_str(), properties, {}, &__P_Ty::Reflect(), TypeTrait::GUIDCreator::GetTypeID<__Ty>() }; \
    return type; \

#define MethodOnlyInheritance \
    static const Meta::Type type{ type_name.c_str(), {}, methods, &__P_Ty::Reflect(), TypeTrait::GUIDCreator::GetTypeID<__Ty>() }; \
    return type; \
//스크립트 Reflection Inheritance Macros
#define PropertyAndMethodScriptInheritance \
    static const Meta::Type type{ type_name.c_str(), properties, methods, &__P_Ty::ScriptReflect(), TypeTrait::GUIDCreator::GetTypeID<__Ty>() }; \
    return type; \

#define PropertyOnlyScriptInheritance \
    static const Meta::Type type{ type_name.c_str(), properties, {}, &__P_Ty::ScriptReflect(), TypeTrait::GUIDCreator::GetTypeID<__Ty>() }; \
    return type; \

#define MethodOnlyScriptInheritance \
    static const Meta::Type type{ type_name.c_str(), {}, methods, &__P_Ty::ScriptReflect(), TypeTrait::GUIDCreator::GetTypeID<__Ty>() }; \
    return type; \
// 공통 Reflection FieldEnd Macros
#define FieldEnd(T, Mecro) \
        std::string type_name = #T;\
        EXPAND(Mecro) \
// 엔진용 Reflection 자동 등록 Mecros
#define REFLECTION_REGISTER() void RegisterReflect()
#define REFLECTION_REGISTER_EXECUTE() RegisterReflect()

#define AUTO_REGISTER_ENUM(EnumTypeName) \
    static const Meta::EnumAutoRegistrar<EnumTypeName> autoRegistrar_##EnumTypeName;

#define AUTO_REGISTER_CLASS(ClassTypeName) \
    Meta::Register<ClassTypeName>();
// 엔진용 Reflection Body Macros
#define GENERATED_BODY(T) \
    T() \
    { \
        m_name = #T; \
        m_typeID = TypeTrait::GUIDCreator::GetTypeID<T>(); \
    }\
    virtual ~T() = default; \
// 스크립트용 Reflection Body Macros
#define MODULE_BEHAVIOR_BODY(T) \
    T() \
    { \
        m_name = #T; \
        m_typeID = TypeTrait::GUIDCreator::GetTypeID<ModuleBehavior>(); \
        m_scriptTypeID = TypeTrait::GUIDCreator::GetTypeID<T>(); \
    }\
    virtual ~T() = default; \

#define BT_ACTION_BODY(T) \
    T() \
    { \
        m_name = #T; \
        m_typeID = TypeTrait::GUIDCreator::GetTypeID<ActionNode>(); \
        m_scriptTypeID = TypeTrait::GUIDCreator::GetTypeID<T>(); \
    }\
    virtual ~T() = default; \

#define BT_CONDITION_BODY(T) \
    T() \
    { \
        m_name = #T; \
        m_typeID = TypeTrait::GUIDCreator::GetTypeID<ConditionNode>(); \
        m_scriptTypeID = TypeTrait::GUIDCreator::GetTypeID<T>(); \
    }\
    virtual ~T() = default; \

#pragma endregion
