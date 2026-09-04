#pragma once
// LC1 (PHASE 14.5) — session: 결과를 모으고 프로세스 종료 코드를 정한다.
//
// ── 왜 명령마다가 아니라 session 인가 ────────────────────────────────────
//
// 오늘의 결함은 "실패를 종료 코드로 못 알린다"만이 아니다. **뒤의 성공이 앞의
// 실패를 지운다**는 것이 같이 있다. `SetExitCode` 가 12 곳에 흩어져 있고 전부
// 마지막에 쓴 값이 이긴다. 그래서 `selftest.fail` 뒤에 `help` 하나만 붙여도
// 프로세스가 0 으로 끝날 수 있는 구조였다.
//
// session 은 결과를 **누적**한다. 가장 심한 것을 보존하고, 그 뒤에 무엇이
// 성공하든 내려가지 않는다. 이것이 §3.1 의 "뒤의 성공/quit 이 앞의 실패를
// 지우지 못한다"를 성립시키는 유일한 자리다.
//
// ── exit code 를 쓰는 곳은 여기 하나뿐이다 ──────────────────────────────
//
// 계획 §14.1 의 정적 게이트가 "`EngineBootstrap::SetExitCode` 직접 호출은
// session adapter 한 곳"이다. 그 한 곳이 `CommandSession::Record` 다. 다른
// 곳에서 부르면 다시 "마지막에 쓴 값이 이긴다"로 돌아간다.

#include "CommandResult.h"

#include <cstdint>
#include <string>
#include <vector>

namespace CommandCore
{
    /// session 에 기록된 결과 하나.
    struct SessionEntry
    {
        uint64_t      sequence{};
        std::string   commandId;
        CommandStatus status{};
        std::string   code;
    };

    class CommandSession
    {
    public:
        /// 배치 프로세스 전역 session. LC5 가 서비스 요청마다 별도 session 을
        /// 만들 때, 이 클래스는 그대로 쓰고 인스턴스만 늘어난다.
        static CommandSession& Batch();

        /// 결과 하나를 누적하고 종료 코드를 갱신한다.
        ///
        /// 매번 쓰는 이유: 프로세스가 중간에 죽어도 그때까지의 판정이 종료
        /// 코드에 남는다. 마지막에 한 번만 쓰면 크래시가 판정을 통째로 지운다.
        void Record(std::string_view commandId, const CommandResult& result);

        /// 지금까지 가장 심한 상태.
        CommandStatus WorstStatus() const noexcept { return m_worst; }

        /// §5.4 의 배치 exit code.
        int ExitCode() const noexcept;

        /// `--fail-fast` 로 시작했고 이미 실패가 있는가.
        bool ShouldStopEarly() const noexcept;

        void SetFailFast(bool enabled) noexcept { m_failFast = enabled; }
        bool IsFailFast() const noexcept { return m_failFast; }

        const std::vector<SessionEntry>& Entries() const noexcept { return m_entries; }

        std::size_t CountOf(CommandStatus status) const noexcept;

        /// 사람이 읽는 한 줄 요약. 배치 종료 직전에 찍는다.
        std::string Summary() const;

        // 복사되면 누적이 갈라진다 — 사본에 기록한 실패는 프로세스 종료 코드에
        // 닿지 않는다. 오늘은 아무도 복사하지 않지만, LC5 가 세션을 여럿 만들
        // 때 실수로 값 전달하기 쉬운 자리라 지금 막아 둔다.
        CommandSession(const CommandSession&)            = delete;
        CommandSession& operator=(const CommandSession&) = delete;
        CommandSession(CommandSession&&)                 = delete;
        CommandSession& operator=(CommandSession&&)      = delete;

    private:
        CommandSession() = default;

        std::vector<SessionEntry> m_entries;
        CommandStatus             m_worst{ CommandStatus::Succeeded };
        uint64_t                  m_nextSequence{ 0 };
        bool                      m_failFast{ false };
    };

    /// `CommandStatus` → §5.4 배치 exit code.
    ///
    /// `SeverityRank` 와 별개 함수다 — 순위는 내부 비교용이고 이 값은 외부
    /// 계약이다. 오늘 두 표가 같은 숫자를 내는 것은 우연이며, 어느 한쪽이
    /// 바뀌어도 다른 쪽이 따라가지 않아야 한다.
    int ExitCodeFor(CommandStatus status) noexcept;
}
