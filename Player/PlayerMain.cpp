#include "ProjectLayerSettingsIO.h"
#include "PlayerMain.h"
#include "PlayerDdolProbe.h"

// PHASE 14.5 LC8 — Player 명령 계층. Shipping 에서는 서비스 본문이 비고 lib 이
// 링크에서 빠지지만, 이 두 include 는 그대로다 — 호출부에 `#if` 를 흩지 않는다.
#include "PlayerCommandService.h"
#include "PlayerCommands.h"

#include "Render/Scene/EnhancedSceneRenderer.h"
#include "Render/Temporal/TemporalRuntimeControl.h"
#include "PlayerPresentation.h"
#include "RHI/ScreenSizedResource.h"
#include "ClrHost.h"
#include "CoreWindow.h"
#include "Core.Coroutine.h"
#include "DataSystem.h"
#include "EngineBootstrap.h"
#include "InputManager.h"
#include "PathFinder.h"
#include "Scene.h"
#include "CharacterMovementComponent.h"
#include "ScriptComponent.h"
#include "SceneManager.h"
// 시뮬레이션 프레임의 단일 소유자(E3-7) — Editor와 같은 순서를 탄다.
#include "RuntimeFrame.h"
#include "Audio/AudioHost.h"
#include "Audio/AudioProfileProvider.h"
#include "Audio/MiniaudioBackend.h"
#include "Audio/PlaybackService.h"
#include "Audio/AudioCatalog.h"
#include "Experiment/Cooked/CookedAssetCatalog.h"
#include "Experiment/Cooked/CookedAudioClipSource.h"
#include "TagManager.h"
#include "TimeSystem.h"
#include "UIManager.h"
#include "RuntimeSettings.h"
#include "AuthoringParseTelemetry.h"
#include "SerializationProfiler.h"
#if !CE_SHIPPING
#include "ProfileService.h"
#endif

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
    if (g_textParseTelemetryEmitted.exchange(true))
        return;

    const Authoring::TextParseTelemetrySnapshot parseTelemetry = Authoring::GetTextParseTelemetry();
    std::printf("[runtime.text-parser] calls=%llu\n", static_cast<unsigned long long>(parseTelemetry.calls));
		for (const std::string& context : parseTelemetry.contexts)
			std::printf("[runtime.text-parser.call] source=%s\n", context.c_str());

		// ── 직렬화 단계 계측 (SerializationPlan D5-d · §5 완료 기준 2) ──
		//
		// D0 기준선은 Editor의 `serialize.bench`가 저작 경로에서 잰 값이다. §5 기준 2의
		// "Player 씬 전환 SceneParse 호출 0 · SceneLoadTotal ≥35% 감소 · 부팅 catalog
		// ≥80% 감소"는 **쿠킹 경로를 도는 Player**에서 같은 단계 이름으로 재야 D0과
		// 맞댈 수 있는데, 그 출력이 Player에는 없었다. 그래서 text-parser 계수와 같은
		// 자리에서, 같은 규칙(어느 종료 경로든 한 번)으로 찍는다.
		//
		// ★ 씬 단계는 `--smoke`에서만 켠다(Initialize 참조). 부팅 AssetCatalog 슬롯은
		//   DataSystem이 켜짐 여부와 무관하게 기록하므로 항상 값이 있다. 게이트는
		//   `calls`를 함께 단정해 "켜지지 않아 0"을 "빨라서 0"으로 읽지 않는다.
		const SerializationProfile::Snapshot sceneStages = SerializationProfile::Take();
		const SerializationProfile::Snapshot bootStages = SerializationProfile::TakeBoot();
    std::printf("[runtime.serialization] enabled=%s\n", SerializationProfile::IsEnabled() ? "yes" : "no");
		for (uint32_t i = 0; i < SerializationProfile::kStageCount; ++i)
		{
			const auto stage = static_cast<SerializationProfile::Stage>(i);
			const SerializationProfile::StageSample& sample =
            (SerializationProfile::Stage::AssetCatalog == stage) ? bootStages[stage] : sceneStages[stage];
			const std::string_view stageName = SerializationProfile::StageName(stage);
        std::printf("[runtime.serialization] stage=%.*s totalUs=%.3f calls=%llu\n", static_cast<int>(stageName.size()),
                    stageName.data(), static_cast<double>(sample.nanoseconds) / 1000.0,
				static_cast<unsigned long long>(sample.calls));
		}
		std::fflush(stdout);
	}
} // namespace

Player::PlayerMain::PlayerMain()
{
	Core::TimeSystem::GetInstance();
}

Player::PlayerMain::~PlayerMain()
{
    // InitializeTask can throw before App::Finalize is reached. Join both workers
    // before destroying the callback target, including a failed native initialization.
    StopPresentation();
    if (!m_finalized && m_presentation)
    {
        EnhancedSceneRenderer::ShutdownLive();
        SceneManagers->SetRenderScene(nullptr);
    }
    // Finalize already unbound audio before Decommissioning freed the scenes;
    // a second unbind would walk those freed scenes.
    if (!m_finalized)
    {
        SceneManagers->BindAudioPlayback(nullptr);
    }
    Core::TimeSystem::Destroy();
}

