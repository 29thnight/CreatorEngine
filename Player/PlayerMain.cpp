#include "PlayerMain.h"

#include "RHI/DX12/EnhancedSceneRenderer.h"
#include "RHI/DX12/ImGuiDx12Shell.h"
#include "RHI/ScreenSizedResource.h"
#include "Camera.h"
#include "ClrHost.h"
#include "Core.Coroutine.h"
#include "DataSystem.h"
#include "EngineBootstrap.h"
#include "EngineSetting.h"
#include "InputManager.h"
#include "PathFinder.h"
#include "Physx.h"
#include "Scene.h"
#include "SceneManager.h"
#include "ShaderSystem.h"
#include "SoundManager.h"
#include "TagManager.h"
#include "TimeSystem.h"
#include "UIManager.h"
#include "imgui.h"

// 렌더 3자 배리어(Game·CB·CE)의 생존 플래그. Dx11Main과 같은 구조의
// 의도된 사본이다 — 공용 부트 추출(L4')에서 한 벌이 된다.
std::atomic<bool> g_playerGameToRender = false;

Player::PlayerMain::PlayerMain(const std::shared_ptr<DirectX11::DeviceResources>& deviceResources)
	: m_deviceResources(deviceResources)
{
	DirectX11::TimeSystem::GetInstance();
}

Player::PlayerMain::~PlayerMain()
{
	DirectX11::TimeSystem::Destroy();
}

void Player::PlayerMain::Initialize()
{
	m_deviceResources->RegisterDeviceNotify(this);

	TagManagers->Initialize();

	// 화면 크기 버스의 첫 값 — 리사이즈 이후는
	// CreateWindowSizeDependentResources가 같은 창에서 직접 읽어 알린다.
	{
		RECT clientRect{};
		if (auto* window = m_deviceResources->GetWindow())
		{
			GetClientRect(window->GetHandle(), &clientRect);
		}
		ScreenResizeBus::Get().SetSize(
			static_cast<uint32_t>(clientRect.right - clientRect.left),
			static_cast<uint32_t>(clientRect.bottom - clientRect.top));
	}

	ShaderSystem->Initialize();

	std::string enhancedError;
	if (!EnhancedSceneRenderer::InitializeRuntime(enhancedError))
	{
		// 렌더러 없이는 아무것도 못 한다 — 스모크가 이 실패를 종료 코드로
		// 구분할 수 있게 남기고 죽는다(§2.3 Verify 규약).
		EngineBootstrap::SetExitCode(2);
		throw std::runtime_error(enhancedError);
	}
	SceneManagers->SetRenderScene(EnhancedSceneRenderer::GetRenderScene());
	EnhancedSceneRenderer::SetActiveScene(SceneManagers->GetActiveScene());

	// 에디터의 같은 자리 델리게이트는 새 씬에 기본 카메라·라이트를 저작한다.
	// 플레이어의 씬은 파일에서 오므로 렌더 씬 갱신만 한다.
	m_newSceneCreatedHandle = newSceneCreatedEvent.AddLambda([]()
	{
		EnhancedSceneRenderer::SetActiveScene(SceneManagers->GetActiveScene());
	});
	m_activeSceneChangedHandle = activeSceneChangedEvent.AddLambda([]()
	{
		EnhancedSceneRenderer::SetActiveScene(SceneManagers->GetActiveScene());
	});

	// DX12 셸이 유일한 표시 경로다(D4에서 DX11 폴백이 걷혔다).
	// ImGuiRenderer 생성이 곧 셸 초기화다.
	m_imguiRenderer = std::make_unique<ImGuiRenderer>(m_deviceResources);

	Sound->initialize(128);
	DataSystems->Initialize();
	ShaderSystem->SetPSOs_GUID();
	SceneManagers->CreateScene();

	m_inputEventHandle = InputEvent.AddLambda([](float)
	{
		UIManagers->Update();
		Sound->update();
	});

	SceneManagers->ManagerInitialize();
	PhysicsManagers->Initialize();

	// CoreCLR 스크립트 계층. 렌더 스레드를 띄우기 전에 올려둔다.
	// 관리 어셈블리가 없으면 조용히 비활성 상태로 남고 엔진은 그대로 동작한다.
	ClrHost::Get().Initialize();

	// 시작 씬 — 로드 성공 시 SceneManager가 재생 시작을 켜고
	// "Scene loaded" 마커를 남긴다(B0-1의 플레이어 모드 분기).
	{
		const std::wstring sceneName = EngineSettingInstance->GetStartupSceneName();
		const file::path scenePath = PathFinder::Relative("Scenes").append(sceneName);
		Scene* loadedScene = SceneManagers->LoadSceneImmediate(scenePath.string());
		if (nullptr == loadedScene)
		{
			// LoadSceneImmediate는 실패를 삼키고 nullptr를 돌려준다 —
			// 여기서 종료 코드로 승격하지 않으면 스모크가 빈 화면을
			// 성공으로 오판한다(§2.4의 1호 발견이 정확히 이 모양이었다).
			Debug->LogError("[SMOKE] startup scene load FAILED: " + scenePath.string());
			if (g_smoke.IsActive())
			{
				EngineBootstrap::SetExitCode(3);
			}
		}
	}

	g_playerGameToRender = true;

	m_CB_Thread = std::thread([&]
	{
		HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		if (FAILED(hr))
		{
			return;
		}

		while (g_playerGameToRender)
		{
			if (m_isInvokeResize)
			{
				EngineSettingInstance->renderBarrier.ArriveAndWait(0, BarrierRole::CommandBuild);
				EngineSettingInstance->renderBarrier.ArriveAndWait(1, BarrierRole::CommandBuild);
				continue;
			}

			CommandBuildThread();
		}

		CoUninitialize();
	});

	m_CE_Thread = std::thread([&]
	{
		HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		if (FAILED(hr))
		{
			return;
		}

		while (g_playerGameToRender)
		{
			// 에디터 CE 스레드의 WinProcProxy 드레인이 여기 없는 이유:
			// 큐 적재 자체가 에디터 모드 전용이다(CoreWindow::WndProc B0-1).
			if (m_isInvokeResize)
			{
				CreateWindowSizeDependentResources();
				m_isInvokeResize = false;
			}

			CoroutineManagers->yield_OnRender();
			CommandExecuteThread();
		}

		CoUninitialize();
	});

	// detach하지 않는다 — Finalize에서 join으로 회수한다(Dx11Main과 같은 근거).
}

