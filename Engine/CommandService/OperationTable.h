#pragma once
// LC5 (PHASE 14.5) — 장시간 명령의 operation 표 (§5.2 · §7.4).
//
// ── 왜 필요한가 ─────────────────────────────────────────────────────────
//
// §7.1 의 지연 계약은 **짧은 명령**의 것이다. `game.pak`(Release 패키지 빌드)이나
// 전수 코퍼스 검사는 초 단위로 가고, 그것을 동기 응답으로 기다리게 하면 계약이
// 거짓말이 된다. LC3 이 `cost` 를 전수로 붙여 둔 이유가 이것이다 — 서비스가
// 동기/202 를 **추측하지 않고 판정한다**.
//
// ── 취소하지 않는다 ─────────────────────────────────────────────────────
//
// 오늘 취소 지점을 가진 명령은 하나도 없다. 그래서 `Cancel` 은 "요청을 기록"할
// 뿐이고 실제로 끊지 않는다 — §7.4 대로 **취소 불가 명령의 cancel 은 409** 다.
// 끊는 시늉을 하고 실제로는 계속 도는 것이 가장 나쁘다: 호출자는 끝났다고 믿고
// 그 위에서 다음 판단을 한다.

#include "JsonValue.h"

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace CommandService
{
    enum class OperationState : uint8_t
    {
        Queued,
        Running,
        Completed,
    };

    std::string_view ToString(OperationState state) noexcept;

    struct OperationRecord
    {
        std::string    id;
        std::string    command;
        OperationState state{ OperationState::Queued };

        /// 완료 시 채워진다. 동기 응답과 **같은 모양**이다 — 클라이언트가
        /// 폴링 결과와 즉시 응답을 다르게 다루지 않아도 되게.
        std::string    status;
        std::string    code;
        std::string    message;
        std::string    dataJson{ "{}" };

        double         queuedMs{ 0.0 };
        double         executedMs{ 0.0 };
        uint32_t       waitedFrames{ 0 };

        std::string    createdUtc;
        std::string    completedUtc;

        /// 취소를 요청받았는가. 오늘은 기록만 한다(위 주석).
        bool           cancelRequested{ false };

        /// 상태 전이 이벤트. 스트림이 이것을 흘린다.
        ///
        /// ★ **명령 단위 진행률은 아직 없다.** 그것을 내려면 명령이 진행을
        ///   생산해야 하는데 오늘 그런 명령이 하나도 없다. 여기서 흘리는 것은
        ///   수명 전이(queued→running→completed)뿐이고, 그것만으로도 폴링을
        ///   없앨 수 있다. 진행률 생산자는 LC6 이후 domain 이 채운다.
        std::vector<std::string> events;
    };

    class OperationTable
    {
    public:
        /// 새 operation 을 만들고 id 를 돌려준다.
        std::string Create(const std::string& command);

        void MarkRunning(const std::string& id);

        /// 결과를 채우고 완료로 표시한다.
        void Complete(const std::string& id, const std::string& status, const std::string& code,
                      const std::string& message, const std::string& dataJson,
                      double queuedMs, double executedMs, uint32_t waitedFrames);

        /// 없으면 false.
        bool Get(const std::string& id, OperationRecord& out) const;

        /// 취소 요청을 기록한다. 없으면 false.
        bool RequestCancel(const std::string& id);

        /// `GET /operations/{id}` 본문.
        static JsonValue ToJson(const OperationRecord& record);

    private:
        /// 완료 후 보관 기한과 개수 상한(§7.4). 넘으면 오래된 것부터 버린다 —
        /// 표가 메모리를 잠식하는 것을 막는다(§15 의 위험 항목).
        static constexpr std::size_t kMaxRecords = 256;

        void TrimLocked();

        mutable std::mutex          m_mutex;
        std::deque<OperationRecord> m_records;
        uint64_t                    m_nextId{ 1 };
    };
}
