#ifndef DYNAMICCPP_EXPORTS
#include "Dx11Main.h"
#include "CoreWindow.h"
#include "BootProgress.h"
// ★ 구 RHI 계통을 걷었다 (R5, 2026-08-09).
//
//   여기 있던 것: RHI/RHI.h · RHI/DX11RHI.h. 지워진 일곱 파일은
//   RHI.{h,cpp} · RHIDevice.h · RHICommandContext.h · RHIDefinitions.h ·
//   DX11RHI.{h,cpp}다.
//
//   PHASE 3-1이 세운 "DeviceStates를 타입 있는 경계 하나로 좁히는 중간 계단"
//   이었고, 그 계단의 목적은 3-5(렌더그래프)가 서면 말라 죽는 것이었다.
//   실제로 그렇게 됐다 - DX12 라이브 렌더러가 자기 그래프로 기록하면서
//   RHI::Immediate()의 호출부가 0이 됐고, 마지막 표면이던
//   RenderPassData::BindFrameCameraBuffers도 함께 사라졌다.
//
//   ★ 남아 있던 것은 include 셋뿐이었다. 이 파일의 둘과 RenderPassData.cpp의
//     하나. RHI::Initialize를 부르는 코드조차 없었으므로 백엔드는 등록된 적이
//     없고, Device()를 불렀다면 assert에 걸렸을 것이다.
//
//   지금 살아 있는 RHI 경계는 IRHIDeviceResources.h(디바이스·스왑체인 계약)와
//   ScreenSizedResource.h(화면 크기 버스) 둘이다. 이름만 같고 계보가 다르다.
#include "RHI/DX12/EnhancedSceneRenderer.h"
#include "RHI/DX12/ImGuiDx12Shell.h"
#include "RHI/ScreenSizedResource.h"
#include "InputManager.h"
#include "ImGuiRegister.h"
#include "Physx.h"
#include "SoundManager.h"
#include "Benchmark.hpp"
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
#include "AIManager.h"
#include "GameObject.h"
#include "Scene.h"
#include "CameraComponent.h"
#include "LightComponent.h"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
std::atomic<bool> isGameToRender = false;
std::atomic<bool> isCB_Thread_End = false;
std::atomic<bool> isCE_Thread_End = false;

DirectX11::Dx11Main::Dx11Main(const std::shared_ptr<DeviceResources>& deviceResources)	: m_deviceResources(deviceResources)
{
    DirectX11::TimeSystem::GetInstance();
}

DirectX11::Dx11Main::~Dx11Main()
{
    DirectX11::TimeSystem::Destroy();
}

