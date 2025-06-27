#ifndef DYNAMICCPP_EXPORTS
#include "Dx11Main.h"
#include "CoreWindow.h"
#include "InputManager.h"
#include "ImGuiRegister.h"
#include "Physx.h"
#include "SoundManager.h"
#include "Benchmark.hpp"
#include "ImGuiLogger.h"
#include "TimeSystem.h"
#include "HotLoadSystem.h"
#include "DataSystem.h"
#include "ShaderSystem.h"
#include "SceneManager.h"
#include "EngineSetting.h"
#include "CullingManager.h"
#include "UIManager.h"
#include "InputActionManager.h"
#include "Profiler.h"
#include "WinProcProxy.h"

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
//#include "SwapEvent.h"


extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
std::atomic<bool> isGameToRender = false;

DirectX11::Dx11Main::Dx11Main(const std::shared_ptr<DeviceResources>& deviceResources)	: m_deviceResources(deviceResources)
{
    PROFILER_INITIALIZE(5, 1024);
    PROFILE_REGISTER_THREAD("[GameThread]");

    g_progressWindow->SetStatusText(L"Initializing RenderEngine...");
	m_deviceResources->RegisterDeviceNotify(this);

    XMFLOAT3 center = { 0.0f, 0.0f, 0.0f };
    XMFLOAT3 extents = { 2000.f, 2000.f, 2000.f };
    BoundingBox fixedBounds(center, extents);
	CullingManagers->Initialize(fixedBounds, 3, 30);

    g_progressWindow->SetProgress(50);
	m_sceneRenderer = std::make_shared<SceneRenderer>(m_deviceResources);
	m_imguiRenderer = std::make_unique<ImGuiRenderer>(m_deviceResources);
    g_progressWindow->SetProgress(55);
#ifdef EDITOR
	m_gizmoRenderer     = std::make_shared<GizmoRenderer>(m_sceneRenderer.get());
	m_renderPassWindow  = std::make_unique<RenderPassWindow>(m_sceneRenderer.get(), m_gizmoRenderer.get());
	m_sceneViewWindow   = std::make_unique<SceneViewWindow>(m_sceneRenderer.get(), m_gizmoRenderer.get());
	m_menuBarWindow     = std::make_unique<MenuBarWindow>(m_sceneRenderer.get());
	m_gameViewWindow    = std::make_unique<GameViewWindow>(m_sceneRenderer.get());
	m_hierarchyWindow   = std::make_unique<HierarchyWindow>(m_sceneRenderer.get());
	m_inspectorWindow   = std::make_unique<InspectorWindow>(m_sceneRenderer.get());
    g_progressWindow->SetProgress(60);
#endif // !EDITOR

    g_progressWindow->SetStatusText(L"Script Building...");
	ScriptManager->Initialize();
    g_progressWindow->SetProgress(65);

    g_progressWindow->SetStatusText(L"Initializing SoundManager...");
	Sound->initialize((int)ChannelType::MaxChannel);
    g_progressWindow->SetProgress(70);

    g_progressWindow->SetStatusText(L"Loading Assets...");
	DataSystems->Initialize();
    g_progressWindow->SetProgress(75);
    //CreateScene
    g_progressWindow->SetStatusText(L"Loading Project...");
    SceneManagers->CreateScene();
    g_progressWindow->SetProgress(80);

    m_InputEvenetHandle = InputEvent.AddLambda([&](float deltaSecond)
    {
        InputManagement->Update(deltaSecond);
        InputActionManagers->Update(deltaSecond);
#ifdef EDITOR
        if (ImGui::IsKeyPressed(ImGuiKey_LeftCtrl) && ImGui::IsKeyDown(ImGuiKey_Z))
        {
			Meta::UndoCommandManager->Undo();
        }

        if (ImGui::IsKeyPressed(ImGuiKey_LeftCtrl) && ImGui::IsKeyDown(ImGuiKey_Y))
        {
			Meta::UndoCommandManager->Redo();
        }
#endif // !EDITOR
		UIManagers->Update();
        Sound->update();
    });
    g_progressWindow->SetProgress(81);
    m_SceneRenderingEventHandle = SceneRenderingEvent.AddLambda([&](float deltaSecond)
    {
        m_sceneRenderer->OnWillRenderObject(EngineSettingInstance->frameDeltaTime);
        m_sceneRenderer->SceneRendering();
    });
    g_progressWindow->SetProgress(82);
	m_OnGizmoEventHandle = OnDrawGizmosEvent.AddLambda([&]()
	{
		m_gizmoRenderer->OnDrawGizmos();
	});
    g_progressWindow->SetProgress(83);
    m_GUIRenderingEventHandle = GUIRenderingEvent.AddLambda([&]()
    {
        OnGui();
    });

    m_EndOfFrameEventHandle = endOfFrameEvent.AddLambda([&]() 
    {
        m_sceneRenderer->EndOfFrame(EngineSettingInstance->frameDeltaTime);
    });

    g_progressWindow->SetProgress(85);
    SceneManagers->ManagerInitialize();
    g_progressWindow->SetProgress(90);
	PhysicsManagers->Initialize();

    isGameToRender = true;

    PROFILE_FRAME();

    m_CB_Thread = std::thread([&] 
	{
        PROFILE_REGISTER_THREAD("[CB-Thread]");
		while (isGameToRender)
		{
            if (!m_isInvokeResize)
            {
                CommandBuildThread();
            }
		}
	});
    
    m_CE_Thread = std::thread([&]
    {
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
    });
    
    m_CB_Thread.detach();
    m_CE_Thread.detach();
}

