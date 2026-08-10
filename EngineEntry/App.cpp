#ifndef DYNAMICCPP_EXPORTS
#include "App.h"
#include "Resource.h"
#include "ProgressSink.h"
#include "ConsoleCommandSystem.h"
#include "Camera.h"
#include "InputManager.h"
#include "PathFinder.h"
#include "DumpHandler.h"
#include "CoreWindow.h"
#include "DataSystem.h"
#include "DebugStreamBuf.h"
#include "EngineSetting.h"
#include "PrefabUtility.h"
#include "TagManager.h"
#include "GpuDiagnostics.h"
#include "ReflectionRegister.h"
#include "ReflectionVectorFactory.h"
#include "ReflectionVectorInvoker.h"
#include "ComponentFactory.h"
#include <imgui_impl_win32.h>
#include <ppltasks.h>
#include <ppl.h>
#include "InputActionManager.h"
#include "EngineBootstrap.h"
#include "BootProgress.h"
#include "RHI/DX12/EnhancedSceneRenderer.h"
#include "SceneManager.h"

#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

MAIN_ENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow)
{
	return EngineBootstrap::Run<Core::App>(
		hInstance, L"Creator Editor", 1920, 1080, EngineRunMode::Editor);
}

void Core::App::Initialize(HINSTANCE hInstance, const wchar_t* title, int width, int height)
{

    std::wstring loadingImgPath = PathFinder::IconPath() / L"Loading.bmp";
    g_progressWindow->Launch(ProgressWindowStyle::InitStyle, loadingImgPath);

    // 아래층(셰이더 리로드 등)이 게시하는 진행률을 이 창이 받도록 싱크를 건다.
    // 아래층은 ProgressWindow의 존재를 모른다 — Utility_Framework/ProgressSink.h 참고.
    Progress::GetSink() = Progress::Sink{
        []() { g_progressWindow->Launch(); },
        [](const std::wstring& t) { g_progressWindow->SetTitle(t); },
        [](const std::wstring& t) { g_progressWindow->SetStatusText(t); },
        [](float p) { g_progressWindow->SetProgress(p); },
        []() { g_progressWindow->Close(); },
    };
    BootProgress::Begin(BootProgress::kEditorBootSteps);
    BootProgress::Step(L"Initializing Core...");

	// 아이콘 리소스는 이 exe의 소유물이라 여기서 넘긴다(B0-3에서 코어의
	// Resource.h 상향 include를 걷은 자리).
	CoreWindow coreWindow(hInstance, title, width, height, IDI_ACADEMY4Q);
	// 덤프 종류 지정과 기록자 등록은 EngineBootstrap::InitializeRuntime이 이미 했다.
	// 여기서 또 부르면 등록 로그가 두 번 찍히고, 무엇보다 '여기가 등록 지점'이라는
	// 오해를 남긴다 — 그 오해 때문에 부팅 전반이 덤프 사각지대였다.
    m_hWnd = coreWindow.GetHandle();

	// "Initializing Dx11 Device..." 단계가 여기 있었다 (2026-08-10).
	// DX11 디바이스는 더 이상 만들어지지 않는다 — 씬은 EnhancedSceneRenderer가,
	// 화면 출력은 ImGui DX12 셸이 각자 자기 디바이스를 세운다.

    BootProgress::Step(L"Initializing Windows API...");
    RegisterHandler(coreWindow);
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
	std::printf("[SHUTDOWN] EditorMain Finalize 반환\n");

	// 종료 시점에 남아있는 GPU 객체를 디버거 출력에 쏟는다.
	//
	// ★ 타입별 집계(LogCensus)가 여기 있었다 (2026-08-10, DX12 이관).
	//   집계는 디바이스가 살아 있어야 하는데, 바로 위 EditorMain::Finalize가
	//   ShutdownLive를 지나며 라이브 러너의 DX12 디바이스를 파괴한다. DX11
	//   시절에는 이 디바이스가 App 소유라 여기까지 살아 있었지만 지금은 아니다.
	//
	//   그래서 집계를 '디바이스 파괴 직전'으로 옮겼다(EnhancedSceneRendererLive의
	//   파이프라인 해체). 그 자리가 D3D 디버그 레이어가 요구하는 바로 그 지점이고,
	//   여기보다 정확하다.
	//
	//   아래 보고는 프로세스 범위(DXGI 디버그 계층)라 디바이스 없이도 돈다 —
	//   디바이스가 사라진 뒤에도 남아 있는 것이 무엇인지가 오히려 여기서 보인다.
	GpuDiagnostics::ReportLiveObjects();
	std::printf("[SHUTDOWN] LogLiveObjectCensus 반환\n");
	std::printf("[SHUTDOWN] Finalize 완료\n");
}

