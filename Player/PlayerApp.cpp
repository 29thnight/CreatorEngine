#include "PlayerApp.h"

#include "Camera.h"
#include "CameraComponent.h"
#include "Scene.h"
#include "EngineBootstrap.h"
#include "EngineLaunchConfig.h"
#include "GpuDiagnostics.h"
#include "InputManager.h"
#include "LogSystem.h"
#include "Material.h"
#include "PakHelper.h"
#include "Render/Scene/EnhancedSceneRenderer.h"
#include "SceneManager.h"

#include <shellapi.h>

#include <cstdio>
#include <array>
#include <filesystem>

#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace
{
	constexpr int kRuntimeCleanupFailureExitCode = 5;

	std::filesystem::path ResolvePlayerRuntimeOwnerRoot()
	{
		std::array<wchar_t, MAX_PATH> buffer{};
		const DWORD length = GetTempPathW(
			static_cast<DWORD>(buffer.size()), buffer.data());
		if (0 == length || length >= buffer.size()) return {};
		return (std::filesystem::path(buffer.data()) /
			L"CreatorEngine" / L"Player").lexically_normal();
	}

	bool HasSafeExistingAncestors(const std::filesystem::path& path) noexcept
	{
		try
		{
			std::error_code error{};
			const std::filesystem::path absolutePath =
				std::filesystem::absolute(path, error).lexically_normal();
			if (error || absolutePath.empty() || absolutePath.root_path().empty()) return false;

			std::filesystem::path current = absolutePath.root_path();
			for (const auto& component : absolutePath.relative_path())
			{
				current /= component;
				const DWORD attributes = GetFileAttributesW(current.c_str());
				if (attributes == INVALID_FILE_ATTRIBUTES)
				{
					const DWORD win32Error = GetLastError();
					return win32Error == ERROR_FILE_NOT_FOUND ||
						win32Error == ERROR_PATH_NOT_FOUND;
				}
				if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) return false;
			}
			return true;
		}
		catch (...) { return false; }
	}

	bool PreparePlayerRuntimeContent(const EnginePaths& paths) noexcept
	{
		try
		{
			const std::filesystem::path pakPath =
				(paths.executableRoot / L"GameAssets.pak").lexically_normal();
			return UnpackageGameAssets(pakPath, paths.runtimeContentRoot);
		}
		catch (...)
		{
			if (Log::IsAlive())
			{
				Debug->LogError("Player package preparation threw an unknown exception.");
			}
			return false;
		}
	}

	std::filesystem::path ResolvePlayerDeploymentDirectory(
		const std::filesystem::path& executableRoot,
		const std::filesystem::path& name) noexcept
	{
		try
		{
			std::error_code error{};
			const std::filesystem::path appLocal = (executableRoot / name).lexically_normal();
			if (std::filesystem::is_directory(appLocal, error) && !error) return appLocal;

			error.clear();
			const std::filesystem::path shared =
				(executableRoot.parent_path() / name).lexically_normal();
			if (std::filesystem::is_directory(shared, error) && !error) return shared;
			return appLocal;
		}
		catch (...) { return {}; }
	}

	EngineLaunchConfig MakePlayerLaunchConfig()
	{
		EngineLaunchConfig config{};
		config.compatibilityRunMode = EngineRunMode::Player;
		config.logSessionName = "Game";
		config.prepareRuntimeContent = &PreparePlayerRuntimeContent;

		const std::filesystem::path executableRoot =
			ResolveProcessExecutableDirectory();
		// Only the Editor owns an authoring project root. Each Player process owns an
		// isolated runtime-content directory, so parallel Players never clean or
		// overwrite one another's extracted package.
		const std::filesystem::path runtimeOwnerRoot = ResolvePlayerRuntimeOwnerRoot();
		const std::filesystem::path runtimeProcessRoot = runtimeOwnerRoot.empty()
			? std::filesystem::path{}
			: (runtimeOwnerRoot / std::to_wstring(GetCurrentProcessId())).lexically_normal();
		const std::filesystem::path runtimeContentRoot = runtimeProcessRoot.empty()
			? std::filesystem::path{}
			: (runtimeProcessRoot / L"RuntimeContent").lexically_normal();
		const std::filesystem::path runtimeDataRoot = runtimeProcessRoot.empty()
			? std::filesystem::path{}
			: (runtimeProcessRoot / L"RuntimeData").lexically_normal();
		if (runtimeContentRoot.empty() || runtimeDataRoot.empty() ||
			!HasSafeExistingAncestors(runtimeContentRoot) ||
			!HasSafeExistingAncestors(runtimeDataRoot))
		{
			std::fputs("[Player] runtime root crosses an unsafe reparse point\n", stderr);
			return config;
		}
		config.paths.executableRoot = executableRoot;
		config.paths.projectRoot = runtimeContentRoot;
		config.paths.runtimeOwnerRoot = runtimeOwnerRoot;
		config.paths.runtimeProcessRoot = runtimeProcessRoot;
		config.paths.runtimeContentRoot = runtimeContentRoot;
		config.paths.runtimeDataRoot = runtimeDataRoot;
		config.paths.assetsRoot =
			(runtimeContentRoot / L"Assets").lexically_normal();
		config.paths.managedRoot =
			ResolvePlayerDeploymentDirectory(executableRoot, L"Managed");
		config.paths.engineResourceRoot =
			ResolvePlayerDeploymentDirectory(executableRoot, L"Resources");
		config.paths.testArtifactRoot =
			(runtimeDataRoot / L"Tests").lexically_normal();
		config.paths.enableAssetAuthoring = false;

		config.window.title = L"Creator Player";
		config.window.clientWidth = 1920;
		config.window.clientHeight = 1080;
		config.window.style = WS_POPUP;
		config.window.centerOnDesktop = false;
		config.window.fitNearestMonitor = true;
		config.window.showOnCreate = true;
		config.window.acceptFileDrops = true;
		return config;
	}
}

