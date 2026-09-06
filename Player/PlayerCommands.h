#pragma once
// PHASE 14.5 LC8 — Player registry(축소)와 그 실행 계층.
//
// ── Editor 의 ConsoleCommandSystem 을 왜 쓰지 않는가 ────────────────────
//
// 그 파일은 배치 프론트엔드(`--exec`·`--script`·stdin)와 세션 판정, 그리고 190 개
// 저작 명령의 등록을 함께 들고 있고, 그 전부가 Editor.lib 에 매여 있다. Player 가
// 그것을 링크하면 에디터 창·자산 저작·렌더 테스트가 통째로 따라 들어온다.
//
// Player 가 실제로 필요한 것은 셋뿐이다: ① 이름→핸들러 표 ② 게임 스레드
// 프레임 경계에서 하나씩 꺼내 실행하는 큐 ③ 결과를 기다리는 사람 깨우기.
// 그 셋만 여기 있다. 배치 입력도, 세션 누적도, exit code 판정도 없다 —
// Player 의 판정은 HTTP 응답으로 가고, 프로세스 종료 코드는 스모크의 것이다.
//
// ── 무엇을 공유하는가 ───────────────────────────────────────────────────
//
// `CommandCore`(descriptor·registry·result)는 role 중립이라 그대로 쓴다
// (§12 의 "role 중립 실행 코어"). seed 표도 공유한다 — schema 의 단일 정본을
// 호스트마다 복제하면 LC3 이 없앤 drift 가 호스트 수만큼 돌아온다.

#include "CommandCore/CommandResult.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace PlayerCmd
{
	/// 명령 하나의 지연 계측. Editor 의 `CommandTiming` 과 같은 뜻이다.
	struct Timing
	{
		double   queuedMs{ 0.0 };
		uint32_t waitedFrames{ 0 };
		double   executedMs{ 0.0 };
	};

	using Completion =
		std::function<void(const CommandCore::CommandResult&, const Timing&)>;

	/// Player 의 명령 표와 큐. **실행은 게임 스레드 전용**이다.
	class CommandHost
	{
	public:
		static CommandHost& Get();

		/// 표를 채운다. 서비스를 열기 **전에** 불러야 한다.
		///
		/// ★ Editor 가 같은 자리에서 겪은 실측을 그대로 피한다
		///   (`EnsureRegistryPopulated`). 표가 비어 있는 채로 수신 스레드를 띄우면
		///   첫 요청의 `cost` 조회가 빗나가고, 동시에 게임 스레드가 채우는 중인
		///   vector 를 수신 스레드가 훑는다.
		void EnsureRegistered();

		/// 결과를 기다리는 사람이 있는 적재. 상한을 넘으면 넣지 않고 false.
		///
		/// `arguments[0]` 이 명령 이름이다. 라인 문법을 거치지 않는다 —
		/// Player 에는 애초에 라인 입력이 없다.
		bool Enqueue(std::vector<std::string> arguments, Completion completion,
		             std::size_t queueCap);

		/// 프레임 경계에서 부른다. 예산만큼 꺼내 실행한다.
		void Pump();

		std::size_t QueueDepth() const;

		/// `quit` 이 왔는가. 프레임 루프가 받아 창을 닫는다.
		bool IsQuitRequested() const noexcept
		{
			return m_quitRequested.load(std::memory_order_acquire);
		}

		/// 핸들러가 쓰는 표면. `quit` 하나뿐이라 창구도 하나다.
		void RequestQuit() noexcept
		{
			m_quitRequested.store(true, std::memory_order_release);
		}

		/// GT 가 멈춰 있어도 답해야 하는 값들(§7.3). 원자 변수와 짧은 락만 읽는다.
		struct Status
		{
			uint64_t    frame{ 0 };
			std::size_t queueDepth{ 0 };
			double      oldestQueuedMs{ 0.0 };
			bool        executing{ false };
			std::string currentCommand;
		};
		Status Snapshot() const;

	private:
		CommandHost() = default;

		CommandHost(const CommandHost&)            = delete;
		CommandHost& operator=(const CommandHost&) = delete;

		CommandCore::CommandResult Execute(const std::vector<std::string>& arguments);

		struct Pending
		{
			std::vector<std::string>              arguments;
			std::chrono::steady_clock::time_point enqueuedAt{};
			uint64_t                              enqueuedFrame{};
			Completion                            completion;
		};

		mutable std::mutex   m_mutex;
		std::deque<Pending>  m_pending;

		// 프레임 경계에서만 증가한다. 수신 스레드가 읽으므로 원자적이어야 한다.
		std::atomic<uint64_t> m_frameIndex{ 0 };
		std::atomic<bool>     m_quitRequested{ false };
		std::atomic<bool>     m_executing{ false };
		std::atomic<bool>     m_registered{ false };

		mutable std::mutex   m_statusMutex;
		std::string          m_currentCommand;
	};
}
