#include "App.h"
#include "Resource.h"
#include "ProgressSink.h"
#include "ConsoleCommandSystem.h"
#include "Camera.h"
#include "CameraComponent.h"
#include "Scene.h"
#include "InputManager.h"
#include "PathFinder.h"
#include "DumpHandler.h"
#include "CoreWindow.h"
#include "DataSystem.h"
#include "Material.h"
#include "DebugStreamBuf.h"
#include "EditorSettingsStore.h"
#include "EditorSessionState.h"
#include "EditorAssetDatabase.h"
#include "EditorAssetPresentation.h"
#include "EditorPlatform.h"
#include "EditorWindowChrome.h"
#include "ViewportHostWindow.h"
#include "RHI/ImGuiWin32Cursor.h"
#include "PrefabUtility.h"
#include "TagManager.h"
#include "GpuDiagnostics.h"
#include "ReflectionRegister.h"
#include "ComponentFactory.h"
#include <imgui_impl_win32.h>
#include <ppltasks.h>
#include <ppl.h>
#include "InputActionManager.h"
#include "EngineBootstrap.h"
#include "EngineLaunchConfig.h"
#include "BootProgress.h"
#include "WinProcProxy.h"
#include "Render/Scene/EnhancedSceneRenderer.h"
#include "SceneManager.h"
#include <shellapi.h>
#include <chrono>
#include <thread>

#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace
{
	std::filesystem::path EditorPathArgument(const wchar_t* option)
	{
		int count{};
		wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &count);
		std::filesystem::path result;
		if (!arguments) return result;
		for (int i = 1; i + 1 < count; ++i)
			if (wcscmp(arguments[i], option) == 0) { result = std::filesystem::absolute(arguments[i + 1]).lexically_normal(); break; }
		LocalFree(arguments);
		return result;
	}

	/// 이 실행이 사람을 위한 것인가, 하네스를 위한 것인가.
	///
	/// `CoreWindow::IsUnattended()`는 답이 될 수 없다 — 그 값을 세우는
	/// `ConsoleCommandSystem::InitializeFromCommandLine`이 창을 띄운 **뒤**에
	/// 돌기 때문이다. 부팅 예열은 그보다 앞에서 판정해야 하므로 명령줄을
	/// 직접 본다. 목록은 ConsoleCommandSystem의 인자 해석과 같은 다섯이다.
	bool EditorHasAutomationArgument()
	{
		int count{};
		wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &count);
		if (!arguments) return false;
		bool automation = false;
		for (int i = 1; i < count && !automation; ++i)
		{
			const std::wstring_view argument{ arguments[i] };
			automation = argument == L"--script" || argument == L"--exec"
				|| argument == L"--console" || argument == L"--commandlet"
				|| argument == L"--commandlet-script";
		}
		LocalFree(arguments);
		return automation;
	}

	std::filesystem::path ResolveEditorWorkspaceRoot(
		const std::filesystem::path& executableRoot) noexcept
	{
		try
		{
			std::filesystem::path candidate = executableRoot;
			for (int depth = 0; depth < 8 && !candidate.empty(); ++depth)
			{
				std::error_code error{};
				if (std::filesystem::is_directory(candidate / L"Dynamic_CPP", error) &&
					!error && std::filesystem::is_regular_file(
						candidate / L"EngineOutput.props", error) && !error)
				{
					return candidate.lexically_normal();
				}
				const std::filesystem::path parent = candidate.parent_path();
				if (parent == candidate) break;
				candidate = parent;
			}
		}
		catch (...) {}
		return {};
	}

	bool InitializeEditorHostSettings() noexcept
	{
		return EditorSettingsStore::Get().Initialize();
	}

	EngineLaunchConfig MakeEditorLaunchConfig()
	{
		EngineLaunchConfig config{};
		config.compatibilityRunMode = EngineRunMode::Editor;
		config.logSessionName = "Editor";

		const std::filesystem::path executableRoot =
			ResolveProcessExecutableDirectory();
		const std::filesystem::path workspaceRoot =
			ResolveEditorWorkspaceRoot(executableRoot);
		const std::filesystem::path binaryRoot = executableRoot.parent_path();
		// Directory-based authoring is an explicit development adapter. The product
		// --project contract is reserved for validated .creatorproject descriptors (DL1).
		if (!EditorPathArgument(L"--project").empty())
			throw std::runtime_error("Product descriptor startup is not implemented; use --development-project for the development adapter.");
		const auto explicitProject = EditorPathArgument(L"--development-project");
		const std::filesystem::path projectRoot = !explicitProject.empty() ? explicitProject : workspaceRoot.empty()
			? std::filesystem::path{}
			: (workspaceRoot / L"Dynamic_CPP").lexically_normal();
		config.paths.executableRoot = executableRoot;
		config.paths.projectRoot = projectRoot;
		config.paths.runtimeContentRoot = projectRoot;
		config.paths.runtimeDataRoot = (executableRoot / L"Saved").lexically_normal();
		config.paths.assetsRoot = (projectRoot / L"Assets").lexically_normal();
		config.paths.managedRoot = (binaryRoot / L"Managed").lexically_normal();
		const auto explicitManaged = EditorPathArgument(L"--managed-root");
		if (!explicitManaged.empty()) config.paths.managedRoot = explicitManaged;
		if (!explicitProject.empty()) config.paths.runtimeDataRoot = projectRoot / L"Saved" / L"Editor";
		config.paths.engineResourceRoot = (binaryRoot / L"Resources").lexically_normal();
		config.paths.testArtifactRoot = workspaceRoot.empty()
			? (executableRoot / L"Saved" / L"Tests").lexically_normal()
			: (workspaceRoot / L"Artifacts" / L"Tests" / L"Editor").lexically_normal();
		config.paths.enableAssetAuthoring = true;
		config.initializeHostSettings = &InitializeEditorHostSettings;

		config.window.title = L"Creator Editor";
		config.window.clientWidth = 1920;
		config.window.clientHeight = 1080;
		config.window.iconResourceId = IDI_ACADEMY4Q;
		config.window.style = WS_OVERLAPPEDWINDOW;
		config.window.centerOnDesktop = true;
		// 1920x1080 은 이제 **논리** 크기다. 200% 모니터에서는 창도 두 배로
		// 열려야 UI 배율과 아귀가 맞는다.
		config.window.scaleClientToDpi = true;
		config.window.fitNearestMonitor = false;
		config.window.showOnCreate = false;
		config.window.acceptFileDrops = true;
		config.window.messageInterceptor =
			[](HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
				-> std::optional<LRESULT>
		{
			// 셸 크롬이 먼저 본다. 캡션 제거(WM_NCCALCSIZE)는 창 생성 도중
			// 이미 발화하므로 Attach보다 앞서 돌아야 한다 — 그래서 크롬은
			// 저장된 핸들이 아니라 인자로 온 hWnd로 프레임을 계산한다.
			if (const std::optional<LRESULT> handled =
					EditorWindowChrome::Get().HandleWindowMessage(
						hWnd, message, wParam, lParam))
			{
				return handled;
			}

			if (const std::optional<LRESULT> handled =
					ImGuiWin32Cursor::HandleWindowMessage(hWnd, message, wParam, lParam))
			{
				return handled;
			}

			// ImGui Win32 handling belongs to the presentation thread. Preserve the
			// existing main-thread queue instead of touching the ImGui context here.
			WinProcProxy::GetInstance()->PushMessage(hWnd, message, wParam, lParam);
			return std::nullopt;
		};
		return config;
	}
}

