#include "PlayerCommandService.h"

// ── Shipping 갈래 ───────────────────────────────────────────────────────
//
// `CE_SHIPPING` 은 Directory.Build.targets 가 정의한다(Development/Shipping 스위치).
// 이 갈래에서는 `Engine/CommandService` 헤더를 **include 조차 하지 않는다** —
// 그 lib 이 링크에 없으므로 include 만 해도 선언된 심볼을 참조하는 순간 깨진다.

#if CE_SHIPPING

#include <cstdio>

namespace PlayerCommandService
{
	bool Start(const std::string&, std::string& outError)
	{
		// ★ 조용히 실패하지 않는다. `--command-service` 를 준 사람에게 "이 빌드에는
		//   서비스가 없다" 를 말해 준다 — 아무 말 없이 열리지 않으면 방화벽·포트·
		//   토큰을 뒤지게 된다.
		outError = "이 빌드(Shipping)에는 명령 서비스가 컴파일되어 있지 않다";
		return false;
	}

	void Stop() noexcept {}
	bool IsRunning() noexcept { return false; }
	uint16_t Port() noexcept { return 0; }
	bool IsCompiledIn() noexcept { return false; }
}

#else   // CE_SHIPPING

#include "PlayerCommands.h"

#include "CommandCore/CommandRegistry.h"
#include "CommandCore/CommandResult.h"

#include "../Engine/CommandService/CommandGateway.h"
#include "../Engine/CommandService/CommandService.h"
#include "../Engine/CommandService/JsonValue.h"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>

namespace PlayerCommandService
{
	namespace
	{
		/// `CommandCore::CommandData` → `CommandService::JsonValue`.
		///
		/// Editor 어댑터에 같은 함수가 있다. 공유하지 않는 이유는 §12 의 의존
		/// 방향이다 — 그 함수는 Editor 어댑터의 내부 익명 이름공간에 있고, 그것을
		/// 꺼내 공유하려면 Editor 와 Player 가 함께 링크하는 자리가 필요한데
		/// 그 자리를 만드는 것이 곧 Editor.lib 을 Player 에 들이는 일이다.
		///
		/// ★ **JSON codec 은 `Engine/CommandService` 것을 그대로 쓴다**(§11.2).
		///   authoring 문서 경로(`AuthoringParsedDocument`)를 지나지 않는 것이
		///   요점이고, 그 조건은 이 codec 을 쓰는 것으로 이미 만족된다 —
		///   `JsonValue` 는 자체 codec 이라 ryml 도 authoring 계측도 건드리지
		///   않는다. `runtime.text-parser calls=0` 이 그 증거로 남는다.
		CommandService::JsonValue ToJson(const CommandCore::CommandData& data)
		{
			using CK = CommandCore::CommandData::Kind;
			using JV = CommandService::JsonValue;

			switch (data.GetKind())
			{
			case CK::Null:   return JV();
			case CK::Bool:   return JV::Bool(data.AsBool());
			case CK::Int:    return JV::Int(data.AsInt());
			case CK::Double: return JV::Double(data.AsDouble());
			case CK::String: return JV::String(data.AsString());
			case CK::Array:
			{
				JV array = JV::Array();
				for (const CommandCore::CommandData& item : data.Items()) array.Append(ToJson(item));
				return array;
			}
			case CK::Object:
			{
				JV object = JV::Object();
				for (const auto& field : data.Fields()) object.Set(field.first, ToJson(field.second));
				return object;
			}
			}
			return JV();
		}

		/// §5.3 의 상태 사상. Editor 와 같은 표를 쓴다 — 두 호스트가 같은 계약을
		/// 쓴다는 것이 §11.2 의 전제이고, 상태 코드가 갈리면 그 전제가 깨진다.
		int HttpStatusFor(CommandCore::CommandStatus status, const std::string& code)
		{
			if ("command.unknown" == code) return 404;

			switch (status)
			{
			case CommandCore::CommandStatus::Succeeded:           return 200;
			case CommandCore::CommandStatus::Failed:              return 200;
			case CommandCore::CommandStatus::InvalidArguments:    return 400;
			case CommandCore::CommandStatus::PreconditionsFailed: return 409;
			case CommandCore::CommandStatus::Cancelled:           return 200;
			case CommandCore::CommandStatus::TimedOut:            return 200;
			case CommandCore::CommandStatus::InternalError:       return 500;
			}
			return 500;
		}

