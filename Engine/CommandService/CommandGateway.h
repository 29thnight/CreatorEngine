#pragma once
// LC4 (PHASE 14.5) — 서비스가 엔진에 닿는 유일한 창구.
//
// ── 왜 인터페이스인가 (§12) ─────────────────────────────────────────────
//
// "`Engine/CommandService` 는 Editor 헤더를 include 하지 않는다. registry 를
// 주입받는다. 이 방향이 지켜져야 Player 가 같은 코드를 쓸 수 있다."
//
// 그래서 서비스는 `ConsoleCommandSystem` 도 `CommandResult` 도 모른다. 어댑터가
// Editor 쪽에 살면서 그 둘을 아래 값 타입으로 바꿔 준다. LC8 의 Player 는 같은
// 인터페이스에 자기 어댑터를 끼운다.

#include <cstdint>
#include <string>
#include <vector>

namespace CommandService
{
    /// 명령 하나의 실행 결과. `CommandCore::CommandResult` 의 전송용 사본이다.
    struct CommandOutcome
    {
        /// §5.3 의 HTTP 상태. 어댑터가 `CommandStatus` 를 보고 정한다.
        ///
        /// ★ 논리 실패를 500 으로 내지 않는다. selftest 가 정직하게 "실패"를
        ///   판정한 것과 서버가 망가진 것은 다른 사건이고, 섞으면 클라이언트가
        ///   재시도해서는 안 될 것을 재시도한다.
        int         httpStatus{ 200 };

        std::string status;      ///< "succeeded" · "failed" · ...
        std::string code;        ///< "scene.not_found"
        std::string message;
        std::string dataJson;    ///< 이미 직렬화된 JSON 객체. 없으면 "{}"

        double      queuedMs{ 0.0 };
        double      executedMs{ 0.0 };
        uint32_t    waitedFrames{ 0 };

        bool        timedOut{ false };
    };

    /// 서비스가 엔진에 요청하는 것 전부.
    class ICommandGateway
    {
    public:
        virtual ~ICommandGateway() = default;

        /// 명령 하나를 게임 스레드에 넣고 결과를 기다린다.
        ///
        /// `arguments[0]` 이 명령 이름이다. **라인 문법을 거치지 않는다** —
        /// LC2 가 연 구조화 경로를 그대로 쓴다(§3.2 의 왕복 손실 없음).
        ///
        /// `timeoutMs` 안에 끝나지 않으면 `timedOut` 을 세워 돌려준다. **명령을
        /// 죽이지는 않는다** — 이미 시작한 GT 작업을 중간에 끊는 것이 더 위험하다(§5.2).
        virtual CommandOutcome Execute(const std::vector<std::string>& arguments,
                                       int timeoutMs) = 0;

        /// `GET /commands` 의 본문(JSON 배열). LC3 의 registry snapshot 이다.
        virtual std::string CommandsJson() = 0;

        /// `GET /commands/{id}`. 없으면 빈 문자열.
        virtual std::string CommandJson(const std::string& name) = 0;

        /// `GET /health` 가 낼 상태. 수신 스레드에서 부른다.
        ///
        /// ★ **게임 스레드가 멈춰 있어도 답해야 한다.** LC0 실측이 그 이유다 —
        ///   `scene.load` 가 큐를 2.4 초 막는 동안 `Pump()` 는 아예 돌지 않았다.
        ///   그 구간에 상태를 못 내면 §7.3 의 "멈춤은 지연이 아니라 상태다"가
        ///   성립하지 않는다. 구현은 원자 변수만 읽어야 한다.
        struct HealthSnapshot
        {
            std::string role;          ///< "editor" | "player"
            uint64_t    frame{ 0 };
            std::size_t queueDepth{ 0 };
            double      oldestQueuedMs{ 0.0 };
            std::string state;         ///< "idle" | "busy"
            std::string blockedReason; ///< 비어 있으면 막혀 있지 않다
            std::string currentCommand;
        };
        virtual HealthSnapshot Health() = 0;
    };
}
