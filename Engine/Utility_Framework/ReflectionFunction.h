#pragma once
#include "TypeTrait.h"
#include "TypeDefinition.h" // CT11-c: 종전 MetaUtility.h 경유 전이 include를 직접
#include <typeindex>        // CT11-c: 흡수된 ToString(std::type_index)의 자급
#include "ReflectionType.h"
#include "ReflectionRegister.h"
#include "LogSystem.h"
#include "HashingString.h"
#include <array>        // 옛 include 사슬(MetaSchema.h 경유) — 소비자가 전이로 받던 표준 헤더
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include "ReflgenRuntime.h"

// reflgen 도입 P5: 런타임 타입 표(Meta::Type·Property·Method)와 그 공급원(TypeOf·Register·MakeProperty·MakeMethod·
// create_enum_type)을 걷었다 — 런타임 타입은 reflgen 서술자이고 창구는 ReflgenRuntime.h 다(Meta::Find·TypeOf·
// LocalFields…). 콘솔 필드 설정은 field_info::address() 에 값을 쓰고, 인스펙터 메서드 호출은 method_info::invoke()
// 로, enum 표는 field_info::enumeration() 으로 간다. 이 헤더는 옛 include 사슬만 남는다.
//
// 남아 있던 이름 도구(ToString·RemoveObjectPrefix·IsSharedPtr·IsWeakPtr·IsRawPtr·ExtractPointee)는 소비자가 0이었다
// — Property::typeName 을 만들던 것이다. 필드 타입 이름은 이제 field_info::type_name()(reflgen 표기)이다.
