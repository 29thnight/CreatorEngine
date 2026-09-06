#pragma once
// LC9 (PHASE 14.5) — schema v1 결과 봉투의 **단일 정본**.
//
// ── 왜 함수 하나로 묶는가 ───────────────────────────────────────────────
//
// §18 은 "배치 JSONL과 서비스 JSON이 같은 schema v1을 공유하고 logical result가
// 같다"를 완료 조건으로 둔다. 두 곳에서 같은 모양을 **각자 조립하면** 그 조건은
// 작성 시점에만 참이고, 한쪽에 필드가 하나 늘어나는 날 조용히 갈라진다 — LC3 이
// help 와 registry 에서 이미 겪은 drift 이고, 그때 답은 "대조하지 말고 생성하라"
// 였다. 같은 답을 여기에 쓴다.
//
// 그래서 HTTP 응답도 배치 JSONL 도 이 함수가 만든 값을 낸다. 갈라질 자리가 없다.
//
// ── 왜 `Engine/CommandService` 인가 ─────────────────────────────────────
//
// §12 의 방향("`Engine/CommandService` 는 Editor 헤더를 include 하지 않는다")을
// 지키려면 이 함수의 인자가 전부 원시 타입이어야 한다. 그래서 `CommandResult` 도
// `CommandTiming` 도 받지 않고, 이미 문자열·숫자로 낮춰진 값만 받는다. 낮추는
// 일은 호출자(Editor 어댑터·배치 writer)가 한다.
//
// ★ `dataJson` 을 **문자열로** 받는 것은 성능이 아니라 손실 때문이다. 어댑터가
//   이미 직렬화한 값을 여기서 다시 파싱했다가 다시 직렬화하면 왕복이 하나 더
//   생기고, 그 왕복마다 손실이 가능하다. 파싱은 봉투에 넣기 위한 한 번뿐이다.

#include "JsonValue.h"

#include <cstdint>
#include <string>

namespace CommandService
{
    /// 명령 하나의 terminal 결과를 schema v1 봉투로 만든다.
    ///
    /// `correlationId` 가 비면 그 필드를 넣지 않는다 — 서비스가 그렇게 해 왔고,
    /// 배치에는 애초에 상관자가 없다.
    JsonValue BuildResultEnvelope(const std::string& command,
                                  const std::string& status,
                                  const std::string& code,
                                  const std::string& message,
                                  const std::string& dataJson,
                                  double             queuedMs,
                                  uint32_t           waitedFrames,
                                  double             executedMs,
                                  const std::string& correlationId = {});
}
