#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <set>
#include <unordered_map>
#include <sstream>
#include <Windows.h>

std::filesystem::path GetExecutablePath()
{
    char buffer[MAX_PATH];
    GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    return std::filesystem::path(buffer).parent_path();
}

namespace fs = std::filesystem;

struct MethodEntry {
    std::string name;
    std::vector<std::string> args;
};

std::string GenerateMacroBlock(const std::string& className, const std::string& inheritance, const std::vector<std::string>& properties, const std::vector<MethodEntry>& methods) {
    std::ostringstream out;
    out << "#pragma once\n\n";
    out << "#define Reflect" << className << " \\\n";

    if (!inheritance.empty()) {
        out << "ReflectionFieldInheritance(" << className << ", " << inheritance << ") \\\n{ \\\n";
    }
    else {
        out << "ReflectionField(" << className << ") \\\n{ \\\n";
    }

    if (!properties.empty()) {
        out << "\tPropertyField \\\n\t({ \\\n";
        for (const auto& p : properties)
            out << "\t\tmeta_property(" << p << ") \\\n";
        out << "\t}); \\\n";
    }

    if (!methods.empty()) {
        out << "\tMethodField \\\n\t({ \\\n";
        for (const auto& m : methods) {
            out << "\t\tmeta_method(" << m.name;
            for (const auto& arg : m.args)
                out << ", \"" << arg << "\"";
            out << ") \\\n";
        }
        out << "\t}); \\\n";
    }

    std::string mode;
    if (!properties.empty() && !methods.empty()) mode = "PropertyAndMethod";
    else if (!properties.empty()) mode = "PropertyOnly";
    else if (!methods.empty()) mode = "MethodOnly";
    else mode = "None";

    if (!inheritance.empty()) mode += "Inheritance";

    out << "\tFieldEnd(" << className << ", " << mode << ") \\\n";
    out << "};\n";
    return out.str();
}

void ProcessHeaderToMacroFile(const fs::path& filepath)
{
    std::ifstream in(filepath);
    if (!in.is_open()) return;

    std::string line, className, inheritance;
    std::vector<std::string> properties;
    std::vector<MethodEntry> methods;
    std::vector<std::string> lines;

    std::smatch match;

    // Serializable attribute
    std::regex serializableRegex(R"(^\s*\[\[Serializable(?:\(Inheritance:(\w+)\))?\]\])");

    // Constructor line에서 클래스 이름 추출
    std::regex constructorRegex(R"(^\s*([A-Za-z_]\w*)\s*\(\s*\)\s*(?:=\s*default)?\s*[;{]?)");

    // GENERATED_BODY 매크로에서 클래스 이름 추출
    std::regex generatedBodyRegex(R"(^\s*GENERATED_BODY\s*\((\w+)\)\s*;?)");

    std::regex serialRegex(R"(\[\[Serializable(?:\(Inheritance:(\w+)\))?\]\])");
    std::regex propRegex(R"(\[\[Property\]\])");
    std::regex methodRegex(R"(\[\[Method\]\])");
    std::regex varLine(R"(\s*(?:[\w:<>]+\s*[*&]?)\s+(\w+)\s*(?:[=;\[])?)");
    std::regex funcLine(R"(\s*(?:[\w:<>&*]+)\s+(\w+)\s*\(([^)]*)\)\s*(?:;|\{))");
    std::regex includeRegex(R"(^\s*#include\s+["]([^"]+)["])");

    bool nextIsProp = false, nextIsMethod = false;
    size_t serializableLineIndex = -1;
    size_t lastIncludeIndex = size_t(-1);
    bool hasSerializable = false;
    bool expectClassFromNextLine = false;

    while (std::getline(in, line))
    {
        lines.push_back(line);
    }

    // 분석을 한 번에
    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string& line = lines[i];

        if (std::regex_search(line, match, serialRegex)) {
            hasSerializable = true;
            if (match[1].matched) inheritance = match[1];
            serializableLineIndex = i;

            // 클래스 이름 추출은 바로 다음 줄에서만!
            if (i + 1 < lines.size()) {
                const std::string& nextLine = lines[i + 1];
                if (std::regex_search(nextLine, match, constructorRegex)) {
                    className = match[1];
                }
                else if (std::regex_search(nextLine, match, generatedBodyRegex)) {
                    className = match[1];
                }
            }
        }
        if (i + 1 < lines.size() && std::regex_search(line, propRegex)) {
            const std::string& nextLine = lines[i + 1];
            if (std::regex_search(nextLine, match, varLine)) {
                properties.push_back(match[1]);
            }
        }
        if (std::regex_search(line, match, methodRegex) && i + 1 < lines.size()) {
            const std::string& nextLine = lines[i + 1];
            if (std::regex_search(nextLine, match, funcLine)) {
                MethodEntry me;
                me.name = match[1];
                std::string args = match[2];
                std::regex argRegex(R"((?:[\w:<>&*]+)\s+(\w+))");
                auto argsBegin = std::sregex_iterator(args.begin(), args.end(), argRegex);
                auto argsEnd = std::sregex_iterator();
                for (auto it = argsBegin; it != argsEnd; ++it)
                    me.args.push_back((*it)[1]);
                methods.push_back(me);
            }
        }
        if (std::regex_search(line, match, includeRegex)) {
            lastIncludeIndex = i + 1;
        }
    }

    if (!className.empty() && hasSerializable) {
        fs::path outputFile = filepath;
        outputFile.replace_filename(className + ".generated.h");

        if (fs::exists(outputFile)) {
            auto headerTime = fs::last_write_time(filepath);
            auto reflectTime = fs::last_write_time(outputFile);
            if (reflectTime >= headerTime) {
                return;
            }
        }

        std::ofstream out(outputFile);
        out << GenerateMacroBlock(className, inheritance, properties, methods);
        out.close();

        std::string reflectInclude = "#include \"" + outputFile.filename().string() + "\"";
        std::string reflectCall = "   Reflect" + className;

        // 매크로 호출 중복 방지
        bool alreadyCalled = false;
        for (const auto& l : lines) {
            if (l.find(reflectCall) != std::string::npos) {
                alreadyCalled = true;
                break;
            }
        }

        if (!alreadyCalled && serializableLineIndex != size_t(-1)) {
            lines.insert(lines.begin() + serializableLineIndex, reflectCall);
        }

        // include 아래쪽에 반영
        bool alreadyIncluded = false;
        for (const auto& l : lines) {
            if (l.find(reflectInclude) != std::string::npos) {
                alreadyIncluded = true;
                break;
            }
        }
        if (!alreadyIncluded && lastIncludeIndex != size_t(-1)) {
            lines.insert(lines.begin() + lastIncludeIndex, reflectInclude);
        }

        std::ofstream hout(filepath);
        for (const auto& l : lines) hout << l << "\n";
    }
}


