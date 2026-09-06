#pragma once
#include "Delegate.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

// 게임 플레이어의 메인 루프 (BuildPipelinePlan B0-2).
//
// ── 계보 ──
//
// TrainAsis/GameMain의 후계다. 그 파일은 에디터 메인(Dx11Main)의 76% 클론이
// DX12 이관을 따라가지 못한 채 얼어 있던 것이라(삭제된 SceneRenderer 소비),
// 소생 대신 현재의 에디터 부트 패턴을 참조해 새로 썼다. Dx11Main과의 중복은
// 알고 만든 빚이다 — 공용 부트 추출(L4')이 갚는다.
//
// ── 에디터 메인과 무엇이 다른가 ──
//
// · 에디터 GUI 창·기즈모·Undo 계통이 없다. ImGui는 위젯이 아니라 표시
//   경로로만 쓴다 — DX12 셸이 유일한 프레젠트 소유자이고(D4에서 DX11
//   폴백이 걷혔다), 게임 카메라의 표시 슬롯을 전체 화면으로 블릿한다.
// · 씬 델리게이트가 기본 오브젝트(카메라·라이트)를 저작하지 않는다 —
//   플레이어의 씬은 파일에서 온다.
// · WinProcProxy 드레인이 없다 — 큐 적재 자체가 에디터 모드 전용이다
//   (CoreWindow::WndProc의 B0-1 분기).

namespace Player
{
	// --smoke N: N프레임 렌더 후 스스로 종료하고, 성패를 종료 코드와 로그
	// 마커로 알린다(BuildPipelinePlan §2.3 Verify). wWinMain이 파싱해 채운다.
	struct SmokeOptions
	{
		uint64_t frameLimit{ 0 };

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
		double GetFrameDeltaTime() const noexcept { return m_frameDeltaTime; }

	private:
		void StartPresentationThread();
		void StopPresentationThread();
		void PresentationThreadMain();
		void PresentFrame();
		void OnGui();
		void CreateWindowSizeDependentResources();

	private:
		// DeviceResources(DX11) 멤버가 여기 있었다 — 창 핸들 하나 때문이었고,
		// 그것은 CoreWindow 싱글턴이 답한다(2026-08-10).
		// ImGui 표시는 IImGuiHost 경계(GetImGuiHost)를 직접 소비한다.
		// 구 ImGuiRenderer 멤버가 여기 있었다 — 그 겸직 탓에 에디터 독스페이스
		// 빌더와 창 펌프가 플레이어에서도 매 프레임 돌았다(EditorRenderer 재작성).

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
		std::atomic_bool m_isInvokeResize = false;
	};
}
