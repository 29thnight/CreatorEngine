#pragma once
// LC4 (PHASE 14.5) — 서비스와 에디터를 잇는 어댑터.
//
// ── 왜 Editor 쪽에 사는가 (§12) ─────────────────────────────────────────
//
// "`Engine/CommandService` 는 Editor 헤더를 include 하지 않는다. registry 를
// 주입받는다. 이 방향이 지켜져야 Player 가 같은 코드를 쓸 수 있다."
//
// 그래서 `CommandResult` → `CommandOutcome`, `CommandDescriptor` → JSON 변환은
// 전부 여기서 한다. 서비스는 그 두 타입을 모른다. LC8 의 Player 는 같은
// 인터페이스에 자기 어댑터를 끼운다.

#include <string>

namespace EditorCommandService
{
    /// `--command-service` 로 켠다. **기본은 off** 다(§8).
    ///
    /// 실패하면 false 와 사유. 실패해도 에디터는 계속 뜬다 — 서비스가 안 열린
    /// 것과 에디터가 못 뜨는 것은 다른 사건이고, 후자로 만들 이유가 없다.
    bool Start(const std::string& projectRoot, std::string& outError);

    void Stop() noexcept;

    bool     IsRunning() noexcept;
    uint16_t Port() noexcept;
}
