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
#include "SpinLock.h"
#include "Core.FenceFlag.h"
#include "InputActionManager.h"

std::atomic_flag gameToRenderLock = ATOMIC_FLAG_INIT;
std::atomic<bool> isGameToRender = false;
std::atomic<uint64> gameFrame{};
std::atomic<uint64> renderFrame{};
std::atomic<double> frameDeltaTime{};
FenceFlag fenceGameToRender;

DirectX11::Dx11Main::Dx11Main(const std::shared_ptr<DeviceResources>& deviceResources)	: m_deviceResources(deviceResources)
{
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
        m_sceneRenderer->OnWillRenderObject(frameDeltaTime);
    });

    g_progressWindow->SetProgress(85);
    SceneManagers->ManagerInitialize();
    g_progressWindow->SetProgress(90);
	PhysicsManagers->Initialize();

	isGameToRender = true;

    m_renderThread = std::thread([&] 
	{
		while (isGameToRender)
		{
            if (m_isInvokeResize)
            {
                CreateWindowSizeDependentResources();
                m_isInvokeResize = false;
            }

            CoroutineManagers->yield_OnRender();
			RenderWorkerThread();
		}
	});
    m_renderThread.detach();
}

DirectX11::Dx11Main::~Dx11Main()
{
	m_deviceResources->RegisterDeviceNotify(nullptr);
    SceneManagers->Decommissioning();
	isGameToRender = false;
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
	SpinLock lock(gameToRenderLock);

    m_timeSystem.Tick([&]
    {
        InfoWindow();
#ifdef EDITOR
        if(!SceneManagers->m_isGameStart)
        {
            SceneManagers->Editor();
            SceneManagers->InputEvents(m_timeSystem.GetElapsedSeconds());
            SceneManagers->GameLogic();
        }
        else
        {
			SceneManagers->Editor();
            SceneManagers->Initialization();
			SceneManagers->Physics(m_timeSystem.GetElapsedSeconds());
            SceneManagers->InputEvents(m_timeSystem.GetElapsedSeconds());
            SceneManagers->GameLogic(m_timeSystem.GetElapsedSeconds());
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
#endif // !EDITOR

    if(gameFrame > renderFrame)
    {
    }
        fenceGameToRender.Wait();

    frameDeltaTime = m_timeSystem.GetElapsedSeconds();
    SceneManagers->EndOfFrame();

    fenceGameToRender.Reset();
}

bool DirectX11::Dx11Main::Render()
{
	// 처음 업데이트하기 전에 아무 것도 렌더링하지 마세요.
	if (m_timeSystem.GetFrameCount() == 0)
    { 
        fenceGameToRender.Signal();
        renderFrame.store(gameFrame);
        return false;
    }

    auto GameSceneStart = SceneManagers->m_isGameStart && !SceneManagers->m_isEditorSceneLoaded;
    auto GameSceneEnd = !SceneManagers->m_isGameStart && SceneManagers->m_isEditorSceneLoaded;
    
    if (GameSceneStart || GameSceneEnd)
    {
        fenceGameToRender.Signal();
        renderFrame.store(gameFrame);
        return false;
    }

	{
        SceneManagers->SceneRendering(frameDeltaTime);
#if defined(EDITOR)
		SceneManagers->OnDrawGizmos();
        SceneManagers->GUIRendering();
#endif // !EDITOR
	}

    fenceGameToRender.Signal();
    renderFrame.store(gameFrame);
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

    gameFrame = m_timeSystem.GetFrameCount();
}

void DirectX11::Dx11Main::OnGui()
{
    if (!EngineSettingInstance->IsGameView())
    {
        m_imguiRenderer->BeginRender();
		m_menuBarWindow->RenderMenuBar();
		m_sceneViewWindow->RenderSceneViewWindow();
		m_gameViewWindow->RenderGameViewWindow();
        m_gizmoRenderer->EditorView();
        m_imguiRenderer->Render();
        m_imguiRenderer->EndRender();
    }
}

void DirectX11::Dx11Main::DisableOrEnable()
{
	SceneManagers->DisableOrEnable();
}

void DirectX11::Dx11Main::RenderWorkerThread()
{
	if (Render())
	{
		m_deviceResources->Present();
	}
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


