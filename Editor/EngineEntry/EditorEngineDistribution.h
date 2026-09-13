#pragma once
#include "EngineRuntimePaths.h"
#include "EngineMetadataFile.h"
#include <fstream>

struct EditorEngineDistribution
{
    std::filesystem::path root;
    std::wstring configuration;
    bool shipping{};
};

inline EditorEngineDistribution ResolveEditorEngineDistribution(bool preferRelease = false)
{
    try
    {
        const auto binaryRoot = ResolveEngineRuntimeDirectory().parent_path();
        auto candidate = binaryRoot;
        for (int depth = 0; depth <= 2; ++depth)
        {
            if (std::filesystem::is_regular_file(candidate / L"engine.info")) break;
            candidate = candidate.parent_path();
        }
        if (!std::filesystem::is_regular_file(candidate / L"engine.info"))
        {
            auto pointer = binaryRoot / L"engine.distribution.path";
            const auto release = binaryRoot.parent_path() / L"x64-Release" / L"engine.distribution.path";
            if (preferRelease && std::filesystem::is_regular_file(release)) pointer = release;
            std::ifstream stream(pointer);
            std::string path;
            std::getline(stream, path);
            candidate = std::filesystem::path(std::u8string(path.begin(), path.end()));
            if (!candidate.is_absolute()) return {};
        }
        const auto manifest = ReadEngineMetadataFile(candidate / L"engine.info");
        const auto config = manifest.at("configuration");
        if (config != "Debug" && config != "Release") return {};
        return { candidate, std::wstring(config.begin(), config.end()), manifest.at("shipping") == "true" };
    }
    catch (...) { return {}; }
}
