#pragma once
#include "Core.Definition.h"
#include "Core.Runtime.h"
#include "Core.Memory.hpp"
#include "Core.Coroutine.h"
#include "PathFinder.h"
#include "LogSystem.h"
// CT3: Reflection.hpp를 뺐다 — 리플렉션을 쓰는 TU만 직접 include한다.
// 이 한 줄이 yaml-cpp·magic_enum·<any>를 280개 TU에 실어 나르고 있었다
// (헤더 터치 재빌드 4m29s의 몸통, ReflectionRedesignPlan CT0 실측).
#include "Delegate.h"
