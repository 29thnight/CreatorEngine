#include "PlayerMain.h"

// PHASE 14.5 LC8 — Player 명령 계층. Shipping 에서는 서비스 본문이 비고 lib 이
// 링크에서 빠지지만, 이 두 include 는 그대로다 — 호출부에 `#if` 를 흩지 않는다.
#include "PlayerCommandService.h"
#include "PlayerCommands.h"

#include "Render/Scene/EnhancedSceneRenderer.h"
#include "RHI/IImGuiHost.h"
#include "RHI/ImGuiHostPresentationSink.h"
#include "RHI/ScreenSizedResource.h"
#include "ClrHost.h"
#include "CoreWindow.h"
#include "Core.Coroutine.h"
#include "DataSystem.h"
#include "EngineBootstrap.h"
#include "InputManager.h"
#include "PathFinder.h"
#include "Physx.h"
#include "Scene.h"
#include "SceneManager.h"
// 시뮬레이션 프레임의 단일 소유자(E3-7) — Editor와 같은 순서를 탄다.
#include "RuntimeFrame.h"
#include "SoundManager.h"
#include "TagManager.h"
#include "TimeSystem.h"
#include "UIManager.h"
#include "RuntimeSettings.h"
#include "AuthoringParseTelemetry.h"
#include "imgui.h"

#include <cstdio>
#include <algorithm>
#include <chrono>
#include <cstdlib>
// 스모크 판정 마커("Scene loaded:")를 여기서 찍는다(E3-6). 전이 include에 기대지
// 않는다 — 이 저장소는 그 함정으로 비유니티 빌드가 두 번 깨졌다.
#include <iostream>
// 파이프라인 노드 목록을 줄 단위로 쪼갠다(E4 게이트).
#include <sstream>
#include <stdexcept>

namespace
{
	/// 창 핸들. 디바이스가 아니라 창이 창을 안다(2026-08-10).
	HWND PlayerWindowHandle()
	{
		auto* window = CoreWindow::GetForCurrentInstance();
		return (nullptr == window) ? nullptr : window->GetHandle();
	}

	// 표시 sink 어댑터는 HostImGuiPresentation의 공용 타입을 쓴다(E4-6c).

	// ── runtime text-parser 계수 (PHASE 14.5 LC8 에서 자리를 옮겼다) ──
	//
	// ★ 예전에는 **스모크 성공 분기 안에서만** 찍었다.
	//
	//   그래서 `--smoke` 없이 띄운 Player 는 이 값을 한 번도 내지 않았고,
	//   LC8 이 연 명령 서비스 세션 — 정확히 이 계수가 흔들릴 수 있는 새 경로 —
	//   에서는 확인할 방법이 없었다. "Player 전용 JSON codec 이 authoring 문서
	//   경로를 쓰지 않는다"(§11.2)가 서비스를 쓰는 실행에서 검증되지 않는 셈이다.
	//
	//   종료 경로 어디서 나가든 한 번 찍는다. 스모크 출력의 마커는 그대로이므로
	//   `Tools/build.ps1` 의 정규식은 예전과 같이 찾는다.
	//
	// ★★ **한 번만** 찍는다. 스모크 분기와 Finalize 가 둘 다 지나는 실행에서
	//   두 번 찍으면 계수가 두 벌 보이고, 그것을 세는 소비자가 생기면 그 순간
	//   거짓이 된다.
	std::atomic<bool> g_textParseTelemetryEmitted{ false };

	void EmitTextParseTelemetry()
	{
		if (g_textParseTelemetryEmitted.exchange(true)) return;

		const Authoring::TextParseTelemetrySnapshot parseTelemetry =
			Authoring::GetTextParseTelemetry();
		std::printf("[runtime.text-parser] calls=%llu\n",
			static_cast<unsigned long long>(parseTelemetry.calls));
		for (const std::string& context : parseTelemetry.contexts)
			std::printf("[runtime.text-parser.call] source=%s\n", context.c_str());
		std::fflush(stdout);
	}
}

Player::PlayerMain::PlayerMain()
{
	Core::TimeSystem::GetInstance();
}

Player::PlayerMain::~PlayerMain()
{
	Core::TimeSystem::Destroy();
}