void Player::PlayerMain::Finalize()
{
	// 순서는 에디터 종료가 실측으로 다듬은 그대로다: 관리 측 → 렌더 스레드
	// join → 씬 해체 → 렌더러 → 셰이더. 근거는 Dx11Main::Finalize 주석.
	ClrHost::Get().Shutdown();

	g_playerGameToRender = false;
	EngineSettingInstance->renderBarrier.Finalize();

	if (m_CB_Thread.joinable()) m_CB_Thread.join();
	if (m_CE_Thread.joinable()) m_CE_Thread.join();

	TagManagers->Finalize();
	SceneManagers->Decommissioning();

	// 에디터는 여기서 SaveSettings를 부른다 — 플레이어의 설정 루트는
	// %TEMP% 언팩 사본이라 저장할 곳이 아니다.

	EnhancedSceneRenderer::ShutdownLive();
	SceneManagers->SetRenderScene(nullptr);

	ShaderSystem->Finalize();
	m_deviceResources->RegisterDeviceNotify(nullptr);
}

void Player::PlayerMain::Update()
{
	const bool isPaused = SceneManagers->IsGamePaused();
	const double deltaSeconds = Time->GetElapsedSeconds();
	EngineSettingInstance->frameDeltaTime = isPaused ? 0.0 : deltaSeconds;

	Time->Tick([&]
	{
		InputManagement->Update(EngineSettingInstance->frameDeltaTime);

		// 에디터의 재생 분기에서 SceneManagers->Editor()만 뺀 형태다 —
		// 그것은 에디터 씬 상태 머신(선택·프리뷰)이지 게임 로직이 아니다.
		SceneManagers->Initialization();
		SceneManagers->InputEvents(EngineSettingInstance->frameDeltaTime);
		if (!SceneManagers->IsGamePaused())
		{
			SceneManagers->Physics(EngineSettingInstance->frameDeltaTime);
			SceneManagers->GameLogic(EngineSettingInstance->frameDeltaTime);

			if (!SceneManagers->HasPendingSceneStructureChange())
			{
				TickScripts(EngineSettingInstance->frameDeltaTime);
			}
		}
		else
		{
			SceneManagers->Pausing();
		}
	});

	EngineSettingInstance->renderBarrier.ArriveAndWait(0, BarrierRole::Game);

	// 두 랑데뷰 사이 — 렌더 스레드가 묶여 있어 씬 구조 변경이 안전한 구간.
	SceneManagers->ApplyPendingSceneStructureChange();
	SceneManagers->DisableOrEnable();
	SceneManagers->EndOfFrame();

	EngineSettingInstance->renderBarrier.ArriveAndWait(1, BarrierRole::Game);

	HWND handle = m_deviceResources->GetWindow()->GetHandle();

	if (g_smoke.IsActive() && Time->GetFrameCount() >= g_smoke.frameLimit)
	{
		// 성공 마커 — Verify는 이 줄과 "Scene loaded"(SceneManager), 종료
		// 코드 0을 함께 본다. 실패 코드(2·3)는 이미 찍혔다면 그대로 남는다.
		Debug->LogDebug("[SMOKE] frame limit reached — clean exit ("
			+ std::to_string(Time->GetFrameCount()) + " frames)");
		PostMessage(handle, WM_CLOSE, 0, 0);
		return;
	}

	if (SceneManagers->IsDecommissioning())
	{
		PostMessage(handle, WM_CLOSE, 0, 0);
	}
}

