#ifndef DYNAMICCPP_EXPORTS
#include "GameBuilderSystem.h"
#include "EngineSetting.h"
#include "MSBuildHelper.h"
#include "PakHelper.h"

void GameBuilderSystem::Initialize()
{
	if(!m_isInitialized)
	{
		// �ʱ�ȭ ����
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
	// ���� ����
}

void GameBuilderSystem::BuildGame()
{
	// ���� ���� ����
	// ��: m_buildSlnPath�� m_buildCommand�� ����Ͽ� ���� ����
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