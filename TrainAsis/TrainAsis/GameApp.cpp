#include "GameApp.h"
#include "Camera.h"
#include "InputManager.h"
#include "PathFinder.h"
#include "DumpHandler.h"
#include "CoreWindow.h"
#include "DataSystem.h"
#include "DebugStreamBuf.h"
#include "EngineSetting.h"
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
#include "PakHelper.h"

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
	// 덤프 기록자 등록은 EngineBootstrap::InitializeRuntime이 부팅 초반에 이미 했다.
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
#ifdef BUILD_FLAG
	DataSystems->Finalize();
#endif // !BUILD_FLAG
	CleanupUnpackedGameAssets();
}

void GameBuilder::App::SetWindow(CoreWindow& coreWindow)
{
	m_deviceResources->SetWindow(coreWindow);
}

void GameBuilder::App::RegisterHandler(CoreWindow& coreWindow)
{
	coreWindow.RegisterHandler(WM_SIZE, this, &App::HandleResizeEvent);
	// WM_KEYDOWN은 더 이상 가로채지 않는다. 문자 입력은 메시지 펌프의
	// TranslateMessage가 만드는 WM_CHAR로 ImGui 백엔드에 그대로 전달된다.
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

// HandleCharEvent를 제거했다. App.cpp에 있던 것과 한 글자도 다르지 않은 사본으로,
// 메시지 펌프에 TranslateMessage가 없어 WM_CHAR가 생성되지 않던 것을 ToUnicode로
// 우회하던 코드다. 펌프를 표준대로 되돌렸으므로(CoreWindow::Then) 필요 없다.

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