// `import std;` 만으로 표준 라이브러리를 쓰는 번역 단위.
//
// 엔진의 /std 설정(stdcpp23 → v145 에서 /std:c++23preview)으로 std.ixx 를 직접
// 빌드하고 import 할 수 있는지가 판정이다. #include 는 하나도 없다.
//
// 판정은 종료 코드와 표식 CPP_MODULE_STD_OK 다.
import std;

int main()
{
	std::vector<int> values{ 5, 3, 1, 4, 2 };
	std::ranges::sort(values);

	const std::string text = std::format("{}-{}", values.front(), values.back());
	const std::expected<int, std::string> parsed = 42;
	const std::unordered_map<std::string, int> table{ { "alpha", 1 } };
	const std::filesystem::path path = "Assets/Scene.creator";
	const std::optional<int> empty;
	const std::span<const int> view{ values };

	const bool ok = text == "1-5" && parsed.value() == 42 && table.at("alpha") == 1 &&
	                path.extension() == ".creator" && !empty.has_value() && view.size() == 5;

	std::println("{}", ok ? "CPP_MODULE_STD_OK=true" : "CPP_MODULE_STD_FAILED");
	return ok ? 0 : 1;
}
