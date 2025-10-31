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

        std::wstring pakStem = SanitizePakStem(L"TRAIN_ASIS");
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

    static void SetAttrsNormal(const fs::path& p)
    {
        const std::wstring w = p.c_str();
        DWORD attrs = GetFileAttributesW(w.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES)
        {
            // 숨김/시스템/읽기전용 제거
            attrs &= ~(FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN);
            SetFileAttributesW(w.c_str(), attrs);
        }
    }

    static bool DeleteFileWithRetry(const std::wstring& wpath, int tries = 8, DWORD waitMs = 50)
    {
        for (int i = 0; i < tries; ++i)
        {
            if (DeleteFileW(wpath.c_str()))
                return true;

            DWORD err = GetLastError();
            if (err == ERROR_SHARING_VIOLATION || err == ERROR_ACCESS_DENIED || err == ERROR_LOCK_VIOLATION)
                std::this_thread::sleep_for(std::chrono::milliseconds(waitMs));
            else
                break;
        }
        return false;
    }

    static bool RemoveDirWithRetry(const std::wstring& wpath, int tries = 8, DWORD waitMs = 50)
    {
        for (int i = 0; i < tries; ++i)
        {
            if (RemoveDirectoryW(wpath.c_str()))
                return true;

            DWORD err = GetLastError();
            if (err == ERROR_SHARING_VIOLATION || err == ERROR_ACCESS_DENIED || err == ERROR_DIR_NOT_EMPTY)
                std::this_thread::sleep_for(std::chrono::milliseconds(waitMs));
            else
                break;
        }
        return false;
    }

    static void ScheduleDeleteOnReboot(const std::wstring& wpath)
    {
        // 실패해도 어쩔 수 없음: best-effort
        MoveFileExW(wpath.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
    }

    static bool ForceDeleteTree(const fs::path& root)
    {
        if (!fs::exists(root)) return true;

        // 재귀로 하위부터 삭제
        for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied);
            it != fs::end(it); ++it)
        {
            const auto& p = it->path();
            std::wstring w = p.c_str();

            // 디렉터리는 나중에 삭제되도록 건너뜀
            if (it->is_regular_file() || it->is_symlink())
            {
                SetAttrsNormal(p);
                if (!DeleteFileWithRetry(w))
                {
                    // 마지막 수단: 재부팅 시 삭제
                    ScheduleDeleteOnReboot(w);
                }
            }
        }
        // 이제 디렉터리들을 바닥에서 위로 삭제
        // recursive_directory_iterator는 위에서 아래로 가므로, 여기선 reverse_iterator로 정리
        std::vector<fs::path> dirs;
        for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied);
            it != fs::end(it); ++it)
        {
            if (it->is_directory()) dirs.push_back(it->path());
        }
        std::sort(dirs.begin(), dirs.end(),
            [](const fs::path& a, const fs::path& b) { return a.native().size() > b.native().size(); });

        for (const auto& d : dirs)
        {
            SetAttrsNormal(d);
            if (!RemoveDirWithRetry(d.c_str()))
                ScheduleDeleteOnReboot(d.c_str());
        }

        // 최상위 폴더
        SetAttrsNormal(root);
        if (!RemoveDirWithRetry(root.c_str()))
            ScheduleDeleteOnReboot(root.c_str());

        return !fs::exists(root);
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
            if (ec) Debug->LogWarning("Failed to query '" + PathToUtf8(extractRoot) + "': " + ec.message());
            return true;
        }

        if (!ForceDeleteTree(extractRoot))
        {
            Debug->LogError("Failed to delete unpacked assets directory '" + PathToUtf8(extractRoot) + "'. "
                "Some entries may be scheduled for deletion on reboot.");
            return false;
        }

        Debug->Log("Removed unpacked assets directory '" + PathToUtf8(extractRoot) + "'.");
        return true;
    }

    bool UnpackageGameAssets()
    {
        std::wstring pakStem = SanitizePakStem(L"TRAIN_ASIS");
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