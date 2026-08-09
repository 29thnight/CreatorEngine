#include "PlayerApp.h"

#include "Camera.h"
#include "EngineBootstrap.h"
#include "EngineSetting.h"
#include "InputManager.h"
#include "PakHelper.h"
#include "RHI/DX12/EnhancedSceneRenderer.h"
#include "SceneManager.h"

#include <shellapi.h>

#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

MAIN_ENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow)
{
	// --smoke N — 명령줄 파싱은 이 exe에 이것뿐이라 인프라를 들이지 않는다.
	// 판정 규약(종료 코드 + 로그 마커)은 BuildPipelinePlan §2.3.
	int argc = 0;
	if (LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc))
	{
		for (int i = 1; i < argc; ++i)
		{
			if (0 == wcscmp(argv[i], L"--smoke") && i + 1 < argc)
			{
				Player::g_smoke.frameLimit = wcstoull(argv[i + 1], nullptr, 10);
			}
		}
		LocalFree(argv);
	}

	// ★ 모드가 곧 정체다 — 같은 라이브러리를 링크하는 두 exe는 이 인자
	//   하나로 갈린다(에셋 루트·pak 언팩·보더리스 창·자동 시작, B0-1).
	return EngineBootstrap::Run<Player::App>(
		hInstance, L"Creator Player", 1920, 1080, EngineRunMode::Player);
}

void Player::App::Initialize(HINSTANCE hInstance, const wchar_t* title, int width, int height)
{
	CoreWindow coreWindow(hInstance, title, width, height);
	m_hWnd = coreWindow.GetHandle();
	m_deviceResources = std::make_shared<DirectX11::DeviceResources>();
	SetWindow(coreWindow);
	RegisterHandler(coreWindow);
	Load();
	Run();
}

void Player::App::Finalize()
{
	m_main->Finalize();
	DataSystems->Finalize();
	m_deviceResources->ReportLiveDeviceObjects();
	CleanupUnpackedGameAssets();
}

void Player::App::SetWindow(CoreWindow& coreWindow)
{
	// 프레젠트 소유자는 DX12 셸 하나다 — DX11 폴백은 D4에서 걷혔으므로
	// 에디터처럼 설정을 묻지 않고 못박는다. 창을 붙이기 전에 정해야
	// DX11이 같은 HWND에 스왑체인을 만들지 않는다(D2의 교훈).
	EngineSettingInstance->SetDx12ImGuiShellEnabled(true);
	m_deviceResources->SetPresentOwnedExternally(true);
	m_deviceResources->SetWindow(coreWindow);
}

void Player::App::RegisterHandler(CoreWindow& coreWindow)
{
	coreWindow.RegisterHandler(WM_SIZE, this, &App::HandleResizeEvent);
	coreWindow.RegisterHandler(WM_CLOSE, this, &App::Shutdown);
}

void Player::App::Load()
{
	if (nullptr == m_main)
	{
		m_main = std::make_unique<PlayerMain>(m_deviceResources);
	}
}

void Player::App::Run()
{
	CoreWindow::GetForCurrentInstance()->InitializeTask([&]
	{
		m_main->Initialize();
		InputManagement->Initialize(m_hWnd);
	})
	.Then([&]
	{
		m_main->Update();

		// 프레임 경계 밀봉 — 에디터와 같은 자리, 게임 카메라 하나만 넘긴다.
		Camera* cameras[EnhancedSceneRenderer::kMaxLiveCameraViews]{};
		uint32_t cameraCount = 0;
		if (const auto gameCamera = CameraManagement->GetLastCamera())
		{
			cameras[cameraCount++] = gameCamera.get();
		}
		EnhancedSceneRenderer::TickLive(
			static_cast<float>(EngineSettingInstance->frameDeltaTime),
			cameras, cameraCount, SceneManagers->IsSceneLoading());
	});
}

LRESULT Player::App::Shutdown(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
	PostQuitMessage(0);
	return 0;
}

LRESULT Player::App::HandleResizeEvent(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
	if (wParam == SIZE_MINIMIZED)
	{
		EngineSettingInstance->SetMinimized(true);
		return 0;
	}

	if (EngineSettingInstance->IsMinimized())
	{
		if (wParam == SIZE_RESTORED || wParam == SIZE_MAXIMIZED)
		{
			EngineSettingInstance->SetMinimized(false);
			return 0;
		}
	}

	m_main->InvokeResizeFlag();
	return 0;
}
