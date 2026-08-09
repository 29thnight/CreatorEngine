#include "GameMain.h"
#include "CoreWindow.h"
#include "InputManager.h"
#include "ImGuiRegister.h"
#include "Physx.h"
#include "SoundManager.h"
#include "Benchmark.hpp"
#include "TimeSystem.h"
#include "DataSystem.h"
#include "ShaderSystem.h"
#include "SceneManager.h"
#include "EngineSetting.h"
#include "UIManager.h"
#include "InputActionManager.h"
#include "Profiler.h"
#include "DeviceState.h"
#include "WinProcProxy.h"
#include "AIManager.h"
#include "TagManager.h"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

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

    // 코어(DeviceResources)가 백버퍼 RTV를 다시 만들 때마다 전역 렌더 상태를
    // 갱신한다. 예전에는 DeviceResources.cpp가 DeviceStates에 직접 대입했는데
    // (코어→렌더 역방향), 방향을 뒤집어 진입점이 배선을 소유한다.
    // 등록 시점에 RTV가 이미 있으면 즉시 한 번 호출되므로 초기값도 보장된다.
    m_deviceResources->SetBackBufferPublishCallback([](ID3D11RenderTargetView* rtv)
    {
        DirectX11::DeviceStates->g_backBufferRTV = rtv;
    });

    // 옥트리 컬링 초기화가 여기 있었다 — 계통 전체를 걷었다
    // (RenderSceneViewPlan ③).
    TagManagers->Initialize();

    m_sceneRenderer = std::make_shared<SceneRenderer>(m_deviceResources);
    m_imguiRenderer = std::make_unique<ImGuiRenderer>(m_deviceResources);

    Sound->initialize(128);
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

    std::wstring sceneName = EngineSettingInstance->GetStartupSceneName();
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
            if (m_isInvokeResize)
            {
                EngineSettingInstance->renderBarrier.ArriveAndWait(0);
                EngineSettingInstance->renderBarrier.ArriveAndWait(1);
                std::this_thread::yield();
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

    // detach하지 않는다. Finalize에서 join으로 회수한다(Dx11Main과 같은 이유).
}

void DirectX11::GameMain::Finalize()
{
    // 렌더 스레드를 먼저 완전히 세우고 나서 그들이 만지던 것을 부순다.
    // (에디터 쪽 Dx11Main::Finalize와 같은 이유 — 거기에 자세히 적어 두었다)
    isGameToRender = false;
    EngineSettingInstance->renderBarrier.Finalize();

    if (m_CB_Thread.joinable()) m_CB_Thread.join();
    if (m_CE_Thread.joinable()) m_CE_Thread.join();

    TagManagers->Finalize();
    SceneManagers->Decommissioning();
    EngineSettingInstance->SaveSettings();

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
    const bool isPaused = SceneManagers->IsGamePaused();
    const double deltaSeconds = Time->GetElapsedSeconds();
    EngineSettingInstance->frameDeltaTime = isPaused ? 0.0 : deltaSeconds;

    Time->Tick([&]
    {
        InfoWindow();
        InputManagement->Update(EngineSettingInstance->frameDeltaTime);

        SceneManagers->Initialization();
        SceneManagers->InputEvents(EngineSettingInstance->frameDeltaTime);
        if (!SceneManagers->IsGamePaused())
        {
            SceneManagers->Physics(EngineSettingInstance->frameDeltaTime);
            SceneManagers->GameLogic(EngineSettingInstance->frameDeltaTime);
        }
        else
        {
            SceneManagers->Pausing();
        }
    });

    EngineSettingInstance->renderBarrier.ArriveAndWait(0);

    DisableOrEnable();
    SceneManagers->EndOfFrame();
    //RenderCommandFence.Begin();
    //RenderCommandFence.Wait();
    EngineSettingInstance->renderBarrier.ArriveAndWait(1);

    if (SceneManagers->IsDecommissioning())
    {
        HWND handle = m_deviceResources->GetWindow()->GetHandle();
        PostMessage(handle, WM_CLOSE, 0, 0);
    }
}

bool DirectX11::GameMain::ExecuteRenderPass()
{
    // 처음 업데이트하기 전에 아무 것도 렌더링하지 마세요.
    if (Time->GetFrameCount() == 0)
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
        << DeviceStates->g_Viewport.Width
        << L" Height: "
        << DeviceStates->g_Viewport.Height
        << L" FPS: "
        << Time->GetFramesPerSecond()
        << L" FrameCount: "
        << Time->GetFrameCount()
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
    if (Time->GetFrameCount() == 0)
    {
        //RenderCommandFence.Signal();
        EngineSettingInstance->renderBarrier.ArriveAndWait(0);
        EngineSettingInstance->renderBarrier.ArriveAndWait(1);
        return;
    }

    //RHICommandFence.Begin();
    m_sceneRenderer->CreateCommandListPass();
    //RHICommandFence.Wait();
    //RenderCommandFence.Signal();
    EngineSettingInstance->renderBarrier.ArriveAndWait(0);
    EngineSettingInstance->renderBarrier.ArriveAndWait(1);
}

void DirectX11::GameMain::CommandExecuteThread()
{
    if (ExecuteRenderPass())
    {
        m_deviceResources->Present();
    }
    //RHICommandFence.Signal();
    EngineSettingInstance->renderBarrier.ArriveAndWait(0);
    EngineSettingInstance->renderBarrier.ArriveAndWait(1);
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
