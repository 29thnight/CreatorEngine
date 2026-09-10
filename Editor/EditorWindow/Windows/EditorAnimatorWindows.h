#pragma once
// 애니메이터 편집 창 선언 (PHASE 21 M4 3b · 계획서 부록 B.3).
//
// 셋 다 선택 종속이다 — 인스펙터가 Animator를 그리고 있을 때만 존재한다.
// 계획서가 "어휘 시험대 넷" 중 하나로 지목한 것이 이 종속이고(부록 B.3),
// 답은 새 어휘가 아니라 이미 있던 존재 술어였다. 의존을 선언에 적는 대신
// 각 창이 자기 대상을 씬에 물어 답한다.
//
// 옛 코드에서 셋은 서로 중첩돼 있었다 — AvatarMask는 Animation Controllers
// 창의 Layers 탭 안에서 `Begin` 을 불렀다. 그래서 부모를 닫거나 다른 탭으로
// 옮기면 자식도 사라졌다. 이제 셋은 형제이고, 사라지는 조건은 자기 대상이
// 없을 때뿐이다.
//
// ★ 앞선 스물둘과 달리 본문 저장소(`bind_window_body`)를 쓰지 않는다.
//   저장소는 본문이 객체의 것이라 `this` 를 물어야 할 때 필요한 장치인데,
//   이 셋의 본문은 자유 함수다. 그럴 때 직접 부르면 배선 단계가 하나 줄고,
//   "등록은 됐는데 본문이 안 걸렸다"는 상태가 아예 생기지 않는다.

#include "EditorWindowSchema.h"
#include "EditorWindowNames.h"

namespace editor::windows
{
    // 아래 다섯의 정의는 `EngineGUIWindow/AnimatorEditorWindows.cpp` 에 있다.
    // 선언은 여기가 하고 구현은 저기가 한다 — 의존 방향이 한쪽이다.
    void draw_animator_event();
    void draw_animation_controllers();
    void draw_avatar_mask();

    /// 인스펙터가 그릴 Animator가 있는가. 선택 개체에서 다시 유도한다.
    bool animator_selected();

    /// 위에 더해 아바타 마스크가 열 컨트롤러 번호가 유효한가.
    bool avatar_mask_target_valid();
}

struct editor_animator_windows
{
    static consteval auto for_editor()
    {
        using namespace editor;
        return window_set(
            // 첫 크기는 옛 `SetNextWindowSize` 그대로다. 나머지 둘은
            // 아무것도 부르지 않았으므로 조건이 없다.
            panel<&windows::draw_animator_event>(
                EditorWindowName::kAnimatorEvent, EditorWindowName::kAnimatorEvent)
                .initial_size(1100.f, 400.f)
                .open_by_default(false)
                .available(&windows::animator_selected),

            panel<&windows::draw_animation_controllers>(
                EditorWindowName::kAnimationControllers, EditorWindowName::kAnimationControllers)
                .open_by_default(false)
                .available(&windows::animator_selected),

            panel<&windows::draw_avatar_mask>(
                EditorWindowName::kAvatarMask, EditorWindowName::kAvatarMask)
                .open_by_default(false)
                .available(&windows::avatar_mask_target_valid));
    }
};
