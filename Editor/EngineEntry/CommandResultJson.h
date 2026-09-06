#pragma once
// LC9 (PHASE 14.5) — `CommandResult` 를 schema v1 JSON 으로 낮추는 **한 곳**.
//
// ── 왜 별도 파일인가 ────────────────────────────────────────────────────
//
// §18 은 "배치 JSONL과 서비스 JSON이 같은 schema v1을 공유하고 logical result가
// 같다"를 완료 조건으로 둔다. 그 조건을 지키는 방법은 둘이다 — 두 곳을 맞대 보는
// 검사를 두거나, 애초에 한 곳에서 나오게 하거나. LC3 이 help·registry 에서 같은
// 선택을 했고 답은 후자였다("대조하지 말고 생성하라").
//
// 그래서 변환은 여기 하나뿐이다. 서비스 어댑터도 배치 writer 도 이것을 부른다.
//
// ── 왜 Editor 쪽인가 ────────────────────────────────────────────────────
//
// 입력이 `CommandCore::CommandResult` 라 `Engine/CommandService` 에 둘 수 없다
// (§12: 서비스는 Editor 헤더를 include 하지 않는다). 봉투 조립은 서비스 쪽
// `BuildResultEnvelope` 가 하고, 이 파일은 그 앞단 — 값을 낮추는 일 — 만 한다.
//
// ★ Player 에는 이 파일이 없다. 배치가 없기 때문이다(`--exec`·`--script`·stdin 이
//   없다). Player 어댑터는 자기 안에 같은 모양의 변환을 갖고 있고, 그 중복은
//   `PlayerCommandService.cpp` 에 사유와 함께 적혀 있다.

#include "CommandCore/CommandResult.h"

#include "../../Engine/CommandService/JsonValue.h"

#include <cstdint>
#include <string>

namespace EditorCommandJson
{
    /// `CommandCore::CommandData` → `CommandService::JsonValue`.
    ///
    /// 두 트리가 같은 모양인데 타입이 다른 이유는 §12 의 의존 방향이다 —
    /// 서비스는 Editor 헤더를 모른다. 변환 한 번이 그 값을 치른다.
    CommandService::JsonValue ToJson(const CommandCore::CommandData& data);

    /// `data` 를 직렬화한다. 값이 없으면 `"{}"` — `null` 이 아니다(§5.2).
    std::string DataJson(const CommandCore::CommandResult& result);

    /// 배치 JSONL 한 줄. 서비스 응답 본문과 **같은 함수**가 봉투를 만든다.
    ///
    /// 줄바꿈은 붙이지 않는다 — 쓰는 쪽이 붙인다.
    std::string ResultLine(const std::string&                command,
                           const CommandCore::CommandResult& result,
                           double                            queuedMs,
                           uint32_t                          waitedFrames,
                           double                            executedMs);
}
