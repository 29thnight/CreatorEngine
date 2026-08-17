#pragma once
// 리플렉션 공개 관문. ReflectionMecro.h는 열거형 점검(8-17)에서 삭제 —
// 마지막 생존 매크로 AUTO_REGISTER_ENUM이 Property::enumType 직접 소유로
// 대체되며 매크로 0종이 됐다. 실체는 함수 API(ReflectionFunction.h 사슬)와
// 타입별 reflect() 레시피 + 스키마 코어(MetaSchema.h·글루는 ReflectionMeta.h)다.
#include "ReflectionFunction.h"
#include "ReflectionMeta.h"
