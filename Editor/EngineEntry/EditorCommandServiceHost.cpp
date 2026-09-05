#include "EditorCommandServiceHost.h"

#include "ConsoleCommandSystem.h"
#include "CommandCore/CommandRegistry.h"
#include "CommandCore/CommandResult.h"

#include "../../Engine/CommandService/CommandGateway.h"
#include "../../Engine/CommandService/CommandService.h"
#include "../../Engine/CommandService/JsonValue.h"

#include "CommandResultJson.h"   // LC9: 변환·봉투의 단일 정본

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>

namespace EditorCommandService
{
    namespace
    {
        // ★ LC9: `ToJson` 이 여기 있었다 — 지금은 `CommandResultJson` 하나뿐이다.
        //
        //   배치 JSONL 이 생기면서 같은 변환이 두 벌이 될 뻔했고, 두 벌이 되는
        //   순간 §18 의 "배치 JSONL 과 서비스 JSON 이 같은 schema v1 을 공유한다"
        //   는 작성 시점에만 참인 문장이 된다. 변환도 봉투도 한 곳에서 나온다.
        using EditorCommandJson::ToJson;

        /// §5.3 의 상태 사상.
        ///
        /// ★ 논리 실패를 500 으로 내지 않는다. selftest 가 정직하게 "실패"를
        ///   판정한 것과 서버가 망가진 것은 다른 사건이고, 섞으면 클라이언트가
        ///   재시도해서는 안 될 것을 재시도한다. 200 + `status:"failed"` 다.
        int HttpStatusFor(CommandCore::CommandStatus status, const std::string& code)
        {
            // §5.3 은 "없는 명령"을 404 로 둔다. 그것은 문법 오류(400)와 다른
            // 사건이다 — 400 은 "요청이 잘못됐다"이고 404 는 "그런 것이 없다"라,
            // 클라이언트가 재시도할지 이름을 고칠지가 갈린다.
            if ("command.unknown" == code) return 404;

            switch (status)
            {
            case CommandCore::CommandStatus::Succeeded:           return 200;
            case CommandCore::CommandStatus::LegacyUnreported:    return 200;
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
            object.Set("name",    JV::String(d.canonical));

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

        class EditorGateway final : public CommandService::ICommandGateway
        {
        public:
            CommandService::CommandOutcome Execute(const std::vector<std::string>& arguments,
                                                   int timeoutMs) override
            {
                // 결과를 스레드 사이로 넘긴다. 수신 스레드가 여기서 기다리고,
                // GT 가 completion 에서 채워 깨운다.
                struct Slot
                {
                    std::mutex              mutex;
                    std::condition_variable ready;
                    bool                    done{ false };
                    CommandCore::CommandResult              result;
                    ConsoleCommandSystem::CommandTiming     timing;
                };
                auto slot = std::make_shared<Slot>();

                const bool accepted = ConsoleCommandSystem::Get().EnqueueStructured(arguments,
                    [slot](const CommandCore::CommandResult& result,
                           const ConsoleCommandSystem::CommandTiming& timing)
                    {
                        // GT 에서 불린다. 값만 옮기고 곧 반환한다.
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
                    // 상한에서 거절됐다. 넣지 않았으므로 기다릴 것도 없다 —
                    // 여기서 곧장 429 를 돌려준다.
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
                    // ★ 명령을 죽이지 않는다(§5.2).
                    //
                    //   이미 시작한 GT 작업을 중간에 끊는 것이 더 위험하다. 지금은
                    //   응답만 먼저 돌려주고, operationId 로 승격하는 것은 LC5 다.
                    //   `slot` 이 shared_ptr 인 이유가 이것이다 — 나중에 도착한
                    //   completion 이 죽은 스택을 만지면 안 된다.
                    outcome.httpStatus = 200;
                    outcome.status     = "timed_out";
                    outcome.code       = "command.timeout";
                    outcome.message    = "명령이 " + std::to_string(timeoutMs)
                                       + "ms 안에 끝나지 않았다(실행은 계속된다)";
                    outcome.dataJson   = "{}";
                    outcome.timedOut   = true;
                    return outcome;
                }

                outcome.httpStatus   = HttpStatusFor(slot->result.status, slot->result.code);
                outcome.status       = std::string(CommandCore::ToString(slot->result.status));
                outcome.code         = slot->result.code;
                outcome.message      = slot->result.message;

                // `data` 는 항상 객체다(§5.2). 값이 없으면 `null` 이 아니라 `{}` 다 —
                // 소비자가 `data.frames` 를 읽기 전에 형을 확인하지 않아도 되게 한다.
                outcome.dataJson = (CommandCore::CommandData::Kind::Null == slot->result.data.GetKind())
                    ? std::string("{}")
                    : ToJson(slot->result.data).Serialize();
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

                // ★ 없는 명령은 **참으로 답하지 않는다.**
                //
                //   없는 이름은 404 로 끝나야 하고(§5.3), 여기서 참을 내면 오타
                //   하나가 403 이 된다 — 호출자는 이름이 틀린 것을 권한 문제로
                //   읽고 플래그를 켜 가며 재시도한다.
                return (nullptr != descriptor) && descriptor->executesUserCode;
            }

            bool ExecuteAsync(const std::vector<std::string>& arguments,
                              AsyncCompletion onDone) override
            {
                return ConsoleCommandSystem::Get().EnqueueStructured(arguments,
                    [onDone](const CommandCore::CommandResult& result,
                             const ConsoleCommandSystem::CommandTiming& timing)
                    {
                        CommandService::CommandOutcome outcome;
                        outcome.httpStatus   = HttpStatusFor(result.status, result.code);
                        outcome.status       = std::string(CommandCore::ToString(result.status));
                        outcome.code         = result.code;
                        outcome.message      = result.message;
                        outcome.dataJson     =
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
                return ConsoleCommandSystem::Get().ServiceQueueDepth();
            }

            std::size_t BatchQueueDepth() override
            {
                return ConsoleCommandSystem::Get().BatchQueueDepth();
            }

            void SetQueueCapacity(std::size_t capacity) override
            {
                m_queueCapacity.store(capacity, std::memory_order_relaxed);
            }

            std::string CommandsJson() override
            {
                // LC3 의 snapshot 을 그대로 낸다. 정렬도 그쪽이 보장한다 —
                // 소비자가 diff 로 비교할 수 있어야 한다.
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
                const ConsoleCommandSystem::ServiceStatus status =
                    ConsoleCommandSystem::Get().SnapshotStatus();

                HealthSnapshot health;
                health.role            = "editor";
                health.frame           = status.frame;
                health.queueDepth      = status.serviceQueueDepth;
                health.batchQueueDepth = status.batchQueueDepth;
                health.oldestQueuedMs  = status.oldestQueuedMs;
                health.currentCommand  = status.currentCommand;
                health.state           = status.executing ? "busy" : "idle";

                // 막혀 있음을 "지연"이 아니라 "상태"로 낸다(§7.3).
                //
                // ★ 실행 중이 아닌 정지도 정지다. `wait N` 과 씬 로딩은 `Pump()`
                //   를 조기 반환시켜 서비스 큐를 통째로 세운다 — 그 동안
                //   `executing` 은 거짓이라, 이 세 갈래가 없으면 "idle"만 나간다.
                if (status.executing && !status.currentCommand.empty())
                {
                    health.blockedReason = "command.running:" + status.currentCommand;
                }
                else if (status.sceneLoading)
                {
                    health.state         = "blocked";
                    health.blockedReason = "scene.loading";
                }
                else if (status.waitFramesRemaining > 0)
                {
                    health.state         = "blocked";
                    health.blockedReason = "batch.wait:"
                                         + std::to_string(status.waitFramesRemaining);
                }
                return health;
            }

        private:
            /// 서비스가 `Start` 에서 알려 주는 큐 상한. 0 이면 무제한이다.
            /// 수신 스레드 여럿이 읽으므로 원자적이어야 한다.
            std::atomic<std::size_t> m_queueCapacity{ 0 };
        };

        EditorGateway&           Gateway() { static EditorGateway gateway; return gateway; }
        CommandService::Service& Instance() { static CommandService::Service service; return service; }
    }

    bool Start(const std::string& projectRoot, bool allowUserCode, std::string& outError)
    {
        CommandService::ServiceConfig config;
        config.role          = "editor";
        config.projectRoot   = projectRoot;
        config.allowUserCode = allowUserCode;
        return Instance().Start(config, Gateway(), outError);
    }

    void Stop() noexcept   { Instance().Stop(); }
    bool IsRunning() noexcept { return Instance().IsRunning(); }
    uint16_t Port() noexcept  { return Instance().Port(); }
}