void Player::PlayerMain::Initialize()
{
    // Register the real GT/input/RT/PT timing owner before either renderer or
    // presenter can bootstrap Streamline. Editor and fixture hosts stay opt-out.
    TemporalRuntimeControl::Get().InitializeProjectDefaults(RuntimeSettings::Get().GetTemporalProductSettings());
    TemporalRuntimeControl::Get().RegisterPlayerLatencyHost();
    if (!SceneManagers->ConfigureSimulationSession(SimulationSessionPolicy::mode::runtime))
        throw std::runtime_error("Player simulation policy must be configured before startup");

	TagManagers->Initialize();
    const auto layers = ProjectLayerSettingsIO::Read(PathFinder::ProjectSettingPath(ProjectLayerSettingsIO::filename));
    m_projectLayers = std::make_shared<ProjectLayerSettings>();
    if (!layers || !m_projectLayers->Restore(*layers) || !SceneManagers->BindProjectLayerSettings(m_projectLayers))
        throw std::runtime_error("Player requires valid CLYR project layers before scene startup");

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
        std::printf("[PLAYER] 화면 %ux%u (모니터 %dx%d)\n", clientWidth, clientHeight, GetSystemMetrics(SM_CXSCREEN),
                    GetSystemMetrics(SM_CYSCREEN));
	}

	// (텔레메트리 출력은 EmitTextParseTelemetry 가 한 번만 찍는다 — 아래 정의)

    std::string enhancedError;
    const EnhancedLiveBackend startupBackend = RenderBackend::Vulkan == RuntimeSettings::Get().GetRenderBackend()
                                                   ? EnhancedLiveBackend::Vulkan
                                                   : EnhancedLiveBackend::DX12;
    m_presentation = startupBackend == EnhancedLiveBackend::Vulkan
        ? CreateVulkanPresentation() : CreateDX12Presentation();
    m_presentation->SetDisplayAvailableCallback([this] { NotifyDisplayAvailable(); });
    EnhancedSceneRenderer::SetDisplayPresentationSink(m_presentation);
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
    m_newSceneCreatedHandle = newSceneCreatedEvent.AddLambda(
        []() { EnhancedSceneRenderer::SetActiveScene(SceneManagers->GetActiveScene()); });
    m_activeSceneChangedHandle = activeSceneChangedEvent.AddLambda(
        []() { EnhancedSceneRenderer::SetActiveScene(SceneManagers->GetActiveScene()); });

    RECT presentationRect{};
    GetClientRect(PlayerWindowHandle(), &presentationRect);
    const uint32_t presentationWidth = static_cast<uint32_t>(presentationRect.right - presentationRect.left);
    const uint32_t presentationHeight = static_cast<uint32_t>(presentationRect.bottom - presentationRect.top);
    std::string presentationError;
    if (!m_presentation->Initialize(PlayerWindowHandle(), presentationWidth, presentationHeight, presentationError))
    {
        m_presentation->Shutdown();
        EnhancedSceneRenderer::SetDisplayPresentationSink({});
        EngineBootstrap::SetExitCode(5);
        throw std::runtime_error("Player native presentation initialization failed: " + presentationError);
    }
    std::printf("[RenderBackend] source=runtime.render.backend active=%s scene=%s presentation=%s transfer=%s\n",
        RenderBackendName(RuntimeSettings::Get().GetRenderBackend()),
        startupBackend == EnhancedLiveBackend::Vulkan ? "vulkan" : "dx12", m_presentation->GetName(),
        startupBackend == EnhancedLiveBackend::Vulkan ? "cpu-readback-upload" : "shared-image");

	#if CE_SHIPPING
    constexpr bool profileAudioCallbacks = false;
#else
    constexpr bool profileAudioCallbacks = true;