void Player::PlayerMain::Initialize()
{
	TagManagers->Initialize();

	// 화면 크기 버스의 첫 값 — 리사이즈 이후는
	// CreateWindowSizeDependentResources가 같은 창에서 직접 읽어 알린다.
	{
		RECT clientRect{};
		GetClientRect(PlayerWindowHandle(), &clientRect);
		const uint32_t clientWidth = static_cast<uint32_t>(clientRect.right - clientRect.left);
		const uint32_t clientHeight = static_cast<uint32_t>(clientRect.bottom - clientRect.top);
		ScreenResizeBus::Get().SetSize(clientWidth, clientHeight);

		// 잡은 화면 크기를 남긴다. 플레이어는 테두리 없는 전체화면 창이라
		// 이 값이 모니터 해상도와 같아야 한다 — "왜 전체화면이 아니냐"를
		// 화면만 보고는 가릴 수 없고, 여기 한 줄이면 바로 갈린다.
		std::printf("[PLAYER] 화면 %ux%u (모니터 %dx%d)\n",
			clientWidth, clientHeight,
			GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
	}


	// (텔레메트리 출력은 EmitTextParseTelemetry 가 한 번만 찍는다 — 아래 정의)

	// 표시 sink 설치(E4-6a) — 렌더러 초기화(렌더 스레드 기동) 전이어야
	// RT의 첫 리드백 프레임 게시부터 실린다. Core는 ImGui 셸을 모른다.
	EnhancedSceneRenderer::SetDisplayPresentationSink(
		std::make_shared<ImGuiHostPresentationSink>());

	std::string enhancedError;
	const EnhancedLiveBackend startupBackend =
		RenderBackend::Vulkan == RuntimeSettings::Get().GetRenderBackend()
		? EnhancedLiveBackend::Vulkan : EnhancedLiveBackend::DX12;
	if (!EnhancedSceneRenderer::InitializeRuntime(startupBackend, enhancedError))
	{
		// 렌더러 없이는 아무것도 못 한다 — 스모크가 이 실패를 종료 코드로
		// 구분할 수 있게 남기고 죽는다(§2.3 Verify 규약).
		//
		// ★ **2 → 5 (PHASE 14.5 LC8 · §5.4).** 새 표에서 5 는 infrastructure 이고
		//   2 는 "CLI 문법·unknown command·argument 오류" 다. 렌더러가 서지 않은
		//   것은 호출자가 잘못 부른 것이 아니라 이 기계에서 디바이스가 안 선
		//   것이므로 5 다 — 계획 §5.4 가 이 이동을 이름으로 지목한다.
		//
		//   안전한 근거: 소비자 중 특정 비-0 값을 판정에 쓰는 곳이 없다(§3.1
		//   inventory). 실측으로 다시 확인했다 — `Tools/build.ps1` 의 스모크 판정은
		//   `if ($exitCode -ne 0)` 하나뿐이다.
		EngineBootstrap::SetExitCode(5);
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

	// DX12/Vulkan renderer backend 중 프로젝트 설정이 고른 표시 경로다.
	//
	// ★ IImGuiHost 경계만 소비한다 (EditorRenderer 재작성, 2026-08-10).
	//   예전에는 ImGuiRenderer를 통째로 들었는데, 그 겸직 탓에 에디터
	//   독스페이스 빌더와 ImGuiRegister 펌프가 플레이어에서도 매 프레임
	//   돌았다. 이제 에디터 오케스트레이션은 링크조차 되지 않는다.
	{
		std::string hostError;
		if (!GetImGuiHost().Initialize(PlayerWindowHandle(), hostError))
		{
			GetImGuiHost().Shutdown();
			EngineBootstrap::SetExitCode(5);   // infrastructure (§5.4 · LC8)
			throw std::runtime_error("Player ImGui backend 초기화 실패: " + hostError);
		}
	}
	const bool imguiIsVulkan = ImGuiRendererBackendKind::Vulkan ==
		GetImGuiHost().GetBackendKind();
	if ((EnhancedLiveBackend::Vulkan == startupBackend) != imguiIsVulkan)
	{
		EngineBootstrap::SetExitCode(5);   // infrastructure (§5.4 · LC8)
		throw std::runtime_error("Player scene/ImGui backend 설정 불일치");
	}
	std::printf("[RenderBackend] source=runtime.render.backend active=%s scene=%s imgui=%s\n",
		RenderBackendName(RuntimeSettings::Get().GetRenderBackend()),
		EnhancedLiveBackend::Vulkan == startupBackend ? "vulkan" : "dx12",
		GetImGuiHost().GetBackendName());

	Sound->initialize(128);
	DataSystems->Initialize();
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

	// 시작 씬 — 로드에 성공하면 그 자리에서 재생을 켠다.
	//
	// 예전에는 SceneManager::LoadSceneImmediate가 EngineMode::IsPlayer()를 물어보고
	// 스스로 켰다(E3-6에서 옮겨 왔다). "씬이 로드되는 순간이 곧 재생 시작"은
	// 플레이어의 정책이지 씬 로더가 알아야 할 일이 아니다 — 로더가 실행 모드를
	// 캐묻는 대신 정책을 가진 쪽이 런타임 primitive(SetGameStart)를 직접 부른다.
	// 그래서 Core에서 마지막 Player mode 분기가 사라졌다.
	{
		const std::wstring sceneName = RuntimeSettings::Get().GetStartupSceneName();
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
				// §5.4 의 3 = precondition 불충족. LC8 이 표를 이관하며 다시 봤고
				// **그대로 둔다** — 시작 씬이 없는 것은 부를 수 없는 상태이지
				// infrastructure 고장이 아니다.
				EngineBootstrap::SetExitCode(3);
			}
		}
		else
		{
			SceneManagers->SetGameStart(true);

			// 스모크(--smoke)의 판정 마커다 — Tools/build.ps1이
			// 'Scene loaded:[^\r\n]*<시작 씬>'으로 찾는다. 문구와 인자를 바꾸면
			// 게임 빌드 검증이 조용히 깨진다. 옛 코드가 찍던 것과 같은 문자열이다
			// (Core는 이 함수에 넘어온 경로 문자열을 그대로 찍었다).
			std::cout << "Scene loaded: " << scenePath.string() << std::endl;
		}
	}

	// ── 명령 서비스 (PHASE 14.5 LC8 · §11.2) ────────────────────────────
	//
	// ★ **씬이 선 뒤에 연다.** 서비스가 먼저 열리면 첫 요청이 씬 없는 상태를
	//   만나 `scene.none` 을 받는다 — 붙는 쪽은 "붙었다" 와 "붙었는데 아직
	//   아무것도 없다" 를 구분할 수 없고, 그 구간은 프레임 몇 개가 아니라
	//   씬 로드만큼(LC0 실측 2.4 초) 길다.
	//
	// ★★ 실패해도 게임은 계속 돈다. 서비스가 안 열린 것과 게임이 못 뜨는 것은
	//    다른 사건이다.
	if (g_service.enabled)
	{
		std::string error;
		if (PlayerCommandService::Start(g_service.endpointRoot, error))
		{
			std::printf("[PLAYER] command service listening 127.0.0.1:%u\n",
				static_cast<unsigned>(PlayerCommandService::Port()));
		}
		else
		{
			std::fprintf(stderr, "[PLAYER] command service 시작 실패: %s\n", error.c_str());
			std::fflush(stderr);
		}
	}

	// ★ 컴파일 여부를 **항상** 찍는다.
	//
	//   플래그를 주지 않은 실행에서도 찍는다. 이 한 줄이 없으면 스모크 로그에서
	//   "서비스를 안 켰다" 와 "이 빌드에는 서비스가 없다" 가 똑같이 침묵으로
	//   보이고, Shipping 격리 게이트가 무엇을 확인했는지도 로그에 남지 않는다.
	std::printf("[player.service] compiled=%s enabled=%s\n",
		PlayerCommandService::IsCompiledIn() ? "yes" : "no",
		g_service.enabled ? "yes" : "no");

	StartPresentationThread();
}

