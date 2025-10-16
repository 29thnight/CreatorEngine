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
#include "EngineBootstrap.h"

#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

MAIN_ENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow)
{
	return EngineBootstrap::Run<GameBuilder::App>(hInstance, L"Kori: the Spritail", 1920, 1080);
}

void GameBuilder::App::Initialize(HINSTANCE hInstance, const wchar_t* title, int width, int height)
{
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
		// 메인 루프
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
	// Virtual Key를 Unicode 문자로 변환
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
		return 0; // 최소화된 경우 무시
	}

	if (EngineSettingInstance->IsMinimized())
	{
		if (wParam == SIZE_RESTORED || wParam == SIZE_MAXIMIZED)
		{
			EngineSettingInstance->SetMinimized(false);
			return 0; // 복원된 경우 무시
		}
	}

	m_main->InvokeResizeFlag();

	return 0;
}