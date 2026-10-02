// 관찰 항목 — 표준 헤더를 **먼저 include 하고 그 뒤에** `import std;` 한다.
//
// 전환 중의 실제 모양이다. 엔진 헤더(Core.Definition.h 등)가 STL 을 #include 로
// 여는 동안, 그 헤더를 여는 .cpp 가 `import std;` 를 쓰기 시작하면 한 번역
// 단위에 두 경로가 섞인다. 이것이 붉으면 `import std;` 는 엔진 헤더 전체가
// 바뀐 뒤에만 쓸 수 있다.
#include <format>
#include <string>
#include <vector>

import std;

int main()
{
	std::vector<std::string> names{ "Scene", "Asset" };
	const std::string joined = std::format("{}/{}", names[0], names[1]);
	std::println("{}", joined == "Scene/Asset" ? "CPP_MODULE_STD_MIXED_OK=true" : "CPP_MODULE_STD_MIXED_FAILED");
	return joined == "Scene/Asset" ? 0 : 1;
}
