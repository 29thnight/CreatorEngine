// 관찰 항목 — 한 번역 단위가 진단 헤더를 **먼저 include 하고 그 뒤에 import** 한다.
//
// 전환 중에는 이 모양이 실제로 생긴다. EngineBootstrap.h · ProfilerView.h ·
// CollisionGeometryLibrary.h 가 ProfileScope.h 를 공개 헤더에서 열고 있으므로,
// 그 헤더를 여는 .cpp 가 import 로 바뀌어도 헤더 쪽 정의가 전이로 함께 들어온다.
//
// 표준은 두 경로의 정의를 하나로 합치라고 한다(전역 모듈에 붙은 같은 엔터티).
// 컴파일러가 실제로 그렇게 하는지가 이 항목이 재는 것이다. 붉으면 전환 단위가
// ".cpp 하나" 가 아니라 "그 .cpp 가 전이로 여는 공개 헤더까지" 로 커진다.
//
// 컴파일만 한다(/c). 링크 동일성은 diagnostics_module_probe 가 맡는다.
#include "ProfileScope.h"
#include "ProfileCaptureFile.h"

import ce.diagnostics;

#include "diagnostics_build_guard.h"

int cpp_module_mixed_include_then_import()
{
	ce::profile_event event{};
	event.marker = ce::marker<"CppModuleProbe.Mixed">();
	return static_cast<int>(sizeof(event)) + static_cast<int>(ce::kEventsPerChunk) +
	       static_cast<int>(ce::kCaptureFileVersion) + static_cast<int>(event.marker);
}