void DirectX11::Dx11Main::Initialize()
{
    PROFILER_INITIALIZE(5, 1024);
    PROFILE_REGISTER_THREAD("[GameThread]");

    BootProgress::Step(L"Initializing RenderEngine...");
    m_deviceResources->RegisterDeviceNotify(this);

    // 옥트리 컬링 초기화가 여기 있었다 — 계통 전체를 걷었다
    // (RenderSceneViewPlan ③, MeshRenderer::Awake의 주석 참고).
    TagManagers->Initialize();

    BootProgress::Step(L"Creating Renderers...");
    InitializeDeviceState();
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


    m_imguiRenderer = std::make_unique<ImGuiRenderer>(m_deviceResources);
#ifdef EDITOR
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
#endif // !EDITOR

    BootProgress::Step(L"Initializing SoundManager...");
    Sound->initialize(128);

    BootProgress::Step(L"Loading Assets...");
    DataSystems->Initialize();
    ShaderSystem->SetPSOs_GUID();
    //CreateScene
    BootProgress::Step(L"Loading Project...");
    SceneManagers->CreateScene();

    BootProgress::Step(L"Registering Frame Events...");
    m_InputEvenetHandle = InputEvent.AddLambda([&](float deltaSecond)
    {
#ifdef EDITOR
        bool isPressedCtrl = InputManagement->IsKeyPressed((uint32)KeyBoard::LeftControl);
        if (isPressedCtrl && InputManagement->IsKeyDown('Z'))
        {
            Meta::UndoCommandManager->Undo();
        }

        if (isPressedCtrl && InputManagement->IsKeyDown('Y'))
        {
            Meta::UndoCommandManager->Redo();
        }
#endif // !EDITOR
        UIManagers->Update();
        Sound->update();
    });
    m_GUIRenderingEventHandle = GUIRenderingEvent.AddLambda([&]()
    {
        OnGui();
    });

    BootProgress::Step(L"Initializing Managers...");
    SceneManagers->ManagerInitialize();
    PhysicsManagers->Initialize();

    // CoreCLR 스크립트 계층. 렌더 스레드를 띄우기 전에 올려둔다.
    // 관리 어셈블리가 없으면 조용히 비활성 상태로 남고 엔진은 그대로 동작한다.
    ClrHost::Get().Initialize();

    isGameToRender = true;

    PROFILE_FRAME();

    m_CB_Thread = std::thread([&]
    {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(hr))
        {
            return;
        }

        PROFILE_REGISTER_THREAD("[CB-Thread]");
        while (isGameToRender)
        {
            if (m_isInvokeResize)
            {
                EngineSettingInstance->renderBarrier.ArriveAndWait(0, BarrierRole::CommandBuild);
                EngineSettingInstance->renderBarrier.ArriveAndWait(1, BarrierRole::CommandBuild);
                continue;
            }

            CommandBuildThread();
        }

        isCB_Thread_End = true;
        CoUninitialize();
    });

    m_CE_Thread = std::thread([&]
    {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(hr))
        {
            return;
        }
        PROFILE_REGISTER_THREAD("[CE-Thread]");
        while (isGameToRender)
        {
            while (!WinProcProxy::GetInstance()->IsEmpty())
            {
                auto [hwnd, message, wParam, lParam] = WinProcProxy::GetInstance()->PopMessage();

                if (ImGui_ImplWin32_WndProcHandler(hwnd, message, wParam, lParam))
                {
                    continue;
                }
            }

            if (m_isInvokeResize)
            {
                CreateWindowSizeDependentResources();
                m_isInvokeResize = false;
            }

            CoroutineManagers->yield_OnRender();
            CommandExecuteThread();
        }

		isCE_Thread_End = true;
        CoUninitialize();
    });

    // detach하지 않는다. Finalize에서 join으로 회수한다.
    // 종료 시점에 살아 있는 스레드는 ExitProcess가 임의 지점에서 죽이므로,
    // 하필 힙 락을 쥔 순간이면 남은 종료 절차가 그 위에서 힙을 만지게 된다.
}

