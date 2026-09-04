#pragma once
// LC2 (PHASE 14.5) — 배치 라인 문법이 사는 유일한 곳.
//
// ── 무엇을 고치는가 ──────────────────────────────────────────────────────
//
// tokenizer 는 큰따옴표 구간을 한 토큰으로 만든다. 그런데 여러 핸들러가 그
// 결과를 **버리고** 원문을 다시 잘라 이름을 역산한다(계획 §3.2).
//
//     object.parent "Big Boss Character" "Main Characters"
//
// `parts.back()` 은 `Main Characters`(따옴표 없음)인데, 핸들러는 그 문자열을
// **원문에서** `rfind` 해서 앞쪽을 자식 이름으로 삼는다. 원문에는 따옴표가
// 남아 있으므로 복원된 자식 이름은 `"Big Boss Character"` — 따옴표째다.
// 그 이름으로 씬을 찾으니 영영 못 찾는다.
//
// ★ **서비스에서는 같은 결함이 다른 얼굴로 재발한다.** JSON 이
//   `{"args":["Big Boss","Main Characters"]}` 로 이미 갈라 온 값을 라인 문법으로
//   다시 이어 붙였다가 다시 자르면 그 왕복에서 같은 손실이 생긴다. 그래서
//   **서비스 입력은 라인 문자열을 거치지 않는다** — 이 헤더의 `CommandInvocation`
//   이 두 입력이 만나는 지점이고, 라인은 그 앞에서만 존재한다.
//
// ── 규칙 ────────────────────────────────────────────────────────────────
//
//   · 따옴표 밖의 backslash 는 아무 뜻이 없다. Windows 경로가 그대로 산다.
//   · 따옴표 안에서는 `\"` 와 `\\` 만 escape 다. 나머지 backslash 는 보존한다
//     (`"C:\Program Files\x"` 가 온전하다).
//   · 닫히지 않은 따옴표는 **오류**다. 예전에는 조용히 통과했다.
//   · 빈 따옴표 `""` 는 "값을 비웠다"는 뜻의 토큰 하나다.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace CommandCore
{
    struct TokenizeResult
    {
        std::vector<std::string> tokens;

        bool        ok{ true };
        std::string errorCode;      ///< "parse.unclosed_quote" 등
        std::string errorMessage;
    };

    /// 배치 라인 한 줄을 토큰으로 자른다.
    TokenizeResult Tokenize(std::string_view line);

    /// `parts[index]` 부터 끝까지를 공백 하나로 이어 붙인다.
    ///
    /// 예전 코드의 `TrimLine(line.substr(cmd.size()))` 를 대체한다. 원문을 자르는
    /// 대신 토큰을 잇는다는 것이 요점이다 — 따옴표는 이미 벗겨져 있다.
    ///
    /// ★ **토큰을 둘 이상 이으면 legacy 사용으로 센다.**
    ///
    ///   토큰이 둘 이상이라는 것은 호출자가 "따옴표 없이 공백 있는 값을 준다"는
    ///   옛 규약에 기대고 있다는 뜻이다(§3.2 의 결정: 공백 포함 문자열의 배치
    ///   문법은 quote 다). 그 형식에서는 공백 **연속**이 하나로 접히므로,
    ///   공백이 값의 일부인 자유 형식 payload 는 따옴표로 감싸야 정확하다.
    ///   세어 두면 LC9 가 "아무도 안 쓴다"를 근거로 규약을 뗄 수 있다.
    std::string JoinFrom(const std::vector<std::string>& parts, std::size_t index);

    /// "마지막 토큰이 뒤 이름, 그 앞이 앞 이름" 형태를 푼다.
    ///
    /// `object.parent` · `object.duplicate` · `object.rename` · `prefab.create` 가
    /// 공유하는 입력 규약이다. 예전에는 원문을 `rfind` 로 잘라 따옴표를 흘렸다.
    struct TrailingNameSplit
    {
        std::string leading;    ///< 앞 이름
        std::string trailing;   ///< 마지막 토큰
        bool        ok{ false };

        /// 토큰이 정확히 둘이 아니라서 앞쪽을 이어 붙였는가.
        ///
        /// 따옴표를 쓴 입력은 `false` 다. `true` 는 따옴표 없이 공백 이름을 준
        /// legacy 형식이고, 그 사용량을 세어 LC9 가 제거 시점을 정한다.
        bool        usedLegacyJoin{ false };
    };

    /// `trailingCount` 개의 뒤 토큰을 떼고 그 앞을 앞 이름으로 합친다.
    ///
    /// `trailingCount == 1` 이면 `trailing` 에 마지막 토큰이 담긴다. 2 이상이면
    /// 호출자가 뒤 토큰들을 직접 읽는다(`animator.state <오브젝트> <상태> <행동>`
    /// 처럼 뒤가 둘인 명령이 있다).
    TrailingNameSplit SplitTrailingName(const std::vector<std::string>& parts,
                                        std::size_t                     firstIndex,
                                        std::size_t                     trailingCount = 1);

    /// 따옴표 없이 공백 있는 값을 준 legacy 형식이 쓰인 횟수.
    ///
    /// `JoinFrom` 이 토큰을 둘 이상 이었거나 `SplitTrailingName` 이 앞쪽을
    /// 이었을 때 오른다. LC9 가 0 을 확인하고 규약을 제거한다.
    uint64_t LegacyJoinUseCount() noexcept;
    void     ResetLegacyJoinUseCount() noexcept;

    // ── invocation (§6.1) ───────────────────────────────────────────────
    //
    // queue 를 건너는 값은 전부 owned 다. `string_view`·`Scene*`·`Entity*`·
    // backend handle 을 여기에 담지 않는다 — 만든 프레임이 끝나면 사라지는 것을
    // 가리키면, 서비스가 다른 스레드에서 읽을 때 무엇을 볼지 알 수 없다.
    enum class CommandSource : uint8_t
    {
        ExecArgument,       ///< `--exec "<명령>"`
        ExecArgumentList,   ///< `--exec-args <명령> <인자>...` — 라인 문법을 거치지 않는다
        ScriptFile,         ///< `--script <파일>`
        InteractiveConsole, ///< stdin
        HttpService,        ///< LC4
    };

    struct CommandInvocation
    {
        uint64_t                 sequence{};
        CommandSource            source{ CommandSource::ExecArgument };
        std::string              commandId;
        std::vector<std::string> arguments;   ///< commandId 를 포함하지 않는다
        std::string              sourceName;  ///< script 경로 · "--exec" · "stdin"
        uint32_t                 sourceLine{};
        std::string              correlationId;

        /// 핸들러가 오늘 기대하는 형태(`parts[0]` 이 명령 이름).
        ///
        /// 이행이 끝나면 사라진다 — 그때 핸들러는 `arguments` 만 본다.
        std::vector<std::string> ToLegacyParts() const;
    };
}
