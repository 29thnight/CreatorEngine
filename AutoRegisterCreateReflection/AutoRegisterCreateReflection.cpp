#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <set>
#include <unordered_map>
#include <sstream>
#include <algorithm>
#include <Windows.h>

std::filesystem::path GetExecutablePath()
{
    char buffer[MAX_PATH];
    GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    return std::filesystem::path(buffer).parent_path();
}

namespace fs = std::filesystem;

// 출력이 기존 파일과 내용 동일하면 쓰지 않는다 (PHASE 18 CT3).
// 이 툴은 매 pre-build마다 도는데, 무가드 재작성은 원본 헤더·generated.h·def의
// mtime을 매번 갱신해 증분 빌드를 통째로 무효화한다. 줄바꿈은 정규화해 비교한다
// — 디스크는 CRLF, 생성 문자열은 LF 기준이라 원문 비교로는 항상 "다름"이 된다.
static std::string NormalizeNewlines(std::string s)
{
    s.erase(std::remove(s.begin(), s.end(), '\r'), s.end());
    return s;
}

static bool WriteFileIfChanged(const fs::path& path, const std::string& content)
{
    if (fs::exists(path))
    {
        std::ifstream in(path, std::ios::binary);
        std::string existing((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (NormalizeNewlines(existing) == NormalizeNewlines(content))
        {
            return false;
        }
    }
    std::ofstream out(path); // 텍스트 모드 — 기존 CRLF 관례 유지
    out << content;
    return true;
}

struct MethodEntry
{
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

        WriteFileIfChanged(outputFile, GenerateMacroBlock(className, inheritance, properties, methods));

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

        // 원본 헤더 재작성 — 삽입이 실제로 없었으면 내용이 같아 스킵된다.
        // (예전에는 무가드로 매 실행 재작성해 완결된 헤더의 mtime까지 건드렸다.)
        std::ostringstream hbuf;
        for (const auto& l : lines) hbuf << l << "\n";
        WriteFileIfChanged(filepath, hbuf.str());
    }
}

int main()
{
    std::filesystem::path exeDir = GetExecutablePath();
    std::filesystem::path root = exeDir.parent_path().parent_path();
    if (!root.filename().empty())
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

    // Generate output .def file — 내용 동일 시 재작성 생략 (CT3)
    std::ostringstream defBuf;
    defBuf << "// Auto-generated RegisterReflect.generated.def\n\n";
    defBuf << "#pragma once\n\n";
    // 헤더 include
    for (const auto& [className, header] : classToHeader)
    {
        if (includes.insert(header).second)
        {
            defBuf << "#include \"" << header << "\"\n";
        }
    }

    // REFLECTION_REGISTER 함수 정의
    defBuf << "\nREFLECTION_REGISTER()\n{\n";
    for (const auto& className : classNames)
    {
        defBuf << "    AUTO_REGISTER_CLASS(" << className << ");\n";
    }
    defBuf << "}\n";
    WriteFileIfChanged(outputPath, defBuf.str());

    std::cout << "Generated " << classNames.size() << " reflected classes.\n";
    return 0;
}

