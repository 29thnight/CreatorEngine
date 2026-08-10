#ifndef DYNAMICCPP_EXPORTS
#include "EditorMain.h"
#include "CoreWindow.h"
#include "BootProgress.h"
#include "RHI/DX12/EnhancedSceneRenderer.h"
#include "RHI/ScreenSizedResource.h"
#include "InputManager.h"
#include "ImGuiRegister.h"
#include "Physx.h"
#include "SoundManager.h"
#include "TimeSystem.h"
#include "DataSystem.h"
#include "ShaderSystem.h"
#include "SceneManager.h"
#include "ClrHost.h"
#include "EngineSetting.h"
#include "UIManager.h"
#include "Profiler.h"
#include "WinProcProxy.h"
#include "TagManager.h"
#include "GameObject.h"
#include "Scene.h"
// OpenFile 재정의 훅이 쓴다 (PHASE 4-3)
#include "PrefabEditor.h"
#include "PathFinder.h"
#include "CameraComponent.h"
#include "LightComponent.h"
#include "imgui.h"
#include "imgui_impl_win32.h"

#include <sstream>
#include <stdexcept>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace
{
	// 렌더 스레드 둘의 수명 깃발. 파일 밖에서 볼 이유가 없다 —
	// 예전에는 전역이었고 isCB/CE_Thread_End 둘이 더 있었는데, join으로
	// 회수하게 되면서 그 둘은 읽는 곳이 없어졌다.
	std::atomic<bool> g_isGameToRender = false;

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

	ShaderSystem->Initialize();

	std::string enhancedError;
	if (!EnhancedSceneRenderer::InitializeRuntime(enhancedError))
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

		scene->CreateGameObject("Main Camera", GameObjectType::Camera)
			->AddComponent<CameraComponent>();
		auto lightObject =
			scene->CreateGameObject("Directional Light", GameObjectType::Light);
		lightObject->SetTag("MainCamera");
		auto light = lightObject->AddComponent<LightComponent>();
		light->m_lightStatus = LightStatus::StaticShadows;
	});
	m_activeSceneChangedHandle = activeSceneChangedEvent.AddLambda([]()
	{
		EnhancedSceneRenderer::SetActiveScene(SceneManagers->GetActiveScene());
	});

	// 호스트(IImGuiHost → DX12 셸)가 여기서 선다. 구 ImGuiRenderer는 HWND
	// 하나 때문에 DX11 DeviceResources를 통째로 들었다 — 이제 핸들만 넘긴다.
	m_editorRenderer = std::make_unique<EditorRenderer>(EditorWindowHandle());

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

	// ── 자산 시스템에 에디터 지식을 걸어 둔다 (PHASE 4-3) ──
	//
	// DataSystem::OpenFile은 자산 시스템의 일반 서비스인데, 어떤 확장자를
	// 어느 편집기로 보낼지는 에디터 정책이다. 그래서 정책만 여기서 건다.
	// (씬 오브젝트 드롭 훅은 슬라이스 2에서 사라졌다 — 그 코드는 이제
	//  ContentsBrowserWindow가 직접 들고 있다.)
	DataSystems->SetOpenFileOverride([](const file::path& filepath) -> bool
	{
		if (filepath.extension() == ".prefab")
		{
			PrefabEditors->Open(filepath.string());
			return true;
		}
		return false;
	});

	DataSystems->Initialize();
	ShaderSystem->SetPSOs_GUID();

	// 셰이더 선택 창 둘. ShaderSystem이 등록하던 것을 에디터가 가져왔다
	// (PHASE 4-3 슬라이스 5) — 플레이어는 이제 등록조차 하지 않는다.
	ShaderSelectionWindow::Register();

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
	m_guiRenderingEventHandle = GUIRenderingEvent.AddLambda([this]()
	{
		OnGui();
	});

	BootProgress::Step(L"Initializing Managers...");
	SceneManagers->ManagerInitialize();
	PhysicsManagers->Initialize();

	// CoreCLR 스크립트 계층. 렌더 스레드를 띄우기 전에 올려둔다.
	// 관리 어셈블리가 없으면 조용히 비활성 상태로 남고 엔진은 그대로 동작한다.
	ClrHost::Get().Initialize();

	g_isGameToRender = true;

	PROFILE_FRAME();

	m_commandBuildThread = std::thread([this]
	{
		if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)))
		{
			return;
		}

		PROFILE_REGISTER_THREAD("[CB-Thread]");
		while (g_isGameToRender)
		{
			CommandBuildThread();
		}

		CoUninitialize();
	});

	m_commandExecuteThread = std::thread([this]
	{
		if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)))
		{
			return;
		}

		PROFILE_REGISTER_THREAD("[CE-Thread]");
		while (g_isGameToRender)
		{
			while (!WinProcProxy::GetInstance()->IsEmpty())
			{
				auto [hwnd, message, wParam, lParam] =
					WinProcProxy::GetInstance()->PopMessage();
				ImGui_ImplWin32_WndProcHandler(hwnd, message, wParam, lParam);
			}

			if (m_isInvokeResize)
			{
				HandleWindowResize();
				m_isInvokeResize = false;
			}

			CoroutineManagers->yield_OnRender();
			CommandExecuteThread();
		}

		CoUninitialize();
	});

	// detach하지 않는다. Finalize에서 join으로 회수한다.
	// 종료 시점에 살아 있는 스레드는 ExitProcess가 임의 지점에서 죽이므로,
	// 하필 힙 락을 쥔 순간이면 남은 종료 절차가 그 위에서 힙을 만지게 된다.
}

