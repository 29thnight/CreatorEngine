#include "Dx11Main.h"
#include "Utility_Framework/CoreWindow.h"
#include "InputManager.h"
#include "ImGuiHelper/ImGuiRegister.h"
#include "Physics/Common.h"
#include "SoundManager.h"
#include "Utility_Framework/Banchmark.hpp"
#include "Utility_Framework/ImGuiLogger.h"
#include "Utility_Framework/TimeSystem.h"
#include "ScriptBinder/HotLoadSystem.h"
#include "RenderEngine/DataSystem.h"
#include "RenderEngine/ShaderSystem.h"

DirectX11::Dx11Main::Dx11Main(const std::shared_ptr<DeviceResources>& deviceResources)	: m_deviceResources(deviceResources)
{
	m_deviceResources->RegisterDeviceNotify(this);

	m_sceneRenderer = std::make_shared<SceneRenderer>(m_deviceResources);
	m_sceneRenderer->Initialize();

	Sound->initialize((int)ChannelType::MaxChannel);
	m_imguiRenderer = std::make_unique<ImGuiRenderer>(m_deviceResources);

	ScriptManager->Initialize();
	DataSystems->Initialize();

	SceneInitialize();
}

DirectX11::Dx11Main::~Dx11Main()
{
	m_deviceResources->RegisterDeviceNotify(nullptr);
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
        InputManagement->Update(m_timeSystem.GetElapsedSeconds());
        Sound->update();
    });

    if(m_isGameStart)
    {
        //GameUpdate
        m_timeSystem.Tick([&]
        {
                //Sound->update();

                //InputManagement->UpdateControllerVibration(m_timeSystem.GetElapsedSeconds()); //패드 진동 업데이트*****
        });
    }

	if (InputManagement->IsKeyReleased(VK_F5))
	{
		m_isGameView = !m_isGameView;
	}
#ifdef EDITOR
	if (InputManagement->IsKeyReleased(VK_F6))
	{
		//loadlevel = 0;
	}
	if (InputManagement->IsKeyDown(VK_F11)) 
	{
		m_sceneRenderer->SetWireFrame();
	}
	if (InputManagement->IsKeyDown(VK_F10)) {
		m_sceneRenderer->SetLightmapPass();
	}
#endif // !EDITOR
	if (InputManagement->IsKeyReleased(VK_F9)) 
	{
		Physics->ConnectPVD();
	}

#if defined(EDITOR)
#endif // !Editor

}

bool DirectX11::Dx11Main::Render()
{
	// 처음 업데이트하기 전에 아무 것도 렌더링하지 마세요.
	if (m_timeSystem.GetFrameCount() == 0) return false;
	{
		m_sceneRenderer->OnWillRenderObject(m_timeSystem.GetElapsedSeconds());
		m_sceneRenderer->SceneRendering();
	}

#if defined(EDITOR)
    OnGui();
#endif // !EDITOR

	Debug->Flush();

	return true;
}

void DirectX11::Dx11Main::OnGui()
{
    if (!m_isGameView)
    {
        m_imguiRenderer->BeginRender();
        m_sceneRenderer->EditorView();
        m_imguiRenderer->Render();
        m_imguiRenderer->EndRender();
    }
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


