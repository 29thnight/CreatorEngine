#pragma once
// 리플렉션 매크로 — 최종 생존자 1종 (PHASE 18 CT7-b 정산).
//
// GENERATED_BODY는 CRTP 정체성 베이스(meta::identity<T, Base> —
// ReflectionMeta.h)로 대체되어 삭제됐다: 상속 선언 자체가 m_name·m_typeID
// 스탬핑을 수행하므로 개발자가 정체성을 신경 쓸 일이 없다. GameObject·Scene
// 처럼 m_name이 인스턴스 이름인 비컴포넌트 타입만 생성자 수동 스탬핑을
// 유지한다.
//
// AUTO_REGISTER_ENUM은 ## 이름 결합으로 정적 등록자를 만드는 실작업이라
// 남는다 — 매크로가 아니면 표현할 수 없는 유일한 자리다.
#include "ReflectionFunction.h"

#define AUTO_REGISTER_ENUM(EnumTypeName)     static const Meta::EnumAutoRegistrar<EnumTypeName> autoRegistrar_##EnumTypeName;
