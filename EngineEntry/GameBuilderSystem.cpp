#ifndef DYNAMICCPP_EXPORTS
#include "GameBuilderSystem.h"
#include "EngineSetting.h"
#include "MSBuildHelper.h"
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

        std::wstring pakStem = SanitizePakStem(EngineSettingInstance->GetBuildGameName());
        if (pakStem.empty())
        {
            pakStem = L"GameAssets";
        }

        fs::path outputDir = PathFinder::RelativeToExecutable("");
        if (outputDir.empty())
        {
            outputDir = assetsRoot.parent_path();
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

            std::size_t fileCount = 0;
            const fs::recursive_directory_iterator end{};
            for (; it != end; it.increment(ec))
            {
                if (ec)
                {
                    Debug->LogWarning("Error while iterating assets: " + ec.message());
                    ec.clear();
                    continue;
                }

                const auto& entry = *it;
                if (!entry.is_regular_file(ec))
                {
                    ec.clear();
                    continue;
                }

                auto relative = fs::relative(entry.path(), assetsRoot, ec);
                if (ec)
                {
                    Debug->LogWarning("Failed to resolve relative path for asset '" + PathToUtf8(entry.path()) + "': " + ec.message());
                    ec.clear();
                    continue;
                }

                auto virtualPath = relative.generic_u8string();
                if (virtualPath.empty())
                {
                    continue;
                }

                builder.addFile(std::string(virtualPath.begin(), virtualPath.end()), entry.path());
                ++fileCount;
            }

            if (fileCount == 0)
            {
                Debug->LogWarning("No files found under assets root. Skipping pak generation.");
                return false;
            }

            builder.finish();
            Debug->Log("Packaged " + std::to_string(fileCount) + " assets into pak: " + PathToUtf8(pakPath));
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

        fs::path extractRoot = PathFinder::DumpPath();
        if (extractRoot.empty())
        {
            extractRoot = pakBaseDir / "UnpackedAssets";
        }
        else
        {
            extractRoot /= "UnpackedAssets";
        }
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

void GameBuilderSystem::Initialize()
{
	if(!m_isInitialized)
	{
		// 초기화 로직
		m_buildSlnPath = PathFinder::GameBuildSlnPath().wstring();
		m_MSBuildPath = EngineSettingInstance->GetMsbuildPath();
		if (m_MSBuildPath.empty())
		{
			Debug->LogError("MSBuild path is not set. Please check your Visual Studio installation.");
			return;
		}

		m_buildCommand = std::wstring(L"cmd /c \"")
			+ L"\"" + m_MSBuildPath + L"\" "
			+ L"\"" + m_buildSlnPath + L"\" "
			+ L"/m /t:Build /p:Configuration=GameBuild /p:Platform=x64 /nologo"
			+ L"\"";

		std::wstring slnPath = PathFinder::DynamicSolutionPath("Dynamic_CPP.sln").wstring();
		m_scriptBuildCommand = std::wstring(L"cmd /c \"")
			+ L"\"" + m_MSBuildPath + L"\" "
			+ L"\"" + slnPath + L"\" "
			+ L"/m /t:Build /p:Configuration=GameBuild /p:Platform=x64 /nologo"
			+ L"\"";

		m_isInitialized = true;
	}
}

void GameBuilderSystem::Finalize()
{
	// 정리 로직
}

void GameBuilderSystem::BuildGame()
{
	// 게임 빌드 로직
	// 예: m_buildSlnPath와 m_buildCommand를 사용하여 빌드 수행
	try
	{
		RunMsbuildWithLiveLogAndProgress(m_scriptBuildCommand, L"Script Compile...");
		RunMsbuildWithLiveLogAndProgress(m_buildCommand);

        if (!PackageGameAssets())
        {
            Debug->LogWarning("Asset packaging step completed with warnings.");
        }
	}
	catch (const std::exception& e)
	{
		Debug->LogError("Failed to build game: " + std::string(e.what()));
		return;
	}
	
}

bool GameBuilderSystem::PackageGameAssets()
{
	return ::PackageGameAssets();
}

bool GameBuilderSystem::UnpackageGameAssets()
{
	return ::UnpackageGameAssets();
}

#endif // !DYNAMICCPP_EXPORTS