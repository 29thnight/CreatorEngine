#pragma once
#include "EnginePaths.h"
#include "PathFinder.h"
#include "LogSystem.h"
#include "Paklib.hpp"

#include <cwctype>
#include <cstdio>
#include <filesystem>
#include <set>
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

    void RuntimeCleanupError(const std::string& message) noexcept
    {
        if (Log::IsAlive())
        {
            Debug->LogError(message);
            return;
        }
        std::fprintf(stderr, "[PakCleanup] %s\n", message.c_str());
    }

    void RuntimeCleanupInfo(const std::string& message) noexcept
    {
        if (Log::IsAlive()) Debug->Log(message);
    }

    bool EnsureDirectoryExists(const fs::path& directory)
    {
        if (directory.empty())
        {
            Debug->LogError("Directory path is empty.");
            return false;
        }

        std::error_code ec{};
        const bool exists = fs::exists(directory, ec);
        if (ec)
        {
            Debug->LogError("Failed to query directory '" + PathToUtf8(directory) + "': " + ec.message());
            return false;
        }
        if (exists)
        {
            if (!fs::is_directory(directory, ec) || ec)
            {
                Debug->LogError("Path exists but is not a directory: " + PathToUtf8(directory));
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

    // pak 이름의 잠정 상수 (B0-3). 예전에는 "TRAIN_ASIS"가 Package/Unpackage
    // 두 함수에 각각 하드코딩돼 있었다 — 게임 이름이 프로젝트 파일에서 오는
    // 것은 L3'(Project.cproj)의 몫이고, 그때 이 상수가 그리로 옮겨 간다.
    inline constexpr const wchar_t* kGamePakStem = L"GameAssets";

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

        std::wstring pakStem = SanitizePakStem(kGamePakStem);
        if (pakStem.empty())
        {
            pakStem = L"GameAssets";
        }

        // 패키지 출력은 Editor.exe가 우연히 놓인 폴더 깊이가 아니라 Host가
        // 넘긴 프로젝트 루트를 기준으로 잡는다. x64/Debug와 Bin/Editor처럼
        // 실행 위치가 달라져도 같은 workspace/x64/GameBuild를 가리켜야 한다.
        const fs::path projectRoot = PathFinder::BaseProjectPath();
        const fs::path outputDir = projectRoot.empty()
            ? fs::path{}
            : (projectRoot.parent_path() / "x64" / "GameBuild").lexically_normal();

        if (outputDir.empty())
        {
            Debug->LogError("Project root path is empty. Cannot resolve pak output directory.");
            return false;
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

    bool ValidateExistingPathHasNoReparsePoints(const fs::path& path)
    {
        std::error_code ec{};
        const fs::path absolutePath = fs::absolute(path, ec).lexically_normal();
        if (ec || absolutePath.empty() || absolutePath.root_path().empty())
        {
            RuntimeCleanupError("Unable to normalize runtime path '" + PathToUtf8(path) + "'.");
            return false;
        }

        fs::path current = absolutePath.root_path();
        for (const auto& component : absolutePath.relative_path())
        {
            current /= component;
            const DWORD attributes = GetFileAttributesW(current.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES)
            {
                const DWORD error = GetLastError();
                if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
                {
                    return true;
                }
                RuntimeCleanupError("Unable to inspect runtime path '" + PathToUtf8(current) +
                    "' (Win32=" + std::to_string(error) + ").");
                return false;
            }
            if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
            {
                RuntimeCleanupError("Runtime path crosses a symbolic/reparse point: " +
                    PathToUtf8(current));
                return false;
            }
        }
        return true;
    }

    bool ValidateNoReparseTree(const fs::path& root)
    {
        if (!ValidateExistingPathHasNoReparsePoints(root)) return false;

        std::error_code ec{};
        if (!fs::exists(root, ec)) return !ec;
        if (ec || !fs::is_directory(root, ec) || ec)
        {
            RuntimeCleanupError("Runtime cleanup root is not a readable directory: " + PathToUtf8(root));
            return false;
        }

        fs::recursive_directory_iterator iterator(root, fs::directory_options::none, ec);
        const fs::recursive_directory_iterator end{};
        if (ec)
        {
            RuntimeCleanupError("Unable to enumerate runtime cleanup root: " + ec.message());
            return false;
        }
        while (iterator != end)
        {
            const fs::path entryPath = iterator->path();
            const DWORD attributes = GetFileAttributesW(entryPath.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES)
            {
                RuntimeCleanupError("Unable to inspect runtime cleanup entry: " + PathToUtf8(entryPath));
                return false;
            }
            if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
            {
                RuntimeCleanupError("Refusing to delete a runtime tree containing a reparse point: " +
                    PathToUtf8(entryPath));
                return false;
            }
            iterator.increment(ec);
            if (ec)
            {
                RuntimeCleanupError("Unable to enumerate runtime cleanup root: " + ec.message());
                return false;
            }
        }
        return true;
    }

    bool ResolveSafeExtractPath(const fs::path& extractRoot, std::string_view archivePath,
        fs::path& outPath, std::wstring& outKey)
    {
        if (archivePath.empty() || archivePath.find('\0') != std::string_view::npos)
        {
            Debug->LogError("Pak entry has an empty or invalid path.");
            return false;
        }

        const std::u8string encodedPath(archivePath.begin(), archivePath.end());
        const fs::path relativePath = fs::u8path(encodedPath);
        if (relativePath.empty() || relativePath.is_absolute() || relativePath.has_root_name() ||
            relativePath.has_root_directory())
        {
            Debug->LogError("Pak entry path is not relative: " + std::string(archivePath));
            return false;
        }

        constexpr std::wstring_view invalidComponentChars = L"<>:\"|?*";
        for (const auto& component : relativePath)
        {
            const std::wstring value = component.native();
            if (value.empty() || value == L"." || value == L".." ||
                value.find_first_of(invalidComponentChars) != std::wstring::npos ||
                value.back() == L' ' || value.back() == L'.')
            {
                Debug->LogError("Pak entry contains an unsafe path component: " +
                    std::string(archivePath));
                return false;
            }
        }

        std::error_code ec{};
        const fs::path normalizedRoot = fs::absolute(extractRoot, ec).lexically_normal();
        if (ec) return false;
        outPath = (normalizedRoot / relativePath.lexically_normal()).lexically_normal();
        const std::wstring rootNative = normalizedRoot.native();
        const std::wstring rootPrefix = rootNative + fs::path::preferred_separator;
        const std::wstring outputNative = outPath.native();
        if (outPath == normalizedRoot || outputNative.size() <= rootPrefix.size() ||
            _wcsnicmp(outputNative.c_str(), rootPrefix.c_str(), rootPrefix.size()) != 0)
        {
            Debug->LogError("Pak entry escapes the extraction root: " + std::string(archivePath));
            return false;
        }

        outKey = outputNative;
        std::transform(outKey.begin(), outKey.end(), outKey.begin(),
            [](wchar_t value) { return static_cast<wchar_t>(std::towlower(value)); });
        return true;
    }

    static bool ForceDeleteTree(const fs::path& root)
    {
        std::error_code ec{};
        const fs::path absoluteRoot = fs::absolute(root, ec).lexically_normal();
        if (ec || absoluteRoot.empty() || absoluteRoot == absoluteRoot.root_path() ||
            absoluteRoot.parent_path() == absoluteRoot.root_path())
        {
            RuntimeCleanupError("Refusing unsafe runtime cleanup root: " + PathToUtf8(root));
            return false;
        }
        if (!fs::exists(absoluteRoot, ec)) return !ec;
        if (ec || !ValidateNoReparseTree(absoluteRoot)) return false;

        fs::remove_all(absoluteRoot, ec);
        if (ec)
        {
            RuntimeCleanupError("Failed to delete runtime tree '" + PathToUtf8(absoluteRoot) +
                "': " + ec.message());
            return false;
        }
        return !fs::exists(absoluteRoot, ec) && !ec;
    }

    enum class RuntimeCleanupScope
    {
        Content,
        Process,
    };

    bool CleanupOwnedRuntime(const EnginePaths& paths, RuntimeCleanupScope scope)
    {
        if (!paths.HasValidRuntimeOwnership())
        {
            RuntimeCleanupError("Injected runtime ownership contract is invalid; cleanup refused.");
            return false;
        }

        // The target is selected inside this capability API. Callers cannot supply
        // an arbitrary target/parent pair that merely looks related.
        const fs::path root = RuntimeCleanupScope::Content == scope
            ? paths.runtimeContentRoot
            : paths.runtimeProcessRoot;
        if (!ValidateExistingPathHasNoReparsePoints(root)) return false;

        std::error_code ec{};
        if (!fs::exists(root, ec))
        {
            if (ec)
            {
                RuntimeCleanupError("Failed to query runtime root '" + PathToUtf8(root) +
                    "': " + ec.message());
                return false;
            }
            return true;
        }

        if (!ForceDeleteTree(root))
        {
            RuntimeCleanupError("Failed to delete owned runtime directory '" +
                PathToUtf8(root) + "'.");
            return false;
        }

        RuntimeCleanupInfo("Removed owned runtime directory '" + PathToUtf8(root) + "'.");
        return true;
    }

    bool UnpackageGameAssets(const fs::path& pakPath, const fs::path& extractRoot)
    {
        if (pakPath.empty())
        {
            Debug->LogError("Pak file path is empty.");
            return false;
        }

        std::error_code ec{};
        const fs::path absolutePakPath =
            fs::absolute(pakPath, ec).lexically_normal();
        if (ec || absolutePakPath.empty() ||
            !ValidateExistingPathHasNoReparsePoints(absolutePakPath))
        {
            Debug->LogError("Pak file path is invalid: " + PathToUtf8(pakPath));
            return false;
        }

        ec.clear();
        if (!fs::is_regular_file(absolutePakPath, ec) || ec)
        {
            Debug->LogError("Pak file not found or not a regular file: " +
                PathToUtf8(absolutePakPath));
            return false;
        }

        if (extractRoot.empty())
        {
            Debug->LogError("Unpacked assets root is empty.");
            return false;
        }
        if (!ValidateExistingPathHasNoReparsePoints(extractRoot)) return false;
        if (!EnsureDirectoryExists(extractRoot))
        {
            Debug->LogError("Unable to prepare extraction root: " + PathToUtf8(extractRoot));
            return false;
        }
        if (!ValidateExistingPathHasNoReparsePoints(extractRoot)) return false;

        try
        {
            Pak::OpenOptions options{};
            Pak::Archive archive(absolutePakPath, options);

            auto entries = archive.list();
            if (entries.empty())
            {
                Debug->LogWarning("Pak archive contains no entries: " +
                    PathToUtf8(absolutePakPath));
                return false;
            }

            bool allSucceeded = true;
            std::size_t extractedCount = 0;
            std::set<std::wstring> extractedPaths;

            for (const auto& entry : entries)
            {
                fs::path outPath;
                std::wstring outputKey;
                if (!ResolveSafeExtractPath(extractRoot, entry.path, outPath, outputKey) ||
                    !extractedPaths.insert(outputKey).second)
                {
                    Debug->LogError("Pak contains an unsafe or duplicate output path: " + entry.path);
                    return false;
                }
                fs::path parent = outPath.parent_path();

                if (!ValidateExistingPathHasNoReparsePoints(parent)) return false;
                if (!parent.empty() && !EnsureDirectoryExists(parent))
                {
                    allSucceeded = false;
                    continue;
                }
                if (!ValidateExistingPathHasNoReparsePoints(parent)) return false;
                const DWORD existingAttributes = GetFileAttributesW(outPath.c_str());
                if (existingAttributes != INVALID_FILE_ATTRIBUTES)
                {
                    Debug->LogError("Pak extraction destination already exists: " + PathToUtf8(outPath));
                    return false;
                }
                const DWORD destinationError = GetLastError();
                if (destinationError != ERROR_FILE_NOT_FOUND && destinationError != ERROR_PATH_NOT_FOUND)
                {
                    Debug->LogError("Unable to inspect pak extraction destination '" +
                        PathToUtf8(outPath) + "' (Win32=" +
                        std::to_string(destinationError) + ").");
                    return false;
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