void DirectX11::Dx11Main::Finalize()
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
    // 예전에는 순서가 반대였다. isGameToRender만 내려놓고 곧바로
    // SceneManagers->Decommissioning()으로 렌더 씬을 해체했는데, 그 플래그는
    // 루프 맨 위에서만 확인되므로 CB 스레드는 여전히 한 프레임 분량의 커맨드를
    // 만드는 중이었다. 그 사이 렌더 씬·패스·RenderPassData가 발밑에서 사라졌고,
    // 결과는 종료 구간의 간헐 크래시다. 실제 덤프 두 건이 전부 이 모양이었다:
    //   ShadowMapPass::CreateCommandListCascadeShadow → concurrent_queue::push
    //   RenderPassData::ClearRenderQueue → concurrent_vector::clear
    // 둘 다 파괴된 동시성 컨테이너를 만진 흔적(0xFFFFFFFFFFFFFFFF 읽기)이다.
    isGameToRender = false;

    // 배리어에 걸려 있는 스레드를 깨워야 루프 조건을 다시 볼 수 있다.
    // 이게 없으면 아래 대기가 영원히 끝나지 않는다.
    EngineSettingInstance->renderBarrier.Finalize();
    std::printf("[SHUTDOWN] renderBarrier.Finalize 반환\n");

    // 플래그 폴링 대신 실제로 회수한다.
    //
    // 예전에는 두 스레드를 detach하고 isCB/CE_Thread_End를 100ms마다 확인했다.
    // 문제가 둘이었다. (1) 플래그는 CoUninitialize 직전에 켜지므로, 그것만 보고
    // 진행하면 그 스레드는 아직 COM 정리 중이고 프로세스가 죽을 때 그 지점에서
    // 강제 종료된다. (2) 스레드 진입부의 CoInitializeEx가 실패하면 플래그를
    // 켜지 않고 그냥 return해서, 대기가 영원히 끝나지 않았다.
    // join은 둘 다 해결한다 — 스레드가 정말로 끝난 것을 보장한다.
    std::printf("[SHUTDOWN] CB join 진입(joinable=%d)\n",
        m_CB_Thread.joinable() ? 1 : 0);
    if (m_CB_Thread.joinable()) m_CB_Thread.join();
    std::printf("[SHUTDOWN] CB join 반환\n");

    std::printf("[SHUTDOWN] CE join 진입(joinable=%d)\n",
        m_CE_Thread.joinable() ? 1 : 0);
    if (m_CE_Thread.joinable()) m_CE_Thread.join();
    std::printf("[SHUTDOWN] CE join 반환\n");

    // 여기서부터는 렌더 스레드가 없다. 이제 해체해도 안전하다.
    TagManagers->Finalize();
    std::printf("[SHUTDOWN] TagManagers 반환\n");

    SceneManagers->Decommissioning();
    std::printf("[SHUTDOWN] SceneManagers 반환\n");

    EngineSettingInstance->SaveSettings();
    std::printf("[SHUTDOWN] SaveSettings 반환\n");

    // 메인 DX12 렌더러의 최종 정리. 렌더 스레드 join 후·DX11 셸 디바이스
    // 해체 전이어야 공유 SRV와 런타임 RenderScene을 안전하게 놓을 수 있다.
    EnhancedSceneRenderer::ShutdownLive();
    SceneManagers->SetRenderScene(nullptr);
    std::printf("[SHUTDOWN] EnhancedRenderer 반환\n");

    ShaderSystem->Finalize();
    std::printf("[SHUTDOWN] ShaderSystem 반환\n");
    ShutdownDeviceState();
    OnResizeReleaseEvent.Clear();
	OnResizeEvent.Clear();
    m_deviceResources->RegisterDeviceNotify(nullptr);
    PROFILER_SHUTDOWN();
}

void DirectX11::Dx11Main::CreateWindowSizeDependentResources()
{
	//렌더러의 창 크기에 따라 리소스를 다시 만드는 코드를 여기에 추가합니다.
    m_deviceResources->ReleaseSwapChain();

    // 해제 → 크기 통지 두 단계다. DX11 스왑체인은 백버퍼를 참조하는 뷰가
    // 하나라도 살아 있으면 리사이즈가 실패하므로 만들기 전에 전부 놓아야 한다.
    OnResizeReleaseEvent();
    ScreenResizeBus::Get().BroadcastRelease();

    RECT rect;
    HWND hwnd = m_deviceResources->GetWindow()->GetHandle();

    GetClientRect(hwnd, &rect);
    DirectX11::Sizef size;
    size.width = rect.right - rect.left;
    size.height = rect.bottom - rect.top;

    // Create the render target view and depth stencil view.
    m_deviceResources->SetLogicalSize(size);

    OnResizeEvent(size.width, size.height);

    // 백엔드 중립 버스에도 알린다. DX12 쪽 리소스가 이쪽을 구독하므로,
    // 교체 후 DX11 경로가 사라져도 크기 추종은 그대로 남는다.
    ScreenResizeBus::Get().BroadcastResize(
        static_cast<uint32_t>(size.width), static_cast<uint32_t>(size.height));

}
 
void DirectX11::Dx11Main::TickScripts(float deltaTime)
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

