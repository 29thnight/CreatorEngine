// 관찰 항목 — **먼저** `import std;` 하고 그 뒤에 표준 헤더를 include 한다.
//
// std_include_then_import.cpp 의 반대 순서다. 컴파일러 구현에서는 이 순서가 오래
// 더 약했다. 둘을 따로 재야 붉은 쪽이 어느 순서인지 안다.
import std;

#include <format>
#include <string>
#include <vector>

int main()
{
	std::vector<std::string> names{ "Scene", "Asset" };
	const std::string joined = std::format("{}/{}", names[0], names[1]);
	std::println("{}", joined == "Scene/Asset" ? "CPP_MODULE_STD_MIXED_OK=true" : "CPP_MODULE_STD_MIXED_FAILED");
	return joined == "Scene/Asset" ? 0 : 1;
}