void Player::PlayerMain::StartPresentationThread()
{
	m_presentationThreadTestDelayMs = 0;
	char* delay = nullptr;
	size_t delayLength = 0;
	if (0 == _dupenv_s(&delay, &delayLength,
		"CREATOR_PRESENTATION_THREAD_TEST_DELAY_MS") && nullptr != delay)
	{
		m_presentationThreadTestDelayMs = static_cast<uint32_t>((std::min)(250,
			(std::max)(0, std::atoi(delay))));
		std::free(delay);
	}

	{
		std::lock_guard<std::mutex> lock(m_presentationMutex);
		m_presentationThreadStarted = false;
		m_presentationThreadStartFailed = false;
		m_presentationStopRequested = false;
		m_requestedPresentationFrameId = 0;
		m_consumedPresentationFrameId = 0;
		m_presentationRequests = 0;
		m_presentationFrames = 0;
		m_presentationLatestWins = 0;
		m_presentationShutdownDiscarded = 0;
	}

	m_presentationThread = std::thread([this] { PresentationThreadMain(); });

	std::unique_lock<std::mutex> lock(m_presentationMutex);
	m_presentationWake.wait(lock, [this] { return m_presentationThreadStarted; });
	if (!m_presentationThreadStartFailed) return;

	lock.unlock();
	if (m_presentationThread.joinable()) m_presentationThread.join();
	EngineBootstrap::SetExitCode(5);   // infrastructure (§5.4 · LC8)
	throw std::runtime_error("Player PresentationThread COM 초기화 실패");
}