void Editor::EditorMain::Finalize()
{
	// 렌더 스레드를 세우기 전에 관리 측을 먼저 정리한다.
	// 스크립트가 들고 있던 핸들이 남아 있으면 이후 파괴 순서가 꼬인다.
	// ★ 단계마다 즉시 찍는다. 종료가 멈추는 자리를 찾는 데 로그가
	//   없으면 어디까지 갔는지조차 알 수 없다.
	std::printf("[SHUTDOWN] ClrHost 진입\n");
	ClrHost::Get().Shutdown();
	std::printf("[SHUTDOWN] ClrHost 반환\n");

	// 렌더 스레드를 먼저 완전히 세운다. 그 다음에야 그들이 만지던 것을 부순다.
	//
	// 예전에는 순서가 반대였다. 깃발만 내려놓고 곧바로
	// SceneManagers->Decommissioning()으로 렌더 씬을 해체했는데, 그 깃발은
	// 루프 맨 위에서만 확인되므로 CB 스레드는 여전히 한 프레임 분량의 커맨드를
	// 만드는 중이었다. 그 사이 렌더 씬·패스·RenderPassData가 발밑에서 사라졌고,
	// 결과는 종료 구간의 간헐 크래시다. 실제 덤프 두 건이 전부 이 모양이었다:
	//   ShadowMapPass::CreateCommandListCascadeShadow → concurrent_queue::push
	//   RenderPassData::ClearRenderQueue → concurrent_vector::clear
	// 둘 다 파괴된 동시성 컨테이너를 만진 흔적(0xFFFFFFFFFFFFFFFF 읽기)이다.
	g_isGameToRender = false;

	// 배리어에 걸려 있는 스레드를 깨워야 루프 조건을 다시 볼 수 있다.
	// 이게 없으면 아래 대기가 영원히 끝나지 않는다.
	EngineSettingInstance->renderBarrier.Finalize();
	std::printf("[SHUTDOWN] renderBarrier.Finalize 반환\n");

	// 깃발 폴링 대신 실제로 회수한다.
	//
	// 예전에는 두 스레드를 detach하고 종료 깃발을 100ms마다 확인했다.
	// 문제가 둘이었다. (1) 깃발은 CoUninitialize 직전에 켜지므로, 그것만 보고
	// 진행하면 그 스레드는 아직 COM 정리 중이고 프로세스가 죽을 때 그 지점에서
	// 강제 종료된다. (2) 스레드 진입부의 CoInitializeEx가 실패하면 깃발을
	// 켜지 않고 그냥 return해서, 대기가 영원히 끝나지 않았다.
	// join은 둘 다 해결한다 — 스레드가 정말로 끝난 것을 보장한다.
	std::printf("[SHUTDOWN] CB join 진입(joinable=%d)\n",
		m_commandBuildThread.joinable() ? 1 : 0);
	if (m_commandBuildThread.joinable()) m_commandBuildThread.join();
	std::printf("[SHUTDOWN] CB join 반환\n");

	std::printf("[SHUTDOWN] CE join 진입(joinable=%d)\n",
		m_commandExecuteThread.joinable() ? 1 : 0);
	if (m_commandExecuteThread.joinable()) m_commandExecuteThread.join();
	std::printf("[SHUTDOWN] CE join 반환\n");

	// 여기서부터는 렌더 스레드가 없다. 이제 해체해도 안전하다.
	TagManagers->Finalize();
	std::printf("[SHUTDOWN] TagManagers 반환\n");

	SceneManagers->Decommissioning();
	std::printf("[SHUTDOWN] SceneManagers 반환\n");

	EngineSettingInstance->SaveSettings();
	std::printf("[SHUTDOWN] SaveSettings 반환\n");

	// 메인 DX12 렌더러의 최종 정리. 렌더 스레드 join 뒤여야 공유 SRV와
	// 런타임 RenderScene을 안전하게 놓을 수 있다. 최종 GPU 집계도 이 안에서
	// 난다 — 디바이스 파괴 직전이 그 집계의 유일하게 옳은 자리다.
	EnhancedSceneRenderer::ShutdownLive();
	SceneManagers->SetRenderScene(nullptr);
	std::printf("[SHUTDOWN] EnhancedRenderer 반환\n");

	ShaderSystem->Finalize();
	std::printf("[SHUTDOWN] ShaderSystem 반환\n");

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

	clr.TickAwake();          // 새로 붙은 스크립트의 Awake/OnEnable

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

	clr.TickUpdate(deltaTime);
	clr.TickLateUpdate(deltaTime);
}

