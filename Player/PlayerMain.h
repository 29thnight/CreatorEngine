#pragma once
class ProjectLayerSettings;
namespace wave
{
    class AudioHost;
    class PlaybackService;
    class AudioCatalog;
}
#include "Delegate.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <future>

#include <memory>
#include <mutex>
#include <string>
#include <thread>

class Scene;
class CharacterMovementComponent;

// Packaged game loop. Rendering publishes completed game images; a Player-owned
// native presentation backend composes them into the window without editor UI.

namespace Player
{
    class DdolProbe;
    class Presentation;
	// --smoke N: N프레임 렌더 후 스스로 종료하고, 성패를 종료 코드와 로그
	// 마커로 알린다(BuildPipelinePlan §2.3 Verify). wWinMain이 파싱해 채운다.
	struct SmokeOptions
	{
		uint64_t frameLimit{ 0 };
		uint64_t minimumPromotions{ 2 };
		bool reloadScene{ false };
        std::string ddolCharacter;
        std::string reloadDestination;
        bool ddolHierarchy = false;
        bool ddolGeometry = false;
        bool geometryFailure = false;

		bool IsActive() const { return 0 != frameLimit; }
	};

	inline SmokeOptions g_smoke{};

	// --command-service: 로컬 HTTP/JSON 명령 서비스를 켠다(PHASE 14.5 LC8 · §11.2).
	//
	// ★ **기본은 off 다.** 서비스는 실행 표면이라 "설정 파일에 켜져 있었다" 로
	//   열리면 안 된다(§8). 켜는 것은 이 명시 플래그뿐이다.
	//
	// ★★ Shipping 빌드에는 플래그가 있어도 서비스가 **없다.** 그 격리는 이
	//   구조체가 아니라 `Player.vcxproj` 의 구성 조건부 ProjectReference 가 한다 —
	//   여기서 플래그를 무시하는 것은 격리가 아니라 예의다.
	struct ServiceOptions
	{
		bool        enabled{ false };

		/// `Library/CommandService/endpoint.json` 이 놓일 뿌리.
		///
		/// Player 는 프로세스마다 격리된 runtime 데이터 뿌리를 갖는다(PlayerApp 의
		/// `runtimeDataRoot`). 그 안에 두면 Player 를 여럿 띄워도 endpoint 파일이
		/// 서로를 덮지 않는다 — 에디터는 프로젝트 뿌리 하나를 쓰지만 그쪽은
		/// 한 번에 하나만 뜬다는 전제가 있고, Player 에는 그 전제가 없다.
		std::string endpointRoot;
	};

	inline ServiceOptions g_service{};

	// ★ IDeviceNotify 상속이 여기 있었다 (2026-08-10, DeviceResources 은퇴).
	//   OnDeviceLost는 비어 있었고 OnDeviceRestored는 리사이즈 경로를 한 번 더
	//   부르는 것뿐이었다. DX11 디바이스 소실 통지에 대응하던 계약인데 그
	//   디바이스가 사라졌다.
	class PlayerMain
	{
	public:
		PlayerMain();
		~PlayerMain();

		void Initialize();
		void Finalize();
		void Update();
		void InvokeResizeFlag();
		void NotifyRenderFramePublished(uint64_t frameId);
        uint64_t GetTemporalRealFrameId() const { return m_temporalFrameReady ? m_temporalRealFrameId : 0; }
        void DiscardTemporalRealFrame();
		double GetFrameDeltaTime() const noexcept { return m_frameDeltaTime; }

	private:
        std::shared_ptr<ProjectLayerSettings> m_projectLayers;
        std::unique_ptr<wave::AudioHost> m_audioHost;
        std::unique_ptr<wave::PlaybackService> m_audioPlayback;
        std::unique_ptr<wave::AudioCatalog> m_audioCatalog;

        bool PollStartupScene();
        std::future<Scene*> m_startupScene;
        std::string m_startupScenePath;
        Scene* m_startupSceneToActivate{}; // SceneManager owns the constructed result.
        bool m_startupComplete{};
        bool m_startupFailed{};

		void StartPresentationThread();
		void StopPresentationThread();
		void PresentationThreadMain();
        bool PresentFrame(std::string& outError);
        bool CreateWindowSizeDependentResources(std::string& outError);
        void NotifyDisplayAvailable();
        void StopPresentation();

	private:
        std::shared_ptr<Presentation> m_presentation;
        std::atomic_bool m_presentationFailed{false};
        std::atomic<uint64_t> m_submittedGameFrameId{0};
        uint64_t m_temporalRealFrameId{0};
        bool m_temporalFrameReady{false};
        bool m_displayAvailable{false};
        bool m_presentationStopped{false};
        bool m_finalized{false};

		Core::DelegateHandle m_inputEventHandle;
		Core::DelegateHandle m_newSceneCreatedHandle;
		Core::DelegateHandle m_activeSceneChangedHandle;

		std::thread m_presentationThread;
		std::mutex m_presentationMutex;
		std::condition_variable m_presentationWake;
		bool m_presentationThreadStarted{ false };
		bool m_presentationThreadStartFailed{ false };
		bool m_presentationStopRequested{ false };
		uint64_t m_requestedPresentationFrameId{ 0 };
		uint64_t m_consumedPresentationFrameId{ 0 };
		uint64_t m_presentationRequests{ 0 };
		uint64_t m_presentationFrames{ 0 };
		uint64_t m_presentationLatestWins{ 0 };
		uint64_t m_presentationShutdownDiscarded{ 0 };
		uint32_t m_presentationThreadTestDelayMs{ 0 };
		double m_frameDeltaTime{ 0.0 };
		std::future<Scene*> m_smokeReload;
		Scene* m_smokeReloadScene{ nullptr };
        CharacterMovementComponent* m_smokeDdolCharacter{ nullptr };
        int m_smokeWrapperInstance{ -1 };
        std::unique_ptr<DdolProbe> m_smokeDdolProbe;
		bool m_smokeReloadStarted{ false };
		bool m_smokeReloadActivated{ false };
		uint64_t m_smokeReloadPublishedFrame{ 0 };
        std::chrono::steady_clock::time_point m_smokeReloadReport{};
        std::chrono::steady_clock::time_point m_smokeProgressReport{};
		std::atomic_bool m_isInvokeResize = false;
	};
}