void Player::PlayerMain::StopPresentationThread()
{
	{
		std::lock_guard<std::mutex> lock(m_presentationMutex);
		if (m_requestedPresentationFrameId > m_consumedPresentationFrameId)
		{
			m_consumedPresentationFrameId = m_requestedPresentationFrameId;
			++m_presentationShutdownDiscarded;
		}
		m_presentationStopRequested = true;
	}
	m_presentationWake.notify_all();
	if (!m_presentationThread.joinable()) return;

	m_presentationThread.join();

	std::lock_guard<std::mutex> lock(m_presentationMutex);
	const uint64_t pending =
		m_requestedPresentationFrameId > m_consumedPresentationFrameId ? 1ull : 0ull;
	const bool balanced = m_presentationRequests ==
		m_presentationFrames + m_presentationLatestWins +
		m_presentationShutdownDiscarded + pending;
	std::printf("[PresentationThread] shutdown — request %llu / present %llu"
		" / latest-wins %llu / shutdown-discard %llu / pending %llu / balanced %u\n",
		static_cast<unsigned long long>(m_presentationRequests),
		static_cast<unsigned long long>(m_presentationFrames),
		static_cast<unsigned long long>(m_presentationLatestWins),
		static_cast<unsigned long long>(m_presentationShutdownDiscarded),
		static_cast<unsigned long long>(pending), balanced ? 1u : 0u);
}

void Player::PlayerMain::PresentationThreadMain()
{
	const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	{
		std::lock_guard<std::mutex> lock(m_presentationMutex);
		m_presentationThreadStartFailed = FAILED(comResult);
		m_presentationThreadStarted = true;
	}
	m_presentationWake.notify_all();
	if (FAILED(comResult)) return;

	SetThreadDescription(GetCurrentThread(), L"PresentationThread");
	for (;;)
	{
		bool hasFrameRequest = false;
		{
			std::unique_lock<std::mutex> lock(m_presentationMutex);
			m_presentationWake.wait(lock, [this]
			{
				return m_presentationStopRequested ||
					m_isInvokeResize.load(std::memory_order_acquire) ||
					m_requestedPresentationFrameId > m_consumedPresentationFrameId;
			});
			if (m_presentationStopRequested) break;

			hasFrameRequest =
				m_requestedPresentationFrameId > m_consumedPresentationFrameId;
			if (hasFrameRequest)
				m_consumedPresentationFrameId = m_requestedPresentationFrameId;
		}

		if (m_isInvokeResize.exchange(false, std::memory_order_acq_rel))
			CreateWindowSizeDependentResources();

		if (0 != m_presentationThreadTestDelayMs)
		{
			std::this_thread::sleep_for(
				std::chrono::milliseconds(m_presentationThreadTestDelayMs));
		}

		PresentFrame();

		if (hasFrameRequest)
		{
			std::lock_guard<std::mutex> lock(m_presentationMutex);
			++m_presentationFrames;
		}
	}

	CoUninitialize();
}

