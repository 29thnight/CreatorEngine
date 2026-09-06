#include "EditorMain.h"
#include "ReflectionUndo.h"
#include "CoreWindow.h"
#include "BootProgress.h"
#include "Render/Scene/EnhancedSceneRenderer.h"
#include "RHI/IImGuiHost.h"
#include "RHI/ImGuiHostPresentationSink.h"
#include "RHI/ScreenSizedResource.h"
#include "InputManager.h"
#include "ImGuiRegister.h"
#include "Physx.h"
#include "SoundManager.h"
#include "TimeSystem.h"
#include "DataSystem.h"
#include "SceneManager.h"
// 시뮬레이션 프레임의 단일 소유자(E3-7) — Player와 같은 순서를 탄다.
#include "RuntimeFrame.h"
#include "ClrHost.h"
#include "RuntimeSettings.h"
#include "EditorSettingsStore.h"
#include "EditorSessionState.h"
#include "EditorPlatform.h"
#include "EditorAssetDatabase.h"
#include "EditorAssetPresentation.h"
#include "EditorModelPlacement.h"
#include "EditorSceneOverlayContributor.h"
#include "UIManager.h"
#include "Profiler.h"
#include "WinProcProxy.h"
#include "TagManager.h"
#include "Entity.h"
#include "Scene.h"
// OpenFile 재정의 훅이 쓴다 (PHASE 4-3)
#include "PrefabEditor.h"
#include "PathFinder.h"
#include "CameraComponent.h"
#include "LightComponent.h"
#include "imgui.h"
#include "imgui_impl_win32.h"

#include <sstream>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <stdexcept>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace
{
	/// 창 핸들. 디바이스가 아니라 창이 창을 안다 — 구 코드는 이것을
	/// DeviceResources로 물었고, 그것이 그 클래스를 붙들던 마지막 이유였다.
	HWND EditorWindowHandle()
	{
		auto* window = CoreWindow::GetForCurrentInstance();
		return (nullptr == window) ? nullptr : window->GetHandle();
	}

	// 표시 sink 어댑터는 HostImGuiPresentation의 공용 타입을 쓴다(E4-6c) —
	// E4-6a 때 여기 있던 ~20줄이 Player 쪽 중복과 함께 그리로 합쳐졌다.
}

Editor::EditorMain::EditorMain()
{
	Core::TimeSystem::GetInstance();
}

Editor::EditorMain::~EditorMain()
{
	Core::TimeSystem::Destroy();
}

