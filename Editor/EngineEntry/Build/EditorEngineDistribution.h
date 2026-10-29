#pragma once
#if defined(CE_CONTRACT_STANDALONE)
#include "../../../Tools/build/ModuleAdapters/Creator.Core.Distribution.h"
#else
import Creator.Core.Distribution;
#endif
#if defined(CE_CONTRACT_STANDALONE)
#include "../../../Tools/build/ModuleAdapters/Creator.Core.Paths.h"
#else
import Creator.Core.Paths;
#endif
#include "../../../Engine/Utility_Framework/Foundation/StandardLibrary.h"

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
        const auto binaryRoot = ce::paths::resolve_runtime_directory().parent_path();
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
        const auto manifest = ce::distribution::read_metadata_file(candidate / L"engine.info");
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
// no engine.info) compiles scripts with the checkout's own F# script project.
// A published distribution snapshot can contain a different scripting contract from this editor.
inline EditorSourceCheckout ResolveEditorSourceCheckout()
{
    try
    {
        const auto binaryRoot = ce::paths::resolve_runtime_directory().parent_path();
        if (binaryRoot.empty()) return {};
        const auto repository = binaryRoot.parent_path().parent_path();
        for (const auto& candidate : { binaryRoot, binaryRoot.parent_path(), repository })
            if (std::filesystem::is_regular_file(candidate / L"engine.info")) return {};
        const auto folder = binaryRoot.filename().wstring();
        if (!folder.starts_with(L"x64-") || !std::filesystem::is_regular_file(repository / L"EngineVersion.json") ||
            !std::filesystem::is_regular_file(repository / L"Engine/Scripting/GameScripts/ScriptNative.GameScripts.fsproj")) return {};
        const auto configuration = folder.substr(4);
        if (configuration != L"Debug" && configuration != L"Release") return {};
        return { repository, configuration };
    }
    catch (...) { return {}; }
}