MAIN_ENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow)
{
	// --smoke N — 명령줄 파싱은 이 exe에 이것뿐이라 인프라를 들이지 않는다.
	// 판정 규약(종료 코드 + 로그 마커)은 BuildPipelinePlan §2.3.
	int argc = 0;
	if (LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc))
	{
		for (int i = 1; i < argc; ++i)
		{
			if (0 == wcscmp(argv[i], L"--smoke") && i + 1 < argc)
			{
				Player::g_smoke.frameLimit = wcstoull(argv[i + 1], nullptr, 10);
			}
			else if (0 == wcscmp(argv[i], L"--command-service"))
			{
				// PHASE 14.5 LC8 — 기본 off. 켜는 것은 이 플래그뿐이다(§8 · §11.2).
				Player::g_service.enabled = true;
			}
		}
		LocalFree(argv);
	}

	const EngineLaunchConfig launchConfig = MakePlayerLaunchConfig();

	// endpoint 파일은 **이 프로세스의** runtime 데이터 뿌리에 놓는다.
	// Player 를 여럿 띄우는 것이 정상 경로라(각자 격리된 runtimeProcessRoot),
	// 에디터처럼 프로젝트 뿌리 하나를 쓰면 나중에 뜬 쪽이 앞선 쪽의 파일을 덮는다.
	Player::g_service.endpointRoot = launchConfig.paths.runtimeDataRoot.string();
	// Player is always a packaged Host. An invalid launch config must not bypass
	// ownership preflight merely because its remaining capability fields are defaults.
	if (!CleanupOwnedRuntime(launchConfig.paths, RuntimeCleanupScope::Process))
	{
		std::fputs("[Player] runtime root preparation failed\n", stderr);
		// ★ 2 → 5 (§5.4 · LC8). 런타임 뿌리를 준비하지 못한 것은 IO·infrastructure
		//   이지 호출자의 인자 오류가 아니다. 바로 아래 cleanup 실패가 이미 5 를
		//   쓰고 있었고(kRuntimeCleanupFailureExitCode), 같은 성격의 두 실패가
		//   다른 값을 내고 있었다.
		return kRuntimeCleanupFailureExitCode;
	}

	const int exitCode = EngineBootstrap::Run<Player::App>(hInstance, launchConfig);
	int finalExitCode = exitCode;

	// RuntimeGuard has finished every file consumer and the logger when Run returns.
	// CleanupOwnedRuntime uses a stderr-only fallback after Log::Finalize.
	// A successful smoke preserves this root until the orchestrator verifies the
	// extracted bytes. Failed startup/smoke runs remove partial content immediately.
	if (0 != finalExitCode || 0 == Player::g_smoke.frameLimit)
	{
		try
		{
			if (!CleanupOwnedRuntime(launchConfig.paths, RuntimeCleanupScope::Process))
			{
				std::fwprintf(stderr, L"[Player] runtime content cleanup failed: %ls\n",
					launchConfig.paths.runtimeProcessRoot.c_str());
				if (0 == finalExitCode) finalExitCode = kRuntimeCleanupFailureExitCode;
			}
		}
		catch (...)
		{
			std::fwprintf(stderr, L"[Player] runtime cleanup threw: %ls\n",
				launchConfig.paths.runtimeProcessRoot.c_str());
			if (0 == finalExitCode) finalExitCode = kRuntimeCleanupFailureExitCode;
		}
	}
	return finalExitCode;
}