void Editor::EditorMain::Update()
{
	const bool isPaused = SceneManagers->IsGamePaused();
	const double deltaSeconds = Time->GetElapsedSeconds();
	EngineSettingInstance->frameDeltaTime = isPaused ? 0.0 : deltaSeconds;

	PROFILE_CPU_BEGIN("GameLogic");
	Time->Tick([this]
	{
		UpdateTitleBar();
		InputManagement->Update(EngineSettingInstance->frameDeltaTime);

		if (!SceneManagers->IsGameStart())
		{
			SceneManagers->Editor();
			SceneManagers->InputEvents(EngineSettingInstance->frameDeltaTime);
			SceneManagers->GameLogic();

			// 편집 모드에서는 스크립트를 돌리지 않는다(Unity와 같은 규약).
			// 붙여 둔 스크립트는 보류 큐에 쌓였다가 재생 시작 시 한꺼번에 Awake된다.
			return;
		}

		SceneManagers->Editor();
		SceneManagers->Initialization();
		SceneManagers->InputEvents(EngineSettingInstance->frameDeltaTime);

		if (SceneManagers->IsGamePaused())
		{
			SceneManagers->Pausing();
			return;
		}

		SceneManagers->Physics(EngineSettingInstance->frameDeltaTime);
		SceneManagers->GameLogic(EngineSettingInstance->frameDeltaTime);

		// 재생 버튼을 누른 프레임은 아직 에디터 원본 씬이 활성이다 —
		// 씬 사본 생성은 렌더 배리어 사이(ApplyPendingSceneStructureChange)에서 일어난다.
		// 여기서 그냥 돌리면 곧 접힐 원본 스크립트가 Awake를 한 번 실행해
		// 스폰·사운드 같은 부작용이 두 번 일어난다. 한 프레임 미룬다.
		if (!SceneManagers->HasPendingSceneStructureChange())
		{
			TickScripts(EngineSettingInstance->frameDeltaTime);
		}
	});

	if (InputManagement->IsKeyReleased(VK_F5))
	{
		EngineSettingInstance->ToggleGameView();
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

	EngineSettingInstance->renderBarrier.ArriveAndWait(0, BarrierRole::Game);

	// 여기부터 두 번째 랑데뷰까지는 커맨드 빌드/실행 스레드가 모두 묶여 있다.
	// 빌드 스레드는 워커 풀을 기다린 뒤 도달하므로 워커도 놀고 있다.
	// 씬 오브젝트 목록을 통째로 갈아엎는 작업은 이 구간에서만 안전하다.
	SceneManagers->ApplyPendingSceneStructureChange();

	PROFILE_CPU_BEGIN("EndOfFrame");
	SceneManagers->DisableOrEnable();
	SceneManagers->EndOfFrame();
	PROFILE_CPU_END();

	PROFILE_FRAME();
	EngineSettingInstance->renderBarrier.ArriveAndWait(1, BarrierRole::Game);

	if (SceneManagers->IsDecommissioning())
	{
		PostMessage(EditorWindowHandle(), WM_CLOSE, 0, 0);
	}
}

bool Editor::EditorMain::ExecuteRenderPass()
{
	PROFILE_CPU_BEGIN("CommandExecute");
	const bool gameSceneStart =
		SceneManagers->m_isGameStart && !SceneManagers->m_isEditorSceneLoaded;
	const bool gameSceneEnd =
		!SceneManagers->m_isGameStart && SceneManagers->m_isEditorSceneLoaded;

	// 처음 업데이트하기 전에 아무 것도 렌더링하지 않는다.
	if (0 == Time->GetFrameCount() || gameSceneStart || gameSceneEnd || m_isInvokeResize)
	{
		PROFILE_CPU_END();
		return false;
	}

	SceneManagers->SceneRendering(EngineSettingInstance->frameDeltaTime);
	SceneManagers->OnDrawGizmos();
	SceneManagers->GUIRendering();

	PROFILE_CPU_END();
	return true;
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
	if (EngineSettingInstance->IsGameView())
	{
		return;
	}

	m_editorRenderer->BeginRender();

	PROFILE_CPU_BEGIN("ImGuiRenderMenuBar");
	m_menuBarWindow->RenderMenuBar();
	PROFILE_CPU_END();

	PROFILE_CPU_BEGIN("ImGuiRenderSceneViewWindow");
	m_sceneViewWindow->RenderSceneViewWindow();
	PROFILE_CPU_END();

	PROFILE_CPU_BEGIN("ImGuiRenderGameViewWindow");
	m_gameViewWindow->RenderGameViewWindow();
	PROFILE_CPU_END();

	PROFILE_CPU_BEGIN("ImGuiEditorView");
	m_gizmoRenderer->EditorView();
	PROFILE_CPU_END();

	m_editorRenderer->Render();
	m_editorRenderer->EndRender();
}

void Editor::EditorMain::CommandBuildThread()
{
	// ★ 이 스레드는 아무것도 만들지 않는다.
	//
	//   DX11 SceneRenderer의 커맨드 빌드가 여기 있었고, 그것이 은퇴하면서
	//   본문이 비었다. 스레드를 없애지 않는 이유는 렌더 배리어가 3자 랑데뷰라
	//   참가자 수가 계약이기 때문이다 — 빠지면 나머지 둘이 영원히 기다린다.
	//
	//   배리어를 2자로 줄이는 것은 별건이다(PHASE 9 생명주기 재설계).
	EngineSettingInstance->renderBarrier.ArriveAndWait(0, BarrierRole::CommandBuild);
	EngineSettingInstance->renderBarrier.ArriveAndWait(1, BarrierRole::CommandBuild);
}

void Editor::EditorMain::CommandExecuteThread()
{
	// 프레젠트는 ImGui DX12 셸이 EndRender에서 한다 — 여기 있던 DX11
	// 스왑체인 Present 분기는 그 소유권 이관(D2) 뒤로 죽은 가지였다.
	ExecuteRenderPass();

	EngineSettingInstance->renderBarrier.ArriveAndWait(0, BarrierRole::CommandExecute);
	EngineSettingInstance->renderBarrier.ArriveAndWait(1, BarrierRole::CommandExecute);
}

void Editor::EditorMain::InvokeResizeFlag()
{
	m_isInvokeResize = true;
}

#endif // !DYNAMICCPP_EXPORTS
