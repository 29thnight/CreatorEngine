#pragma once
// 메뉴 문맥 유도 (PHASE 21 M1 · 계획서 부록 A.6).
//
// 그리는 자리가 프레임당 한 번 문맥을 유도한다. 선언 계층은 씬을 모르고, 여기가
// 둘을 잇는 유일한 지점이다.
//
// ★ 신원을 **값으로** 만들어 넘기는 것이 요점이다. 메뉴 콜백은
//   PresentationThread 에서 돌지만 실제 작업은 게임 스레드로 넘어가므로(A.6),
//   큐를 한 번 거치는 사이 `Entity*` 가 죽을 수 있다. 그래서
//   `@scene:index:generation` 문자열로 들고 다시 찾는다 — 공통 편집 계층이 이미
//   그 신원을 쓴다(`EditorObjectOperations::ObjectId`).
//
// ★ 매 프레임 다시 유도한다. 어디에도 저장하지 않는다 — M4 가 애니메이터 창에서
//   같은 판단을 했다. 게시해 두면 게시 시점과 쓰는 시점 사이에 틈이 생긴다.

#include "EditorMenuSurface.h"

#include <optional>

namespace editor::targets
{
    /// 지금 선택된 엔티티의 신원. 선택이 없거나 씬이 없으면 `nullopt`.
    std::optional<entity_target> selected_entity();
}
