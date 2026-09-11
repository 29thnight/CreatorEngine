#pragma once
// 중앙 뷰포트 창 선언 (PHASE 21 M4 3단계 · 계획서 부록 B.3).
//
// Scene과 Game 둘뿐이고 둘 다 `window_role::central` 이다. 가운데 노드를
// 탭으로 공유하며 닫을 수 없다 — 닫으면 다시 열 자리가 없기 때문이다(§4.2).
//
// 이 둘은 옛 코드에서 프레임 앞에 스타일을 눌렀다. 그중 창 배경과 창 여백만
// `Begin` 이 소비하므로 선언이 들고, 항목 간격이나 버튼 색처럼 본문에 걸리는
// 것은 본문에 남는다.

#include "EditorWindowSchema.h"
#include "EditorWindowNames.h"

// ── 본문 저장소를 거치지 않는다 (PHASE 21 W3) ────────────────────────────
//
// 옛 배선은 진입점이 `run_window_body(이름)` 로 저장소를 뒤지는 트램펄린이었고,
// `has_*` 는 "본문이 걸렸는가" 를 답했다. 두 창 다 소유자가 사라지면서 저장소에
// 걸 일이 없어졌으므로 선언이 본문을 직접 가리킨다. 존재 술어도 뗐다 — 이 둘은
// 언제나 있다. 애니메이터 창 셋이 먼저 간 길과 같은 모양이다.
//
// 정의는 `EngineGUIWindow/SceneViewWindow.cpp` 와 `GameViewWindow.cpp` 에 있다.
// 선언은 여기가 하고 구현은 저기가 한다 — 의존 방향이 한쪽이다.
namespace editor::windows
{
    void draw_scene_view();
    void draw_game_view();
}

struct editor_viewport_windows
{
    static consteval auto for_editor()
    {
        using namespace editor;
        return window_set(
            // 성질은 SceneViewWindow 의 `gizmoWindowFlags` 초기값 그대로다.
            // 그 값에 `|= NoMove` 를 누적하던 줄은 초기값에 이미 같은 비트가
            // 있어 죽은 코드였다(§1.3-1). 이관하며 지웠다.
            central<&windows::draw_scene_view>(
                EditorWindowName::kScene, EditorWindowName::kScene)
                .traits(window_trait::no_move |
                        window_trait::no_bring_to_front_on_focus |
                        window_trait::no_scrollbar |
                        window_trait::no_scroll_with_mouse)
                .stacking(window_stacking::display_back)
                .background(0.f, 0.f, 0.f, 1.f)
                .padding(0.f, 0.f),

            central<&windows::draw_game_view>(
                EditorWindowName::kGame, EditorWindowName::kGame)
                .padding(0.f, 0.f)
                .stacking(window_stacking::display_back));
    }
};