#endif
    m_audioHost = std::make_unique<wave::AudioHost>(std::make_unique<wave::MiniaudioBackend>(profileAudioCallbacks), 128u);
    if (!m_audioHost->Start({}))
    {
        throw std::runtime_error("Audio runtime initialization failed");
    }
    if (m_audioHost->Mode() == wave::AudioHostMode::Null)
    {
        Debug::PrintLog(spdlog::level::warn, "[audio.runtime.null] Audio graph initialization failed; silent logical runtime active");
    }
    else if (m_audioHost->Mode() == wave::AudioHostMode::DegradedDevice)
    {
        Debug::PrintLog(spdlog::level::warn, "[audio.device.degraded] Output device unavailable; graph retained and output recovery will retry");
    }
    m_audioPlayback = std::make_unique<wave::PlaybackService>(*m_audioHost->Service());
    m_audioCatalog = std::make_unique<wave::AudioCatalog>(*m_audioHost->Service(), *m_audioPlayback);
    SceneManagers->BindAudioPlayback(m_audioPlayback.get(), [this](std::string_view key)
    {
        std::string error;
        const auto resolved = m_audioCatalog->ResolveLegacyClip(key, error);
        Uuid::Uuid16 guid;
        if (!Uuid::TryParse(resolved, guid))
        {
            if (!key.empty())
            {
                Debug::PrintLog(spdlog::level::err, "[audio.clip.resolve] " + error);
            }
            return wave::ClipKey{};
        }
        return wave::ClipKey::FromGuid(guid);
    });
	DataSystems->Initialize();
    if (const auto catalog = DataSystems->GetCookedCatalog())
    {
        std::string error;
        auto bytes = std::make_shared<experiment::cooked::LooseArtifactByteSource>(catalog->DerivedRoot());
        if (!m_audioCatalog->LoadCookedAssets(*catalog, std::move(bytes), error))
        {
            throw std::runtime_error("Cooked audio catalog failed: " + error);
        }
    }
	SceneManagers->CreateScene();

    m_inputEventHandle = InputEvent.AddLambda([](float) {
		UIManagers->Update();
	});

	SceneManagers->ManagerInitialize();

	// CoreCLR 스크립트 계층. 렌더 스레드를 띄우기 전에 올려둔다.
	// 관리 어셈블리가 없으면 조용히 비활성 상태로 남고 엔진은 그대로 동작한다.
	ClrHost::Get().Initialize();

    // Startup uses the same staged scene preparation as later transitions.
    // Metadata/payload jobs progress without blocking the owner or render thread;
    // simulation and command admission begin only after successful activation.
    if (g_smoke.IsActive())
    {
        SerializationProfile::SetEnabled(true);
    }
    const auto sceneName = RuntimeSettings::Get().GetStartupSceneName();
    m_startupScenePath = PathFinder::Relative("Scenes").append(sceneName).string();
    m_startupScene = SceneManagers->LoadSceneAsync(m_startupScenePath);

	// ★ 컴파일 여부를 **항상** 찍는다.
	//
	//   플래그를 주지 않은 실행에서도 찍는다. 이 한 줄이 없으면 스모크 로그에서
	//   "서비스를 안 켰다" 와 "이 빌드에는 서비스가 없다" 가 똑같이 침묵으로
	//   보이고, Shipping 격리 게이트가 무엇을 확인했는지도 로그에 남지 않는다.
    std::printf("[player.service] compiled=%s enabled=%s\n", PlayerCommandService::IsCompiledIn() ? "yes" : "no",
		g_service.enabled ? "yes" : "no");

    StartPresentationThread();
}

bool Player::PlayerMain::PollStartupScene()
{
    if (m_startupComplete)
    {
        return !m_startupFailed;
    }
    const auto fail = [this](std::string_view reason)
    {
        m_startupComplete = true;
        m_startupFailed = true;
        EngineBootstrap::SetExitCode(3);
        Debug::PrintLog(spdlog::level::err, "[SMOKE] startup scene load FAILED: "
            + m_startupScenePath + ": " + std::string(reason));
#if CE_DEVELOPMENT
        PlayerCmd::CommandHost::Get().StartBatch(false);
#endif
        PostMessage(PlayerWindowHandle(), WM_CLOSE, 0, 0);
        return false;
    };
    try
    {
        SceneManagers->PollSceneLoads();
        if (!m_startupSceneToActivate)
        {
            if (!m_startupScene.valid())
            {
                return fail("startup preparation has no result ticket");
            }
            if (m_startupScene.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            {
                return false;
            }
            m_startupSceneToActivate = m_startupScene.get();
            if (!m_startupSceneToActivate)
            {
                return fail("preparation did not produce a scene");
            }
            // The SceneManager already owns the constructed scene. Startup has
            // no separate raw asset lifetime and never retries a Stale ticket.
            SceneManagers->ActivateScene(m_startupSceneToActivate, true);
        }
        SceneManagers->ApplyPendingSceneStructureChange();
        if (SceneManagers->GetActiveScene() != m_startupSceneToActivate)
        {
            return fail("scene activation was rejected");
        }
        m_startupSceneToActivate = nullptr;
        SceneManagers->SetGameStart(true);
        SceneManagers->ApplyPendingSceneStructureChange();
        if (!SceneManagers->IsPlayCommitted() || SceneManagers->PlayFailureCount() != 0)
        {
            return fail("play transaction was rejected: " + SceneManagers->LastPlayFailure());
        }
        Time->ResetElapsedTime();
        m_startupComplete = true;
        Debug::PrintLog(spdlog::level::info, "Scene loaded: {}", m_startupScenePath);
    }
    catch (const std::exception& exception)
    {
        return fail(exception.what());
    }

    // Keep the existing command-service contract: no requests are admitted
    // while startup is still an empty scene or an incomplete payload ticket.
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


#if CE_DEVELOPMENT
    PlayerCmd::CommandHost::Get().StartBatch(EngineBootstrap::g_exitCode == 0);
#endif
    return true;
}

void Player::PlayerMain::StartPresentationThread()
{
	m_presentationThreadTestDelayMs = 0;
	char* delay = nullptr;
	size_t delayLength = 0;
    if (0 == _dupenv_s(&delay, &delayLength, "CREATOR_PRESENTATION_THREAD_TEST_DELAY_MS") && nullptr != delay)
	{
        m_presentationThreadTestDelayMs = static_cast<uint32_t>((std::min)(250, (std::max)(0, std::atoi(delay))));
		std::free(delay);
	}

	{
		std::lock_guard<std::mutex> lock(m_presentationMutex);
		m_presentationThreadStarted = false;
		m_presentationThreadStartFailed = false;
		m_presentationStopRequested = false;
        m_displayAvailable = false;
        m_presentationFailed.store(false, std::memory_order_release);
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
    if (!m_presentationThreadStartFailed)
        return;

	lock.unlock();
    if (m_presentationThread.joinable())
        m_presentationThread.join();
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
    if (!m_presentationThread.joinable())
        return;

	m_presentationThread.join();

	std::lock_guard<std::mutex> lock(m_presentationMutex);
    const uint64_t pending = m_requestedPresentationFrameId > m_consumedPresentationFrameId ? 1ull : 0ull;
	const bool balanced = m_presentationRequests ==
                          m_presentationFrames + m_presentationLatestWins + m_presentationShutdownDiscarded + pending;
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
    if (FAILED(comResult))
        return;

    SetThreadDescription(GetCurrentThread(), L"PresentationThread");
    try
    {
        for (;;)
        {
            bool hasFrameRequest = false;
            {
                std::unique_lock<std::mutex> lock(m_presentationMutex);
                m_presentationWake.wait(lock, [this] {
                    return m_presentationStopRequested || m_isInvokeResize.load(std::memory_order_acquire) ||
                        m_displayAvailable || m_requestedPresentationFrameId > m_consumedPresentationFrameId;
                });
                if (m_presentationStopRequested)
                {
                    break;
                }
                hasFrameRequest = m_requestedPresentationFrameId > m_consumedPresentationFrameId;
                if (hasFrameRequest)
                {
                    m_consumedPresentationFrameId = m_requestedPresentationFrameId;
                }
                m_displayAvailable = false;
            }

            std::string error;
            const bool resized = !m_isInvokeResize.exchange(false, std::memory_order_acq_rel) ||
                CreateWindowSizeDependentResources(error);
            if (m_presentationThreadTestDelayMs != 0)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(m_presentationThreadTestDelayMs));
            }
            const bool presented = resized && PresentFrame(error);
            if (hasFrameRequest)
            {
                std::lock_guard<std::mutex> lock(m_presentationMutex);
                ++m_presentationFrames;
            }
            if (!presented)
            {
                throw std::runtime_error(error);
            }
        }
    }
    catch (const std::exception& exception)
    {
        std::printf("[Player presentation] FAILED: %s\n", exception.what());
        m_presentationFailed.store(true, std::memory_order_release);
        PostMessage(PlayerWindowHandle(), WM_CLOSE, 0, 0);
    }
    catch (...)
    {
        std::printf("[Player presentation] FAILED: unknown exception\n");
        m_presentationFailed.store(true, std::memory_order_release);
        PostMessage(PlayerWindowHandle(), WM_CLOSE, 0, 0);
    }

	CoUninitialize();
}

