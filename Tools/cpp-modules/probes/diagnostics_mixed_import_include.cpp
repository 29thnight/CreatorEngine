// 관찰 항목 — 한 번역 단위가 **먼저 import 하고 그 뒤에** 진단 헤더를 include 한다.
//
// diagnostics_mixed_include_import.cpp 의 반대 순서다. 표준상 순서는 상관없어야
// 하지만, 컴파일러 구현에서는 "import 뒤의 #include" 쪽이 오래 더 약했다.
// 둘을 따로 재야 붉은 쪽이 어느 순서인지 안다.
//
// 컴파일만 한다(/c).
import ce.diagnostics;

#include "ProfileScope.h"
#include "ProfileCaptureFile.h"

#include "diagnostics_build_guard.h"

int cpp_module_mixed_import_then_include()
{
	ce::profile_event event{};
	event.marker = ce::marker<"CppModuleProbe.Mixed">();
	return static_cast<int>(sizeof(event)) + static_cast<int>(ce::kEventsPerChunk) +
	       static_cast<int>(ce::kCaptureFileVersion) + static_cast<int>(event.marker);
}
