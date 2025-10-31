#pragma once
#include "PathFinder.h"
#include "LogSystem.h"
#include "EngineSetting.h"
#include "Paklib.hpp"

#include <cwctype>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

namespace
{
    namespace fs = std::filesystem;

    std::string PathToUtf8(const fs::path& value)
    {
#if defined(_WIN32)
        auto asU8 = value.u8string();
        return std::string(asU8.begin(), asU8.end());
#else
        return value.string();
#endif
    }

    bool EnsureDirectoryExists(const fs::path& directory)
    {
        if (directory.empty())
        {
            Debug->LogError("Directory path is empty.");
            return false;
        }

        std::error_code ec{};
        if (fs::exists(directory, ec))
        {
            if (ec)
            {
                Debug->LogError("Failed to query directory '" + PathToUtf8(directory) + "': " + ec.message());
                return false;
            }

            return true;
        }

        ec.clear();
        fs::create_directories(directory, ec);
        if (ec)
        {
            Debug->LogError("Failed to create directory '" + PathToUtf8(directory) + "': " + ec.message());
            return false;
        }

        return true;
    }

    std::wstring SanitizePakStem(const std::wstring& desiredName)
    {
        constexpr std::wstring_view invalidChars = L"<>:\"/\\|?*";
        const std::wstring fallback{ L"GameAssets" };

        std::wstring sanitized;
        sanitized.reserve(desiredName.size());

        for (wchar_t ch : desiredName)
        {
            if (ch < 32 || invalidChars.find(ch) != std::wstring_view::npos)
            {
                sanitized.push_back(L'_');
                continue;
            }

            if (std::iswcntrl(ch))
            {
                sanitized.push_back(L'_');
                continue;
            }

            sanitized.push_back(ch);
        }

        while (!sanitized.empty() && (sanitized.back() == L' ' || sanitized.back() == L'.'))
        {
            sanitized.back() = L'_';
        }

        if (sanitized.empty())
        {
            return fallback;
        }

        return sanitized;
    }

