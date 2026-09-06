#pragma once

// 검증 프로브 표 — **registry 밖에 산다.**
//
// ── 왜 registry 밖인가 ─────────────────────────────────────────────────
//
// 이 저장소의 기준은 "기본 동작만 명령으로 둔다" 이다. 그런데 2026-09-06 시점
// registry 199 개 중 **114 개가 특정 자산·시나리오에 묶인 검증 프로브**였다 —
// `dx12.ssao` · `vk.gbuffer` · `experiment.boneresolve` 처럼 검사 하나에 이름
// 하나가 붙어 있었다. 그것들은 조작이 아니라 **검사**다.
//
// 명령이 되면 값이 붙는다: 골든 행 · descriptor · help 줄 · 서명 이행 대상.
// PHASE 14.5 §18 이 "모든 command 가 정확히 하나의 terminal `CommandResult` 를
// 만든다" 를 요구하므로, 검사를 명령으로 두고 이행하면 **나가야 할 것이 못
// 나가게 박힌다.** 실제로 그렇게 되고 있었다.
//
// ── 기본 동작은 하나다 ────────────────────────────────────────────────
//
//   selftest             — 등록된 검사 이름을 낸다
//   selftest <이름>       — 그 검사를 돌리고 판정을 결과로 낸다
//
// "이름을 받아 그 검사를 돌린다" 가 조작이고, 검사 목록은 조작이 아니다.
// 그래서 이름들은 `commands.list` 에 나오지 않는다 — 그것이 이 표의 요점이다.
//
// ── 도메인이 등록한다 ─────────────────────────────────────────────────
//
// `Registrar::SelfTest(name, fn)` 로 도메인 TU 가 자기 검사를 건다. 핸들러는
// 도메인 TU 안에서 계속 `static` 이다(등록 창구와 같은 이유 — 유니티 빌드에서
// 외부 링크 심볼을 늘리지 않는다).

#include "CommandCore/CommandResult.h"

#include <string>
#include <vector>

struct ConsoleCommandContext;

namespace ConsoleCmd
{
    using SelfTestHandler = CommandCore::CommandResult (*)(const ConsoleCommandContext&);

    /// 이름 하나를 표에 건다. 같은 이름을 두 번 걸면 나중 것이 거부된다 —
    /// 조용히 덮으면 어느 검사가 도는지 아무도 모르게 된다.
    void RegisterSelfTest(const char* name, SelfTestHandler fn);

    /// 등록된 이름 전부(정렬됨).
    [[nodiscard]] std::vector<std::string> SelfTestNames();

    /// 이름으로 찾는다. 없으면 nullptr.
    [[nodiscard]] SelfTestHandler FindSelfTest(const std::string& name);
}