void Editor::EditorMain::Initialize()
{
	PROFILER_INITIALIZE(5, 1024);
	PROFILE_REGISTER_THREAD("[GameThread]");

	// Undo 수명은 에디터가 소유한다(E1-6 완결). 공통 bootstrap과 Player는
	// 이 싱글턴을 링크하지 않는다 — 옛 inline 전역이 정적 초기화 때 Player
	// 에서도 인스턴스를 만들던 것을 걷은 자리다.
	Meta::UndoSystemInitialize();

	BootProgress::Step(L"Initializing RenderEngine...");

	// 옥트리 컬링 초기화가 여기 있었다 — 계통 전체를 걷었다
	// (RenderSceneViewPlan ③, MeshRenderer::Awake의 주석 참고).

	BootProgress::Step(L"Creating Renderers...");

	// 화면 크기 버스의 첫 값. 이후 리사이즈는 HandleWindowResize가 같은 창에서
	// 직접 읽어 알린다 — 첫 값만 DX11 출력 크기를 거치고 있었고 그것이 D4의
	// 마지막 고리였다.
	{
		RECT clientRect{};
		GetClientRect(EditorWindowHandle(), &clientRect);
		ScreenResizeBus::Get().SetSize(
			static_cast<uint32_t>(clientRect.right - clientRect.left),
			static_cast<uint32_t>(clientRect.bottom - clientRect.top));
	}


	// 씬 오버레이(그리드·기즈모 체인)는 Editor가 파이프라인 조립에 기여한다.
	// 파이프라인은 렌더 스레드에서 서므로 렌더러 초기화(렌더 스레드 기동) 전에
	// 설치해야 첫 조립부터 오버레이가 실린다(E4-2). Player는 설치하지 않는다.
	EnhancedSceneRenderer::SetRenderFeatureContributor(
		std::make_shared<EditorSceneOverlayContributor>());

	// 표시 sink도 같은 시점에 설치한다(E4-6a) — RT가 첫 리드백 프레임을
	// 게시하기 전에 있어야 한다. 셸이 아직 Initialize 전이어도 위임은
	// 안전하다(비활성 셸은 no-op/0).
	EnhancedSceneRenderer::SetDisplayPresentationSink(
		std::make_shared<ImGuiHostPresentationSink>());

	std::string enhancedError;
	const EnhancedLiveBackend startupBackend =
		RenderBackend::Vulkan == RuntimeSettings::Get().GetRenderBackend()
		? EnhancedLiveBackend::Vulkan : EnhancedLiveBackend::DX12;
	if (!EnhancedSceneRenderer::InitializeRuntime(startupBackend, enhancedError))
	{
		throw std::runtime_error(enhancedError);
	}
	SceneManagers->SetRenderScene(EnhancedSceneRenderer::GetRenderScene());
	EnhancedSceneRenderer::SetActiveScene(SceneManagers->GetActiveScene());

	// 에디터 씬 뷰 카메라는 Editor 세션이 소유한다(E4-5). Core의
	// InitializeRuntime은 카메라를 만들지 않고, 씬 오버레이 뷰 판정도 Host가
	// 뷰 요청(EnhancedLiveViewRequest)에 선언한다. avoid 플래그는 Core가
	// 만들던 시절의 값 그대로다.
	{
		auto cameraRig = std::make_unique<EditorCameraRig>();
		EditorSessionState::Get().SetCameraRig(std::move(cameraRig));
	}

	// 기본 씬 저작 정책은 렌더 패스 소유권과 분리해 엔트리 계층에 둔다.
	// EnhancedRenderer의 이벤트는 같은 시점에 RenderScene만 갱신한다.
	m_newSceneCreatedHandle = newSceneCreatedEvent.AddLambda([]()
	{
		Scene* scene = SceneManagers->GetActiveScene();
		if (nullptr == scene) return;

		EnhancedSceneRenderer::SetActiveScene(scene);

		auto* mainCamera = scene->CreateEntity("Main Camera", GameObjectType::Camera)
			->AddComponent<CameraComponent>();
		mainCamera->SetPrimary(true);
		auto lightObject =
			scene->CreateEntity("Directional Light", GameObjectType::Light);
		lightObject->SetTag("MainCamera");
		auto light = lightObject->AddComponent<LightComponent>();
		light->m_lightStatus = LightStatus::StaticShadows;
	});
	m_activeSceneChangedHandle = activeSceneChangedEvent.AddLambda([]()
	{
		EnhancedSceneRenderer::SetActiveScene(SceneManagers->GetActiveScene());
	});

	// 호스트(IImGuiHost → DX12/Vulkan backend)가 여기서 선다. 구 ImGuiRenderer는 HWND
	// 하나 때문에 DX11 DeviceResources를 통째로 들었다 — 이제 핸들만 넘긴다.
	m_editorRenderer = std::make_unique<EditorRenderer>(EditorWindowHandle());
	const bool imguiIsVulkan = ImGuiRendererBackendKind::Vulkan ==
		GetImGuiHost().GetBackendKind();
	if ((EnhancedLiveBackend::Vulkan == startupBackend) != imguiIsVulkan)
		throw std::runtime_error("Editor scene/ImGui backend 설정 불일치");
	std::printf("[RenderBackend] source=render.backend active=%s scene=%s imgui=%s\n",
		RenderBackendName(RuntimeSettings::Get().GetRenderBackend()),
		EnhancedLiveBackend::Vulkan == startupBackend ? "vulkan" : "dx12",
		GetImGuiHost().GetBackendName());

	m_gizmoRenderer = std::make_shared<GizmoRenderer>(
		EnhancedSceneRenderer::GetRenderScene(),
		EditorSessionState::Get().EditorCamera());
	m_sceneViewWindow = std::make_unique<SceneViewWindow>(
		EditorSessionState::Get().CameraRig(), m_gizmoRenderer.get());
	m_menuBarWindow = std::make_unique<MenuBarWindow>();
	m_gameViewWindow = std::make_unique<GameViewWindow>();
	m_hierarchyWindow = std::make_unique<HierarchyWindow>();
	m_inspectorWindow = std::make_unique<InspectorWindow>();
	m_projectWindow = std::make_unique<AssetBundleWindow>();
	m_resourceCounterWindow = std::make_unique<ResourceCounterWindow>();
	m_renderDebugWindow = std::make_unique<EnhancedRenderDebugWindow>();

	BootProgress::Step(L"Initializing SoundManager...");
	Sound->initialize(128);

	BootProgress::Step(L"Loading Assets...");

	// 확장자별 open 정책과 source asset watcher는 Editor Host 소유다.
	EditorPlatform::Get().SetOpenFileOverride([](const file::path& filepath) -> bool
	{
		if (filepath.extension() == ".prefab")
		{
			PrefabEditors->Open(filepath.string());
			return true;
		}
		return false;
	});

	DataSystems->Initialize();
	if (!EditorAssetDatabase::Get().Initialize())
		throw std::runtime_error("Editor asset database initialization failed");
	EditorAssetPresentation::Get().Initialize();

	// 콘텐츠 브라우저는 presentation이 아이콘·폰트를 올린 뒤 만든다.
	m_contentsBrowserWindow = std::make_unique<ContentsBrowserWindow>();

	// ★ 태그·레이어 저작은 asset database가 authoring handler를 설치한 뒤에만
	//   가능하다. 예전에는 이 호출이 부팅 훨씬 앞(렌더러 초기화 직전)에 있었는데,
	//   그 자리에서 Save를 부르면 handler가 아직 없어 첫 실행 기본값이 조용히
	//   사라진다. 아래 CreateScene이 태그를 읽는 첫 지점이므로 그 사이가
	//   유일하게 안전한 자리다.
	TagManagers->Initialize();

	BootProgress::Step(L"Loading Project...");
	SceneManagers->CreateScene();

	BootProgress::Step(L"Registering Frame Events...");

	// 재생 전환에서 Editor만 하는 일을 여기서 건다. 델리게이트 구독뿐이라 씬보다
	// 앞이든 뒤든 무방하지만, 첫 재생 전환보다는 반드시 앞이어야 한다.
	m_playModeController.Initialize();

	m_inputEventHandle = InputEvent.AddLambda([](float)
	{
		const bool isPressedCtrl =
			InputManagement->IsKeyPressed((uint32)KeyBoard::LeftControl);
		if (isPressedCtrl && InputManagement->IsKeyDown('Z'))
		{
			Meta::UndoManager::GetInstance()->Undo();
		}
		if (isPressedCtrl && InputManagement->IsKeyDown('Y'))
		{
			Meta::UndoManager::GetInstance()->Redo();
		}

		UIManagers->Update();
		Sound->update();
	});
	BootProgress::Step(L"Initializing Managers...");
	SceneManagers->ManagerInitialize();
	PhysicsManagers->Initialize();

	// CoreCLR 스크립트 계층. 렌더 스레드를 띄우기 전에 올려둔다.
	// 관리 어셈블리가 없으면 조용히 비활성 상태로 남고 엔진은 그대로 동작한다.
	ClrHost::Get().Initialize();
	Editor::ModelPlacement::Get().Initialize();

	PROFILE_FRAME();
	StartPresentationThread();
}

