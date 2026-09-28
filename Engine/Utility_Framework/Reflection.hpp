#pragma once
// 리플렉션 공개 관문. ReflectionMecro.h는 열거형 점검(8-17)에서 삭제 —
// 마지막 생존 매크로 AUTO_REGISTER_ENUM이 Property::enumType 직접 소유로
// 대체되며 매크로 0종이 됐다. 실체는 함수 API(ReflectionFunction.h 사슬)와
// 타입별 reflect() 레시피 + 스키마 코어(MetaSchema.h·글루는 ReflectionMeta.h)다.
//
// reflgen 전환 뒤 타입의 서술은 [[reflgen::reflect]] 에서 생성되고 다리(ReflgenBridge.h)가 엔진 스키마로
// 옮긴다. 비공개 멤버를 반영하는 타입은 friend struct reflgen::access; 를 적으므로 여기서 reflgen 을
// 보이게 한다 — 빌드는 주입 header 로 먼저 보여 주지만, 생성기는 header 를 주입 없이 읽는다.
#include "ReflectionFunction.h"
#include "ReflectionMeta.h"
#include "ReflgenBridge.h"
