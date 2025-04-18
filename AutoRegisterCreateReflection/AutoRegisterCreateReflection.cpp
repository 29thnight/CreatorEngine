#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <set>
#include <unordered_map>

namespace fs = std::filesystem;

int main()
{
	std::string inputPath = "D:\\LastProject\\Academy_4Q\\";
	std::string outputPath = "test.h";

	std::regex reflectionRegex(R"(ReflectionField(Inheritance)?\s*\((\w+))");
	std::unordered_map<std::string, std::string> classToHeader;
	std::set<std::string> includes;
	std::set<std::string> classNames;

	for (const auto& file : fs::recursive_directory_iterator(inputPath))
	{
		if (!file.is_regular_file() || file.path().extension() != ".h")
			continue;

		std::ifstream in(file.path());
		std::string line;

		while (std::getline(in, line))
		{
			std::smatch match;
			if (std::regex_search(line, match, reflectionRegex))
			{
				std::string className = match[2];
				classToHeader[className] = file.path().filename().string(); // 상대경로로 바꾸고 싶으면 수정
				classNames.insert(className);
			}
		}
	}

	// Generate .cpp file
	std::ofstream out(outputPath);
	out << "// Auto-generated RegisterReflect.h\n\n";
	for (const auto& [className, header] : classToHeader)
	{
		if (includes.insert(header).second)
		{
			out << "#include \"" << header << "\"\n";
		}
	}

	out << "\n#define AUTO_REGISTER_CLASS(ClassTypeName) \\\n";
	out << "    Meta::Register<ClassTypeName>();\n\n";

	out << "void REFLECTION_REGISTER()\n{\n";
	for (const auto& className : classNames)
	{
		out << "    AUTO_REGISTER_CLASS(" << className << ");\n";
	}
	out << "}\n";

	std::cout << "Generated " << classNames.size() << " reflected classes.\n";
	return 0;
}