void Player::PlayerMain::NotifyDisplayAvailable()
{
    {
        std::lock_guard<std::mutex> lock(m_presentationMutex);
        if (m_presentationStopRequested)
        {
            return;
        }
        m_displayAvailable = true;
    }
    m_presentationWake.notify_one();
}

void Player::PlayerMain::NotifyRenderFramePublished(uint64_t frameId)
{
	{
		std::lock_guard<std::mutex> lock(m_presentationMutex);
        if (m_presentationStopRequested || frameId <= m_requestedPresentationFrameId)
			return;

		++m_presentationRequests;
		if (m_requestedPresentationFrameId > m_consumedPresentationFrameId)
			++m_presentationLatestWins;
		m_requestedPresentationFrameId = frameId;
	}
	m_presentationWake.notify_one();
}

void Player::PlayerMain::DiscardTemporalRealFrame()
{
    if (m_presentation) m_presentation->DiscardTemporalFrame(m_temporalRealFrameId);
}

void Player::PlayerMain::StopPresentation()
{
    if (m_presentation) m_presentation->StopSimulationFrames();
    if (m_presentationStopped)
    {
        return;
    }
    StopPresentationThread();
    if (m_presentation)
    {
        EnhancedSceneRenderer::StopLiveRenderThread();
        EnhancedSceneRenderer::SetDisplayPresentationSink({});
        // No producer callback remains in flight after RT joins. Drain the native
        // consumer before releasing any producer interop allocation.
        m_presentation->SetDisplayAvailableCallback({});
        m_presentation->Shutdown();
        if (m_presentation->HasShutdownFailure())
        {
            m_presentationFailed.store(true, std::memory_order_release);
            EngineBootstrap::SetExitCode(5);
        }
    }
    m_presentationStopped = true;
}

void Player::PlayerMain::Finalize()
{
    if (m_finalized)
    {
        return;
    }
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

	// Stop producers, join PT/RT, then run scene cleanup while CLR/audio
	// are alive. The collector only reclaims already-cleaned objects.
	SceneManagers->SetDecommissioning();
	SceneManagers->DrainSceneLoads();
	SceneManagers->DrainAIUpdates();

    StopPresentation();
    if (m_presentationFailed.load(std::memory_order_acquire))
    {
        EngineBootstrap::SetExitCode(5);
    }

	SceneManagers->Decommissioning();
	ClrHost::Get().Shutdown();
	TagManagers->Finalize();
    SceneManagers->BindAudioPlayback(nullptr);
    m_audioPlayback->Shutdown();
    m_audioCatalog->Clear();
    m_audioHost->Shutdown();
    m_projectLayers.reset();

	// 에디터는 여기서 SaveSettings를 부른다 — 플레이어의 설정 루트는
	// %TEMP% 언팩 사본이라 저장할 곳이 아니다.

	EnhancedSceneRenderer::ShutdownLive();
	SceneManagers->SetRenderScene(nullptr);


    const auto resources = ce::physics::read_resource_statistics();
    if (resources.enabled || Player::g_smoke.frameLimit > 0)
    {
        std::printf("[physics.player.resources] {\"enabled\":%s,\"balanced\":%s,\"created\":[",
                    resources.enabled ? "true" : "false", resources.balanced() ? "true" : "false");

        for (std::size_t index = 0; index < resources.created.size(); ++index)
            std::printf("%s%llu", index ? "," : "", static_cast<unsigned long long>(resources.created[index]));

        std::printf("],\"released\":[");
        for (std::size_t index = 0; index < resources.released.size(); ++index)
            std::printf("%s%llu", index ? "," : "", static_cast<unsigned long long>(resources.released[index]));

        std::printf("]}\n");
        std::fflush(stdout); // The loader retains the runtime DLL until process termination.
    }
    m_finalized = true;
}