void Editor::EditorMain::StartPresentationThread()
{
	m_presentationThreadTestDelayMs = 0;
	char* delay = nullptr;
	size_t delayLength = 0;
	if (0 == _dupenv_s(&delay, &delayLength,
		"CREATOR_PRESENTATION_THREAD_TEST_DELAY_MS") && nullptr != delay)
	{
		m_presentationThreadTestDelayMs = static_cast<uint32_t>((std::min)(250,
			(std::max)(0, std::atoi(delay))));
		std::free(delay);
	}

	{
		std::lock_guard<std::mutex> lock(m_presentationMutex);
		m_presentationThreadStarted = false;
		m_presentationThreadStartFailed = false;
		m_presentationStopRequested = false;
		m_requestedPresentationFrameId = 0;
		m_consumedPresentationFrameId = 0;
		m_presentationRequests = 0;
		m_presentationFrames = 0;
		m_presentationLatestWins = 0;
		m_presentationShutdownDiscarded = 0;
	}

	m_presentationThread = std::thread([this] { PresentationThreadMain(); });

	std::unique_lock<std::mutex> lock(m_presentationMutex);
	m_presentationWake.wait(lock, [this] { return m_presentationThreadStarted; });
	if (!m_presentationThreadStartFailed) return;

	lock.unlock();
	if (m_presentationThread.joinable()) m_presentationThread.join();
	throw std::runtime_error("Editor PresentationThread COM 초기화 실패");
}

