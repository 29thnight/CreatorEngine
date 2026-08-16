#include "PlayerApp.h"

#include "Camera.h"
#include "EngineBootstrap.h"
#include "EngineSetting.h"
#include "GpuDiagnostics.h"
#include "InputManager.h"
#include "PakHelper.h"
#include "Render/Scene/EnhancedSceneRenderer.h"
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

	// scene·ImGui 표시 RHI는 프로젝트 빌드 설정(build.render.backend)에서
	// 함께 고른다. Player도 Editor와 같은 IImGuiHost 경계를 쓰며, 선택은
	// EngineSetting 로드 시 한 번 고정된다.

	RegisterHandler(coreWindow);
	Load();
	Run();
}

void Player::App::Finalize()
{
	m_main->Finalize();
	// ★ 구 GameApp의 종료 절차 둘을 걷어냈다(둘 다 BUILD_FLAG 전용이라
	//   한 번도 실행된 적 없던 코드고, 첫 스모크가 둘 다 잡았다):
	//   · DataSystems->Finalize() — FinalizeRuntime의 Destroy와 겹쳐 이중 해제.
	//   · CleanupUnpackedGameAssets() — FinalizeRuntime(DataSystem::Destroy)보다
	//     먼저 %TEMP% 에셋 트리를 지워, 소멸자가 사라진 파일을 밟았다.
	//     언팩 정리는 다음 부팅의 언팩 직전으로 옮겼다(EngineSetting::Initialize)
	//     — 크래시로 죽어도 다음 실행이 스스로 정리하는, 종료 의존 없는 패턴.
	GpuDiagnostics::ReportLiveObjects();
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
		m_main = std::make_unique<PlayerMain>();
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
		EnhancedLiveFramePacket renderFrame =
			EnhancedSceneRenderer::BuildLiveFramePacket(
			static_cast<float>(EngineSettingInstance->frameDeltaTime),
			cameras, cameraCount, SceneManagers->IsSceneLoading());
		const uint64_t publishedFrameId = renderFrame.frameId;
		if (EnhancedSceneRenderer::PublishLiveFrame(std::move(renderFrame)))
		{
			m_main->NotifyRenderFramePublished(publishedFrameId);
		}
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
