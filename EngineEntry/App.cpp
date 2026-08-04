#ifndef DYNAMICCPP_EXPORTS
#include "App.h"
#include "ConsoleCommandSystem.h"
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
	return EngineBootstrap::Run<Core::App>(hInstance, L"Creator Editor", 1920, 1080);
}

void Core::App::Initialize(HINSTANCE hInstance, const wchar_t* title, int width, int height)
{

    std::wstring loadingImgPath = PathFinder::IconPath() / L"Loading.bmp";
    g_progressWindow->Launch(ProgressWindowStyle::InitStyle, loadingImgPath);
    g_progressWindow->SetStatusText(L"Initializing Core...");

	CoreWindow coreWindow(hInstance, title, width, height);
	// 덤프 종류 지정과 기록자 등록은 EngineBootstrap::InitializeRuntime이 이미 했다.
	// 여기서 또 부르면 등록 로그가 두 번 찍히고, 무엇보다 '여기가 등록 지점'이라는
	// 오해를 남긴다 — 그 오해 때문에 부팅 전반이 덤프 사각지대였다.
    m_hWnd = coreWindow.GetHandle();

    g_progressWindow->SetProgress(10);

    g_progressWindow->SetStatusText(L"Initializing Dx11 Device...");
	m_deviceResources = std::make_shared<DirectX11::DeviceResources>();
    g_progressWindow->SetProgress(20);

    g_progressWindow->SetStatusText(L"Initializing Windows API...");
	SetWindow(coreWindow);
    g_progressWindow->SetProgress(30);
    RegisterHandler(coreWindow);
    g_progressWindow->SetProgress(40);
	Load();
	Run();
}

void Core::App::Finalize()
{
	// ★ 단계마다 즉시 찍는다.
	//
	//   종료가 멈추는 자리를 쫓는데 로그가 없으면 어디까지 갔는지조차
	//   알 수 없다. 함수가 끝나야 찍히는 것은 소용이 없다 —
	//   dx12.compare 크래시와 씬 로드 행에서 각각 같은 자리를 겪었다.
	std::printf("[SHUTDOWN] Finalize 진입\n");

	ConsoleCommandSystem::Get().Shutdown();
	std::printf("[SHUTDOWN] CLI Shutdown 반환\n");

	m_main->Finalize();
	std::printf("[SHUTDOWN] Dx11Main Finalize 반환\n");

	// 종료 시점에 남아있는 GPU 객체를 로그에 정량 기록한다.
	// 여기서 잡히는 잔존 객체가 곧 세션 전체의 누수 총량이다.
	// m_main->Finalize()가 커맨드 빌드/실행 스레드를 정리한 뒤라, 여기서는
	// 디바이스 자식 객체를 안전하게 순회할 수 있다(런타임 호출은 금지).
	m_deviceResources->LogLiveObjectCensus("에디터 종료 시점", true);
	std::printf("[SHUTDOWN] LogLiveObjectCensus 반환\n");

	m_deviceResources->ReportLiveDeviceObjects();
	std::printf("[SHUTDOWN] Finalize 완료\n");
}

void Core::App::SetWindow(CoreWindow& coreWindow)
{
	m_deviceResources->SetWindow(coreWindow);
}

void Core::App::RegisterHandler(CoreWindow& coreWindow)
{
    coreWindow.RegisterHandler(WM_INPUT,		this, &App::ProcessRawInput);
	coreWindow.RegisterHandler(WM_SIZE,			this, &App::HandleResizeEvent);
	coreWindow.RegisterHandler(WM_SYSKEYDOWN,	this, &App::HandleMaximizeEvent);
    coreWindow.RegisterHandler(WM_KEYDOWN,		this, &App::HandleCharEvent);
    coreWindow.RegisterHandler(WM_CLOSE,		this, &App::Shutdown);
    coreWindow.RegisterHandler(WM_DROPFILES,	this, &App::HandleDropFileEvent);
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
		m_main->Initialize();
		g_progressWindow->SetStatusText(L"Initializing Input...");
        InputManagement->Initialize(m_hWnd);
		//InputActionManagers->LoadManager();
		g_progressWindow->SetProgress(100);

		g_progressWindow->Close();

		// 초기화가 끝난 뒤 CLI를 연다. 그래야 명령이 완성된 엔진 위에서 실행된다.
		ConsoleCommandSystem::Get().InitializeFromCommandLine();
	})
	.Then([&]
	{
		// 메인 루프
		m_main->Update();

		// 콘솔/스크립트 명령은 프레임 경계에서만 실행한다(게임 스레드 규약).
		auto& cli = ConsoleCommandSystem::Get();
		cli.Pump();
		if (cli.IsQuitRequested())
		{
			m_windowClosed = true;
			PostQuitMessage(0);
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
	ImGuiKey key = ImGuiKey(wParam);

	if (key >= 0 && key < ImGuiKey_COUNT)
	{
		io.AddKeyEvent(key, true);
	}

	return 0;
}

LRESULT Core::App::ImGuiKeyUpHandler(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
	ImGuiIO& io = ImGui::GetIO();
	ImGuiKey key = ImGuiKey(wParam);

	if (key >= 0 && key < ImGuiKey_COUNT)
	{
		io.AddKeyEvent(key, false);
	}


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

LRESULT Core::App::HandleMaximizeEvent(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
	const bool altDown = (lParam & (1 << 29)) != 0; // KF_ALTDOWN
	if (wParam == VK_RETURN && altDown)
	{
		m_main->InvokeResizeFlag();
		return 0;           // 여기서 0을 반환하면 아래의 삑(Beep) 방지에 도움
	}
	
	return 0;
}

LRESULT Core::App::HandleSettingWindowEvent(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
	WNDCLASS wcSetting = { sizeof(WNDCLASS) };
	wcSetting.lpfnWndProc = CoreWindow::WndProc;

	return 0;
}

LRESULT Core::App::HandleDropFileEvent(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
	// 드래그 앤 드롭 이벤트 처리
	HDROP hDrop = (HDROP)wParam;
	UINT nFiles = DragQueryFile(hDrop, 0xFFFFFFFF, NULL, 0);

	if (nFiles > 0)
	{
		std::vector<wchar_t> fileName(MAX_PATH);
		for (UINT i = 0; i < nFiles; ++i)
		{
			DragQueryFile(hDrop, i, fileName.data(), MAX_PATH);
			file::path filePath(fileName.data());
			// 파일 경로 처리
			if(".fbx" == filePath.extension() || ".gltf" == filePath.extension() ||
			   ".glb" == filePath.extension() || ".obj" == filePath.extension())
			{
				DataSystems->LoadModel(filePath.string());
			}
			else if (
				".png" == filePath.extension() || 
				".dds" == filePath.extension() || 
				".jpg" == filePath.extension() ||
				".hdr" == filePath.extension()
			)
			{
				DataSystems->m_LoadTextureAssetQueue.push(filePath);
			}
            else if (".dmp" == filePath.extension())
            {
               file::path dumpGitHash = GetDumpGitHashADS(filePath);
               if (!dumpGitHash.empty())
               {
                   Debug->LogDebug("Git Hash in dump: " + dumpGitHash.string());
				   std::string command = "https://github.com/29thnight/LastProject/commit/" + dumpGitHash.string();
				   ShellExecuteA(nullptr, "open", command.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
				   DataSystems->OpenFile(filePath);
               }
               else
               {
                   Debug->LogWarning("No Git hash found in ADS stream.");
               }
            }
		}
	}

	return 0;
}

#endif // DYNAMICCPP_EXPORTS