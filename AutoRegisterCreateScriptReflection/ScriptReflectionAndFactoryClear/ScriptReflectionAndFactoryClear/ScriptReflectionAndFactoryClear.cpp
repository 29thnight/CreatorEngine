#include <iostream>
#include <fstream>
#include <filesystem>
#include <regex>
#include <unordered_set>
#include <string>
#include <vector>
#include <Windows.h>
#include "pugixml.hpp"

namespace fs = std::filesystem;

std::filesystem::path GetExecutablePath()
{
    char buffer[MAX_PATH];
    GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    return std::filesystem::path(buffer).parent_path();
}

std::unordered_set<std::string> CollectClassNames(const std::string& headerDir)
{
    std::unordered_set<std::string> classNames;
    std::regex classRegex(R"(\bclass\s+(\w+))");

    for (const auto& entry : fs::recursive_directory_iterator(headerDir))
    {
        if (entry.path().extension() == ".h")
        {
            std::ifstream file(entry.path());
            if (!file.is_open()) continue;

            std::string line;
            while (std::getline(file, line))
            {
                std::smatch match;
                if (std::regex_search(line, match, classRegex))
                {
                    classNames.insert(match[1].str());
                }
            }
        }
    }

    return classNames;
}

std::unordered_set<std::string> CollectHeaderFileNames(const std::string& headerDir)
{
    std::unordered_set<std::string> headers;
    for (const auto& entry : fs::recursive_directory_iterator(headerDir))
    {
        if (entry.path().extension() == ".h")
        {
            headers.insert(entry.path().filename().string()); // e.g. GameManager.h
        }
    }
    return headers;
}

void CleanInitModuleFactoryFile(const std::string& factoryFilePath, const std::unordered_set<std::string>& classNames)
{
    std::ifstream inFile(factoryFilePath);
    if (!inFile.is_open())
    {
        std::cerr << "Failed to open factory file: " << factoryFilePath << "\n";
        return;
    }

    std::vector<std::string> newLines;
    std::string line;
    int braceDepth = 0;
    bool factoryFound = false;
    bool inFactory = false;
    std::regex factoryRegex(R"(RegisterFactory\("(\w+)\")");

    while (std::getline(inFile, line))
    {
        std::string trimmed = line;
        trimmed.erase(0, trimmed.find_first_not_of(" \t"));

        // 함수 진입 탐색
        if (!inFactory && !factoryFound &&
            trimmed.find("EXPORT_API void InitModuleFactory") != std::string::npos)
        {
            factoryFound = true;
            newLines.push_back(line);
            continue;
        }

        if (factoryFound && !inFactory)
        {
            braceDepth += std::count(line.begin(), line.end(), '{');
            braceDepth -= std::count(line.begin(), line.end(), '}');

            newLines.push_back(line);
            if (braceDepth > 0)
            {
                inFactory = true;
                factoryFound = false;
            }
            continue;
        }

        if (inFactory)
        {
            braceDepth += std::count(line.begin(), line.end(), '{');
            braceDepth -= std::count(line.begin(), line.end(), '}');

            std::smatch match;
            if (std::regex_search(line, match, factoryRegex))
            {
                std::string className = match[1].str();
                if (classNames.count(className))
                {
                    newLines.push_back(line);
                }
                else
                {
                    std::cout << "클래스 없음: " << className << " → 제거됨\n";
                }
            }
            else
            {
                newLines.push_back(line);
            }

            if (braceDepth == 0)
            {
                inFactory = false;
            }

            continue;
        }

        // 4. 그 외 라인
        newLines.push_back(line);
    }

    inFile.close();

    std::ofstream outFile(factoryFilePath, std::ios::trunc);
    for (const auto& l : newLines)
        outFile << l << "\n";

    std::cout << "InitModuleFactory 정리 완료: " << factoryFilePath << "\n";
}

void CleanAutomationIncludesFile(const std::string& includeFilePath, const std::unordered_set<std::string>& headerFiles)
{
    std::ifstream inFile(includeFilePath);
    if (!inFile.is_open())
    {
        std::cerr << "Failed to open include file: " << includeFilePath << "\n";
        return;
    }

    std::vector<std::string> newLines;
    std::string line;
    bool inIncludeBlock = false;

    std::regex includeRegex(R"(#include\s+\"([^\"]+\.h)\")");

    while (std::getline(inFile, line))
    {
        std::string trimmed = line;
        trimmed.erase(0, trimmed.find_first_not_of(" \t"));

        if (trimmed.find("// Automation include ScriptClass header") != std::string::npos)
        {
            inIncludeBlock = true;
            newLines.push_back(line);
            continue;
        }

        if (inIncludeBlock)
        {
            std::smatch match;
            if (std::regex_search(line, match, includeRegex))
            {
                std::string headerName = match[1].str(); // e.g., GameManager.h
                if (headerFiles.count(headerName))
                {
                    newLines.push_back(line);
                }
                else
                {
                    std::cout << " 헤더 없음: " << headerName << " → 제거됨\n";
                }
                continue;
            }

            // include 아닌 줄 만나면 블록 끝났다고 가정
            if (trimmed.empty() || trimmed.find("#") == std::string::npos)
            {
                inIncludeBlock = false;
            }
        }

        newLines.push_back(line);
    }

    inFile.close();

    std::ofstream outFile(includeFilePath, std::ios::trunc);
    for (const auto& l : newLines)
        outFile << l << "\n";

    std::cout << "Include 블록 정리 완료: " << includeFilePath << "\n";
}