void Editor::EditorMain::StopPresentationThread()
{
	{
		std::lock_guard<std::mutex> lock(m_presentationMutex);
		if (m_requestedPresentationFrameId > m_consumedPresentationFrameId)
		{
			m_consumedPresentationFrameId = m_requestedPresentationFrameId;
			++m_presentationShutdownDiscarded;
		}
		m_presentationStopRequested = true;
	}
	m_presentationWake.notify_all();
	if (!m_presentationThread.joinable()) return;

	m_presentationThread.join();

	std::lock_guard<std::mutex> lock(m_presentationMutex);
	const uint64_t pending =
		m_requestedPresentationFrameId > m_consumedPresentationFrameId ? 1ull : 0ull;
	const bool balanced = m_presentationRequests ==
		m_presentationFrames + m_presentationLatestWins +
		m_presentationShutdownDiscarded + pending;
	std::printf("[PresentationThread] shutdown — request %llu / present %llu"
		" / latest-wins %llu / shutdown-discard %llu / pending %llu / balanced %u\n",
		static_cast<unsigned long long>(m_presentationRequests),
		static_cast<unsigned long long>(m_presentationFrames),
		static_cast<unsigned long long>(m_presentationLatestWins),
		static_cast<unsigned long long>(m_presentationShutdownDiscarded),
		static_cast<unsigned long long>(pending), balanced ? 1u : 0u);
}

void Editor::EditorMain::PresentationThreadMain()
{
	const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	{
		std::lock_guard<std::mutex> lock(m_presentationMutex);
		m_presentationThreadStartFailed = FAILED(comResult);
		m_presentationThreadStarted = true;
	}
	m_presentationWake.notify_all();
	if (FAILED(comResult)) return;

	SetThreadDescription(GetCurrentThread(), L"PresentationThread");
	for (;;)
	{
		bool hasFrameRequest = false;
		{
			std::unique_lock<std::mutex> lock(m_presentationMutex);
			m_presentationWake.wait(lock, [this]
			{
				return m_presentationStopRequested ||
					m_isInvokeResize.load(std::memory_order_acquire) ||
					m_requestedPresentationFrameId > m_consumedPresentationFrameId;
			});
			if (m_presentationStopRequested) break;

			hasFrameRequest =
				m_requestedPresentationFrameId > m_consumedPresentationFrameId;
			if (hasFrameRequest)
				m_consumedPresentationFrameId = m_requestedPresentationFrameId;
		}

		while (!WinProcProxy::GetInstance()->IsEmpty())
		{
			auto [hwnd, message, wParam, lParam] =
				WinProcProxy::GetInstance()->PopMessage();
			ImGui_ImplWin32_WndProcHandler(hwnd, message, wParam, lParam);
		}

		if (m_isInvokeResize.exchange(false, std::memory_order_acq_rel))
			HandleWindowResize();

		if (0 != m_presentationThreadTestDelayMs)
		{
			std::this_thread::sleep_for(
				std::chrono::milliseconds(m_presentationThreadTestDelayMs));
		}

		// UI는 살아 있는 씬 객체를 읽을 수 있다. GT의 파괴/교체 구간과만
		// 상호 배제하고 프레임 진행 자체는 서로 기다리지 않는다.
		{
			std::lock_guard<std::mutex> sceneLock(m_sceneStructureMutex);
			PresentFrame();
		}

		if (hasFrameRequest)
		{
			std::lock_guard<std::mutex> lock(m_presentationMutex);
			++m_presentationFrames;
		}
	}

	CoUninitialize();
}