// ★ SetWindow가 여기 있었다 (2026-08-10).
//
//   하던 일은 둘이었다: 프레젠트 소유권을 정하고(SetPresentOwnedExternally),
//   DX11 DeviceResources에 창을 붙이는 것(SetWindow → 스왑체인 생성).
//
//   D2가 세운 규약("DXGI는 한 HWND에 스왑체인 둘을 허용하지 않으므로, 창을
//   붙이기 전에 누가 소유할지 정해져 있어야 한다")은 경쟁자가 둘일 때의
//   이야기다. DX11이 스왑체인을 만들지 않게 된 지금은 셸이 유일한 소유자라
//   정할 것이 없다 — 규약이 사라진 것이 아니라 지킬 상대가 사라졌다.
//
//   창 자체는 CoreWindow가 소유하고, 필요한 쪽이 GetForCurrentInstance()로
//   묻는다. 디바이스가 창을 들고 있을 이유는 처음부터 없었다.

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
		m_main = std::make_unique<Editor::EditorMain>();
	}
}

void Core::App::Run()
{
	CoreWindow::GetForCurrentInstance()->InitializeTask([&]
	{
		m_main->Initialize();
		BootProgress::Step(L"Initializing Input...");
        InputManagement->Initialize(m_hWnd);
		//InputActionManagers->LoadManager();
		BootProgress::Complete();

		// 로딩창을 완전히 닫은 뒤(스레드 join까지) 에디터 창을 보여준다.
		// 반대 순서로 하면 서로 다른 스레드의 두 창 사이에서 활성화 전환이
		// 일어나며 동기 SendMessage 교착이 비결정적으로 발생했다.
		g_progressWindow->Close();
		CoreWindow::GetForCurrentInstance()->Show();

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

		// 유일한 씬 렌더러. Update의 두 배리어를 지난 프레임 경계에서 씬과
		// 카메라를 밀봉하고 DX12 그래프를 제출한다.
		//
		// 재생 여부와 무관하게 에디터·게임 카메라를 둘 다 넘긴다 — 그래야
		// 재생 전에도 게임뷰가 그려지고, 재생 중에도 씬뷰가 죽지 않는다.
		// 카메라별 표시 슬롯은 TickLive 쪽이 관리한다(MultiCameraRenderPlan.md).
		// camera.editor follow on — 두 뷰의 시점을 통일해 두는 대조 실험용.
		// 카메라 목록을 만들기 전에 적용해야 이번 프레임 밀봉에 반영된다.
		if (ConsoleCommandSystem::IsEditorCameraFollowing())
		{
			ConsoleCommandSystem::MatchEditorCameraToGameCamera();
		}

		Camera* cameras[EnhancedSceneRenderer::kMaxLiveCameraViews]{};
		uint32_t cameraCount = 0;
		if (Camera* editorCamera = EnhancedSceneRenderer::GetEditorCamera())
		{
			cameras[cameraCount++] = editorCamera;
		}
		if (const auto gameCamera = CameraManagement->GetLastCamera();
			gameCamera && (0 == cameraCount || gameCamera.get() != cameras[0]))
		{
			cameras[cameraCount++] = gameCamera.get();
		}
		EnhancedSceneRenderer::TickLive(
			static_cast<float>(EngineSettingInstance->frameDeltaTime),
			cameras, cameraCount, SceneManagers->IsSceneLoading());

		if (cli.IsQuitRequested())
		{
			Debug->LogDebug("[SHUTDOWN] CLI quit 요청 — 종료 시작");
			m_windowClosed = true;
			PostQuitMessage(0);
		}
	});
}

LRESULT Core::App::Shutdown(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
	// 종료의 발원지를 로그에 남긴다. 부팅 리팩토링 검증 중 "누가 종료를
	// 시작했나"를 몰라 한참 헤맸다 — WM_CLOSE 수신과 CLI quit을 구분한다.
	Debug->LogDebug("[SHUTDOWN] WM_CLOSE 수신 — 종료 시작");
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
