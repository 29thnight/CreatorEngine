#ifndef DYNAMICCPP_EXPORTS
#include "GameBuilderSystem.h"
#include "EngineSetting.h"
#include "MSBuildHelper.h"

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
		RunMsbuildWithLiveLogAndProgress(m_scriptBuildCommand);
		RunMsbuildWithLiveLogAndProgress(m_buildCommand);
	}
	catch (const std::exception& e)
	{
		Debug->LogError("Failed to build game: " + std::string(e.what()));
		return;
	}
	
}

#endif // !DYNAMICCPP_EXPORTS