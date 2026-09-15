#include "GameBuilderSystem.h"
#include "EditorSettingsStore.h"
#include "PathFinder.h"
#include "EditorEngineDistribution.h"

#include <Windows.h>
#include <array>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace
{
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
			Debug::PrintLog({}, spdlog::level::err, "Build Settings의 시작 씬은 프로젝트에 있는 .creator 파일이어야 합니다.");
			return false;
		}
		const file::path startupScenePath =
			projectRoot / L"Assets" / L"Scenes" / startupScene;
		pathError.clear();
		if (!file::is_regular_file(startupScenePath, pathError) || pathError)
		{
			Debug::PrintLog({}, spdlog::level::err, "Build Settings 시작 씬 파일을 찾을 수 없습니다.");
			return false;
		}

		const auto distribution = ResolveEditorEngineDistribution(true);
		if (distribution.root.empty())
		{
			Debug::PrintLog({}, spdlog::level::err, "선택 가능한 엔진 배포본이 없습니다. CreatorBuildTool publish-engine으로 배포본을 생성하세요.");
			return false;
		}
		const file::path repositoryRoot = distribution.root;
		const file::path buildToolPath = repositoryRoot / L"Bin" / (L"x64-" + distribution.configuration) /
			L"Tools" / L"CreatorBuildTool" / L"CreatorBuildTool.exe";
		pathError.clear();
		if (!file::is_regular_file(buildToolPath, pathError) || pathError)
		{
			Debug::PrintLog({}, spdlog::level::err, "선택한 엔진 배포본에 CreatorBuildTool.exe가 없습니다.");
			return false;
		}

		const std::wstring backendName =
			backend == RenderBackend::Vulkan ? L"vulkan" : L"dx12";
		std::vector<std::wstring> arguments{
			buildToolPath.wstring(),
			L"package-game",
			L"-Config",
			distribution.configuration,
			L"-InputMode",
			L"Project",
			L"-Project",
			projectRoot.wstring(),
			L"-EngineDistribution",
			distribution.root.wstring(),
			L"-StartupScene",
			startupScene,
			L"-RenderBackend",
			backendName,
		};
		if (distribution.shipping) arguments.push_back(L"-Shipping");

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
		if (!CreateProcessW(buildToolPath.c_str(), mutableCommand.data(), nullptr, nullptr,
			FALSE, CREATE_NO_WINDOW, nullptr, repositoryRoot.c_str(),
			&startupInfo, &processInfo))
		{
			Debug::PrintLog({}, spdlog::level::err, "게임 패키지 빌드 프로세스 시작 실패 (Win32=" +
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
			Debug::PrintLog({}, spdlog::level::err, "게임 패키지 빌드 실패 (exit=" +
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
//   Editor와 CLI는 CreatorBuildTool.exe를 호출하고, 그 도구가
//   BuildManaged/Cook/Stage/Pak/Verify/Publish를 소유한다.
//   제품 경로는 해시로 고정된 엔진 배포본을 선택한다. C++/ScriptCore는
//   다시 빌드하지 않고, 프로젝트의 게임 스크립트와 콘텐츠만 패키징한다.

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

	Debug::PrintLog({}, spdlog::level::debug, "선택한 엔진의 Player 패키지 빌드·검증·게시 완료 (Build/Staging/*.current.json).");
	return true;
}