void Player::PlayerMain::TickScripts(float deltaTime)
{
	// 경계는 여기가 전부다 — 프레임당 통과 횟수 고정, 순회는 관리 영역에서.
	// 게임 스레드에서만 부른다(CoreCLR GC가 스레드를 정지시킨다).
	auto& clr = ClrHost::Get();
	if (!clr.IsReady()) return;

	clr.TickAwake();
	clr.FlushPhysicsEvents();
	clr.FlushAniEvents();
	clr.FlushScriptMessages();
	clr.FlushAITicks();
	clr.TickUpdate(deltaTime);
	clr.TickLateUpdate(deltaTime);
}

bool Player::PlayerMain::ExecuteRenderPass()
{
	// 첫 업데이트 전과 리사이즈 프레임에는 그리지 않는다.
	if (Time->GetFrameCount() == 0 || m_isInvokeResize)
	{
		return false;
	}

	SceneManagers->SceneRendering(EngineSettingInstance->frameDeltaTime);
	OnGui();
	return true;
}

void Player::PlayerMain::OnGui()
{
	// ImGui는 위젯이 아니라 표시 경로다 — 게임 카메라의 표시 슬롯을
	// 전체 화면으로 블릿한다(GameViewWindow가 창 안에서 하는 것과 같은
	// GetLiveDisplayImTextureId 경로, 창 장식만 없다).
	m_imguiRenderer->BeginRender();

	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(viewport->Pos);
	ImGui::SetNextWindowSize(viewport->Size);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
	constexpr ImGuiWindowFlags kFlags =
		ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoBringToFrontOnFocus;
	if (ImGui::Begin("##PlayerGameView", nullptr, kFlags))
	{
		if (const auto gameCamera = CameraManagement->GetLastCamera())
		{
			if (const uint64_t textureId =
				EnhancedSceneRenderer::GetLiveDisplayImTextureId(gameCamera.get()))
			{
				ImGui::Image((ImTextureID)textureId, viewport->Size);
			}
		}
	}
	ImGui::End();
	ImGui::PopStyleVar(2);

	m_imguiRenderer->Render();
	m_imguiRenderer->EndRender();
}

void Player::PlayerMain::CommandBuildThread()
{
	// DX11 커맨드 빌드는 은퇴했다 — 이 스레드는 3자 렌더 배리어의 참가자
	// 수를 유지하는 동기화 역할만 한다(Dx11Main과 동일).
	if (Time->GetFrameCount() == 0 || m_isInvokeResize)
	{
		EngineSettingInstance->renderBarrier.ArriveAndWait(0, BarrierRole::CommandBuild);
		EngineSettingInstance->renderBarrier.ArriveAndWait(1, BarrierRole::CommandBuild);
		return;
	}

	EngineSettingInstance->renderBarrier.ArriveAndWait(0, BarrierRole::CommandBuild);
	EngineSettingInstance->renderBarrier.ArriveAndWait(1, BarrierRole::CommandBuild);
}

void Player::PlayerMain::CommandExecuteThread()
{
	ExecuteRenderPass();
	// Present는 DX12 셸(EndRender)이 했다 — DX11 스왑체인은 만들지도 않았다
	// (App::SetWindow의 SetPresentOwnedExternally(true)).
	EngineSettingInstance->renderBarrier.ArriveAndWait(0, BarrierRole::CommandExecute);
	EngineSettingInstance->renderBarrier.ArriveAndWait(1, BarrierRole::CommandExecute);
}

void Player::PlayerMain::CreateWindowSizeDependentResources()
{
	m_deviceResources->ReleaseSwapChain();

	OnResizeReleaseEvent();
	ScreenResizeBus::Get().BroadcastRelease();

	RECT rect{};
	HWND hwnd = m_deviceResources->GetWindow()->GetHandle();
	GetClientRect(hwnd, &rect);

	DirectX11::Sizef size;
	size.width = rect.right - rect.left;
	size.height = rect.bottom - rect.top;
	m_deviceResources->SetLogicalSize(size);

	OnResizeEvent(size.width, size.height);
	ScreenResizeBus::Get().BroadcastResize(
		static_cast<uint32_t>(size.width), static_cast<uint32_t>(size.height));
}

void Player::PlayerMain::InvokeResizeFlag()
{
	m_isInvokeResize = true;
}

void Player::PlayerMain::OnDeviceLost()
{
}

void Player::PlayerMain::OnDeviceRestored()
{
	CreateWindowSizeDependentResources();
}
