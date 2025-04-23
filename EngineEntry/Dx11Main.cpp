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
#include "UIManager.h"

DirectX11::Dx11Main::Dx11Main(const std::shared_ptr<DeviceResources>& deviceResources)	: m_deviceResources(deviceResources)
{
	m_deviceResources->RegisterDeviceNotify(this);


	m_sceneRenderer = std::make_shared<SceneRenderer>(m_deviceResources);
    //init Engine GUI windows
	m_renderPassWindow = std::make_unique<RenderPassWindow>(m_sceneRenderer.get());
	m_sceneViewWindow = std::make_unique<SceneViewWindow>(m_sceneRenderer.get());
	m_menuBarWindow = std::make_unique<MenuBarWindow>(m_sceneRenderer.get());
	m_gameViewWindow = std::make_unique<GameViewWindow>(m_sceneRenderer.get());
	m_hierarchyWindow = std::make_unique<HierarchyWindow>(m_sceneRenderer.get());
	m_inspectorWindow = std::make_unique<InspectorWindow>(m_sceneRenderer.get());

    //CreateScene
    SceneManagers->CreateScene();

	Sound->initialize((int)ChannelType::MaxChannel);
	m_imguiRenderer = std::make_unique<ImGuiRenderer>(m_deviceResources);

	ScriptManager->Initialize();
	DataSystems->Initialize();

    m_InputEvenetHandle = SceneManagers->InputEvent.AddLambda([&](float deltaSecond)
    {
        InputManagement->Update(deltaSecond);
        if (ImGui::IsKeyPressed(ImGuiKey_LeftCtrl) && ImGui::IsKeyDown(ImGuiKey_Z))
        {
			Meta::UndoCommandManager->Undo();
        }

        if (ImGui::IsKeyPressed(ImGuiKey_LeftCtrl) && ImGui::IsKeyDown(ImGuiKey_Y))
        {
			Meta::UndoCommandManager->Redo();
        }
        //Mathf::Vector2 mousePos = InputManagement->GetMousePos();
        /*if(InputManagement->IsMouseButtonDown(MouseKey::LEFT))
          UIManagers->m_clickEvent.Broadcast(mousePos);*/
		UIManagers->Update();
        Sound->update();
    });

    m_SceneRenderingEventHandle = SceneManagers->SceneRenderingEvent.AddLambda([&](float deltaSecond)
    {
        m_sceneRenderer->OnWillRenderObject(deltaSecond);
        m_sceneRenderer->SceneRendering();
    });

    m_GUIRenderingEventHandle = SceneManagers->GUIRenderingEvent.AddLambda([&]()
    {
        OnGui();
    });

    SceneManagers->ManagerInitialize();
}

DirectX11::Dx11Main::~Dx11Main()
{
	m_deviceResources->RegisterDeviceNotify(nullptr);
    SceneManagers->Deccommissioning();
}
//test code
void DirectX11::Dx11Main::SceneInitialize()
{
	
}

void DirectX11::Dx11Main::CreateWindowSizeDependentResources()
{
	//렌더러의 창 크기에 따라 리소스를 다시 만드는 코드를 여기에 추가합니다.
	m_deviceResources->ResizeResources();
}

void DirectX11::Dx11Main::Update()
{
	// EditorUpdate
    m_timeSystem.Tick([&]
    {
        InfoWindow();
        SceneManagers->InputEvents(m_timeSystem.GetElapsedSeconds());
        if(!SceneManagers->m_isGameStart)
        {
            SceneManagers->GameLogic(0);
        }
        else
        {
            SceneManagers->GameLogic(m_timeSystem.GetElapsedSeconds());
        }
    });

#ifdef EDITOR
	if (InputManagement->IsKeyReleased(VK_F5))
	{
        EngineSettingInstance->ToggleGameView();
	}

	if (InputManagement->IsKeyReleased(VK_F6))
	{
		//loadlevel = 0;
	}

	if (InputManagement->IsKeyDown(VK_F11)) 
	{
		m_sceneRenderer->SetWireFrame();
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
}

bool DirectX11::Dx11Main::Render()
{
	// 처음 업데이트하기 전에 아무 것도 렌더링하지 마세요.
	if (m_timeSystem.GetFrameCount() == 0) return false;
	{
        SceneManagers->SceneRendering(m_timeSystem.GetElapsedSeconds());
	}

#if defined(EDITOR)
    SceneManagers->GUIRendering();
#endif // !EDITOR

	return true;
}

void DirectX11::Dx11Main::InfoWindow()
{
    std::wostringstream woss;
    woss.precision(6);
    woss << L"Creator Editor - "
        << L"Width: "
        << m_deviceResources->GetOutputSize().width
        << L" Height: "
        << m_deviceResources->GetOutputSize().height
        << L" FPS: "
        << m_timeSystem.GetFramesPerSecond()
        << L" FrameCount: "
        << m_timeSystem.GetFrameCount();

    SetWindowText(m_deviceResources->GetWindow()->GetHandle(), woss.str().c_str());
}

void DirectX11::Dx11Main::OnGui()
{
    if (!m_isGameView)
    {
        m_imguiRenderer->BeginRender();
		m_menuBarWindow->RenderMenuBar();
		m_sceneViewWindow->RenderSceneViewWindow();
		m_gameViewWindow->RenderGameViewWindow();
        m_sceneRenderer->EditorView();
        m_imguiRenderer->Render();
        m_imguiRenderer->EndRender();
    }
}

void DirectX11::Dx11Main::DisableOrEnable()
{
	SceneManagers->DisableOrEnable();
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