		CommandService::JsonValue DescriptorToJson(const CommandCore::CommandDescriptor& d)
		{
			using JV = CommandService::JsonValue;
			JV object = JV::Object();
			object.Set("name", JV::String(d.canonical));

			JV aliases = JV::Array();
			for (const std::string& alias : d.aliases) aliases.Append(JV::String(alias));
			object.Set("aliases", std::move(aliases));

			object.Set("summary",       JV::String(d.summary));
			object.Set("usage",         JV::String(d.usage));
			object.Set("cost",          JV::String(std::string(CommandCore::ToString(d.cost))));
			object.Set("roles",         JV::String(std::string(CommandCore::ToString(d.roles))));
			object.Set("class",         JV::String(std::string(CommandCore::ToString(d.cls))));
			object.Set("liveness",      JV::String(std::string(CommandCore::ToString(d.liveness))));
			object.Set("userCode",      JV::Bool(d.executesUserCode));
			object.Set("resultBearing", JV::Bool(d.resultBearing));
			return object;
		}

		class PlayerGateway final : public CommandService::ICommandGateway
		{
		public:
			CommandService::CommandOutcome Execute(const std::vector<std::string>& arguments,
			                                       int timeoutMs) override
			{
				struct Slot
				{
					std::mutex                 mutex;
					std::condition_variable    ready;
					bool                       done{ false };
					CommandCore::CommandResult result;
					PlayerCmd::Timing          timing;
				};
				auto slot = std::make_shared<Slot>();

				const bool accepted = PlayerCmd::CommandHost::Get().Enqueue(arguments,
					[slot](const CommandCore::CommandResult& result,
					       const PlayerCmd::Timing& timing)
					{
						{
							std::lock_guard<std::mutex> guard(slot->mutex);
							slot->result = result;
							slot->timing = timing;
							slot->done   = true;
						}
						slot->ready.notify_one();
					},
					m_queueCapacity.load(std::memory_order_relaxed));

				CommandService::CommandOutcome outcome;

				if (!accepted)
				{
					outcome.httpStatus = 429;
					outcome.status     = "error";
					outcome.code       = "service.queue_full";
					outcome.message    = "서비스 큐가 상한에 찼다";
					outcome.dataJson   = "{}";
					return outcome;
				}

				std::unique_lock<std::mutex> lock(slot->mutex);
				const bool finished = slot->ready.wait_for(
					lock, std::chrono::milliseconds(timeoutMs), [&slot] { return slot->done; });

				if (!finished)
				{
					// 실행 중인 게임을 중간에 끊지 않는다(§5.2). `slot` 이
					// shared_ptr 인 이유가 그것이다 — 늦게 도착한 completion 이
					// 죽은 스택을 만지면 안 된다.
					outcome.httpStatus = 200;
					outcome.status     = "timed_out";
					outcome.code       = "command.timeout";
					outcome.message    = "명령이 " + std::to_string(timeoutMs)
					                   + "ms 안에 끝나지 않았다(실행은 계속된다)";
					outcome.dataJson   = "{}";
					outcome.timedOut   = true;
					return outcome;
				}

				outcome.httpStatus = HttpStatusFor(slot->result.status, slot->result.code);
				outcome.status     = std::string(CommandCore::ToString(slot->result.status));
				outcome.code       = slot->result.code;
				outcome.message    = slot->result.message;
				outcome.dataJson   =
					(CommandCore::CommandData::Kind::Null == slot->result.data.GetKind())
					? std::string("{}") : ToJson(slot->result.data).Serialize();
				outcome.queuedMs     = slot->timing.queuedMs;
				outcome.executedMs   = slot->timing.executedMs;
				outcome.waitedFrames = slot->timing.waitedFrames;
				return outcome;
			}

			bool IsLongRunning(const std::string& command, bool& outFound) override
			{
				const CommandCore::CommandDescriptor* descriptor =
					CommandCore::CommandRegistry::Get().Find(command);
				outFound = (nullptr != descriptor);
				if (!outFound) return false;
				return CommandCore::CommandCost::Long == descriptor->cost;
			}

			bool ExecutesUserCode(const std::string& command) override
			{
				const CommandCore::CommandDescriptor* descriptor =
					CommandCore::CommandRegistry::Get().Find(command);

				// ★ Player registry 에는 오늘 `ExecutesUserCode` 가 하나도 없다 —
				//   §11.3 이 Player 의 라이브 코드 교체(등급 B)를 범위 밖으로 뒀다.
				//   그래도 registry 에 묻는다. 이름 목록을 여기 박으면 그 목록만
				//   조용히 뒤처진다.
				return (nullptr != descriptor) && descriptor->executesUserCode;
			}

