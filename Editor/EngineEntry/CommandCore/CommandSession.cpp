#include "CommandSession.h"

#include "../EngineBootstrap.h"

#include <cstdio>

namespace CommandCore
{
    int ExitCodeFor(CommandStatus status) noexcept
    {
        switch (status)
        {
        case CommandStatus::Succeeded:           return 0;

        // 이행 전 핸들러는 오늘과 같은 거동을 유지한다. 여기서 비-0 을 내면
        // 아직 결과를 안 내는 명령 184 개가 전부 실패로 뒤집혀, 이행이 끝나기
        // 전에 회귀 세트가 통째로 붉어진다. 세는 것은 session 이 따로 한다.
        case CommandStatus::LegacyUnreported:    return 0;

        case CommandStatus::InvalidArguments:    return 2;
        case CommandStatus::PreconditionsFailed: return 3;
        case CommandStatus::Failed:              return 4;
        case CommandStatus::Cancelled:           return 4;
        case CommandStatus::TimedOut:            return 4;
        case CommandStatus::InternalError:       return 5;
        }
        return 5;
    }

    CommandSession& CommandSession::Batch()
    {
        static CommandSession session;
        return session;
    }

    void CommandSession::Record(std::string_view commandId, const CommandResult& result)
    {
        SessionEntry entry;
        entry.sequence  = m_nextSequence++;
        entry.commandId = std::string(commandId);
        entry.status    = result.status;
        entry.code      = result.code;
        m_entries.push_back(std::move(entry));

        if (SeverityRank(result.status) > SeverityRank(m_worst))
        {
            m_worst = result.status;
        }

        // ★ 저장소에서 EngineBootstrap::SetExitCode 를 부르는 자리는 여기 하나다.
        //
        //   다른 곳에서 부르는 순간 "마지막에 쓴 값이 이긴다"로 돌아가고,
        //   그것이 §3.1 이 적은 결함 자체다. 정적 게이트가 이 규칙을 지킨다
        //   (Tools/regression/verify-cli-exit-spine.ps1).
        EngineBootstrap::SetExitCode(ExitCodeFor(m_worst));
    }

    int CommandSession::ExitCode() const noexcept
    {
        return ExitCodeFor(m_worst);
    }

    bool CommandSession::ShouldStopEarly() const noexcept
    {
        if (!m_failFast) return false;
        return SeverityRank(m_worst) >= SeverityRank(CommandStatus::InvalidArguments);
    }

    std::size_t CommandSession::CountOf(CommandStatus status) const noexcept
    {
        std::size_t count = 0;
        for (const SessionEntry& entry : m_entries)
        {
            if (entry.status == status) ++count;
        }
        return count;
    }

    std::string CommandSession::Summary() const
    {
        char buffer[256] = {};
        std::snprintf(buffer, sizeof(buffer),
            "명령 %zu · 성공 %zu · 미보고(legacy) %zu · 인자오류 %zu · 선행조건 %zu · 실패 %zu · 내부오류 %zu · exit %d",
            m_entries.size(),
            CountOf(CommandStatus::Succeeded),
            CountOf(CommandStatus::LegacyUnreported),
            CountOf(CommandStatus::InvalidArguments),
            CountOf(CommandStatus::PreconditionsFailed),
            CountOf(CommandStatus::Failed),
            CountOf(CommandStatus::InternalError),
            ExitCode());
        return buffer;
    }
}
