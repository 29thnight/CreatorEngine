#pragma once
// CT11-c: MetaAlias.h 흡수 은퇴 — 소비자가 이 파일 하나뿐이라 "공유 별칭
// 모음"의 존재 이유가 소멸했다.
//
// reflgen 도입 P5: Meta::Property·Method·MethodParameter·Type·EnumType·EnumValue 와 그 별칭(SetterType·Invoker·View·
// MetaContainer…)을 걷었다 — 런타임 타입 표는 reflgen 서술자다(ReflgenRuntime.h). 서술 보유 판별(HasReflection)도
// 엔진 스키마와 함께 걷었다 — reflgen::reflectable<T> 가 그 자리다. 옛 include 사슬만 남는다.
#include "TypeTrait.h"
#include <any>        // 옛 include 사슬 — 소비자가 전이로 받던 표준 헤더
#include <functional>
#include <memory>
#include <span>
#include <vector>
