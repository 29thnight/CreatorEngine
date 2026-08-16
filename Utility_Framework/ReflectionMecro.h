#pragma once
#include "ReflectionFunction.h"

#pragma region Reflection Macros
#define EXPAND(x) x

#define meta_default(T) { Meta::Register<T>(); }

// �Ϲ� Reflection Macros
#define ReflectionField(T) public: \
 using __Ty = T; \
 static const Meta::Type& Reflect()

#define ReflectionFieldInheritance(T, Parent) using __Ty = T; \
 using __P_Ty = Parent; \
 static const Meta::Type& Reflect()

// ScriptReflect 갈래(ReflectionScriptField 계열)는 CT2에서 삭제했다 —
// ModuleBehavior(C++ 스크립트)가 9-4에서 은퇴하며 베이스 virtual 선언 자체가
// 소멸했고, 남은 사용처는 빌드에서 빠진 Dynamic_CPP 데이터 폴더뿐이었다.

//���� Reflection Prop and Method Macros
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
//�Ϲ� Reflection Inheritance Macros
#define PropertyAndMethodInheritance \
    static const Meta::Type type{ type_name.c_str(), properties, methods, &__P_Ty::Reflect(), TypeTrait::GUIDCreator::GetTypeID<__Ty>() }; \
    return type; \

#define PropertyOnlyInheritance \
    static const Meta::Type type{ type_name.c_str(), properties, {}, &__P_Ty::Reflect(), TypeTrait::GUIDCreator::GetTypeID<__Ty>() }; \
    return type; \

#define MethodOnlyInheritance \
    static const Meta::Type type{ type_name.c_str(), {}, methods, &__P_Ty::Reflect(), TypeTrait::GUIDCreator::GetTypeID<__Ty>() }; \
    return type; \
// ���� Reflection FieldEnd Macros
#define FieldEnd(T, Mecro) \
        std::string type_name = #T;\
        EXPAND(Mecro) \
// ������ Reflection �ڵ� ��� Mecros
#define REFLECTION_REGISTER() void RegisterReflect()
#define REFLECTION_REGISTER_EXECUTE() RegisterReflect()

#define AUTO_REGISTER_ENUM(EnumTypeName) \
    static const Meta::EnumAutoRegistrar<EnumTypeName> autoRegistrar_##EnumTypeName;

#define AUTO_REGISTER_CLASS(ClassTypeName) \
    Meta::Register<ClassTypeName>();
// ������ Reflection Body Macros
#define GENERATED_BODY(T) \
    T() \
    { \
        m_name = #T; \
        m_typeID = TypeTrait::GUIDCreator::GetTypeID<T>(); \
    }\
    virtual ~T() = default;

// BT_*_BODY · ANIBEHAVIOR_BODY 4종도 CT2에서 삭제 — C++ BT/AniBehavior가
// 관리 측(C#)으로 이관되며(9-8) 사용처가 미빌드 Dynamic_CPP에만 남아 있었다.

#pragma endregion
