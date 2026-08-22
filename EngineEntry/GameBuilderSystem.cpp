#ifndef DYNAMICCPP_EXPORTS
#include "GameBuilderSystem.h"
#include "EditorSettingsStore.h"
#include "PathFinder.h"

#include <Windows.h>
#include <array>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace
{
	file::path ResolvePowerShellExecutable()
	{
		constexpr std::array<const wchar_t*, 2> roots{ L"ProgramW6432", L"ProgramFiles" };
		for (const wchar_t* variable : roots)
		{
			const DWORD required = GetEnvironmentVariableW(variable, nullptr, 0);
			if (required <= 1) continue;
			std::vector<wchar_t> value(required);
			if (GetEnvironmentVariableW(variable, value.data(), required) == 0) continue;

			const file::path candidate = file::path(value.data()) /
				L"PowerShell" / L"7" / L"pwsh.exe";
			std::error_code ec{};
			if (file::is_regular_file(candidate, ec) && !ec) return candidate;
		}
		return {};
	}

	std::wstring QuoteWindowsArgument(std::wstring_view argument)
	{
		if (!argument.empty() &&
			argument.find_first_of(L" \t\n\v\"") == std::wstring_view::npos)
		{
			return std::wstring(argument);
		}

		std::wstring quoted{ L'\"' };
		size_t backslashes = 0;
		for (const wchar_t character : argument)
		{
			if (character == L'\\')
			{
				++backslashes;
				continue;
			}

			if (character == L'\"')
			{
				quoted.append(backslashes * 2 + 1, L'\\');
				quoted.push_back(character);
				backslashes = 0;
				continue;
			}

			quoted.append(backslashes, L'\\');
			backslashes = 0;
			quoted.push_back(character);
		}
		quoted.append(backslashes * 2, L'\\');
		quoted.push_back(L'\"');
		return quoted;
	}

	bool RunPackageOrchestrator(const file::path& projectRoot,
		const std::wstring& startupScene, RenderBackend backend)
	{
		std::error_code pathError;
		if (!file::is_directory(projectRoot, pathError) || pathError ||
			startupScene.empty() || file::path(startupScene).filename() != startupScene ||
			_wcsicmp(file::path(startupScene).extension().c_str(), L".creator") != 0)
		{
			Debug->LogError("Build Settings의 시작 씬은 프로젝트에 있는 .creator 파일이어야 합니다.");
			return false;
		}
		const file::path startupScenePath =
			projectRoot / L"Assets" / L"Scenes" / startupScene;
		pathError.clear();
		if (!file::is_regular_file(startupScenePath, pathError) || pathError)
		{
			Debug->LogError("Build Settings 시작 씬 파일을 찾을 수 없습니다.");
			return false;
		}

		const file::path repositoryRoot = projectRoot.parent_path();
		const file::path scriptPath = repositoryRoot / L"Tools" / L"build.ps1";
		pathError.clear();
		if (!file::is_regular_file(scriptPath, pathError) || pathError)
		{
			Debug->LogError("게임 패키지 오케스트레이터를 찾을 수 없습니다: Tools/build.ps1");
			return false;
		}

		const file::path pwshPath = ResolvePowerShellExecutable();
		if (pwshPath.empty())
		{
			Debug->LogError("게임 패키지 빌드에는 Program Files의 PowerShell 7(pwsh.exe)이 필요합니다.");
			return false;
		}

		const std::wstring backendName =
			backend == RenderBackend::Vulkan ? L"vulkan" : L"dx12";
		const std::array<std::wstring, 20> arguments{
			pwshPath.wstring(),
			L"-NoProfile",
			L"-NonInteractive",
			L"-ExecutionPolicy",
			L"Bypass",
			L"-File",
			scriptPath.wstring(),
			L"-Target",
			L"Game",
			L"-Config",
			L"Release",
			L"-InputMode",
			L"Project",
			L"-Project",
			projectRoot.wstring(),
			L"-BuildNative",
			L"-StartupScene",
			startupScene,
			L"-RenderBackend",
			backendName,
		};

		std::wstring commandLine;
		for (const auto& argument : arguments)
		{
			if (!commandLine.empty()) commandLine.push_back(L' ');
			commandLine += QuoteWindowsArgument(argument);
		}
		std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
		mutableCommand.push_back(L'\0');

		STARTUPINFOW startupInfo{};
		startupInfo.cb = sizeof(startupInfo);
		PROCESS_INFORMATION processInfo{};
		if (!CreateProcessW(pwshPath.c_str(), mutableCommand.data(), nullptr, nullptr,
			FALSE, CREATE_NO_WINDOW, nullptr, repositoryRoot.c_str(),
			&startupInfo, &processInfo))
		{
			Debug->LogError("게임 패키지 빌드 프로세스 시작 실패 (Win32=" +
				std::to_string(GetLastError()) + ")");
			return false;
		}

		const DWORD waitResult = WaitForSingleObject(processInfo.hProcess, INFINITE);
		DWORD exitCode = ERROR_GEN_FAILURE;
		const bool exitCodeRead = waitResult == WAIT_OBJECT_0 &&
			GetExitCodeProcess(processInfo.hProcess, &exitCode) != FALSE;
		CloseHandle(processInfo.hThread);
		CloseHandle(processInfo.hProcess);
		if (!exitCodeRead || exitCode != 0)
		{
			Debug->LogError("게임 패키지 빌드 실패 (exit=" +
				std::to_string(exitCodeRead ? exitCode : ERROR_GEN_FAILURE) + ")");
			return false;
		}
		return true;
	}
}

// ★ Editor의 직접 MSBuild 조립이 사라졌다 (PHASE 12 B2).
//
//   예전에는 여기서 GameBuild.sln을 /t:Rebuild로 다시 컴파일했다 — 게임
//   빌드가 엔진을 게임용 구성으로 재컴파일하던 언리얼식 모델의 잔재다.
//   이제 Editor와 CLI는 Tools/build.ps1 하나를 호출하고, 그 오케스트레이터가
//   Player/AssetPacker의 Release 빌드부터 Stage/Pak/Verify/Publish까지 소유한다.
//   Core DLL/version provenance가 생기기 전에는 stale Player 배포를 막기 위해
//   제품 경로도 -BuildNative를 명시한다.

void GameBuilderSystem::Initialize()
{
	m_isInitialized = true;
}

void GameBuilderSystem::Finalize()
{
}

bool GameBuilderSystem::BuildGame()
{
	const file::path projectRoot = PathFinder::BaseProjectPath();
	const BuildSettings& buildSettings = EditorSettingsStore::Get().Build();
	const std::wstring startupScene = buildSettings.GetStartupSceneName();
	if (!RunPackageOrchestrator(projectRoot, startupScene,
		buildSettings.GetRenderBackend()))
	{
		return false;
	}

	Debug->LogDebug("Release Player 패키지 빌드·검증·게시 완료 (Build/Staging/*.current.json).");
	return true;
}

#endif // !DYNAMICCPP_EXPORTS
