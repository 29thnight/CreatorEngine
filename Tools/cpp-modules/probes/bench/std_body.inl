// std_include.cpp · std_import.cpp 가 **같은 본문**을 컴파일하게 하는 조각.
//
// 표준 헤더를 스스로 열지 않는다 — 앞에서 #include 로 열었든 `import std;` 로
// 들였든 같은 이름을 쓴다. 두 쪽의 차이는 표준 라이브러리를 들이는 방법 하나다.
int cpp_module_bench_std_body()
{
	std::vector<int> values{ 5, 3, 1, 4, 2 };
	std::ranges::sort(values);

	std::unordered_map<std::string, int> table;
	table.emplace("alpha", values.front());

	const std::filesystem::path path = "Assets/Scene.creator";
	const std::string text = std::format("{}:{}", path.filename().string(), table.at("alpha"));

	std::mutex lock;
	std::scoped_lock guard{ lock };
	std::optional<std::chrono::milliseconds> elapsed = std::chrono::milliseconds{ 16 };

	return static_cast<int>(text.size()) + static_cast<int>(elapsed->count());
}
