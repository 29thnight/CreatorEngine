#include "EditorCommandServiceHost.h"

#include "ConsoleCommandSystem.h"
#include "CommandCore/CommandRegistry.h"
#include "CommandCore/CommandResult.h"

#include "../../Engine/CommandService/CommandGateway.h"
#include "../../Engine/CommandService/CommandService.h"
#include "../../Engine/CommandService/JsonValue.h"

#include <condition_variable>
#include <memory>
#include <mutex>

namespace EditorCommandService
{
    namespace
    {
        /// `CommandCore::CommandData` → `CommandService::JsonValue`.
        ///
        /// 두 트리가 같은 모양인데 타입이 다른 이유는 §12 의 의존 방향이다 —
        /// 서비스는 Editor 헤더를 모른다. 변환 한 번이 그 값을 치른다.
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

                ConsoleCommandSystem::Get().EnqueueStructured(arguments,
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
                    });

                CommandService::CommandOutcome outcome;

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
                health.role           = "editor";
                health.frame          = status.frame;
                health.queueDepth     = status.queueDepth;
                health.oldestQueuedMs = status.oldestQueuedMs;
                health.currentCommand = status.currentCommand;
                health.state          = status.executing ? "busy" : "idle";

                // 막혀 있음을 "지연"이 아니라 "상태"로 낸다(§7.3).
                if (status.executing && !status.currentCommand.empty())
                {
                    health.blockedReason = "command.running:" + status.currentCommand;
                }
                return health;
            }
        };

        EditorGateway&           Gateway() { static EditorGateway gateway; return gateway; }
        CommandService::Service& Instance() { static CommandService::Service service; return service; }
    }

    bool Start(const std::string& projectRoot, std::string& outError)
    {
        CommandService::ServiceConfig config;
        config.role        = "editor";
        config.projectRoot = projectRoot;
        return Instance().Start(config, Gateway(), outError);
    }

    void Stop() noexcept   { Instance().Stop(); }
    bool IsRunning() noexcept { return Instance().IsRunning(); }
    uint16_t Port() noexcept  { return Instance().Port(); }
}
