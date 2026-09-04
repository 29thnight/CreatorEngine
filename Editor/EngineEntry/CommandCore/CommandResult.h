#pragma once
// LC1 (PHASE 14.5) — 명령 결과의 정본.
//
// ── 무엇이 없어서 이것이 생겼나 ──────────────────────────────────────────
//
// 오늘 명령의 출구는 `std::printf` 689 곳뿐이다. 핸들러가 `bool passed`를 손에
// 쥐고도 그것을 문자열로 찍고 버린다. 그래서 셋이 동시에 성립한다.
//
//   · 호출자에게 돌려줄 값이 없다 — HTTP 200 의 본문에 담을 것이 없다(LC4).
//   · 실패가 프로세스 종료 코드에 닿지 않는다 — 실패를 출력한 뒤 quit 하면
//     0 으로 끝난다. LC0 canary 가 4/4 로 그 상태를 고정해 두었다.
//   · 자동화가 판정을 읽으려면 한국어 로그 문안을 정규식으로 긁어야 한다.
//
// `CommandResult` 는 그 셋의 공통 원인 하나를 없앤다. **명령 하나는 정확히 하나의
// terminal 결과를 만든다.** printf 는 사람이 읽는 부수 출력으로 남고, 판정의
// 정본은 이 값이다.
//
// ── queue 를 건너는 값은 전부 owned 다 ──────────────────────────────────
//
// `CommandData` 에 raw pointer, `Meta::Type*`, RHI native object 를 넣지 않는다.
// 결과는 프레임 경계에서 만들어져 다른 스레드(LC4 의 수신 스레드)가 읽으므로,
// 만든 프레임이 끝나면 사라지는 것을 가리키면 안 된다. 큰 바이너리(PNG·dump)는
// inline base64 가 아니라 artifact 경로와 digest 로 참조한다(§6.4).

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace CommandCore
{
    // ── 상태 ────────────────────────────────────────────────────────────
    //
    // 계획 §6.4 의 열거를 그대로 쓴다. `LegacyUnreported` 만 이행기 전용이다.
    enum class CommandStatus : uint8_t
    {
        Succeeded,            ///< 명령이 의도한 일을 했다.
        InvalidArguments,     ///< 이름·문법·인자가 틀렸다. 핸들러는 부르지 않았거나 즉시 반환했다.
        PreconditionsFailed,  ///< 부를 수는 있으나 지금 상태에서 성립하지 않는다(씬 없음, 백엔드 없음).
        Failed,               ///< 명령이 돌았고 판정이 실패다. selftest 의 정직한 "실패"가 여기다.
        Cancelled,            ///< 취소 지점을 가진 명령이 취소됐다(LC5).
        TimedOut,             ///< 시간 안에 끝나지 못했다(LC5).
        InternalError,        ///< 핸들러 밖의 결함. 예외·불변식 위반·IO 실패.

        /// 이행기 전용 — 아직 결과를 내지 않는 legacy void 핸들러.
        ///
        /// "성공"이 아니라 **"모른다"** 이다. 둘을 같은 값으로 두면 미이행
        /// 핸들러가 성공으로 집계되어, 남은 이행 대상이 몇 개인지 세는 자가
        /// 사라진다. exit code 로는 0 이 되지만(오늘과 같은 거동) session 은
        /// 이것을 따로 센다. LC9 가 이 값의 사용량 0 을 확인하고 제거한다.
        LegacyUnreported,
    };

    /// 로그·discovery 표기용. 안정 문자열이라 소비자가 파싱해도 된다.
    std::string_view ToString(CommandStatus status) noexcept;

    /// 심각도 순위. **exit code 와 별개 함수다.**
    ///
    /// 계획 §5.4 가 "숫자 최대값이 아니라 명시한 severity 순서"라고 못박은
    /// 이유는, exit code 는 외부 계약이라 값이 바뀔 수 있는데 "무엇이 더 나쁜가"는
    /// 그것과 독립이어야 하기 때문이다. 오늘은 두 순서가 우연히 일치하지만
    /// 함수를 하나로 합치지 않는다.
    uint8_t SeverityRank(CommandStatus status) noexcept;

    // ── 결과 데이터 ─────────────────────────────────────────────────────
    //
    // 작은 소유형 트리. JSON 이 아니다 — codec 은 LC4 의 산출물이고, 여기서
    // JSON 을 흉내 내면 LC4 가 걷어내야 할 두 번째 codec 이 생긴다. 이 타입은
    // "값이 무엇인가"만 담고, 어떤 문법으로 쓰는지는 formatter 가 정한다.
    //
    // object 를 map 이 아니라 순서 있는 vector 로 두는 이유: 출력이 실행마다
    // 같은 순서여야 golden 과 diff 가 성립한다. 키가 몇 개 안 되므로 선형
    // 탐색으로 충분하다.
    class CommandData
    {
    public:
        enum class Kind : uint8_t { Null, Bool, Int, Double, String, Array, Object };

        CommandData() = default;

        static CommandData Bool(bool value);
        static CommandData Int(int64_t value);
        static CommandData Double(double value);
        static CommandData String(std::string value);
        static CommandData Array();
        static CommandData Object();

        Kind GetKind() const noexcept { return m_kind; }
        bool IsNull()   const noexcept { return Kind::Null == m_kind; }

        bool        AsBool()   const noexcept { return m_bool; }
        int64_t     AsInt()    const noexcept { return m_int; }
        double      AsDouble() const noexcept { return m_double; }
        const std::string& AsString() const noexcept { return m_string; }

        const std::vector<CommandData>& Items() const noexcept { return m_array; }
        const std::vector<std::pair<std::string, CommandData>>& Fields() const noexcept { return m_object; }

        /// Array 에 값을 더한다. Array 가 아니면 Array 로 만든 뒤 더한다.
        void Append(CommandData value);

        /// Object 에 키를 설정한다. 같은 키가 이미 있으면 덮어쓴다 —
        /// 중복 키는 어떤 문법으로도 뜻이 하나가 아니라, 만들지 않는다.
        void Set(std::string key, CommandData value);

        /// 없으면 nullptr.
        const CommandData* Find(std::string_view key) const noexcept;

    private:
        Kind        m_kind{ Kind::Null };
        bool        m_bool{};
        int64_t     m_int{};
        double      m_double{};
        std::string m_string;

        std::vector<CommandData>                         m_array;
        std::vector<std::pair<std::string, CommandData>> m_object;
    };

    // ── 결과 ────────────────────────────────────────────────────────────
    struct CommandResult
    {
        CommandStatus status{ CommandStatus::Succeeded };

        /// 안정 식별자. 사람이 읽는 문장이 아니라 기계가 분기하는 값이다.
        /// `scene.not_found`, `test.pixel_mismatch` 처럼 점으로 구분한다.
        /// 성공에는 비워 두거나 `ok` 를 쓴다.
        std::string code;

        /// 사람이 읽는 한 줄. 이 문자열로 분기하는 소비자를 만들지 않는다.
        std::string message;

        /// 기계가 읽는 값. 없으면 Null.
        CommandData data;

        bool IsSuccess() const noexcept { return CommandStatus::Succeeded == status; }
    };

    // ── 생성 헬퍼 ───────────────────────────────────────────────────────
    //
    // 핸들러가 결과를 만드는 자리는 205 곳이 될 것이므로 짧아야 한다.
    // 길면 사람이 `return {};` 로 도망가고, 그러면 상태가 조용히 Succeeded 가 된다.
    CommandResult Ok(std::string message = {}, CommandData data = {});
    CommandResult Fail(std::string code, std::string message, CommandData data = {});
    CommandResult InvalidArguments(std::string message, std::string code = "args.invalid");
    CommandResult PreconditionFailed(std::string code, std::string message);
    CommandResult InternalError(std::string code, std::string message);

    /// 이행 전 legacy void 핸들러가 남기는 값.
    CommandResult LegacyUnreported();

    /// legacy 핸들러가 `EngineBootstrap::SetExitCode` 로 직접 남긴 코드를 상태로 되읽는다.
    ///
    /// ── 왜 이것이 필요한가 ──────────────────────────────────────────────
    ///
    /// session 이 매 명령마다 exit code 를 쓰기 시작하면서, 아직 이행되지 않은
    /// 핸들러가 직접 쓴 값이 **같은 프레임 안에서 0 으로 덮인다.** 실측으로
    /// 확인했다 — `material.corpus.probe` 를 인자 없이 부르면 핸들러가 6 을
    /// 쓰지만 프로세스는 0 으로 끝났다. LC1 이 고치려던 결함을 LC1 이 더 나쁜
    /// 형태로 만든 자리였다(예전에는 그 6 이 살아남았다).
    ///
    /// 그래서 adapter 가 핸들러 전후의 exit code 를 관측해 그 뜻을 상태로 되돌린다.
    /// 값 자체를 그대로 보존하지 않고 §5.4 표로 사상하는 이유는 두 개의 exit code
    /// 체계가 공존하면 소비자가 어느 쪽을 읽는지 알 수 없기 때문이다. 사상이
    /// 안전한 근거는 §3.1 에 있다 — 특정 비-0 값에 의존하는 소비자가 0 건이다.
    ///
    /// `0` 은 "실패 없음"이라 `LegacyUnreported` 를 낸다.
    CommandResult LegacyDirectExit(int exitCode);
}
