#pragma once

#include <Windows.h>

#include <array>
#include <cwchar>
#include <filesystem>

// Fully resolved roots supplied by the process host. Runtime code derives only
// subdirectories from these values and never decides whether it is Editor or Player.
struct EnginePaths
{
    std::filesystem::path executableRoot{};
    std::filesystem::path projectRoot{};
    // Optional ownership capability supplied by a packaged process Host.
    // Editor paths leave these empty; runtime cleanup is unavailable without them.
    std::filesystem::path runtimeOwnerRoot{};
    std::filesystem::path runtimeProcessRoot{};
    std::filesystem::path runtimeContentRoot{};
    std::filesystem::path runtimeDataRoot{};
    std::filesystem::path assetsRoot{};
    // Host-owned capability. When disabled, runtime code may read packaged assets
    // but must not create source directories, metadata, or file-system watchers.
    bool enableAssetAuthoring{ false };

    bool IsValid() const noexcept
    {
        return !executableRoot.empty() && !projectRoot.empty() &&
            !runtimeContentRoot.empty() && !runtimeDataRoot.empty() &&
            !assetsRoot.empty();
    }

    bool HasRuntimeOwnershipCapability() const noexcept
    {
        return !runtimeOwnerRoot.empty() || !runtimeProcessRoot.empty();
    }

    bool HasValidRuntimeOwnership() const noexcept
    {
        try
        {
            const auto normalizeAbsolute = [](const std::filesystem::path& value,
                std::filesystem::path& output) noexcept
            {
                try
                {
                    if (value.empty() || !value.is_absolute()) return false;
                    output = value.lexically_normal();
                    return !output.empty() && !output.root_path().empty();
                }
                catch (...) { return false; }
            };
            const auto samePath = [](const std::filesystem::path& left,
                const std::filesystem::path& right) noexcept
            {
                return 0 == _wcsicmp(left.c_str(), right.c_str());
            };

            std::filesystem::path owner;
            std::filesystem::path process;
            std::filesystem::path content;
            std::filesystem::path data;
            std::filesystem::path project;
            std::filesystem::path assets;
            if (!normalizeAbsolute(runtimeOwnerRoot, owner) ||
                !normalizeAbsolute(runtimeProcessRoot, process) ||
                !normalizeAbsolute(runtimeContentRoot, content) ||
                !normalizeAbsolute(runtimeDataRoot, data) ||
                !normalizeAbsolute(projectRoot, project) ||
                !normalizeAbsolute(assetsRoot, assets))
            {
                return false;
            }

            // Runtime cleanup must never be able to select a drive root or one of
            // its immediate children as the shared owner capability.
            if (samePath(owner, owner.root_path()) ||
                samePath(owner.parent_path(), owner.root_path()))
            {
                return false;
            }

            return samePath(process.parent_path(), owner) &&
                samePath(content, process / L"RuntimeContent") &&
                samePath(data, process / L"RuntimeData") &&
                !samePath(content, data) &&
                samePath(project, content) &&
                samePath(assets, content / L"Assets");
        }
        catch (...) { return false; }
    }
};

inline std::filesystem::path ResolveProcessExecutableDirectory()
{
    std::array<wchar_t, MAX_PATH> buffer{};
    const DWORD length = GetModuleFileNameW(
        GetModuleHandleW(nullptr), buffer.data(), static_cast<DWORD>(buffer.size()));
    if (0 == length || length >= buffer.size()) return {};
    return std::filesystem::path(buffer.data()).remove_filename().lexically_normal();
}