MAIN_ENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow)
{
	return EngineBootstrap::Run<Core::App>(hInstance, MakeEditorLaunchConfig());
}

void Core::App::Initialize(CoreWindow& coreWindow)
{

    std::wstring loadingImgPath = PathFinder::IconPath() / L"Loading.bmp";
    g_progressWindow->Launch(ProgressWindowStyle::InitStyle, loadingImgPath);

    // 아래층(셰이더 리로드 등)이 게시하는 진행률을 이 창이 받도록 싱크를 건다.
    // 아래층은 ProgressWindow의 존재를 모른다 — Utility_Framework/ProgressSink.h 참고.
    Progress::GetSink() = Progress::Sink{
        []() { g_progressWindow->Launch(); },
        [](const std::wstring& t) { g_progressWindow->SetTitle(t); },
        [](const std::wstring& t) { g_progressWindow->SetStatusText(t); },
        [](float p) { g_progressWindow->SetProgress(p); },
        []() { g_progressWindow->Close(); },
    };
    BootProgress::Begin(BootProgress::kEditorBootSteps);
    BootProgress::Step(L"Initializing Core...");

	// 덤프 종류 지정과 기록자 등록은 EngineBootstrap::InitializeRuntime이 이미 했다.
	// 여기서 또 부르면 등록 로그가 두 번 찍히고, 무엇보다 '여기가 등록 지점'이라는
	// 오해를 남긴다 — 그 오해 때문에 부팅 전반이 덤프 사각지대였다.
    m_hWnd = coreWindow.GetHandle();
    EditorWindowChrome::Get().Attach(m_hWnd);

	// "Initializing Dx11 Device..." 단계가 여기 있었다 (2026-08-10).
	// DX11 디바이스는 더 이상 만들어지지 않는다 — 씬은 EnhancedSceneRenderer가,
	// 화면 출력은 ImGui DX12 셸이 각자 자기 디바이스를 세운다.

    BootProgress::Step(L"Initializing Windows API...");
    RegisterHandler(coreWindow);
	Load();
	Run();
}

