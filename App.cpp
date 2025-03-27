#include "App.h"
#include "InputManager.h"
#include "Utility_Framework/PathFinder.h"
#include "Utility_Framework/DumpHandler.h"
#include "Utility_Framework/Core.Console.hpp"
#include "Utility_Framework/CoreWindow.h"
#include "CustomWindowDefine.h"
#include <imgui_impl_win32.h>
#include <ppltasks.h>
#include <ppl.h>
#ifdef UNICODE
#pragma comment(linker, "/entry:wWinMainCRTStartup /subsystem:console")
#else
#pragma comment(linker, "/entry:WinMainCRTStartup /subsystem:console")
#endif

MAIN_ENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow)
{
	PathFinder::Initialize();
	Log::startLOG("logs.db");
	Concurrency::SchedulerPolicy policy
	{
		3,
		Concurrency::MinConcurrency,
		6,
		Concurrency::MaxConcurrency,
		8,
		Concurrency::ContextPriority,
		THREAD_PRIORITY_HIGHEST
	};
	Concurrency::Scheduler::SetDefaultSchedulerPolicy(policy);
	Concurrency::task init = concurrency::create_task([]()
	{
		return std::string("SUCCESS");
	});
	std::string initResult = init.get();

	//시작
	CoreWindow::RegisterCreateEventHandler([](HWND hWnd, WPARAM wParam, LPARAM lParam) -> LRESULT
	{
		return 0;
	});

	Core::App app;
	app.Initialize(hInstance, L"Creator Editor", 1920, 1080);

	return 0;
}

void Core::App::Initialize(HINSTANCE hInstance, const wchar_t* title, int width, int height)
{
	CoreWindow coreWindow(hInstance, title, width, height);
	CoreWindow::SetDumpType(DUMP_TYPE::DUNP_TYPE_MINI);
	m_deviceResources = std::make_shared<DirectX11::DeviceResources>();
	SetWindow(coreWindow);
	InputManagement->Initialize(coreWindow.GetHandle());
	Load();
	Run();
}

void Core::App::SetWindow(CoreWindow& coreWindow)
{
	m_deviceResources->SetWindow(coreWindow);
	coreWindow.RegisterHandler(WM_INPUT, this, &App::ProcessRawInput);
	coreWindow.RegisterHandler(WM_KEYDOWN, this, &App::HandleCharEvent);
	coreWindow.RegisterHandler(WM_CLOSE, this, &App::Shutdown);
	//coreWindow.RegisterHandler(WM_SIZE, this, &App::HandleResizeEvent);
}

void Core::App::Load()
{
	if (nullptr == m_main)
	{
		m_main = std::make_unique<DirectX11::Dx11Main>(m_deviceResources);
	}
}

void Core::App::Run()
{
	CoreWindow::GetForCurrentInstance()->InitializeTask([&]
	{
		// 초기화 작업
	})
	.Then([&]
	{
		// 메인 루프
		m_main->Update();
		if (m_main->Render())
		{
			m_deviceResources->Present();
		}
	});
}

LRESULT Core::App::Shutdown(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
	m_windowClosed = true;
	PostQuitMessage(0);
	return 0;
}

LRESULT Core::App::ProcessRawInput(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
	//InputManagement->ProcessRawInput(lParam); *****

	return 0;
}

LRESULT Core::App::ImGuiKeyDownHandler(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
	ImGuiIO& io = ImGui::GetIO();

	io.AddKeyEvent(ImGuiKey(wParam), true);

	return 0;
}

LRESULT Core::App::ImGuiKeyUpHandler(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
	ImGuiIO& io = ImGui::GetIO();

	io.AddKeyEvent(ImGuiKey(wParam), false);

	return 0;
}

LRESULT Core::App::HandleCharEvent(HWND hWnd, WPARAM wParam, LPARAM lParam)
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

LRESULT Core::App::HandleResizeEvent(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
	m_main->CreateWindowSizeDependentResources();

	return 0;
}

LRESULT Core::App::HandleSettingWindowEvent(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
	WNDCLASS wcSetting = { sizeof(WNDCLASS) };
	wcSetting.lpfnWndProc = CoreWindow::WndProc;

	return 0;
}