void Player::PlayerMain::Update()
{
    m_temporalFrameReady = false;
    if (m_presentationFailed.load(std::memory_order_acquire))
    {
        EngineBootstrap::SetExitCode(5);
        PostMessage(PlayerWindowHandle(), WM_CLOSE, 0, 0);
        return;
    }
    if (!PollStartupScene())
    {
        // Keep native presentation responsive while preparation is Pending.
        // No simulation/smoke frame is counted before startup is activated.
        m_frameDeltaTime = 0.0;
        m_audioHost->Update(0.f);
        m_audioPlayback->Update();
        return;
    }
#if CE_DEVELOPMENT
    // Local CLI batches and the optional HTTP service share the game-thread host.
    // Shipping has no command host instance or registered command surface.
    PlayerCmd::CommandHost::Get().Pump();
    if (PlayerCmd::CommandHost::Get().IsQuitRequested())
    {
        PostMessage(PlayerWindowHandle(), WM_CLOSE, 0, 0);
        return;
    }
#endif

    ++m_temporalRealFrameId;
    // Wake PT before waiting for a settings-generation change or previous SDK
    // token. The GT does not hold the presentation mailbox mutex during sleep.
    m_presentation->NotifyDisplayAvailable();
    std::string temporalError;
    if (!m_presentation->BeginSimulationFrame(m_temporalRealFrameId, temporalError))
    {
        std::fprintf(stderr, "[Player temporal] %s\n", temporalError.c_str());
        m_presentationFailed.store(true, std::memory_order_release);
        EngineBootstrap::SetExitCode(5);
        PostMessage(PlayerWindowHandle(), WM_CLOSE, 0, 0);
        return;
    }
    RECT temporalClient{};
    m_temporalFrameReady = !IsIconic(PlayerWindowHandle()) && GetClientRect(PlayerWindowHandle(), &temporalClient) &&
        temporalClient.right > temporalClient.left && temporalClient.bottom > temporalClient.top;
    Time->Tick([&] {
		m_frameDeltaTime = Runtime::ResolveFrameDelta();

        m_presentation->MarkTemporalLatency(m_temporalRealFrameId, RHITemporalLatencyMarker::InputSample);
		InputManagement->Update(m_frameDeltaTime);

		// 시뮬레이션 순서는 Runtime이 소유한다(E3-7). 에디터의 재생 분기와 같은
		// 코드를 탄다 — 두 실행 경로가 프레임 구조를 공유해야 재생/빌드 결과가
		// 갈리지 않는다. 에디터가 여기에 더 얹는 것은 SceneManagers->Editor()뿐이고
		// 그것은 에디터 씬 상태 머신(선택·프리뷰)이지 게임 로직이 아니다.
		Runtime::TickSimulationFrame(m_frameDeltaTime);
        m_presentation->MarkTemporalLatency(m_temporalRealFrameId, RHITemporalLatencyMarker::SimulationEnd);
	});

	// 전용 RenderThread는 밀봉된 packet/delta만 소비하므로 씬 구조 변경을 위해
	// 세울 필요가 없다. 이 함수가 끝난 뒤 PlayerApp이 새 frame을 발행한다.
	SceneManagers->ApplyPendingSceneStructureChange();
	CoroutineManagers->yield_OnRender();
	SceneManagers->DisableOrEnable();
	SceneManagers->EndOfFrame();
	SceneManagers->CollectManagedAtFrameBoundary();
    m_audioHost->Update(static_cast<float>(m_frameDeltaTime));
    m_audioPlayback->Update();
    wave::PublishAudioProfile(*m_audioHost, *m_audioPlayback);

#if !CE_SHIPPING
    // Close the same engine frame used by runtime counter and CPU span producers.
    ce::profiler().publish_frame(Time->GetFrameCount());
#endif

    // SceneManager records rejected startup and transition transactions. The Player
    // host owns the fatal policy; normal window shutdown still drains its workers.
    if (SceneManagers->PlayFailureCount() != 0)
    {
        EngineBootstrap::SetExitCode(3);
        std::fprintf(stderr, "[player.simulation.failed] exit=3 reason=%s\n", SceneManagers->LastPlayFailure().c_str());
        std::fflush(stderr);
        char wrapperProbe[2]{};
        if (g_smoke.IsActive() && GetEnvironmentVariableA("CE_PHYSICS_WRAPPER_PROBE", wrapperProbe, 2) == 1 &&
            wrapperProbe[0] == '1')
        {
            auto* actor = Entity::Find("CharacterGateActor");
            auto* script = actor ? actor->GetComponent<ScriptComponent>() : nullptr;
            int receiver = m_smokeWrapperInstance;
            if (receiver < 0 && script)
            {
                // Instance creation is separate from lifecycle/simulation dispatch.
                script->EnsureInstance();
                receiver = script->GetInstanceId();
            }

            if (receiver >= 0)
            {
                ClrHost::Get().QueueScriptMessage(receiver, "VerifyFailedActivationWrappers");
                ClrHost::Get().FlushScriptMessages();
                std::fflush(stdout);
            }
            else
            {
                std::fprintf(stderr, "[physics.player.wrappers.failure] missing destination script\n");
                EngineBootstrap::SetExitCode(4);
            }
        }

        if (g_smoke.geometryFailure)
        {
            auto* actor = Entity::Find("CharacterGateActor");
            auto* floor = Entity::Find("CharacterGateFloor");
            auto* character = actor ? actor->GetComponent<CharacterMovementComponent>() : nullptr;
            auto* body = floor ? floor->GetComponent<PhysicsBodyComponent>() : nullptr;
            const auto state =
                character
                    ? character->ReadState()
                    : ce::physics::result<ScenePhysicsSimulation::character_motion_state>{std::unexpected(
                          ce::physics::error{ce::physics::error_code::invalid_argument, 0, "Missing probe actor"})};

            const auto stats =
                actor ? actor->GetScene()->ReadCollisionGeometryStatistics()
                      : ce::physics::result<ce::physics::CollisionGeometryLibrary::statistics>{std::unexpected(
                            ce::physics::error{ce::physics::error_code::invalid_argument, 0, "Missing probe Scene"})};
            const bool rejected = body && !body->RuntimeHandle() && stats && stats->imports == 2 &&
                                  stats->assets == 2 && stats->cooks == 0 && character && !character->RuntimeHandle() &&
                                  state && state->tick.value == 0;
            std::printf(
                "[physics.player.geometry.rejected] {\"passed\":%d,\"failed\":%d,\"tick\":%llu,\"complete\":true}\n",
                rejected ? 5 : 0, rejected ? 0 : 1, static_cast<unsigned long long>(state ? state->tick.value : 0));
            std::fflush(stdout);
            if (!rejected)
                EngineBootstrap::SetExitCode(4);
        }

        PostMessage(PlayerWindowHandle(), WM_CLOSE, 0, 0);
        return;
    }

	HWND handle = PlayerWindowHandle();

	if (g_smoke.IsActive() && Time->GetFrameCount() >= g_smoke.frameLimit)
	{
        const EnhancedLiveDebugSnapshot renderState = EnhancedSceneRenderer::GetLiveDebugSnapshot();
		if (!renderState.enabled && !renderState.lastError.empty())
		{
			// 파이프라인이 영구 비활성화됐으면 promotion은 절대 오지 않는다.
			// 기다리기만 하면 CI가 timeout으로만 실패해 최초 원인을 잃으므로,
			// renderer가 공개한 정본 오류와 전용 종료 코드를 함께 남긴다.
            Debug::PrintLog(spdlog::level::err, "[SMOKE] render pipeline FAILED: " + renderState.lastError);
            std::printf("[SMOKE] render pipeline FAILED: %s\n", renderState.lastError.c_str());
			// §5.4 의 4 = 명령·selftest 판정 실패. 그대로 둔다(LC8 재검토) —
			// 파이프라인이 스스로 "실패" 를 판정한 것이고 그것이 4 의 뜻이다.
			EngineBootstrap::SetExitCode(4);
			PostMessage(handle, WM_CLOSE, 0, 0);
			return;
		}

        const EnhancedLiveDisplaySnapshot display = EnhancedSceneRenderer::GetLiveDisplaySnapshot();
        const EnhancedLiveDisplayEntrySnapshot& gameDisplay = display.Get(EnhancedLiveDisplayTarget::Game);
        const auto progressNow = std::chrono::steady_clock::now();
        if (progressNow >= m_smokeProgressReport)
        {
            m_smokeProgressReport = progressNow + std::chrono::seconds(10);
            std::printf("[player.smoke.progress] {\"gtFrame\":%llu,\"rendered\":%llu,\"inFlight\":%llu,"
                        "\"published\":%llu,\"consumed\":%llu,\"completed\":%llu,\"promotions\":%llu,"
                        "\"requiredPromotions\":%llu,\"slotMask\":%u,\"ready\":%s,"
                        "\"submittedGameFrames\":%llu}\n",
                        static_cast<unsigned long long>(Time->GetFrameCount()),
                        static_cast<unsigned long long>(renderState.framesRendered),
                        static_cast<unsigned long long>(renderState.framesInFlight),
                        static_cast<unsigned long long>(renderState.publishedFrameId),
                        static_cast<unsigned long long>(renderState.consumedFrameId),
                        static_cast<unsigned long long>(gameDisplay.completedFrameId),
                        static_cast<unsigned long long>(gameDisplay.promotionCount),
                        static_cast<unsigned long long>(g_smoke.minimumPromotions),
                        gameDisplay.promotedSlotMask, gameDisplay.ready ? "true" : "false",
                        static_cast<unsigned long long>(m_presentation->GetSubmittedGameFrames()));
            std::printf("[player.smoke.render.state] enabled=%s idle=%llu error=%s\n",
                        renderState.enabled ? "true" : "false",
                        static_cast<unsigned long long>(renderState.framesIdle), renderState.lastError.c_str());
            std::fflush(stdout);
        }

		const uint32_t slotMask = gameDisplay.promotedSlotMask;
        const bool displayRotated = gameDisplay.ready && gameDisplay.promotionCount >= g_smoke.minimumPromotions &&
                                    0 != slotMask && 0 != (slotMask & (slotMask - 1u));
        if (m_smokeReloadStarted && SceneManagers->GetActiveScene() == m_smokeReloadScene &&
            std::chrono::steady_clock::now() >= m_smokeReloadReport)
        {
            m_smokeReloadReport = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            std::printf("[player.smoke.reload.wait] camera=%s loading=%s rotated=%s published=%llu completed=%llu "
                        "promotions=%llu threshold=%llu\n",
                        m_smokeReloadScene->Cameras().GetPrimaryCamera() ? "true" : "false",
                        SceneManagers->IsSceneLoading() ? "true" : "false", displayRotated ? "true" : "false",
                        static_cast<unsigned long long>(renderState.publishedFrameId),
                        static_cast<unsigned long long>(gameDisplay.completedFrameId),
                        static_cast<unsigned long long>(gameDisplay.promotionCount),
                        static_cast<unsigned long long>(m_smokeReloadPublishedFrame));
            std::fflush(stdout);
        }

        if (!displayRotated || m_submittedGameFrameId.load(std::memory_order_acquire) == 0)
        {
            return;
        }

        // Optional packaged-host regression: prepare through engine jobs, activate
        // at the normal owner boundary, then require a submitted native post-load composition.
        if (g_smoke.reloadScene)
        {
            if (!m_smokeReloadStarted)
            {
                auto path = PathFinder::Relative("Scenes");
                if (g_smoke.reloadDestination.empty())
                path.append(RuntimeSettings::Get().GetStartupSceneName());
                else
                    path.append(g_smoke.reloadDestination);
                m_smokeReload = SceneManagers->LoadSceneAsync(path.string());
                m_smokeReloadStarted = true;
                return;
            }
            if (m_smokeReload.valid())
            {
                if (m_smokeReload.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
                    return;
                m_smokeReloadScene = m_smokeReload.get();
                if (!m_smokeReloadScene)
                {
                    std::printf("[player.smoke.reload] FAILED scene preparation\n");
                    EngineBootstrap::SetExitCode(4);
                    PostMessage(handle, WM_CLOSE, 0, 0);
                    return;
                }
                if (!g_smoke.ddolCharacter.empty())
                {
                    auto* entity = Entity::Find(g_smoke.ddolCharacter);
                    m_smokeDdolCharacter = entity ? entity->GetComponent<CharacterMovementComponent>() : nullptr;
                    if (!m_smokeDdolCharacter || !m_smokeDdolCharacter->Teleport({0, 5, 0}) ||
                        !m_smokeDdolCharacter->SetDesiredVelocity({1.25f, 0, 0}) ||
                        !m_smokeDdolCharacter->ForceVelocity({3, 0, 0}, 10))
                    {
                        std::printf("[player.smoke.ddol] FAILED preparation\n");
                        std::fflush(stdout);
                        EngineBootstrap::SetExitCode(4);
                        PostMessage(handle, WM_CLOSE, 0, 0);
                        return;
                    }

                    try
                    {
                    m_smokeDdolProbe =
                            std::make_unique<DdolProbe>(*entity, g_smoke.ddolHierarchy, g_smoke.ddolGeometry);
                    }
                    catch (const std::exception& error)
                    {
                        std::printf("[physics.player.hierarchy.failure] %s\n", error.what());
                        std::fflush(stdout);
                        EngineBootstrap::SetExitCode(4);
                        PostMessage(handle, WM_CLOSE, 0, 0);
                        return;
                    }

                    if (auto* script = entity->GetComponent<ScriptComponent>())
                        m_smokeWrapperInstance = script->GetInstanceId();

                    Object::SetDontDestroyOnLoad(entity);
                }

                SceneManagers->ActivateScene(m_smokeReloadScene);
                return;
            }
            if (SceneManagers->GetActiveScene() != m_smokeReloadScene)
                return;
            if (!m_smokeReloadActivated)
            {
                m_smokeReloadActivated = true;
                m_smokeReloadPublishedFrame = renderState.publishedFrameId;
                return;
            }
            if (m_smokeDdolCharacter)
            {
                const auto state = m_smokeDdolCharacter->ReadState();
                if (m_smokeDdolCharacter->GetOwner()->GetScene() != m_smokeReloadScene || !state)
                {
                    std::printf("[player.smoke.ddol] FAILED transferred binding\n");
                    EngineBootstrap::SetExitCode(4);
                    PostMessage(handle, WM_CLOSE, 0, 0);
                    return;
                }

                if (state->tick.value < 30)
                    return;

                try
                {
                    if (!m_smokeDdolProbe->Poll(*m_smokeReloadScene, state->tick.value))
                        return;
                }
                catch (const std::exception& error)
                {
                    std::printf("[physics.player.hierarchy.failure] %s\n", error.what());
                    std::fflush(stdout);
                    EngineBootstrap::SetExitCode(4);
                    PostMessage(handle, WM_CLOSE, 0, 0);
                    return;
                }
            }

            if (gameDisplay.completedFrameId <= m_smokeReloadPublishedFrame ||
                m_submittedGameFrameId.load(std::memory_order_acquire) <= m_smokeReloadPublishedFrame)
            {
                return;
            }
            std::printf("[player.smoke.reload] activated=true gameStart=%s pending=%s compositionSubmittedAfterActivation=true\n",
                SceneManagers->IsGameStart() ? "true" : "false",
                SceneManagers->IsSceneLoading() ? "true" : "false");
        }

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
            const EnhancedLiveDebugSnapshot pipelineState = EnhancedSceneRenderer::GetLiveDebugSnapshot();
            for (const auto& node : pipelineState.pipelineNodes)
            {
                const std::string state = !node.conditional ? "always" : node.active ? "active" : "inactive";
                Debug::PrintLog(spdlog::level::debug, "[SMOKE] pipeline.node " + node.name + "|" + state);
            }
            Debug::PrintLog(spdlog::level::debug,
                            "[SMOKE] pipeline.nodes 합계 " + std::to_string(pipelineState.pipelineNodes.size()));
		}

		// 성공 마커 — Verify는 이 줄과 "Scene loaded"(SceneManager), 종료
		// 코드 0을 함께 본다. 락스텝 제거 뒤 GT frame 수만으로 끝내면 실제 GPU
		// 완료가 0이어도 통과하므로 display 슬롯 회전도 함께 요구한다.
        Debug::PrintLog(spdlog::level::debug, "[SMOKE] frame limit reached — clean exit (" +
                                                  std::to_string(Time->GetFrameCount()) + " GT frames, display frame " +
                                                  std::to_string(gameDisplay.completedFrameId) + ", promotions " +
                                                  std::to_string(gameDisplay.promotionCount) + ")");
        std::printf("[player.smoke] "
                    "{\"schemaVersion\":1,\"ready\":%s,\"registeredScriptTypes\":%zu,\"frames\":%llu,"
                    "\"displayPromotions\":%llu,\"submittedGameFrames\":%llu,\"submittedGameFrameId\":%llu}\n",
            ClrHost::Get().IsReady() ? "true" : "false", ClrHost::Get().GetComponentTypeNames().size(),
                    static_cast<unsigned long long>(Time->GetFrameCount()),
                    static_cast<unsigned long long>(gameDisplay.promotionCount),
                    static_cast<unsigned long long>(m_presentation->GetSubmittedGameFrames()),
                    static_cast<unsigned long long>(m_submittedGameFrameId.load(std::memory_order_acquire)));
        // Hidden/occluded windows can submit game composition without compositor acceptance.
        // Normal Finalize still requires the native consumer GPU drain before exit 0.
		EmitTextParseTelemetry();
		PostMessage(handle, WM_CLOSE, 0, 0);
		return;
	}

	if (SceneManagers->IsDecommissioning())
	{
		PostMessage(handle, WM_CLOSE, 0, 0);
	}
}

