#ifndef DYNAMICCPP_EXPORTS
#include "EditorMain.h"
#include "CoreWindow.h"
#include "BootProgress.h"
#include "Render/Scene/EnhancedSceneRenderer.h"
#include "RHI/IImGuiHost.h"
#include "RHI/ScreenSizedResource.h"
#include "InputManager.h"
#include "ImGuiRegister.h"
#include "Physx.h"
#include "SoundManager.h"
#include "TimeSystem.h"
#include "DataSystem.h"
#include "SceneManager.h"
#include "ClrHost.h"
#include "RuntimeSettings.h"
#include "EditorSettingsStore.h"
#include "EditorSessionState.h"
#include "EditorPlatform.h"
#include "EditorAssetDatabase.h"
#include "EditorAssetPresentation.h"
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

	BootProgress::Step(L"Initializing RenderEngine...");

	// 옥트리 컬링 초기화가 여기 있었다 — 계통 전체를 걷었다
	// (RenderSceneViewPlan ③, MeshRenderer::Awake의 주석 참고).
	TagManagers->Initialize();

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

	// 기본 씬 저작 정책은 렌더 패스 소유권과 분리해 엔트리 계층에 둔다.
	// EnhancedRenderer의 이벤트는 같은 시점에 RenderScene만 갱신한다.
	m_newSceneCreatedHandle = newSceneCreatedEvent.AddLambda([]()
	{
		Scene* scene = SceneManagers->GetActiveScene();
		if (nullptr == scene) return;

		EnhancedSceneRenderer::SetActiveScene(scene);

		scene->CreateEntity("Main Camera", GameObjectType::Camera)
			->AddComponent<CameraComponent>();
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
		EnhancedSceneRenderer::GetRenderScene(), EnhancedSceneRenderer::GetEditorCamera());
	m_sceneViewWindow = std::make_unique<SceneViewWindow>(
		EnhancedSceneRenderer::GetEditorCamera(), m_gizmoRenderer.get());
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

	// 콘텐츠 브라우저는 DataSystem이 아이콘·폰트를 올린 뒤라야 뜻이 있다.
	m_contentsBrowserWindow = std::make_unique<ContentsBrowserWindow>();

	BootProgress::Step(L"Loading Project...");
	SceneManagers->CreateScene();

	BootProgress::Step(L"Registering Frame Events...");
	m_inputEventHandle = InputEvent.AddLambda([](float)
	{
		const bool isPressedCtrl =
			InputManagement->IsKeyPressed((uint32)KeyBoard::LeftControl);
		if (isPressedCtrl && InputManagement->IsKeyDown('Z'))
		{
			Meta::UndoCommandManager->Undo();
		}
		if (isPressedCtrl && InputManagement->IsKeyDown('Y'))
		{
			Meta::UndoCommandManager->Redo();
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
	EditorAssetPresentation::Get().Shutdown();
	std::printf("[SHUTDOWN] EditorAssetPresentation 반환\n");
	EditorAssetDatabase::Get().Shutdown();
	std::printf("[SHUTDOWN] EditorAssetDatabase 반환\n");

	// 전용 RenderThread의 bounded queue를 여기서 완전히 drain한다. 아래
	// Decommissioning은 RenderScene::Finalize를 호출하므로 순서가 뒤집히면 RT가
	// 파괴 중인 proxy map을 읽게 된다.
	EnhancedSceneRenderer::StopLiveRenderThread();
	std::printf("[SHUTDOWN] RenderThread drain 반환\n");

	// 여기서부터는 표시/렌더 소비 스레드가 없다. 이제 해체해도 안전하다.
	TagManagers->Finalize();
	std::printf("[SHUTDOWN] TagManagers 반환\n");

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

void Editor::EditorMain::TickScripts(float deltaTime)
{
	// 경계는 여기가 전부다. 스크립트가 몇 개든 프레임당 통과 횟수는 고정이고,
	// 순회는 관리 영역에서 끝난다(설계 문서 02절).
	// 게임 스레드에서만 부른다 — CoreCLR GC가 스레드를 정지시키기 때문이다.
	auto& clr = ClrHost::Get();
	if (!clr.IsReady()) return;

	// 물리·GameLogic이 만들거나 없앤 스크립트를 관리 측 활성 목록에 반영한다.
	// 아래 플러시들이 그 최신 목록으로 배달되어야 하므로 여기가 자리다.
	clr.FlushRegistrations();

	// 물리에서 모인 충돌 이벤트를 Update 전에 흘려보낸다.
	// 발생 시점에 바로 부르지 않는 이유는 설계 문서 02절 참고.
	clr.FlushPhysicsEvents();

	// 애니메이션 상태 전이도 같은 규약이다. 상태 머신이 이번 프레임에 쌓아 둔
	// Enter/Update/Exit를 발생 순서 그대로 넘긴다.
	clr.FlushAniEvents();

	// 이름으로 부르는 콜백(애니메이션 키프레임 이벤트·입력 액션).
	// Update 전에 흘려보내는 이유는 물리와 같다 — 스크립트가 이번 프레임 Update에서
	// 그 결과를 보고 판단할 수 있어야 한다.
	clr.FlushScriptMessages();

	// AI 잡 스레드가 이번 프레임에 담아 둔 트리 틱을 흘려보낸다(PHASE 9-8).
	//
	// 트리가 몇 개든 경계 통과는 한 번이고, 트리 안의 노드 순회는 전부 관리 측에서
	// 끝난다 — BT의 틱이 동기 재귀라 노드 단위로 넘기면 그 규약이 무너진다.
	clr.FlushAITicks();

	clr.TickPostPhysics(deltaTime);
}

// 물리 스텝 **앞**의 관리 틱 (설계 문서 §4 트랙 L5).
//
// 예전에는 관리 측 틱이 전부 물리 뒤에 있었다 — 프레임 루프가
// Physics -> GameLogic -> TickScripts 순이고 그 안에서 Update/LateUpdate를
// 연속으로 넘겼다. 그래서 "이번 프레임의 물리에 영향을 주는" 자리가 아예 없었다.
//
// 크로싱이 프레임당 하나 는다. 규약은 "스크립트가 몇 개든 프레임당 통과 횟수는
// 고정"이지 1회가 아니므로(위 TickScripts 주석) 이 분할은 규약 안이다.
void Editor::EditorMain::TickScriptsPrePhysics(float deltaTime)
{
	auto& clr = ClrHost::Get();
	if (!clr.IsReady()) return;

	clr.TickPrePhysics(deltaTime);
}

void Editor::EditorMain::Update()
{
	PROFILE_CPU_BEGIN("GameLogic");
	Time->Tick([this]
	{
		const bool isPaused = SceneManagers->IsGamePaused();
		const double deltaSeconds = Time->GetElapsedSeconds();
		m_frameDeltaTime = isPaused ? 0.0 : deltaSeconds;

		UpdateTitleBar();
		InputManagement->Update(m_frameDeltaTime);

		if (!SceneManagers->IsGameStart())
		{
			SceneManagers->Editor();
			SceneManagers->InputEvents(m_frameDeltaTime);
			SceneManagers->GameLogic();

			// 편집 모드에서는 스크립트를 돌리지 않는다(Unity와 같은 규약).
			// 붙여 둔 스크립트는 보류 큐에 쌓였다가 재생 시작 시 한꺼번에 Awake된다.
			return;
		}

		SceneManagers->Editor();
		SceneManagers->Initialization();
		SceneManagers->InputEvents(m_frameDeltaTime);

		if (SceneManagers->IsGamePaused())
		{
			SceneManagers->Pausing();
			return;
		}

		// 물리 앞의 관리 틱. 아래 TickScripts(물리 뒤)와 짝이다 — 재생 첫 프레임을
		// 건너뛰는 조건도 같아야 하므로 같은 가드를 쓴다.
		if (!SceneManagers->HasPendingSceneStructureChange())
		{
			TickScriptsPrePhysics(m_frameDeltaTime);
		}

		SceneManagers->Physics(m_frameDeltaTime);
		SceneManagers->GameLogic(m_frameDeltaTime);

		// 재생 버튼을 누른 프레임은 아직 에디터 원본 씬이 활성이다 —
		// 씬 사본 생성은 아래 GT 구조 변경 구간(ApplyPendingSceneStructureChange)에서 일어난다.
		// 여기서 그냥 돌리면 곧 접힐 원본 스크립트가 Awake를 한 번 실행해
		// 스폰·사운드 같은 부작용이 두 번 일어난다. 한 프레임 미룬다.
		if (!SceneManagers->HasPendingSceneStructureChange())
		{
			TickScripts(m_frameDeltaTime);
		}
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
	std::wostringstream woss;
	woss.precision(6);
	woss << L"Creator Editor - Windows"
		<< L"Width: " << ScreenResizeBus::Get().GetWidth()
		<< L" Height: " << ScreenResizeBus::Get().GetHeight()
		<< L" FPS: " << Time->GetFramesPerSecond()
		<< L" FrameCount: " << Time->GetFrameCount()
		// 활성 백엔드 이름 — 교체 스위치(3-9)가 생기면 이 표기가 곧 확인 수단이 된다.
		<< L"<EnhancedRenderer/DX12>";

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

	m_editorRenderer->Render();
	m_editorRenderer->EndRender();
}

void Editor::EditorMain::InvokeResizeFlag()
{
	m_isInvokeResize.store(true, std::memory_order_release);
	m_presentationWake.notify_one();
}

#endif // !DYNAMICCPP_EXPORTS
