#pragma once
// CT3: ClassProperty.h·yaml-cpp(본문 사용 0회 — 죽은 include)와
// MetaStateCommand.h·<stack>(UndoManager 전속 → ReflectionUndo.h로 이관)을
// 잘랐다.
//
// reflgen 도입 P5: 등록 코어(Meta::Registry 싱글턴·MetaDataRegistry·RegisterClassInitalize/Finalize)를 걷었다 — 런타임
// 타입 등록소는 reflgen::registry 하나이고(Meta::Types, 정의는 ReflgenRuntime.cpp) 생성된 등록 함수가 채운다.
// 등록소는 함수 지역 정적이라 부트스트랩이 만들고 부술 것이 없다(옛 Finalize 는 이름·ID 색인을 비우기만 했다).
#include "ReflectionType.h"
#include "MetaPolymorphic.h"
#include "ClassProperty.h"
#include "ReflgenRuntime.h"
#include <any>        // 옛 include 사슬 — 소비자가 전이로 받던 표준 헤더
#include <functional>
#include <string_view>
#include <typeindex>