void Editor::EditorMain::NotifyRenderFramePublished(uint64_t frameId)
{
	{
		std::lock_guard<std::mutex> lock(m_presentationMutex);
		if (m_presentationStopRequested ||
			frameId <= m_requestedPresentationFrameId)
			return;

		++m_presentationRequests;
		if (m_requestedPresentationFrameId > m_consumedPresentationFrameId)
			++m_presentationLatestWins;
		m_requestedPresentationFrameId = frameId;
	}
	m_presentationWake.notify_one();
}

void Editor::EditorMain::Finalize()
{
	// 표시/렌더 소비자를 세우기 전에 관리 측을 먼저 정리한다.
	// 스크립트가 들고 있던 핸들이 남아 있으면 이후 파괴 순서가 꼬인다.
	// ★ 단계마다 즉시 찍는다. 종료가 멈추는 자리를 찾는 데 로그가
	//   없으면 어디까지 갔는지조차 알 수 없다.
	std::printf("[SHUTDOWN] ClrHost 진입\n");
	ClrHost::Get().Shutdown();
	std::printf("[SHUTDOWN] ClrHost 반환\n");

	// 표시 소비자를 먼저 세운다. GT는 이미 메인 루프를 빠져나와 새 frame을
	// 발행하지 않고, condition variable이 배리어 없이 대기 중인 스레드를 깨운다.
	StopPresentationThread();
	std::printf("[SHUTDOWN] PresentationThread join 반환\n");
	Editor::ModelPlacement::Get().Shutdown();
	EditorAssetPresentation::Get().Shutdown();
	std::printf("[SHUTDOWN] EditorAssetPresentation 반환\n");

	// ★ 태그·레이어 저장은 asset database가 handler를 걷기 전에 끝내야 한다.
	//   예전에는 이 호출이 아래 RenderThread drain 뒤에 있었는데, 그 자리는
	//   handler가 이미 해제된 뒤라 종료 시 저장이 통째로 사라진다.
	TagManagers->Finalize();
	std::printf("[SHUTDOWN] TagManagers 반환\n");

	// SceneManager가 살아 있는 동안 구독을 걷는다.
	m_playModeController.Shutdown();

	EditorAssetDatabase::Get().Shutdown();
	std::printf("[SHUTDOWN] EditorAssetDatabase 반환\n");

	// 전용 RenderThread의 bounded queue를 여기서 완전히 drain한다. 아래
	// Decommissioning은 RenderScene::Finalize를 호출하므로 순서가 뒤집히면 RT가
	// 파괴 중인 proxy map을 읽게 된다.
	EnhancedSceneRenderer::StopLiveRenderThread();
	std::printf("[SHUTDOWN] RenderThread drain 반환\n");

	// 기여자 해제는 렌더 스레드가 멎은 뒤가 안전하다 — 더 이상 조립이 없다.
	// 살아 있는 파이프라인의 기여 노드는 자기 패스 묶음을 붙들므로 무관하다.
	EnhancedSceneRenderer::SetRenderFeatureContributor({});
	EnhancedSceneRenderer::SetDisplayPresentationSink({});

	// packet 생산이 끝난 뒤 Editor 소유 rig를 놓는다. RenderCore나 게임 카메라
	// registry와 공유 소유가 없으므로 별도 unregister/종료 순서가 필요 없다.
	EditorSessionState::Get().SetCameraRig({});

	// 여기서부터는 표시/렌더 소비 스레드가 없다. 이제 해체해도 안전하다.
	SceneManagers->Decommissioning();
	std::printf("[SHUTDOWN] SceneManagers 반환\n");

	EditorSettingsStore::Get().Save();
	std::printf("[SHUTDOWN] EditorSettingsStore::Save 반환\n");

	// 메인 DX12 렌더러의 최종 정리. 렌더 스레드 join 뒤여야 공유 SRV와
	// 런타임 RenderScene을 안전하게 놓을 수 있다. 최종 GPU 집계도 이 안에서
	// 난다 — 디바이스 파괴 직전이 그 집계의 유일하게 옳은 자리다.
	EnhancedSceneRenderer::ShutdownLive();
	SceneManagers->SetRenderScene(nullptr);
	std::printf("[SHUTDOWN] EnhancedRenderer 반환\n");


	OnResizeReleaseEvent.Clear();
	OnResizeEvent.Clear();

	// Undo 스택을 리플렉션·씬 정리(공통 bootstrap의 SHUTDOWN_STEP들)보다
	// 먼저 비운다 — 명령이 리플렉션 Property를 참조하므로 옛 순서(등록
	// 정리 뒤 파괴)보다 이쪽이 안전하다.
	Meta::UndoSystemFinalize();
	PROFILER_SHUTDOWN();
}

