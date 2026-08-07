#ifndef DYNAMICCPP_EXPORTS
#include "GameBuilderSystem.h"
#include "EngineSetting.h"
#include "MSBuildHelper.h"
#include "PakHelper.h"

void GameBuilderSystem::Initialize()
{
	if(!m_isInitialized)
	{
		// ÃÊ±âÈ­ ·ÎÁ÷
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
			+ L"/m /t:Rebuild /p:Configuration=GameBuild /p:Platform=x64 /nologo"
			+ L"\"";

		// C++ ìŠ¤í¬ë¦½íŠ¸ ë¹Œë“œ(Dynamic_CPP.sln)ëŠ” ì€í‡´(9-4) â€” ê²Œìž„ ìŠ¤í¬ë¦½íŠ¸ëŠ” C# ì–´ì…ˆë¸”ë¦¬ë¡œ ë°°í¬ëœë‹¤.

		m_isInitialized = true;
	}
}

void GameBuilderSystem::Finalize()
{
	// Á¤¸® ·ÎÁ÷
}

void GameBuilderSystem::BuildGame()
{
	// °ÔÀÓ ºôµå ·ÎÁ÷
	// ¿¹: m_buildSlnPath¿Í m_buildCommand¸¦ »ç¿ëÇÏ¿© ºôµå ¼öÇà
	try
	{
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