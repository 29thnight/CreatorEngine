#ifndef DYNAMICCPP_EXPORTS
#include "GameBuilderSystem.h"
#include "EngineSetting.h"
#include "MSBuildHelper.h"

void GameBuilderSystem::Initialize()
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

}

void GameBuilderSystem::Finalize()
{
	// ���� ����
}

void GameBuilderSystem::BuildGame()
{
	// ���� ���� ����
	// ��: m_buildSlnPath�� m_buildCommand�� ����Ͽ� ���� ����
}

#endif // !DYNAMICCPP_EXPORTS