void Editor::EditorMain::HandleWindowResize()
{
	// 해제 → 크기 통지 두 단계다.
	//
	// ★ 예전에는 첫 단계가 DX11 스왑체인 해제였다. 백버퍼를 참조하는 뷰가
	//   하나라도 살아 있으면 DX11 리사이즈가 실패해서, 만들기 전에 전부 놓아야
	//   했다. 그 스왑체인은 D2에서 셸로 넘어갔고 DeviceResources는 오늘
	//   사라졌지만, 두 단계 구조는 남는다 — 화면 크기를 따라가는 DX12
	//   텍스처들이 같은 규약을 쓴다.
	OnResizeReleaseEvent();
	ScreenResizeBus::Get().BroadcastRelease();

	RECT rect{};
	GetClientRect(EditorWindowHandle(), &rect);
	const float width = static_cast<float>(rect.right - rect.left);
	const float height = static_cast<float>(rect.bottom - rect.top);

	OnResizeEvent(width, height);
	ScreenResizeBus::Get().BroadcastResize(
		static_cast<uint32_t>(width), static_cast<uint32_t>(height));
}

void Editor::EditorMain::Update()
{
	PROFILE_CPU_BEGIN("GameLogic");
	Time->Tick([this]
	{
		m_frameDeltaTime = Runtime::ResolveFrameDelta();

		UpdateTitleBar();
		InputManagement->Update(m_frameDeltaTime);

		if (!SceneManagers->IsGameStart())
		{
			// 편집 모드 — 런타임에는 없는 상태라 Runtime primitive에도 없다.
			SceneManagers->Editor();
			SceneManagers->InputEvents(m_frameDeltaTime);

			// delta 0을 **명시적으로** 넘긴다. 편집 모드는 일시정지가 아니지만
			// 시간도 진행하지 않는 제3의 상태다. 예전에는 GameLogic()의 기본
			// 인자로 0이 조용히 들어갔는데, 그러면 "delta 0은 일시정지에서만"이라는
			// 규약이 깨지고 있는지 호출부만 봐서는 알 수 없다.
			SceneManagers->GameLogic(0.0f);

			// 편집 모드에서는 스크립트를 돌리지 않는다(Unity와 같은 규약).
			// 붙여 둔 스크립트는 보류 큐에 쌓였다가 재생 시작 시 한꺼번에 Awake된다.
			return;
		}

		// 에디터 씬 상태 머신(선택·프리뷰). 시뮬레이션이 아니라 여기 남는다.
		SceneManagers->Editor();

		// 재생 중 순서는 Runtime이 소유한다(E3-7). Player가 타는 것과 같은 코드다.
		Runtime::TickSimulationFrame(m_frameDeltaTime);
	});

	if (InputManagement->IsKeyReleased(VK_F5))
	{
		EditorSessionState::Get().ToggleGameViewHidden();
	}

	if (ImGui::IsKeyPressed(ImGuiKey_LeftCtrl) && ImGui::IsKeyPressed(ImGuiKey_W))
	{
		m_gizmoRenderer->SetWireFrame();
	}

	if (InputManagement->IsKeyReleased(VK_F9))
	{
		Physics->ConnectPVD();
	}
	PROFILE_CPU_END();

	// 전용 RenderThread는 이전에 밀봉된 packet/delta만 소비하므로 여기서 세울
	// 필요가 없다. PresentationThread의 UI가 살아 있는 씬 객체를 읽는 구간과만
	// 좁게 직렬화하고, 씬 전환과 파괴를 끝낸 뒤 호출자가 새 packet을 발행한다.
	{
		std::lock_guard<std::mutex> sceneLock(m_sceneStructureMutex);
		Editor::ModelPlacement::Get().Tick();
		SceneManagers->ApplyPendingSceneStructureChange();

		// OnRender도 게임 상태를 진행시키는 코루틴 단계다. 다른 coroutine queue와
		// 동시에 만지지 않도록 GT에서 실행하고, 결과를 이번 packet에 포함한다.
		CoroutineManagers->yield_OnRender();

		PROFILE_CPU_BEGIN("EndOfFrame");
		SceneManagers->DisableOrEnable();
		SceneManagers->EndOfFrame();
		PROFILE_CPU_END();
	}

	PROFILE_FRAME();

	if (SceneManagers->IsDecommissioning())
	{
		PostMessage(EditorWindowHandle(), WM_CLOSE, 0, 0);
	}
}