void CleanProjectFiles(
    const std::string& scriptProjPath,
    const std::string& scriptFilterPath,
    const std::unordered_set<std::string>& validClasses)
{
    // 경로 prefix
    const std::string scriptPrefix = "Assets\\Script\\";

    // =====================
    // filters 처리
    // =====================
    {
        pugi::xml_document doc;
        if (!doc.load_file(scriptFilterPath.c_str(), pugi::parse_full, pugi::encoding_auto))
            throw std::runtime_error("Failed to load filters file");

        for (pugi::xml_node itemGroup : doc.child("Project").children("ItemGroup"))
        {
            for (pugi::xml_node cl : itemGroup.children())
            {
                if (std::string(cl.name()) != "ClInclude" && std::string(cl.name()) != "ClCompile")
                    continue;

                std::string includePath = cl.attribute("Include").as_string();
                if (includePath.rfind(scriptPrefix, 0) == 0)
                {
                    std::string fileName = includePath.substr(scriptPrefix.length());
                    std::string baseName = fileName.substr(0, fileName.find_last_of('.'));

                    if (!validClasses.count(baseName))
                    {
                        std::cout << "filters 제거됨: " << includePath << "\n";
                        itemGroup.remove_child(cl);
                    }
                }
            }
        }

        if (!doc.save_file(scriptFilterPath.c_str(), PUGIXML_TEXT("\t"), 1U, pugi::encoding_auto))
            throw std::runtime_error("Failed to save filters file");
    }

    // =====================
    // vcxproj 처리
    // =====================
    {
        pugi::xml_document doc;
        if (!doc.load_file(scriptProjPath.c_str(), pugi::parse_full, pugi::encoding_auto))
            throw std::runtime_error("Failed to load project file");

        for (pugi::xml_node itemGroup : doc.child("Project").children("ItemGroup"))
        {
            for (pugi::xml_node cl : itemGroup.children())
            {
                if (std::string(cl.name()) != "ClInclude" && std::string(cl.name()) != "ClCompile")
                    continue;

                std::string includePath = cl.attribute("Include").as_string();
                if (includePath.rfind(scriptPrefix, 0) == 0)
                {
                    std::string fileName = includePath.substr(scriptPrefix.length());
                    std::string baseName = fileName.substr(0, fileName.find_last_of('.'));

                    if (!validClasses.count(baseName))
                    {
                        std::cout << "vcxproj 제거됨: " << includePath << "\n";
                        itemGroup.remove_child(cl);
                    }
                }
            }
        }

        if (!doc.save_file(scriptProjPath.c_str(), PUGIXML_TEXT("\t"), 1U, pugi::encoding_auto))
            throw std::runtime_error("Failed to save project file");
    }
}

int main()
{
    fs::path exeDir = GetExecutablePath();
    fs::path root = exeDir.parent_path().parent_path();
    if (!root.filename().empty())
    {
        std::cout << "Valid project root: " << root << "\n";
    }
    else
    {
        std::cerr << "Unexpected structure: " << root << "\n";
        return 1;
    }

	fs::path dynamicCppPath = root / "Dynamic_CPP";
	fs::path dllProjPath = dynamicCppPath / "Dynamic_CPP.vcxproj";
	fs::path dllFilterPath = dynamicCppPath / "Dynamic_CPP.vcxproj.filters";
    fs::path scriptPath = dynamicCppPath / "Assets\\Script";
    fs::path automationPath = dynamicCppPath / "funcMain.h";
	fs::path automationIncludesPath = dynamicCppPath / "CreateFactory.h";
    if (!fs::exists(scriptPath) || !fs::exists(automationPath) || !fs::exists(automationIncludesPath))
    {
        std::cerr << "Script or Automation path does not exist.\n";
        return 1;
	}

    std::string headerDir       = scriptPath.string(); // 헤더 파일들이 있는 디렉토리
    std::string factoryCpp      = automationPath.string(); // InitModuleFactory 정의된 파일
    std::string includeBlockCpp = automationIncludesPath.string(); // include 헤더 모아둔 파일

    auto classNames     = CollectClassNames(headerDir);
    auto headerFiles    = CollectHeaderFileNames(headerDir);

    CleanInitModuleFactoryFile(factoryCpp, classNames);
    CleanAutomationIncludesFile(includeBlockCpp, headerFiles);
	CleanProjectFiles(dllProjPath.string(), dllFilterPath.string(), classNames);

    return 0;
}