void DirectX11::Dx11Main::Update()
{
	// EditorUpdate
    const bool isPaused = SceneManagers->IsGamePaused();
    const double deltaSeconds = Time->GetElapsedSeconds();
    EngineSettingInstance->frameDeltaTime = isPaused ? 0.0 : deltaSeconds;

    PROFILE_CPU_BEGIN("GameLogic");
    Time->Tick([&]
    {
        InfoWindow();
        InputManagement->Update(EngineSettingInstance->frameDeltaTime);
#ifdef EDITOR
        if(!SceneManagers->IsGameStart())
        {
            SceneManagers->Editor();
            SceneManagers->InputEvents(EngineSettingInstance->frameDeltaTime);
            SceneManagers->GameLogic();

            // 편집 모드에서는 스크립트를 돌리지 않는다(Unity와 같은 규약).
            // 붙여 둔 스크립트는 보류 큐에 쌓였다가 재생 시작 시 한꺼번에 Awake된다.
        }
        else
        {
            SceneManagers->Editor();
            SceneManagers->Initialization();

            SceneManagers->InputEvents(EngineSettingInstance->frameDeltaTime);
            if (!SceneManagers->IsGamePaused())
            {
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
            }
            else
            {
                SceneManagers->Pausing();
            }
        }
#endif // !EDITOR
    });

#ifdef EDITOR
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
#endif // !EDITOR

    EngineSettingInstance->renderBarrier.ArriveAndWait(0, BarrierRole::Game);

    // 여기부터 두 번째 랑데뷰까지는 커맨드 빌드/실행 스레드가 모두 묶여 있다.
    // 빌드 스레드는 워커 풀을 기다린 뒤 도달하므로 워커도 놀고 있다.
    // 씬 오브젝트 목록을 통째로 갈아엎는 작업은 이 구간에서만 안전하다.
    SceneManagers->ApplyPendingSceneStructureChange();

    PROFILE_CPU_BEGIN("EndOfFrame");
    DisableOrEnable();
    SceneManagers->EndOfFrame();
    PROFILE_CPU_END();

    PROFILE_FRAME();
    //RenderCommandFence.Begin();
    //RenderCommandFence.Wait();
    EngineSettingInstance->renderBarrier.ArriveAndWait(1, BarrierRole::Game);

    if (SceneManagers->IsDecommissioning())
    {
        HWND handle = m_deviceResources->GetWindow()->GetHandle();
        PostMessage(handle, WM_CLOSE, 0, 0);
    }
}

bool DirectX11::Dx11Main::ExecuteRenderPass()
{
    PROFILE_CPU_BEGIN("CommandExecute");
    auto GameSceneStart = SceneManagers->m_isGameStart && !SceneManagers->m_isEditorSceneLoaded;
    auto GameSceneEnd = !SceneManagers->m_isGameStart && SceneManagers->m_isEditorSceneLoaded;

	// 처음 업데이트하기 전에 아무 것도 렌더링하지 마세요.
	if (Time->GetFrameCount() == 0 || GameSceneStart || GameSceneEnd || m_isInvokeResize)
    {
        PROFILE_CPU_END();
        return false;
    }

	{
        SceneManagers->SceneRendering(EngineSettingInstance->frameDeltaTime);
#if defined(EDITOR)
		SceneManagers->OnDrawGizmos();
        SceneManagers->GUIRendering();
#endif // !EDITOR
	}

    PROFILE_CPU_END();
	return true;
}

void DirectX11::Dx11Main::InfoWindow()
{
    std::wostringstream woss;
    woss.precision(6);
    woss << L"Creator Editor - Windows"
        << L"Width: "
        << ScreenResizeBus::Get().GetWidth()
        << L" Height: "
        << ScreenResizeBus::Get().GetHeight()
        << L" FPS: "
        << Time->GetFramesPerSecond()
        << L" FrameCount: "
        << Time->GetFrameCount()
        // 활성 백엔드 이름 — 교체 스위치(3-9)가 생기면 이 표기가 곧 확인 수단이 된다.
        << "<EnhancedRenderer/DX12>";

    SetWindowText(m_deviceResources->GetWindow()->GetHandle(), woss.str().c_str());
}

void DirectX11::Dx11Main::OnGui()
{
    if (!EngineSettingInstance->IsGameView())
    {
        m_imguiRenderer->BeginRender();
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
        m_imguiRenderer->Render();
        m_imguiRenderer->EndRender();
    }
}

void DirectX11::Dx11Main::DisableOrEnable()
{
	SceneManagers->DisableOrEnable();
}