DirectX11::Dx11Main::~Dx11Main()
{
    isGameToRender = false;
	m_deviceResources->RegisterDeviceNotify(nullptr);
    SceneManagers->Decommissioning();
    PROFILER_SHUTDOWN();
}
//test code
void DirectX11::Dx11Main::SceneInitialize()
{
	
}

void DirectX11::Dx11Main::CreateWindowSizeDependentResources()
{
	//렌더러의 창 크기에 따라 리소스를 다시 만드는 코드를 여기에 추가합니다.
    m_deviceResources->ReleaseSwapChain();
    OnResizeReleaseEvent();

    RECT rect;
    HWND hwnd = m_deviceResources->GetWindow()->GetHandle();

    GetClientRect(hwnd, &rect);
    DirectX11::Sizef size;
    size.width = rect.right - rect.left;
    size.height = rect.bottom - rect.top;

    // Create the render target view and depth stencil view.
    m_deviceResources->SetLogicalSize(size);

    OnResizeEvent(size.width, size.height);

    m_sceneRenderer->ReApplyCurrCubeMap();
}
 
void DirectX11::Dx11Main::Update()
{
	// EditorUpdate

    EngineSettingInstance->frameDeltaTime = m_timeSystem.GetElapsedSeconds();

    PROFILE_CPU_BEGIN("GameLogic");
    m_timeSystem.Tick([&]
    {
        InfoWindow();
#ifdef EDITOR
        if(!SceneManagers->m_isGameStart)
        {
            SceneManagers->Editor();
            SceneManagers->InputEvents(EngineSettingInstance->frameDeltaTime);
            SceneManagers->GameLogic();
        }
        else
        {
			SceneManagers->Editor();
            SceneManagers->Initialization();
			SceneManagers->Physics(EngineSettingInstance->frameDeltaTime);
            SceneManagers->InputEvents(EngineSettingInstance->frameDeltaTime);
            SceneManagers->GameLogic(EngineSettingInstance->frameDeltaTime);
        }
#endif // !EDITOR
    });

#ifdef EDITOR
	if (InputManagement->IsKeyReleased(VK_F5))
	{
        EngineSettingInstance->ToggleGameView();
	}

	if (InputManagement->IsKeyReleased(VK_F6))
	{
		ScriptManager->CompileEvent();
	}

	if (ImGui::IsKeyPressed(ImGuiKey_LeftCtrl) && ImGui::IsKeyPressed(ImGuiKey_W))
	{
        m_gizmoRenderer->SetWireFrame();
	}

	if (InputManagement->IsKeyDown(VK_F10))
    {
		m_sceneRenderer->SetLightmapPass();
	}
 
	if (InputManagement->IsKeyReleased(VK_F9)) 
	{
		Physics->ConnectPVD();
	}
    PROFILE_CPU_END();
#endif // !EDITOR

    EngineSettingInstance->renderBarrier.ArriveAndWait();

    PROFILE_CPU_BEGIN("EndOfFrame");
    DisableOrEnable();
    SceneManagers->EndOfFrame();
    PROFILE_CPU_END();

    PROFILE_FRAME();
    //RenderCommandFence.Begin();
    //RenderCommandFence.Wait();
    EngineSettingInstance->renderBarrier.ArriveAndWait();
}

bool DirectX11::Dx11Main::ExecuteRenderPass()
{
    PROFILE_CPU_BEGIN("CommandExecute");
    auto GameSceneStart = SceneManagers->m_isGameStart && !SceneManagers->m_isEditorSceneLoaded;
    auto GameSceneEnd = !SceneManagers->m_isGameStart && SceneManagers->m_isEditorSceneLoaded;

	// 처음 업데이트하기 전에 아무 것도 렌더링하지 마세요.
	if (m_timeSystem.GetFrameCount() == 0 || GameSceneStart || GameSceneEnd)
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
        << DeviceState::g_Viewport.Width
        << L" Height: "
        << DeviceState::g_Viewport.Height
        << L" FPS: "
        << m_timeSystem.GetFramesPerSecond()
        << L" FrameCount: "
        << m_timeSystem.GetFrameCount()
        << "<Dx11>";

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
    if (m_timeSystem.GetFrameCount() == 0 || GameSceneStart || GameSceneEnd)
    {
        PROFILE_CPU_END();
        //RenderCommandFence.Signal();
        EngineSettingInstance->renderBarrier.ArriveAndWait();
        EngineSettingInstance->renderBarrier.ArriveAndWait();
        return;
    }

    //RHICommandFence.Begin();
    m_sceneRenderer->CreateCommandListPass();
    PROFILE_CPU_END();
    //RHICommandFence.Wait();
    //RenderCommandFence.Signal();
    EngineSettingInstance->renderBarrier.ArriveAndWait();
    EngineSettingInstance->renderBarrier.ArriveAndWait();
}

void DirectX11::Dx11Main::CommandExecuteThread()
{
	if (ExecuteRenderPass())
	{
        PROFILE_CPU_BEGIN("Present");
		m_deviceResources->Present();
        PROFILE_CPU_END();
	}
    //RHICommandFence.Signal();
    EngineSettingInstance->renderBarrier.ArriveAndWait();
    EngineSettingInstance->renderBarrier.ArriveAndWait();
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

