#include "Dx11Main.h"
#include "Utility_Framework/CoreWindow.h"
#include "InputManager.h"
#include "ImGuiHelper/ImGuiRegister.h"
#include "RenderEngine/UI_DataSystem.h"
#include "Physics/Common.h"
#include "SoundManager.h"
#include "MeshEditor.h"
#include "RenderEngine/FontManager.h"
#include "Utility_Framework/Banchmark.hpp"
#include "Utility_Framework/ImGuiLogger.h"
DirectX11::Dx11Main::Dx11Main(const std::shared_ptr<DeviceResources>& deviceResources)	: m_deviceResources(deviceResources)
{
	m_deviceResources->RegisterDeviceNotify(this);

	//아래 렌더러	초기화 코드를 여기에 추가합니다.
	m_sceneRenderer = std::make_shared<SceneRenderer>(m_deviceResources);

	UISystem->Initialize(m_deviceResources);
	Sound->initialize((int)ChannelType::MaxChannel);
	FontSystem->Initialize();

	m_sceneRenderer->Initialize();
	m_imguiRenderer = std::make_unique<ImGuiRenderer>(m_deviceResources);

	EventInitialize();
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
	if (m_isLoading || m_isChangeScene)
	{
		m_timeSystem.Tick([&]
		{
			Sound->update();
		});

		return;
	}

	//렌더러의 업데이트 코드를 여기에 추가합니다.
	m_timeSystem.Tick([&]
	{
		//렌더러의 업데이트 코드를 여기에 추가합니다.
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
		//렌더러의 업데이트 코드를 여기에 추가합니다.
	
		Sound->update();
		InputManagement->Update(m_timeSystem.GetElapsedSeconds());
		//InputManagement->UpdateControllerVibration(m_timeSystem.GetElapsedSeconds()); //패드 진동 업데이트*****
	});

	if (InputManagement->IsKeyReleased(VK_F5))
	{
		m_isGameView = !m_isGameView;
	}
#ifdef EDITOR
	if (InputManagement->IsKeyReleased(VK_F6))
	{
		loadlevel = 0;
	}
	if (InputManagement->IsKeyDown(VK_F11)) {
		m_sceneRenderer->SetWireFrame();
	}
#endif // !EDITOR
	if (InputManagement->IsKeyReleased(VK_F8))
	{
		loadlevel++;
		if (loadlevel > 2)
		{
			loadlevel = 0;
		}
	}
	if (InputManagement->IsKeyReleased(VK_F9)) {
		Physics->ConnectPVD();
	}

#if defined(EDITOR)
#endif // !Editor

}

bool DirectX11::Dx11Main::Render()
{
	// 처음 업데이트하기 전에 아무 것도 렌더링하지 마세요.
	if (m_timeSystem.GetFrameCount() == 0)
	{
		return false;
	}

	{
		m_sceneRenderer->Update(m_timeSystem.GetElapsedSeconds());
		m_sceneRenderer->Render();
	}

#if defined(EDITOR)
	if(!m_isGameView)
	{
		m_imguiRenderer->BeginRender();
		m_sceneRenderer->EditorView();
		m_imguiRenderer->Render();
		m_imguiRenderer->EndRender();
	}
#endif // !EDITOR

	Debug->Flush();

	return true;
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

void DirectX11::Dx11Main::EventInitialize()
{
}

void DirectX11::Dx11Main::LoadScene()
{
	while(true)
	{
	}
}




