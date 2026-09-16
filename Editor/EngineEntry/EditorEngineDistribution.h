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

struct EditorSourceCheckout
{
    std::filesystem::path repository;
    std::wstring configuration;
};

// An editor built in a source checkout (Bin/x64-<Config> under a repository with EngineVersion.json and
// no engine.info) compiles scripts with the checkout's own GameScripts.csproj, as the editor build does.
// A published distribution snapshot would compile against an older ScriptCore, try to replace the one
// this process has loaded, and miss the checkout's GameScripts/*.cs.
inline EditorSourceCheckout ResolveEditorSourceCheckout()
{
    try
    {
        const auto binaryRoot = ResolveEngineRuntimeDirectory().parent_path();
        if (binaryRoot.empty()) return {};
        const auto repository = binaryRoot.parent_path().parent_path();
        for (const auto& candidate : { binaryRoot, binaryRoot.parent_path(), repository })
            if (std::filesystem::is_regular_file(candidate / L"engine.info")) return {};
        const auto folder = binaryRoot.filename().wstring();
        if (!folder.starts_with(L"x64-") || !std::filesystem::is_regular_file(repository / L"EngineVersion.json") ||
            !std::filesystem::is_regular_file(repository / L"GameScripts/GameScripts.csproj")) return {};
        const auto configuration = folder.substr(4);
        if (configuration != L"Debug" && configuration != L"Release") return {};
        return { repository, configuration };
    }
    catch (...) { return {}; }
}
