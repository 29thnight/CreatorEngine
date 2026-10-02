// 관찰 항목 — `import std;` 와 `import ce.diagnostics;` 를 함께 쓴다.
//
// ce.diagnostics 는 전역 모듈 조각에서 STL 을 **#include** 로 열었고, 이 번역
// 단위는 같은 STL 을 std 모듈로 본다. 같은 std::vector 가 두 경로로 들어오는
// 셈이다. 엔진 전체가 이 모양을 거쳐 가므로(라이브러리마다 전환 시점이 다르다)
// 1단계에서 미리 잰다.
//
// 컴파일만 한다(/c).
import std;
import ce.diagnostics;

#include "diagnostics_build_guard.h"

int cpp_module_std_with_diagnostics()
{
	std::vector<ce::profile_event> events(2);
	events[1].marker = ce::marker<"CppModuleProbe.StdMix">();
	const std::string label = std::format("{}", events.size());
	return label == "2" && ce::kEventsPerChunk == 256 ? 0 : 1;
}