void Core::App::Finalize()
{
	// ★ 단계마다 즉시 찍는다.
	//
	//   종료가 멈추는 자리를 쫓는데 로그가 없으면 어디까지 갔는지조차
	//   알 수 없다. 함수가 끝나야 찍히는 것은 소용이 없다 —
	//   dx12.compare 크래시와 씬 로드 행에서 각각 같은 자리를 겪었다.
	std::printf("[SHUTDOWN] Finalize 진입\n");

	ConsoleCommandSystem::Get().Shutdown();
	std::printf("[SHUTDOWN] CLI Shutdown 반환\n");

	m_main->Finalize();
	std::printf("[SHUTDOWN] EditorMain Finalize 반환\n");

	// 종료 시점에 남아있는 GPU 객체를 디버거 출력에 쏟는다.
	//
	// ★ 타입별 집계(LogCensus)가 여기 있었다 (2026-08-10, DX12 이관).
	//   집계는 디바이스가 살아 있어야 하는데, 바로 위 EditorMain::Finalize가
	//   ShutdownLive를 지나며 라이브 러너의 DX12 디바이스를 파괴한다. DX11
	//   시절에는 이 디바이스가 App 소유라 여기까지 살아 있었지만 지금은 아니다.
	//
	//   그래서 집계를 '디바이스 파괴 직전'으로 옮겼다(EnhancedSceneRenderer.cpp의
	//   파이프라인 해체). 그 자리가 D3D 디버그 레이어가 요구하는 바로 그 지점이고,
	//   여기보다 정확하다.
	//
	//   아래 보고는 프로세스 범위(DXGI 디버그 계층)라 디바이스 없이도 돈다 —
	//   디바이스가 사라진 뒤에도 남아 있는 것이 무엇인지가 오히려 여기서 보인다.
	GpuDiagnostics::ReportLiveObjects();
	std::printf("[SHUTDOWN] LogLiveObjectCensus 반환\n");
	std::printf("[SHUTDOWN] Finalize 완료\n");
}

// ★ SetWindow가 여기 있었다 (2026-08-10).
//
//   하던 일은 둘이었다: 프레젠트 소유권을 정하고(SetPresentOwnedExternally),
//   DX11 DeviceResources에 창을 붙이는 것(SetWindow → 스왑체인 생성).
//
//   D2가 세운 규약("DXGI는 한 HWND에 스왑체인 둘을 허용하지 않으므로, 창을
//   붙이기 전에 누가 소유할지 정해져 있어야 한다")은 경쟁자가 둘일 때의
//   이야기다. DX11이 스왑체인을 만들지 않게 된 지금은 셸이 유일한 소유자라
//   정할 것이 없다 — 규약이 사라진 것이 아니라 지킬 상대가 사라졌다.
//
//   창 자체는 CoreWindow가 소유하고, 필요한 쪽이 GetForCurrentInstance()로
//   묻는다. 디바이스가 창을 들고 있을 이유는 처음부터 없었다.

void Core::App::RegisterHandler(CoreWindow& coreWindow)
{
    coreWindow.RegisterHandler(WM_INPUT,		this, &App::ProcessRawInput);
	coreWindow.RegisterHandler(WM_SIZE,			this, &App::HandleResizeEvent);
	coreWindow.RegisterHandler(WM_SYSKEYDOWN,	this, &App::HandleMaximizeEvent);
    coreWindow.RegisterHandler(WM_KEYDOWN,		this, &App::HandleCharEvent);
    coreWindow.RegisterHandler(WM_CLOSE,		this, &App::Shutdown);
    coreWindow.RegisterHandler(WM_DROPFILES,	this, &App::HandleDropFileEvent);
}

void Core::App::Load()
{
	if (nullptr == m_main)
	{
		m_main = std::make_unique<Editor::EditorMain>();
	}
}

