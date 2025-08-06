#include "GameMain.h"
#include "CoreWindow.h"
#include "InputManager.h"
#include "ImGuiRegister.h"
#include "Physx.h"
#include "SoundManager.h"
#include "Benchmark.hpp"
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
#include "EffectManager.h"
#include "AIManager.h"
#include "TagManager.h"
#include "EffectProxyController.h"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
//#include "SwapEvent.h"

std::atomic<bool> isGameToRender = false;
std::atomic<bool> isCB_Thread_End = false;
std::atomic<bool> isCE_Thread_End = false;

DirectX11::GameMain::GameMain(const std::shared_ptr<DeviceResources>& deviceResources) : m_deviceResources(deviceResources)
{
}

DirectX11::GameMain::~GameMain()
{
}

void DirectX11::GameMain::Initialize()
{
    m_deviceResources->RegisterDeviceNotify(this);

    XMFLOAT3 center = { 0.0f, 0.0f, 0.0f };
    XMFLOAT3 extents = { 2000.f, 2000.f, 2000.f };
    BoundingBox fixedBounds(center, extents);
    CullingManagers->Initialize(fixedBounds, 3, 30);
    TagManagers->Initialize();

    m_sceneRenderer = std::make_shared<SceneRenderer>(m_deviceResources);
    m_imguiRenderer = std::make_unique<ImGuiRenderer>(m_deviceResources);

    ScriptManager->Initialize();
    Sound->initialize((int)ChannelType::MaxChannel);
    DataSystems->Initialize();
    SceneManagers->CreateScene();

    m_InputEvenetHandle = InputEvent.AddLambda([&](float deltaSecond)
    {
        UIManagers->Update();
        Sound->update();
    });

    m_SceneRenderingEventHandle = SceneRenderingEvent.AddLambda([&](float deltaSecond)
    {
        m_sceneRenderer->OnWillRenderObject(EngineSettingInstance->frameDeltaTime);
        m_sceneRenderer->SceneRendering();
    });

    m_GUIRenderingEventHandle = GUIRenderingEvent.AddLambda([&]()
    {
        OnGui();
    });

    m_EndOfFrameEventHandle = endOfFrameEvent.AddLambda([&]()
    {
        m_sceneRenderer->EndOfFrame(EngineSettingInstance->frameDeltaTime);
    });

    SceneManagers->ManagerInitialize();
    PhysicsManagers->Initialize();

    std::wstring sceneName = /*EngineSettingInstance->GetStartupSceneName()*/L"real8pre.creator";
    file::path scenePath = PathFinder::Relative("Scenes").append(sceneName);
    SceneManagers->LoadSceneImmediate(scenePath.string());

    isGameToRender = true;

    m_CB_Thread = std::thread([&]
    {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(hr))
        {
            return;
        }

        while (isGameToRender)
        {
            if (!m_isInvokeResize)
            {
                CommandBuildThread();
            }
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

        while (isGameToRender)
        {
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

    m_CB_Thread.detach();
    m_CE_Thread.detach();
}

void DirectX11::GameMain::Finalize()
{
    isGameToRender = false;
    TagManagers->Finalize();
    SceneManagers->Decommissioning();
    EngineSettingInstance->SaveSettings();
    EngineSettingInstance->renderBarrier.Finalize();

    while (!isCB_Thread_End || !isCE_Thread_End)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    m_sceneRenderer->Finalize();
    ShaderSystem->Finalize();
    OnResizeReleaseEvent.Clear();
    OnResizeEvent.Clear();
    m_deviceResources->RegisterDeviceNotify(nullptr);
}

void DirectX11::GameMain::CreateWindowSizeDependentResources()
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

void DirectX11::GameMain::Update()
{
    // EditorUpdate
    EngineSettingInstance->frameDeltaTime = m_timeSystem.GetElapsedSeconds();

    m_timeSystem.Tick([&]
    {
        InfoWindow();
        InputManagement->Update(EngineSettingInstance->frameDeltaTime);

        SceneManagers->Initialization();
        SceneManagers->Physics(EngineSettingInstance->frameDeltaTime);
        SceneManagers->InputEvents(EngineSettingInstance->frameDeltaTime);
        SceneManagers->GameLogic(EngineSettingInstance->frameDeltaTime);
    });

    EngineSettingInstance->renderBarrier.ArriveAndWait();

    DisableOrEnable();
    SceneManagers->EndOfFrame();
    //RenderCommandFence.Begin();
    //RenderCommandFence.Wait();
    EngineSettingInstance->renderBarrier.ArriveAndWait();
}

bool DirectX11::GameMain::ExecuteRenderPass()
{
    // 처음 업데이트하기 전에 아무 것도 렌더링하지 마세요.
    if (m_timeSystem.GetFrameCount() == 0)
    {
        return false;
    }

    {
        SceneManagers->SceneRendering(EngineSettingInstance->frameDeltaTime);
        SceneManagers->GUIRendering();
    }
    return true;
}

void DirectX11::GameMain::InfoWindow()
{
    std::wostringstream woss;
    woss.precision(6);
	woss << EngineSettingInstance->GetBuildGameName()
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

void DirectX11::GameMain::OnGui()
{
    if (!EngineSettingInstance->IsGameView())
    {
        m_imguiRenderer->BeginRender();
        m_imguiRenderer->Render();
        m_imguiRenderer->EndRender();
    }
}

void DirectX11::GameMain::DisableOrEnable()
{
    SceneManagers->DisableOrEnable();
}

void DirectX11::GameMain::CommandBuildThread()
{
    // 처음 업데이트하기 전에 아무 것도 하지 마세요.
    if (m_timeSystem.GetFrameCount() == 0)
    {
        //RenderCommandFence.Signal();
        EngineSettingInstance->renderBarrier.ArriveAndWait();
        EngineSettingInstance->renderBarrier.ArriveAndWait();
        return;
    }

    //RHICommandFence.Begin();
    m_sceneRenderer->CreateCommandListPass();
    //RHICommandFence.Wait();
    //RenderCommandFence.Signal();
    EngineSettingInstance->renderBarrier.ArriveAndWait();
    EngineSettingInstance->renderBarrier.ArriveAndWait();
}

void DirectX11::GameMain::CommandExecuteThread()
{
    if (ExecuteRenderPass())
    {
        m_deviceResources->Present();
    }
    //RHICommandFence.Signal();
    EngineSettingInstance->renderBarrier.ArriveAndWait();
    EngineSettingInstance->renderBarrier.ArriveAndWait();
}

void DirectX11::GameMain::InvokeResizeFlag()
{
    m_isInvokeResize = true;
}

// 릴리스가 필요한 디바이스 리소스를 렌더러에 알립니다.
void DirectX11::GameMain::OnDeviceLost()
{

}

// 디바이스 리소스가 이제 다시 만들어질 수 있음을 렌더러에 알립니다.
void DirectX11::GameMain::OnDeviceRestored()
{
    CreateWindowSizeDependentResources();
}