void Player::App::Initialize(CoreWindow& coreWindow)
{
	m_hWnd = coreWindow.GetHandle();

	// scene·ImGui 표시 RHI는 패키징이 빌드 선택을 runtime
	// render.backend로 투영한 값을 함께 쓴다. Player도 Editor와 같은
	// IImGuiHost 경계를 쓰며, 선택은
	// RuntimeSettings 로드 시 한 번 고정된다.

	RegisterHandler(coreWindow);
	Load();
	Run();
}

void Player::App::Finalize()
{
	m_main->Finalize();
	// ★ 구 GameApp의 종료 절차 둘을 걷어냈다(둘 다 BUILD_FLAG 전용이라
	//   한 번도 실행된 적 없던 코드고, 첫 스모크가 둘 다 잡았다):
	//   · DataSystems->Finalize() — FinalizeRuntime의 Destroy와 겹쳐 이중 해제.
	//   · CleanupUnpackedGameAssets() — FinalizeRuntime(DataSystem::Destroy)보다
	//     먼저 에셋 트리를 지워, 소멸자가 사라진 파일을 밟았다. 정상 실행의
	//     정리는 EngineBootstrap::Run 반환 뒤로 옮겼다. 비정상 종료 잔재를
	//     안전하게 회수하는 process lease/prune 정책은 별도 후속 작업이다.
	GpuDiagnostics::ReportLiveObjects();
}

void Player::App::RegisterHandler(CoreWindow& coreWindow)
{
	coreWindow.RegisterHandler(WM_SIZE, this, &App::HandleResizeEvent);
	coreWindow.RegisterHandler(WM_CLOSE, this, &App::Shutdown);
}

void Player::App::Load()
{
	if (nullptr == m_main)
	{
		m_main = std::make_unique<PlayerMain>();
	}
}

void Player::App::Run()
{
	CoreWindow::GetForCurrentInstance()->InitializeTask([&]
	{
		m_main->Initialize();
		InputManagement->Initialize(m_hWnd);
	})
	.Then([&]
	{
		m_main->Update();

		// 프레임 경계 밀봉 — 에디터와 같은 자리, 게임 카메라 하나만 넘긴다.
		// 씬 오버레이 뷰 선언(E4-5)은 Editor 전용이라 Player는 항상 false다.
		EnhancedLiveViewRequest views[EnhancedSceneRenderer::kMaxLiveCameraViews]{};
		uint32_t viewCount = 0;
		Scene* activeScene = SceneManagers->GetActiveScene();
		if (CameraComponent* gameCamera = nullptr != activeScene
			? activeScene->Cameras().GetPrimaryCamera() : nullptr)
		{
			views[viewCount++] = {
				{ kEnhancedGameViewId,
					static_cast<uint64_t>(gameCamera->GetInstanceID()) },
				gameCamera->CaptureFrameSnapshot(),
				EnhancedLiveDisplayTarget::Game,
				EnhancedLiveViewFlags::ScreenSpaceUI };
		}
		const std::vector<std::shared_ptr<Material>> requiredMaterials =
			SceneManagers->CaptureRequiredRenderMaterials();
		const EnhancedRequiredAssetPacket requiredAssets =
			EnhancedSceneRenderer::BuildRequiredAssetPacket(requiredMaterials);
		EnhancedLiveFramePacket renderFrame =
			EnhancedSceneRenderer::BuildLiveFramePacket(
			static_cast<float>(m_main->GetFrameDeltaTime()),
			views, viewCount, SceneManagers->IsSceneLoading(), requiredAssets);
		const uint64_t publishedFrameId = renderFrame.frameId;
		if (EnhancedSceneRenderer::PublishLiveFrame(std::move(renderFrame)))
		{
			m_main->NotifyRenderFramePublished(publishedFrameId);
		}
	});
}

LRESULT Player::App::Shutdown(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
	PostQuitMessage(0);
	return 0;
}

LRESULT Player::App::HandleResizeEvent(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
	if (wParam == SIZE_MINIMIZED)
	{
		m_isMinimized = true;
		return 0;
	}

	if (m_isMinimized)
	{
		if (wParam == SIZE_RESTORED || wParam == SIZE_MAXIMIZED)
		{
			m_isMinimized = false;
			return 0;
		}
	}

	m_main->InvokeResizeFlag();
	return 0;
}
