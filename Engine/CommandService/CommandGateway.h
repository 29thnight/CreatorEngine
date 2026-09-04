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

#include <cstddef>
#include <cstdint>
#include <functional>
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

        /// 명령의 비용 등급(LC3 descriptor). 없는 명령이면 false.
        ///
        /// ★ 서비스가 동기/202 를 **추측하지 않게** 하는 값이다(§6.2).
        ///   추측하면 `game.pak` 같은 초 단위 명령을 동기로 기다리게 되고,
        ///   §7.1 의 지연 계약이 그 순간 거짓말이 된다.
        virtual bool IsLongRunning(const std::string& command, bool& outFound) = 0;

        /// 결과를 기다리지 않고 넣는다. 완료되면 `onDone` 이 **게임 스레드에서**
        /// 불린다 — operation 표를 채우는 데 쓴다.
        ///
        /// 상한에서 거절되면 false — 그때 넣지 **않은** 것이므로 `onDone` 도
        /// 불리지 않는다.
        using AsyncCompletion = std::function<void(const CommandOutcome&)>;
        virtual bool ExecuteAsync(const std::vector<std::string>& arguments,
                                  AsyncCompletion onDone) = 0;

        /// 서비스 큐 깊이. 관측용이다(`/health`).
        virtual std::size_t QueueDepth() = 0;

        /// 배치 큐 깊이. HTTP 호출자가 앉는 줄은 아니지만, 이것이 차 있으면
        /// 서비스 큐의 소진도 같이 느려진다 — `/health` 가 둘을 따로 낸다.
        virtual std::size_t BatchQueueDepth() = 0;

        /// 큐 상한을 알린다. 서비스가 `Start` 에서 한 번 밀어 넣는다.
        ///
        /// ★ **상한을 적재 지점이 알아야 한다.** 예전에는 서비스가 `QueueDepth()`
        ///   를 읽어 보고 넘으면 429 를 냈다. 읽기와 넣기가 별개의 임계구역이라,
        ///   동시 요청 여러 개가 전부 검사를 통과한 뒤 차례로 넣으면 상한을
        ///   넘긴다. 검사와 적재가 한 락 안에 있어야 상한이 실제 불변식이 된다.
        virtual void SetQueueCapacity(std::size_t capacity) = 0;

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
            std::size_t queueDepth{ 0 };        ///< 서비스 큐 — HTTP 호출자가 앉는 줄
            std::size_t batchQueueDepth{ 0 };   ///< 배치 큐(--exec·--script·stdin)
            double      oldestQueuedMs{ 0.0 };  ///< 서비스 큐 선두가 기다린 시간
            std::string state;         ///< "idle" | "busy"
            std::string blockedReason; ///< 비어 있으면 막혀 있지 않다
            std::string currentCommand;
        };
        virtual HealthSnapshot Health() = 0;
    };
}
