#pragma once
// Development Player reuses the role-neutral parser, registry, and batch session.
// Shipping retains only the argument rejection surface below.
#include <string>

namespace PlayerCmd
{
    bool ParseCommandLineArgument(int argc, wchar_t* const* argv, int& index, std::string& error);
    bool ValidateCommandLine(bool commandService, bool smoke, std::string& error);
}

#if CE_DEVELOPMENT
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

        // Starts the configured local batch after runtime/scene initialization.
        // With no input this does nothing and does not populate the registry.
        void StartBatch(bool runtimeReady);

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
        /// Local line input is tokenized before it reaches this queue.
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

#endif // CE_DEVELOPMENT