void DirectX11::Dx11Main::CommandBuildThread()
{
    PROFILE_CPU_BEGIN("CommandBuild");
    auto GameSceneStart = SceneManagers->m_isGameStart && !SceneManagers->m_isEditorSceneLoaded;
    auto GameSceneEnd = !SceneManagers->m_isGameStart && SceneManagers->m_isEditorSceneLoaded;

    // 처음 업데이트하기 전에 아무 것도 하지 마세요.
    if (Time->GetFrameCount() == 0 || GameSceneStart || GameSceneEnd || m_isInvokeResize)
    {
        PROFILE_CPU_END();
        //RenderCommandFence.Signal();
        EngineSettingInstance->renderBarrier.ArriveAndWait(0, BarrierRole::CommandBuild);
        EngineSettingInstance->renderBarrier.ArriveAndWait(1, BarrierRole::CommandBuild);
        return;
    }

    // SceneRenderer(DX11)의 커맨드 빌드는 제거됐다. 이 스레드는 기존
    // 3자 렌더 배리어의 참가자 수를 유지하는 동기화 역할만 한다.
    PROFILE_CPU_END();
    //RHICommandFence.Wait();
    //RenderCommandFence.Signal();
    EngineSettingInstance->renderBarrier.ArriveAndWait(0, BarrierRole::CommandBuild);
    EngineSettingInstance->renderBarrier.ArriveAndWait(1, BarrierRole::CommandBuild);
}

void DirectX11::Dx11Main::CommandExecuteThread()
{
	if (ExecuteRenderPass())
	{
        PROFILE_CPU_BEGIN("Present");
		// DX12 셸 모드면 Present는 셸(ImGui EndRender)이 이미 했다 —
		// DX11 스왑체인까지 제시하면 빈 백버퍼가 화면을 덮는다.
		if (!ImGuiDx12Shell::Get().IsActive())
		{
			m_deviceResources->Present();
		}
        PROFILE_CPU_END();
	}
    //RHICommandFence.Signal();
    EngineSettingInstance->renderBarrier.ArriveAndWait(0, BarrierRole::CommandExecute);
    EngineSettingInstance->renderBarrier.ArriveAndWait(1, BarrierRole::CommandExecute);
}

void DirectX11::Dx11Main::InitializeDeviceState()
{
    // ★ DX11 전역 상태(DeviceStates) 배선을 통째로 걷었다 (D4, 2026-08-09).
    //
    //   여기서 디바이스·컨텍스트·뷰포트·백버퍼 RTV·종횡비 열넷을 전역에
    //   실어 두고, 리사이즈마다 같은 목록을 다시 실었다. 그 전역을 읽던
    //   소비자는 D4 부류 A~C에서 전부 사라졌다 - 화면 크기는 버스로 옮겼고
    //   나머지는 죽은 코드로 판명되어 걷혔다.
    //
    // 남는 일은 하나다: 화면 크기 버스의 첫 값을 채우는 것.
    //
    //   ★ 리사이즈 시점은 이미 DX11과 무관했다.
    //     CreateWindowSizeDependentResources가 GetClientRect로 Win32에서
    //     직접 읽어 BroadcastResize로 알린다. 첫 값만 g_ClientRect(DX11
    //     DeviceResources의 출력 크기)를 거치고 있었고 그것이 D4의 마지막
    //     고리였다. 같은 창에서 직접 읽는다.
    RECT clientRect{};
    if (auto* window = m_deviceResources->GetWindow())
    {
        GetClientRect(window->GetHandle(), &clientRect);
    }
    ScreenResizeBus::Get().SetSize(
        static_cast<uint32_t>(clientRect.right - clientRect.left),
        static_cast<uint32_t>(clientRect.bottom - clientRect.top));
}

void DirectX11::Dx11Main::ShutdownDeviceState()
{
    OnResizeEvent -= m_resizeEventHandle;
}


void DirectX11::Dx11Main::InvokeResizeFlag()
{
    m_isInvokeResize = true;
}

// 릴리스가 필요한 디바이스 리소스를 렌더러에 알립니다.
void DirectX11::Dx11Main::OnDeviceLost()
{

}

// 디바이스 리소스가 이제 다시 만들어질 수 있음을 렌더러에 알립니다.
void DirectX11::Dx11Main::OnDeviceRestored()
{
	CreateWindowSizeDependentResources();
}
#endif // !DYNAMICCPP_EXPORTS