void Player::PlayerMain::NotifyRenderFramePublished(uint64_t frameId)
{
	{
		std::lock_guard<std::mutex> lock(m_presentationMutex);
		if (m_presentationStopRequested ||
			frameId <= m_requestedPresentationFrameId)
			return;

		++m_presentationRequests;
		if (m_requestedPresentationFrameId > m_consumedPresentationFrameId)
			++m_presentationLatestWins;
		m_requestedPresentationFrameId = frameId;
	}
	m_presentationWake.notify_one();
}

void Player::PlayerMain::Finalize()
{
	// ★ 서비스를 **가장 먼저** 닫는다(LC8).
	//
	//   수신 스레드가 살아 있는 동안 아래 해체가 진행되면, 그 사이에 들어온
	//   요청이 이미 헐린 씬을 만난다. `Stop()` 은 수신 스레드를 회수하고
	//   endpoint 파일을 지운 뒤에 돌아온다 — 지우지 않으면 다음 실행이 죽은
	//   pid 의 포트로 붙으려 든다.
	PlayerCommandService::Stop();

	// 서비스를 닫은 **뒤에** 찍는다. 서비스가 도는 동안 authoring 파서를 불렀다면
	// 그 호출까지 세어야 하고, 닫기 전에 찍으면 마지막 요청이 빠진다.
	EmitTextParseTelemetry();

	// 순서는 에디터 종료가 실측으로 다듬은 그대로다: 관리 측 → 표시/렌더
	// 소비자 join → 씬 해체 → 렌더러. 새 frame 발행은 메인 루프 종료와 함께 끝났다.
	ClrHost::Get().Shutdown();

	StopPresentationThread();
	EnhancedSceneRenderer::StopLiveRenderThread();
	// 표시 sink 해제(E4-6a) — 렌더 스레드가 멎어 더 이상 게시가 없다.
	EnhancedSceneRenderer::SetDisplayPresentationSink({});

	TagManagers->Finalize();
	SceneManagers->Decommissioning();

	// 에디터는 여기서 SaveSettings를 부른다 — 플레이어의 설정 루트는
	// %TEMP% 언팩 사본이라 저장할 곳이 아니다.

	EnhancedSceneRenderer::ShutdownLive();
	SceneManagers->SetRenderScene(nullptr);


	// 표시 호스트 정리. 예전에는 m_imguiRenderer 멤버 소멸이 맡았는데,
	// 멤버가 사라졌으므로 명시적으로 부른다 — 렌더 스레드는 위에서 이미
	// 멈췄다(호스트 계약).
	GetImGuiHost().Shutdown();
}