    bool PackageGameAssets()
    {
        const fs::path assetsRoot = PathFinder::Relative();
        if (assetsRoot.empty())
        {
            Debug->LogWarning("Assets root path is empty. Skipping pak generation.");
            return false;
        }

        std::error_code ec{};
        if (!fs::exists(assetsRoot, ec) || ec)
        {
            Debug->LogError("Assets directory not found: " + PathToUtf8(assetsRoot));
            return false;
        }

        const fs::path projectSettingsRoot = PathFinder::ProjectSettingPath("");

        std::wstring pakStem = SanitizePakStem(EngineSettingInstance->GetBuildGameName());
        if (pakStem.empty())
        {
            pakStem = L"GameAssets";
        }

        const fs::path executableDir = PathFinder::RelativeToExecutable("");
        fs::path outputDir;

        if (!executableDir.empty())
        {
            outputDir = executableDir.parent_path().parent_path().parent_path() / "x64" / "GameBuild";
        }

        if (!outputDir.empty() && !fs::exists(outputDir, ec))
        {
            fs::create_directories(outputDir, ec);
            if (ec)
            {
                Debug->LogError("Failed to create pak output directory '" + PathToUtf8(outputDir) + "': " + ec.message());
                return false;
            }
        }

        fs::path pakPath = outputDir / (pakStem + L".pak");
        if (fs::exists(pakPath, ec) && !ec)
        {
            fs::remove(pakPath, ec);
            if (ec)
            {
                Debug->LogWarning("Unable to remove existing pak '" + PathToUtf8(pakPath) + "': " + ec.message());
                ec.clear();
            }
        }

        try
        {
            Pak::BuildOptions options{};
            Pak::Builder builder(pakPath, options);
            fs::recursive_directory_iterator it{ assetsRoot, fs::directory_options::skip_permission_denied, ec };
            if (ec)
            {
                Debug->LogError("Failed to enumerate assets: " + ec.message());
                return false;
            }

            constexpr std::array<std::wstring_view, 3> scriptExtensions{
                L".h",
                L".hpp",
                L".cpp"
            };


            struct SourceRoot
            {
                fs::path root;
                std::string mountName;
            };

            std::vector<SourceRoot> sourceRoots;
            sourceRoots.push_back({ assetsRoot, "Assets" });
            if (!projectSettingsRoot.empty())
            {
                sourceRoots.push_back({ projectSettingsRoot, "ProjectSetting" });
            }

            std::size_t totalFileCount = 0;
            const fs::recursive_directory_iterator end{};
            for (const auto& source : sourceRoots)
            {
                ec.clear();
                if (!fs::exists(source.root, ec) || ec)
                {
                    if (!source.mountName.empty())
                    {
                        Debug->LogWarning("Pak source directory not found for '" + source.mountName + "': " + PathToUtf8(source.root));
                    }
                    ec.clear();
                    continue;
                }

                fs::recursive_directory_iterator it{ source.root, fs::directory_options::skip_permission_denied, ec };
                if (ec)
                {
                    Debug->LogError("Failed to enumerate pak source '" + source.mountName + "': " + ec.message());
                    ec.clear();
                    continue;
                }

                std::size_t rootFileCount = 0;
                for (; it != end; it.increment(ec))
                {
                    if (ec)
                    {
                        Debug->LogWarning("Error while iterating pak source '" + source.mountName + "': " + ec.message());
                        ec.clear();
                        continue;
                    }

                    const auto& entry = *it;
                    if (!entry.is_regular_file(ec))
                    {
                        ec.clear();
                        continue;
                    }

                    auto relative = fs::relative(entry.path(), source.root, ec);
                    if (ec)
                    {
                        Debug->LogWarning("Failed to resolve relative path for '" + PathToUtf8(entry.path()) + "' in '" + source.mountName + "': " + ec.message());
                        ec.clear();
                        continue;
                    }

                    fs::path virtualPath = fs::u8path(source.mountName) / relative;
                    auto virtualPathU8 = virtualPath.generic_u8string();
                    if (virtualPathU8.empty())
                    {
                        continue;
                    }

                    builder.addFile(std::string(virtualPathU8.begin(), virtualPathU8.end()), entry.path());
                    ++rootFileCount;
                }

                totalFileCount += rootFileCount;
                Debug->Log("Added " + std::to_string(rootFileCount) + " files from '" + source.mountName + "' to pak queue.");
            }

            if (totalFileCount == 0)
            {
                Debug->LogWarning("No files found under pak source directories. Skipping pak generation.");
                return false;
            }

            builder.finish();
            Debug->Log("Packaged " + std::to_string(totalFileCount) + " files into pak: " + PathToUtf8(pakPath));
            return true;
        }
        catch (const std::exception& e)
        {
            Debug->LogError(std::string("Failed to build asset pak: ") + e.what());
        }
        catch (...)
        {
            Debug->LogError("Failed to build asset pak: unknown error.");
        }

        return false;
    }

    bool CleanupUnpackedGameAssets()
    {
        fs::path pakBaseDir = PathFinder::RelativeToExecutable("");
        if (pakBaseDir.empty())
        {
            pakBaseDir = PathFinder::Relative().parent_path();
        }

        fs::path extractRootBase;

        std::array<wchar_t, MAX_PATH> tempPathBuffer{};
        const DWORD tempPathLen = GetTempPathW(static_cast<DWORD>(tempPathBuffer.size()), tempPathBuffer.data());

        if (tempPathLen > 0 && tempPathLen < tempPathBuffer.size())
        {
            extractRootBase = fs::path(tempPathBuffer.data());
        }
        else
        {
            extractRootBase = PathFinder::DumpPath();
        }

        if (extractRootBase.empty())
        {
            extractRootBase = pakBaseDir;
        }

        fs::path extractRoot = extractRootBase / "UnpackedAssets";
        std::error_code ec{};

        if (!fs::exists(extractRoot, ec))
        {
            if (ec)
            {
                Debug->LogWarning("Failed to query unpacked assets directory '" + PathToUtf8(extractRoot) + "': " + ec.message());
            }

            return true;
        }

        const auto removedCount = fs::remove_all(extractRoot, ec);
        if (ec)
        {
            Debug->LogError("Failed to delete unpacked assets directory '" + PathToUtf8(extractRoot) + "': " + ec.message());
            return false;
        }

        Debug->Log("Removed unpacked assets directory '" + PathToUtf8(extractRoot) + "' (" + std::to_string(removedCount) + " entries).");
        return true;
    }