/// 이번 상태를 밀봉해 전용 RenderThread에 발행한다. 돌려주는 것은 이번에
/// 넘긴 뷰의 수다 — 부팅 예열이 "만들 것이 있는가"를 이 수로 판정한다.
uint32_t Core::App::PublishRenderFrame()
{
	// 유일한 씬 렌더러. Update가 GT의 구조 변경과 EndOfFrame을 끝낸 뒤
	// 카메라와 delta batch를 밀봉해 전용 RenderThread에 발행한다.
	//
	// 넘기는 뷰는 **화면에 있는 것만**이다(PHASE 21 W4). 예전에는 재생
	// 여부와 무관하게 둘 다 넘겼는데, 그때는 "지금 무엇이 보이는가" 에
	// 답할 자리가 없었다 — Scene 과 Game 이 창 둘이라 어느 쪽이 앞 탭인지
	// 제작자가 알 수 없었기 때문이다. 가운데 Host 가 모드를 들고부터는
	// 그 물음에 답이 있으므로, 보이지 않는 타깃의 그림을 만들지 않는다.
	// 카메라별 표시 슬롯은 TickLive 쪽이 관리한다(MultiCameraRenderPlan.md).
	// camera.editor follow on — 두 뷰의 시점을 통일해 두는 대조 실험용.
	// 카메라 목록을 만들기 전에 적용해야 이번 프레임 밀봉에 반영된다.
	if (ConsoleCommandSystem::IsEditorCameraFollowing())
	{
		ConsoleCommandSystem::MatchEditorCameraToGameCamera();
	}

	// 씬 오버레이(저작 보조) 뷰 선언은 Host 몫이다(E4-5) — 에디터 카메라는
	// Editor 세션이 소유하고, Core는 뷰 요청에 실린 값만 안다.
	EnhancedLiveViewRequest views[EnhancedSceneRenderer::kMaxLiveCameraViews]{};
	uint32_t viewCount = 0;
	// UI 가 한 번도 게시하지 않았으면(첫 프레임, 헤드리스) 둘 다 만든다 —
	// 수요를 모르는 것과 수요가 없는 것은 다르다.
	const ::editor::windows::viewport_demand demand =
		::editor::windows::read_viewport_demand();
	const bool editorDemanded = !demand.hostPresent || demand.editorTarget;
	const bool gameDemanded = !demand.hostPresent || demand.gameTarget;

	// 카메라를 **먼저** 집는다. 누계가 재야 하는 것은 "만들 수 있었는데
	// 수요가 없어 만들지 않았다" 이지 "못 만들었다" 가 아니다. 둘을 섞으면
	// 게임 카메라가 없는 씬에서 수요 문을 통째로 걷어도 수가 그대로여서,
	// 그 수를 읽는 게이트가 씬이 무엇을 담고 있느냐에 기대게 된다.
	Camera* const editorCamera = EditorSessionState::Get().EditorCamera();
	if (editorDemanded && nullptr != editorCamera)
	{
		views[viewCount++] = {
			{ kEnhancedEditorViewId, 1 },
			editorCamera->CaptureFrameSnapshot(),
			EnhancedLiveDisplayTarget::Editor,
			EnhancedLiveViewFlags::SceneOverlay |
				EnhancedLiveViewFlags::CanvasPreview };
	}
	Scene* activeScene = SceneManagers->GetActiveScene();
	CameraComponent* const gameCamera = (nullptr != activeScene)
		? activeScene->Cameras().GetPrimaryCamera() : nullptr;
	if (gameDemanded && nullptr != gameCamera)
	{
		views[viewCount++] = {
			{ kEnhancedGameViewId,
				static_cast<uint64_t>(gameCamera->GetInstanceID()) },
			gameCamera->CaptureFrameSnapshot(),
			EnhancedLiveDisplayTarget::Game,
			EnhancedLiveViewFlags::ScreenSpaceUI };
	}
	// M6-P2d-d: 실제 Scene component가 소유한 Material에서 pass별
	// ShaderMeta GUID를 선언한다. DataSystem cache에 없는 복제/런타임
	// Material도 이 owner snapshot에 포함되며 RenderEngine은 Water/Wind
	// 같은 대표 파일 이름을 알 필요가 없다.
	::editor::windows::note_view_submission(
		!editorDemanded && nullptr != editorCamera,
		!gameDemanded && nullptr != gameCamera);
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

	return viewCount;
}