bool Player::PlayerMain::PresentFrame(std::string& outError)
{
    RECT rect{};
    if (!GetClientRect(PlayerWindowHandle(), &rect))
    {
        outError = "Player could not read the presentation window size";
        return false;
    }
    // A hidden smoke window is still a real swapchain. Only minimization/zero
    // extent suspends presentation; visibility is deliberately not a gate.
    if (IsIconic(PlayerWindowHandle()) || rect.right <= rect.left || rect.bottom <= rect.top)
    {
        return true;
    }
    if (!m_presentation->BeginFrame(outError))
    {
        return false;
    }
    const EnhancedLiveDisplayTexture display =
        EnhancedSceneRenderer::GetLiveDisplayTexture(EnhancedLiveDisplayTarget::Game);
    const uint64_t before = m_presentation->GetSubmittedGameFrames();
    if (!m_presentation->Present(display.textureId, outError))
    {
        return false;
    }
    if (m_presentation->GetSubmittedGameFrames() != before)
    {
        m_submittedGameFrameId.store(display.frame.completedFrameId, std::memory_order_release);
    }
    return true;
}

bool Player::PlayerMain::CreateWindowSizeDependentResources(std::string& outError)
{
    RECT rect{};
    if (!GetClientRect(PlayerWindowHandle(), &rect))
    {
        outError = "Player could not read the resized window";
        return false;
    }
    const uint32_t width = IsIconic(PlayerWindowHandle()) ? 0u :
        static_cast<uint32_t>((std::max)(0L, rect.right - rect.left));
    const uint32_t height = IsIconic(PlayerWindowHandle()) ? 0u :
        static_cast<uint32_t>((std::max)(0L, rect.bottom - rect.top));
    if (!m_presentation->Resize(width, height, outError))
    {
        return false;
    }
    if (width == 0 || height == 0)
    {
        return true;
    }
    OnResizeReleaseEvent();
    ScreenResizeBus::Get().BroadcastRelease();
    OnResizeEvent(static_cast<float>(width), static_cast<float>(height));
    ScreenResizeBus::Get().BroadcastResize(width, height);
    return true;
}

void Player::PlayerMain::InvokeResizeFlag()
{
	m_isInvokeResize.store(true, std::memory_order_release);
	m_presentationWake.notify_one();
}

// OnDeviceLost / OnDeviceRestored가 여기 있었다 (2026-08-10,
// DeviceResources 은퇴). 전자는 비어 있었고 후자는
// CreateWindowSizeDependentResources를 한 번 더 부를 뿐이었다.