    bool UnpackageGameAssets()
    {
        std::wstring pakStem = SanitizePakStem(EngineSettingInstance->GetBuildGameName());
        if (pakStem.empty())
        {
            pakStem = L"GameAssets";
        }

        fs::path pakBaseDir = PathFinder::RelativeToExecutable("");
        if (pakBaseDir.empty())
        {
            pakBaseDir = PathFinder::Relative().parent_path();
        }

        fs::path pakPath = pakBaseDir / (pakStem + L".pak");

        std::error_code ec{};
        if (pakPath.empty() || !fs::exists(pakPath, ec) || ec)
        {
            Debug->LogError("Pak file not found: " + PathToUtf8(pakPath));
            return false;
        }

        fs::path extractRootBase;

        wchar_t tempPathBuffer[MAX_PATH]{};
        const DWORD tempPathLen = GetTempPathW(static_cast<DWORD>(std::size(tempPathBuffer)), tempPathBuffer);

        if (tempPathLen > 0 && tempPathLen < std::size(tempPathBuffer))
        {
            extractRootBase = fs::path(tempPathBuffer);
        }
        else
        {
            extractRootBase = PathFinder::DumpPath();
        }

        if (extractRootBase.empty())
        {
            extractRootBase = pakBaseDir;
        }

        fs::path extractRoot = extractRootBase / "UnpackedAssets";
        if (!EnsureDirectoryExists(extractRoot))
        {
            Debug->LogError("Unable to prepare extraction root: " + PathToUtf8(extractRoot));
            return false;
        }

        try
        {
            Pak::OpenOptions options{};
            Pak::Archive archive(pakPath, options);

            auto entries = archive.list();
            if (entries.empty())
            {
                Debug->LogWarning("Pak archive contains no entries: " + PathToUtf8(pakPath));
                return false;
            }

            bool allSucceeded = true;
            std::size_t extractedCount = 0;

            for (const auto& entry : entries)
            {
                std::u8string u8Path(entry.path.begin(), entry.path.end());
                fs::path relativePath = fs::u8path(u8Path);
                fs::path outPath = extractRoot / relativePath;
                fs::path parent = outPath.parent_path();

                if (!parent.empty() && !EnsureDirectoryExists(parent))
                {
                    allSucceeded = false;
                    continue;
                }

                try
                {
                    archive.extractToFile(entry.path, outPath.wstring());
                    ++extractedCount;
                }
                catch (const std::exception& e)
                {
                    Debug->LogError("Failed to extract '" + entry.path + "': " + e.what());
                    allSucceeded = false;
                }
                catch (...)
                {
                    Debug->LogError("Failed to extract '" + entry.path + "': unknown error.");
                    allSucceeded = false;
                }
            }

            Debug->Log("Unpacked " + std::to_string(extractedCount) +
                " assets from pak to '" + PathToUtf8(extractRoot) + "'.");

            return allSucceeded;
        }
        catch (const std::exception& e)
        {
            Debug->LogError(std::string("Failed to open pak archive: ") + e.what());
        }
        catch (...)
        {
            Debug->LogError("Failed to open pak archive: unknown error.");
        }

        return false;
    }
}