/// 첫 그림이 설 때까지 프레임을 돌린다. 창을 보여주기 전에만 부른다.
void Core::App::WarmUpFirstRenderedFrame()
{
	if (EditorHasAutomationArgument()) return;

	BootProgress::Step(L"Preparing renderer...");

	// 상한은 '예열이 실패해도 에디터는 뜬다'를 지키는 자다. 넘기면 예열
	// 없이 예전과 같은 상태로 창을 띄운다 — 검정 씬뷰가 잠시 보일 뿐
	// 부팅이 막히지는 않는다.
	constexpr auto kWarmUpLimit = std::chrono::seconds(60);
	const auto deadline = std::chrono::steady_clock::now() + kWarmUpLimit;

	while (std::chrono::steady_clock::now() < deadline)
	{
		DataSystems->DrainQueuedAssetChanges();
		m_main->Update();

		// 넘길 뷰가 없으면 만들 그림도 없다. 여기서 기다리면 상한까지
		// 헛돈다 — 카메라 없는 씬을 여는 실행이 그렇다.
		if (0 == PublishRenderFrame()) return;

		if (0 != EnhancedSceneRenderer::GetLiveDisplayTexture(
				EnhancedLiveDisplayTarget::Editor).textureId)
		{
			return;
		}

		// RT가 완료한 슬롯을 표시로 승격하는 것은 다음 TickLive다. 잠깐
		// 물러나 그 진행을 기다린다 — 붙어서 발행하면 큐만 덮어쓴다.
		std::this_thread::sleep_for(std::chrono::milliseconds(4));
	}

	Debug->LogWarning("[Boot] 렌더러 예열이 상한을 넘었다 — 예열 없이 창을 띄운다");
}

void Core::App::Run()
{
	CoreWindow::GetForCurrentInstance()->InitializeTask([&]
	{
		m_main->Initialize();
		BootProgress::Step(L"Initializing Input...");
        InputManagement->Initialize(m_hWnd);
		//InputActionManagers->LoadManager();
		// ★ 첫 라이브 프레임은 로딩 화면 **뒤**에서 돌린다 (2026-09-14).
		//
		//   렌더 파이프라인은 GT가 첫 frame packet을 발행해야 서고, 그
		//   구축(패스 초기화·ShaderMeta 적용·재질 밀봉)이 실측 16.5초다.
		//   프레임 루프는 Show() 뒤에 시작하므로 그 16.5초가 통째로 '창은
		//   떴는데 씬뷰가 검정'인 구간이었다 — 부팅이 짧아질수록 이 구간이
		//   길게 드러난다(PHASE 21 W3이 창 생성을 첫 프레임으로 옮긴 뒤의
		//   체감이 그것이다).
		//
		//   여기서 미리 돌린다. 기다리는 조건은 '첫 그림이 실제로 섰는가'
		//   (표시 텍스처 != 0)이고, 만들 뷰가 없으면(에디터 카메라 없음)
		//   기다릴 것도 없으므로 즉시 빠진다. 하네스 실행은 예열하지
		//   않는다 — 명령 하나 돌리고 끝내는 실행에 16초를 물리면 게이트
		//   타임아웃이 통째로 흔들린다.
		WarmUpFirstRenderedFrame();

		BootProgress::Complete();

		// 로딩창을 완전히 닫은 뒤(스레드 join까지) 에디터 창을 보여준다.
		// 반대 순서로 하면 서로 다른 스레드의 두 창 사이에서 활성화 전환이
		// 일어나며 동기 SendMessage 교착이 비결정적으로 발생했다.
		g_progressWindow->Close();
		CoreWindow::GetForCurrentInstance()->Show();

		// 초기화가 끝난 뒤 CLI를 연다. 그래야 명령이 완성된 엔진 위에서 실행된다.
		ConsoleCommandSystem::Get().InitializeFromCommandLine();
	})
	.Then([&]
	{
		// Watcher I/O thread가 게시한 asset 변경은 GT 프레임 경계에서만 적용한다.
		// 이 뒤 Update와 frame packet 밀봉은 같은 ShaderMeta generation을 본다.
		DataSystems->DrainQueuedAssetChanges();

		// 메인 루프
		m_main->Update();

		// 콘솔/스크립트 명령은 프레임 경계에서만 실행한다(게임 스레드 규약).
		auto& cli = ConsoleCommandSystem::Get();
		cli.Pump();

		PublishRenderFrame();

		if (cli.IsQuitRequested())
		{
			Debug->LogDebug("[SHUTDOWN] CLI quit 요청 — 종료 시작");
			m_windowClosed = true;
			PostQuitMessage(0);
		}
	});
}