void Editor::EditorMain::PresentFrame()
{
	// GPU scene 기록은 전용 RenderThread가 끝냈다. 이 스레드는 완성된 display
	// snapshot을 ImGui 셸에 표시할 뿐 SceneRendering/Gizmo 이벤트를 실행하지 않는다.
	OnGui();
}

void Editor::EditorMain::UpdateTitleBar()
{
	const wchar_t* rendererBackend =
		RenderBackend::Vulkan == RuntimeSettings::Get().GetRenderBackend()
		? L"Vulkan" : L"DX12";
	std::wostringstream woss;
	woss.precision(6);
	woss << L"Creator Editor - Windows"
		<< L"Width: " << ScreenResizeBus::Get().GetWidth()
		<< L" Height: " << ScreenResizeBus::Get().GetHeight()
		<< L" FPS: " << Time->GetFramesPerSecond()
		<< L" FrameCount: " << Time->GetFrameCount()
		<< L"<EnhancedRenderer/" << rendererBackend << L">";

	SetWindowText(EditorWindowHandle(), woss.str().c_str());
}

void Editor::EditorMain::OnGui()
{
	if (EditorSessionState::Get().IsGameViewHidden())
	{
		return;
	}

	m_editorRenderer->BeginRender();

	m_menuBarWindow->RenderMenuBar();

	m_sceneViewWindow->RenderSceneViewWindow();

	m_gameViewWindow->RenderGameViewWindow();

	m_gizmoRenderer->EditorView();
	Editor::ModelPlacement::Get().DrawStatus();

	m_editorRenderer->Render();
	m_editorRenderer->EndRender();
}

void Editor::EditorMain::InvokeResizeFlag()
{
	m_isInvokeResize.store(true, std::memory_order_release);
	m_presentationWake.notify_one();
}