int main()
{
    std::filesystem::path exeDir = GetExecutablePath();
    std::filesystem::path root = exeDir.parent_path().parent_path();
    if (root.filename() == "LastProject")
    {
        std::cout << "Valid project root: " << root << "\n";
    }
    else
    {
        std::cerr << "Unexpected structure: " << root << "\n";
        return 1;
    }
    std::filesystem::path outputPath = root / "ScriptBinder" / "RegisterReflect.def";

    for (const auto& file : fs::recursive_directory_iterator(root))
    {
        if (file.path().extension() == ".h")
        {
            ProcessHeaderToMacroFile(file.path());
        }
    }

    std::regex serializableRegex(R"(^\s*\[\[Serializable(\(.*\))?\]\])");
    std::regex constructorRegex(R"(^\s*([A-Za-z_]\w*)\s*\(\s*\)\s*(?:=\s*default)?\s*[;{]?)");

    std::regex generatedBodyRegex(R"(^\s*GENERATED_BODY\s*\((\w+)\)\s*;?)");

    std::unordered_map<std::string, std::string> classToHeader;
    std::set<std::string> includes;
    std::set<std::string> classNames;

    for (const auto& entry : fs::recursive_directory_iterator(root))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".h")
            continue;

        std::ifstream file(entry.path());
        std::string line;
        bool expectConstructor = false;
        std::string headerPath = entry.path().filename().string();

        while (std::getline(file, line))
        {
            if (expectConstructor)
            {
                std::smatch ctorMatch;
                if (std::regex_match(line, ctorMatch, constructorRegex))
                {
                    std::string className = ctorMatch[1].str();
                    classToHeader[className] = headerPath;
                    classNames.insert(className);
                    expectConstructor = false;
                }
                else if (std::regex_match(line, ctorMatch, generatedBodyRegex))
                {
                    std::string className = ctorMatch[1].str();
                    classToHeader[className] = headerPath;
                    classNames.insert(className);
                    expectConstructor = false;
                }
            }
            else if (std::regex_match(line, serializableRegex))
            {
                expectConstructor = true;
            }
        }
    }

    // Generate output .def file
    std::ofstream out(outputPath);
    out << "// Auto-generated RegisterReflect.generated.def\n\n";
    out << "#pragma once\n\n";
    // 헤더 include
    for (const auto& [className, header] : classToHeader)
    {
        if (includes.insert(header).second)
        {
            out << "#include \"" << header << "\"\n";
        }
    }

    // REFLECTION_REGISTER 함수 정의
    out << "\nREFLECTION_REGISTER()\n{\n";
    for (const auto& className : classNames)
    {
        out << "    AUTO_REGISTER_CLASS(" << className << ");\n";
    }
    out << "}\n";

    std::cout << "Generated " << classNames.size() << " reflected classes.\n";
    return 0;
}