LRESULT Core::App::Shutdown(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
	// 종료의 발원지를 로그에 남긴다. 부팅 리팩토링 검증 중 "누가 종료를
	// 시작했나"를 몰라 한참 헤맸다 — WM_CLOSE 수신과 CLI quit을 구분한다.
	Debug->LogDebug("[SHUTDOWN] WM_CLOSE 수신 — 종료 시작");
	m_windowClosed = true;
	PostQuitMessage(0);
	return 0;
}

LRESULT Core::App::ProcessRawInput(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
	//InputManagement->ProcessRawInput(lParam); *****

	return 0;
}

LRESULT Core::App::ImGuiKeyDownHandler(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
	ImGuiIO& io = ImGui::GetIO();
	ImGuiKey key = ImGuiKey(wParam);

	if (key >= 0 && key < ImGuiKey_COUNT)
	{
		io.AddKeyEvent(key, true);
	}

	return 0;
}

LRESULT Core::App::ImGuiKeyUpHandler(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
	ImGuiIO& io = ImGui::GetIO();
	ImGuiKey key = ImGuiKey(wParam);

	if (key >= 0 && key < ImGuiKey_COUNT)
	{
		io.AddKeyEvent(key, false);
	}


	return 0;
}

LRESULT Core::App::HandleCharEvent(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
	ImGuiIO& io = ImGui::GetIO();

	wchar_t wch = 0;
	static BYTE KeyState[256];
	GetKeyboardState(KeyState);
	// Virtual Key를 Unicode 문자로 변환
	if (ToUnicode((UINT)wParam, (UINT)lParam, KeyState, &wch, 1, 0) > 0)
	{
		io.AddInputCharacter(wch);
	}

	return 0;
}

LRESULT Core::App::HandleResizeEvent(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
	if (wParam == SIZE_MINIMIZED)
	{
		m_isMinimized = true;
		return 0; // 최소화된 경우 무시
	}

	if (m_isMinimized)
	{
		if (wParam == SIZE_RESTORED || wParam == SIZE_MAXIMIZED)
		{
			m_isMinimized = false;
			return 0; // 복원된 경우 무시
		}
	}

	m_main->InvokeResizeFlag();

	return 0;
}

LRESULT Core::App::HandleMaximizeEvent(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
	const bool altDown = (lParam & (1 << 29)) != 0; // KF_ALTDOWN
	if (wParam == VK_RETURN && altDown)
	{
		m_main->InvokeResizeFlag();
		return 0;           // 여기서 0을 반환하면 아래의 삑(Beep) 방지에 도움
	}
	
	return 0;
}

LRESULT Core::App::HandleSettingWindowEvent(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
	WNDCLASS wcSetting = { sizeof(WNDCLASS) };
	wcSetting.lpfnWndProc = CoreWindow::WndProc;

	return 0;
}

LRESULT Core::App::HandleDropFileEvent(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
	// 드래그 앤 드롭 이벤트 처리
	HDROP hDrop = (HDROP)wParam;
	UINT nFiles = DragQueryFile(hDrop, 0xFFFFFFFF, NULL, 0);

	if (nFiles > 0)
	{
		std::vector<wchar_t> fileName(MAX_PATH);
		for (UINT i = 0; i < nFiles; ++i)
		{
			DragQueryFile(hDrop, i, fileName.data(), MAX_PATH);
			file::path filePath(fileName.data());
			// 파일 경로 처리
			if(".fbx" == filePath.extension() || ".gltf" == filePath.extension() ||
			   ".glb" == filePath.extension() || ".obj" == filePath.extension())
			{
				const file::path imported = EditorAssetDatabase::Get().ImportSourceAsset(
					filePath, EditorAssetDatabase::ImportKind::Model);
				if (!imported.empty()) DataSystems->LoadModelAssetGenerationByPath(imported.string());
			}
			else if (
				".png" == filePath.extension() || 
				".dds" == filePath.extension() || 
				".jpg" == filePath.extension() ||
				".hdr" == filePath.extension()
			)
			{
				EditorAssetPresentation::Get().QueueTextureImport(filePath);
			}
            // .dmp 드롭 처리(ADS의 GitHash로 옛 저장소 커밋 URL을 열던 기능)는
            // 2026-08-24 버전 체계 전환으로 은퇴했다 — 죽은 저장소(LastProject)를
            // 가리키던 데다, 버전이 git SHA 자동 주입에서 수동 정의로 바뀌었다.
		}
	}

	return 0;
}