void Player::PlayerMain::Update()
{
	// ── 명령 드레인 (PHASE 14.5 LC8) ────────────────────────────────────
	//
	// ★ **프레임 경계에서, 시뮬레이션 앞에서** 편다. 요청이 바꾼 상태가 같은
	//   프레임의 시뮬레이션에 반영되게 하려는 것이다 — 뒤에 두면 `player.move`
	//   가 옮긴 위치를 그 프레임의 물리·애니메이션이 못 보고, 한 프레임 늦게
	//   반영되는 것이 "가끔 한 프레임 어긋난다" 로 나타난다.
	//
	// Shipping 에서도 이 줄은 돈다. 큐는 늘 비어 있다 — 넣을 수 있는 통로가
	// 없기 때문이고, 그래서 여기에 `#if` 를 두지 않는다.
	PlayerCmd::CommandHost::Get().Pump();

	if (PlayerCmd::CommandHost::Get().IsQuitRequested())
	{
		// 창을 닫아 정상 종료 경로를 탄다. 여기서 곧장 죽으면 RenderThread 와
		// presentation 스레드 회수가 통째로 건너뛰어진다.
		PostMessage(PlayerWindowHandle(), WM_CLOSE, 0, 0);
		return;
	}

	Time->Tick([&]
	{
		m_frameDeltaTime = Runtime::ResolveFrameDelta();

		InputManagement->Update(m_frameDeltaTime);

		// 시뮬레이션 순서는 Runtime이 소유한다(E3-7). 에디터의 재생 분기와 같은
		// 코드를 탄다 — 두 실행 경로가 프레임 구조를 공유해야 재생/빌드 결과가
		// 갈리지 않는다. 에디터가 여기에 더 얹는 것은 SceneManagers->Editor()뿐이고
		// 그것은 에디터 씬 상태 머신(선택·프리뷰)이지 게임 로직이 아니다.
		Runtime::TickSimulationFrame(m_frameDeltaTime);
	});

	// 전용 RenderThread는 밀봉된 packet/delta만 소비하므로 씬 구조 변경을 위해
	// 세울 필요가 없다. 이 함수가 끝난 뒤 PlayerApp이 새 frame을 발행한다.
	SceneManagers->ApplyPendingSceneStructureChange();
	CoroutineManagers->yield_OnRender();
	SceneManagers->DisableOrEnable();
	SceneManagers->EndOfFrame();

	HWND handle = PlayerWindowHandle();

	if (g_smoke.IsActive() && Time->GetFrameCount() >= g_smoke.frameLimit)
	{
		const EnhancedLiveDebugSnapshot renderState =
			EnhancedSceneRenderer::GetLiveDebugSnapshot();
		if (!renderState.enabled && !renderState.lastError.empty())
		{
			// 파이프라인이 영구 비활성화됐으면 promotion은 절대 오지 않는다.
			// 기다리기만 하면 CI가 timeout으로만 실패해 최초 원인을 잃으므로,
			// renderer가 공개한 정본 오류와 전용 종료 코드를 함께 남긴다.
			Debug->LogError("[SMOKE] render pipeline FAILED: " +
				renderState.lastError);
			std::printf("[SMOKE] render pipeline FAILED: %s\n",
				renderState.lastError.c_str());
			// §5.4 의 4 = 명령·selftest 판정 실패. 그대로 둔다(LC8 재검토) —
			// 파이프라인이 스스로 "실패" 를 판정한 것이고 그것이 4 의 뜻이다.
			EngineBootstrap::SetExitCode(4);
			PostMessage(handle, WM_CLOSE, 0, 0);
			return;
		}

		const EnhancedLiveDisplaySnapshot display =
			EnhancedSceneRenderer::GetLiveDisplaySnapshot();
		const EnhancedLiveDisplayEntrySnapshot& gameDisplay =
			display.Get(EnhancedLiveDisplayTarget::Game);
		const uint32_t slotMask = gameDisplay.promotedSlotMask;
		const bool displayRotated = gameDisplay.ready &&
			gameDisplay.promotionCount >= 2 && 0 != slotMask &&
			0 != (slotMask & (slotMask - 1u));
		if (!displayRotated) return;

		// 파이프라인 구성을 남긴다 (E4 게이트).
		//
		// E4의 판정 기준은 "Grid/Gizmo는 Scene View에만 기여하고 **Player pipeline에는
		// node 자체가 없다**"인데, Player에는 콘솔 명령 계층이 없어 상태를 물어볼
		// 수단이 없다. 그래서 스모크가 읽는 로그에 직접 찍는다 — 에디터가
		// `pipeline.nodes` CLI로 내는 것과 같은 값(LivePipelineDesc::Dump)이라
		// 두 Host의 구성을 나란히 견줄 수 있다.
		//
		// ⚠ 지금은 Player에도 Editor 패스 노드가 들어 있다(실측). LivePipelineDesc의
		//   DeclareAll은 비활성 노드를 건너뛰지만 InitializeAll에는 그 필터가 없어서,
		//   Player도 Grid/GizmoIcon/GizmoLine 노드의 PSO·버퍼를 만든다. 이 줄은 그
		//   현재 상태를 못 박아 두어, E4-3이 노드를 실제로 걷어낼 때 변화가 드러나게 한다.
		{
			const EnhancedLiveDebugSnapshot pipelineState =
				EnhancedSceneRenderer::GetLiveDebugSnapshot();
			size_t nodeCount = 0;
			std::istringstream stream(pipelineState.pipelineDescription);
			std::string line;
			while (std::getline(stream, line))
			{
				const size_t dot = line.find(". ");
				if (std::string::npos == dot) continue;
				if (line.find_first_not_of(" \t") != 2) continue;

				std::string name = line.substr(dot + 2);
				const size_t bracket = name.find("  [");
				std::string state = "always";
				if (std::string::npos != bracket)
				{
					state = name.substr(bracket + 3);
					if (!state.empty() && ']' == state.back()) state.pop_back();
					name = name.substr(0, bracket);
				}
				Debug->LogDebug("[SMOKE] pipeline.node " + name + "|" + state);
				++nodeCount;
			}
			Debug->LogDebug("[SMOKE] pipeline.nodes 합계 "
				+ std::to_string(nodeCount));
		}

		// 성공 마커 — Verify는 이 줄과 "Scene loaded"(SceneManager), 종료
		// 코드 0을 함께 본다. 락스텝 제거 뒤 GT frame 수만으로 끝내면 실제 GPU
		// 완료가 0이어도 통과하므로 display 슬롯 회전도 함께 요구한다.
		Debug->LogDebug("[SMOKE] frame limit reached — clean exit ("
			+ std::to_string(Time->GetFrameCount()) + " GT frames, display frame "
			+ std::to_string(gameDisplay.completedFrameId) + ", promotions "
			+ std::to_string(gameDisplay.promotionCount) + ")");
		EmitTextParseTelemetry();
		PostMessage(handle, WM_CLOSE, 0, 0);
		return;
	}

	if (SceneManagers->IsDecommissioning())
	{
		PostMessage(handle, WM_CLOSE, 0, 0);
	}
}

