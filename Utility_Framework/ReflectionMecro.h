#pragma once
// 리플렉션 매크로 — 생존자 2종 (PHASE 18 CT7 후속 정련).
//
// 헤더툴 파이프라인의 매크로 사슬은 CT7에서 은퇴했고, AUTO_REGISTER_CLASS는
// Meta::Register<T>()의 순수 별칭이라 삭제했다(등록 정본이 직접 호출).
//
//   - GENERATED_BODY: 생성자에서 m_name·m_typeID를 심는다 — 런타임 정체성
//     (GetTypeID 소비 전반·컴포넌트 마스크·실타입 디스패치)의 원천이며,
//     생성자만이 모든 생성 경로에서 순서가 보장되는 스탬핑 지점이다.
//     (구 매크로의 `virtual ~T() = default;`는 IObject/Object의 가상 소멸자가
//     이미 보장하는 중복이라 제거 — 암묵 소멸자도 가상성을 상속한다.)
//     이름의 "GENERATED"는 헤더툴 시대의 흔적이다 — 리네임은 19파일 코스메틱.
//   - AUTO_REGISTER_ENUM: ## 이름 결합으로 정적 등록자를 만든다(실작업).
#include "ReflectionFunction.h"

#define AUTO_REGISTER_ENUM(EnumTypeName) \
    static const Meta::EnumAutoRegistrar<EnumTypeName> autoRegistrar_##EnumTypeName;

#define GENERATED_BODY(T) \
    T() \
    { \
        m_name = #T; \
        m_typeID = TypeTrait::GUIDCreator::GetTypeID<T>(); \
    }
