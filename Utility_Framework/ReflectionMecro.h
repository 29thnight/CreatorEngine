#pragma once
// 리플렉션 매크로 — 생존자만 남았다 (PHASE 18 CT7 정산).
//
// 헤더툴 파이프라인(generated.h + ReflectionField/PropertyField/FieldEnd/
// meta_property/meta_method 사슬)은 CT4~CT5에서 P2996 유사 표기
// (static consteval describe() + meta::describe/member/method)로 대체되어
// CT7에서 삭제됐다. ScriptReflect 갈래는 CT2, BT/Ani BODY는 CT2에서 은퇴.
//
// 남은 셋은 표기 파이프라인과 무관한 정본이다:
//   - GENERATED_BODY: 생성자에서 m_name·m_typeID를 심는 컴포넌트 관례
//   - AUTO_REGISTER_CLASS: 등록 정본(RegisterReflectManual.h)이 소비
//   - AUTO_REGISTER_ENUM: 열거형 정적 자동 등록
#include "ReflectionFunction.h"

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
    virtual ~T() = default;