void Player::PlayerMain::PresentFrame()
{
	// GPU scene은 전용 RenderThread가 그리고, 여기서는 완료 display snapshot을
	// 전체 화면 ImGui 셸에 표시한다.
	OnGui();
}

void Player::PlayerMain::OnGui()
{
	// ImGui는 위젯이 아니라 표시 경로다 — 게임 카메라의 표시 슬롯을
	// 전체 화면으로 블릿한다(GameViewWindow가 창 안에서 하는 것과 같은
	// GetLiveDisplayImTextureId 경로, 창 장식만 없다).
	GetImGuiHost().BeginFrame();

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
		if (const uint64_t textureId =
			EnhancedSceneRenderer::GetLiveDisplayImTextureId(
				EnhancedLiveDisplayTarget::Game))
		{
			ImGui::Image((ImTextureID)textureId, viewport->Size);
		}
	}
	ImGui::End();
	ImGui::PopStyleVar(2);

	// 창 펌프(ImGuiRegister 순회)는 부르지 않는다 — 그것은 에디터 몫이고,
	// EditorAssetPresentation이 등록하는 material/texture selector는
	// 플레이어에 설치되지 않으므로 플레이어 화면에 섞일 수 없다.
	//
	// 셰이더 선택 창 둘은 여기 예로 적혀 있었는데, PHASE 4-3 슬라이스 5에서
	// 에디터로 나갔다 — 플레이어는 이제 그 둘을 등록조차 하지 않는다.
	GetImGuiHost().EndFrame();
}

void Player::PlayerMain::CreateWindowSizeDependentResources()
{
	// ★ DX11 스왝체인 해제와 SetLogicalSize가 여기 있었다 (2026-08-10).
	//   플레이어는 애초에 DX11 스왝체인을 만든 적이 없어서
	//   (SetPresentOwnedExternally(true)) 둘 다 무의미한 호출이었다.
	//
	//   남는 두 단계는 화면 크기를 따라가는 DX12 텍스처들의 규약이다:
	//   놓게 하고(BroadcastRelease) → 새 크기로 다시 잡게 한다(BroadcastResize).
	OnResizeReleaseEvent();
	ScreenResizeBus::Get().BroadcastRelease();

	RECT rect{};
	GetClientRect(PlayerWindowHandle(), &rect);
	const float width = static_cast<float>(rect.right - rect.left);
	const float height = static_cast<float>(rect.bottom - rect.top);

	OnResizeEvent(width, height);
	ScreenResizeBus::Get().BroadcastResize(
		static_cast<uint32_t>(width), static_cast<uint32_t>(height));
}

void Player::PlayerMain::InvokeResizeFlag()
{
	m_isInvokeResize.store(true, std::memory_order_release);
	m_presentationWake.notify_one();
}

// OnDeviceLost / OnDeviceRestored가 여기 있었다 (2026-08-10,
// DeviceResources 은퇴). 전자는 비어 있었고 후자는
// CreateWindowSizeDependentResources를 한 번 더 부를 뿐이었다.
