#include "GameApp.h"
#include "Camera.h"
#include "InputManager.h"
#include "PathFinder.h"
#include "DumpHandler.h"
#include "CoreWindow.h"
#include "DataSystem.h"
#include "DebugStreamBuf.h"
#include "EngineSetting.h"
#include "HotLoadSystem.h"
#include "EffectProxyController.h"
#include "PrefabUtility.h"
#include "TagManager.h"
#include "ShaderSystem.h"
#include "ReflectionRegister.h"
#include "ReflectionVectorFactory.h"
#include "DeviceState.h"
#include "ReflectionVectorInvoker.h"
#include "ComponentFactory.h"
#include <imgui_impl_win32.h>
#include <ppltasks.h>
#include <ppl.h>
#include "InputActionManager.h"

#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

MAIN_ENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow)
{
	HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	if (FAILED(hr))
	{
		return hr;
	}

	Meta::RegisterClassInitalize();
	Meta::VectorFactoryRegistry::GetInstance();
	Meta::VectorInvokerRegistry::GetInstance();
	PathFinder::Initialize();
	Log::Initialize();

	EngineSettingInstance->Initialize();

	//����
	CoreWindow::RegisterCreateEventHandler([](HWND hWnd, WPARAM wParam, LPARAM lParam) -> LRESULT
	{
		return 0;
	});

	static DebugStreamBuf debugBuf(std::cout.rdbuf());
	std::cout.rdbuf(&debugBuf);

	{
		GameBuilder::App app;
		app.Initialize(hInstance, L"How To Train Asis", 1920, 1080);
		app.Finalize();
	}

	CameraContainer::Destroy();
	ComponentFactory::Destroy();
	HotLoadSystem::Destroy();
	SceneManager::Destroy();
	PhysicsManager::Destroy();
	PhysicX::Destroy();
	EngineSetting::Destroy();
	TagManager::Destroy();
	EffectManager::Destroy();
	EffectProxyController::Destroy();
	InputManager::Destroy();
	DataSystem::Destroy();
	PrefabUtility::Destroy();
	ShaderResourceSystem::Destroy();
	Meta::RegisterClassFinalize();
	Meta::VectorFactoryRegistry::Destroy();
	Meta::VectorInvokerRegistry::Destroy();
	DirectX11::DeviceResourceManager::Destroy();

	Log::Finalize();

	CoUninitialize();

	return 0;
}

void GameBuilder::App::Initialize(HINSTANCE hInstance, const wchar_t* title, int width, int height)
{
	DirectX11::DeviceResourceManager::GetInstance();
	ShaderResourceSystem::GetInstance();
	EngineSetting::GetInstance();
	TagManager::GetInstance();
	InputManager::GetInstance();
	PrefabUtility::GetInstance();
	EffectManager::GetInstance();
	EffectProxyController::GetInstance();
	DataSystem::GetInstance();
	PhysicX::GetInstance();
	PhysicsManager::GetInstance();
	SceneManager::GetInstance();
	HotLoadSystem::GetInstance();
	ComponentFactory::GetInstance();
	CameraContainer::GetInstance();

	CoreWindow coreWindow(hInstance, title, width, height);
	CoreWindow::SetDumpType(DUMP_TYPE::DUNP_TYPE_MINI);
	m_hWnd = coreWindow.GetHandle();
	m_deviceResources = std::make_shared<DirectX11::DeviceResources>();
	SetWindow(coreWindow);
	RegisterHandler(coreWindow);
	Load();
	Run();
}

void GameBuilder::App::Finalize()
{
	m_main->Finalize();
	m_deviceResources->ReportLiveDeviceObjects();
}

void GameBuilder::App::SetWindow(CoreWindow& coreWindow)
{
	m_deviceResources->SetWindow(coreWindow);
}

void GameBuilder::App::RegisterHandler(CoreWindow& coreWindow)
{
	coreWindow.RegisterHandler(WM_SIZE, this, &App::HandleResizeEvent);
	coreWindow.RegisterHandler(WM_KEYDOWN, this, &App::HandleCharEvent);
	coreWindow.RegisterHandler(WM_CLOSE, this, &App::Shutdown);
}

void GameBuilder::App::Load()
{
	if (nullptr == m_main)
	{
		m_main = std::make_unique<DirectX11::GameMain>(m_deviceResources);
	}
}

void GameBuilder::App::Run()
{
	CoreWindow::GetForCurrentInstance()->InitializeTask([&]
	{
		m_main->Initialize();
		InputManagement->Initialize(m_hWnd);
	})
	.Then([&]
	{
		// ���� ����
		m_main->Update();
	});
}

LRESULT GameBuilder::App::Shutdown(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
	m_windowClosed = true;
	PostQuitMessage(0);
	return 0;
}

LRESULT GameBuilder::App::HandleCharEvent(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
	ImGuiIO& io = ImGui::GetIO();

	wchar_t wch = 0;
	static BYTE KeyState[256];
	GetKeyboardState(KeyState);
	// Virtual Key�� Unicode ���ڷ� ��ȯ
	if (ToUnicode((UINT)wParam, (UINT)lParam, KeyState, &wch, 1, 0) > 0)
	{
		io.AddInputCharacter(wch);
	}

	return 0;
}

LRESULT GameBuilder::App::HandleResizeEvent(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
	if (wParam == SIZE_MINIMIZED)
	{
		EngineSettingInstance->SetMinimized(true);
		return 0; // �ּ�ȭ�� ��� ����
	}

	if (EngineSettingInstance->IsMinimized())
	{
		if (wParam == SIZE_RESTORED || wParam == SIZE_MAXIMIZED)
		{
			EngineSettingInstance->SetMinimized(false);
			return 0; // ������ ��� ����
		}
	}

	m_main->InvokeResizeFlag();

	return 0;
}