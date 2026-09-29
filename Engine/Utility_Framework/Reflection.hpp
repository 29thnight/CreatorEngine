#pragma once
// 리플렉션 공개 관문. ReflectionMecro.h는 열거형 점검(8-17)에서 삭제 —
// 마지막 생존 매크로 AUTO_REGISTER_ENUM이 Property::enumType 직접 소유로
// 대체되며 매크로 0종이 됐다.
//
// reflgen 도입 P5: 타입의 서술은 [[reflgen::reflect]] 에서 생성되고, 런타임 타입은 reflgen 서술자다(ReflgenRuntime.h).
// 엔진 스키마(MetaSchema.h)와 reflgen 다리(ReflgenBridge.h)는 걷었다. 비공개 멤버를 반영하는 타입은
// friend struct reflgen::access; 를 적고 엔진 속성(creator::…)을 다므로 여기서 둘을 보이게 한다 — 빌드는 주입 header 로
// 먼저 보여 주지만, 생성기는 header 를 주입 없이 읽는다.
#include "ReflectionFunction.h"
#include "ReflectionMeta.h"
#include "ReflgenAttributes.h"
#include <reflgen/core/hook.h>