			bool ExecuteAsync(const std::vector<std::string>& arguments,
			                  AsyncCompletion onDone) override
			{
				return PlayerCmd::CommandHost::Get().Enqueue(arguments,
					[onDone](const CommandCore::CommandResult& result,
					         const PlayerCmd::Timing& timing)
					{
						CommandService::CommandOutcome outcome;
						outcome.httpStatus = HttpStatusFor(result.status, result.code);
						outcome.status     = std::string(CommandCore::ToString(result.status));
						outcome.code       = result.code;
						outcome.message    = result.message;
						outcome.dataJson   =
							(CommandCore::CommandData::Kind::Null == result.data.GetKind())
							? std::string("{}") : ToJson(result.data).Serialize();
						outcome.queuedMs     = timing.queuedMs;
						outcome.executedMs   = timing.executedMs;
						outcome.waitedFrames = timing.waitedFrames;
						onDone(outcome);
					},
					m_queueCapacity.load(std::memory_order_relaxed));
			}

			std::size_t QueueDepth() override
			{
				return PlayerCmd::CommandHost::Get().QueueDepth();
			}

			/// Player 에는 배치 큐가 없다. `--exec`·`--script`·stdin 이 없기 때문이다.
			/// 0 은 "비어 있다" 가 아니라 **"그런 줄이 없다"** 이고, `/health` 를 읽는
			/// 쪽이 그것을 알 방법은 role 뿐이다(아래 Health 가 "player" 를 낸다).
			std::size_t BatchQueueDepth() override { return 0; }

			void SetQueueCapacity(std::size_t capacity) override
			{
				m_queueCapacity.store(capacity, std::memory_order_relaxed);
			}

			std::string CommandsJson() override
			{
				const CommandCore::CommandRegistry& registry = CommandCore::CommandRegistry::Get();

				CommandService::JsonValue root = CommandService::JsonValue::Object();
				root.Set("schemaVersion", CommandService::JsonValue::Int(1));
				root.Set("count", CommandService::JsonValue::Int(
					static_cast<int64_t>(registry.CommandCount())));

				CommandService::JsonValue list = CommandService::JsonValue::Array();
				for (const CommandCore::CommandDescriptor& d : registry.Sorted())
				{
					list.Append(DescriptorToJson(d));
				}
				root.Set("commands", std::move(list));
				return root.Serialize();
			}

			std::string CommandJson(const std::string& name) override
			{
				const CommandCore::CommandDescriptor* d =
					CommandCore::CommandRegistry::Get().Find(name);
				if (nullptr == d) return {};
				return DescriptorToJson(*d).Serialize();
			}

			HealthSnapshot Health() override
			{
				const PlayerCmd::CommandHost::Status status =
					PlayerCmd::CommandHost::Get().Snapshot();

				HealthSnapshot health;
				health.role            = "player";
				health.frame           = status.frame;
				health.queueDepth      = status.queueDepth;
				health.batchQueueDepth = 0;
				health.oldestQueuedMs  = status.oldestQueuedMs;
				health.currentCommand  = status.currentCommand;
				health.state           = status.executing ? "busy" : "idle";
				if (status.executing && !status.currentCommand.empty())
				{
					health.blockedReason = "command.running:" + status.currentCommand;
				}
				return health;
			}

		private:
			std::atomic<std::size_t> m_queueCapacity{ 0 };
		};

		PlayerGateway&           Gateway()  { static PlayerGateway gateway; return gateway; }
		CommandService::Service& Instance() { static CommandService::Service service; return service; }
	}

	bool Start(const std::string& projectRoot, std::string& outError)
	{
		// ★ 표를 **열기 전에** 채운다. Editor 가 같은 자리에서 겪은 실측이다 —
		//   빈 표로 수신 스레드를 띄우면 첫 요청의 cost 조회가 빗나가고, 게임
		//   스레드가 채우는 중인 vector 를 수신 스레드가 훑는다.
		PlayerCmd::CommandHost::Get().EnsureRegistered();

		CommandService::ServiceConfig config;
		config.role        = "player";
		config.projectRoot = projectRoot;

		// ★★ Player 는 사용자 코드를 열지 않는다(§11.3 — 등급 B 는 Editor 에서
		//    먼저 닫고 Player ALC 수명은 별도로 판정한다). 그래서 기본값 false 를
		//    **명시**한다 — 나중에 누가 등급 B 를 Player 에 열 때 이 줄을 지우는
		//    것이 그 결정의 표식이 되게 한다.
		config.allowUserCode = false;

		return Instance().Start(config, Gateway(), outError);
	}

	void Stop() noexcept          { Instance().Stop(); }
	bool IsRunning() noexcept     { return Instance().IsRunning(); }
	uint16_t Port() noexcept      { return Instance().Port(); }
	bool IsCompiledIn() noexcept  { return true; }
}

#endif  // CE_SHIPPING
