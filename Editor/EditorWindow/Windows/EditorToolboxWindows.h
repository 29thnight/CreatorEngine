#pragma once
// 저작·진단·대화 창 선언 (PHASE 21 M4 3단계 · 계획서 부록 B.3).
//
// 옛 코드에서 직접 `ImGui::Begin` 을 부르던 것들이다. 셋으로 나눈 것은 표면이
// 다르기 때문이다 — 저작은 자산을 고치고, 진단은 상태를 보여 주고, 대화는
// 한 번 읽고 닫는다. `editor.windows` 덤프가 이 갈래로 읽힌다.
//
// 열 개 다 기본이 닫힘이다. 옛 코드에서 `m_bShowX` 같은 bool 이 거짓으로
// 시작해 메뉴가 켜 주던 것과 같은 상태이고, 이제 그 bool 이 표 항목의
// `open` 하나로 접혔다.

#include "EditorWindowSchema.h"
#include "EditorWindowNames.h"
#include "EditorWindowBody.h"

namespace editor::windows
{
    void draw_behavior_tree();       bool has_behavior_tree();
    void draw_black_board();         bool has_black_board();
    void draw_input_action_maps();   bool has_input_action_maps();

    void draw_frame_profiler();      bool has_frame_profiler();
    void draw_output_log();          bool has_output_log();
    void draw_render_pass_debug();   bool has_render_pass_debug();

    void draw_about();               bool has_about();
    void draw_build_scene_setting(); bool has_build_scene_setting();
    void draw_grid_settings();       bool has_grid_settings();
    void draw_model_loading();       bool has_model_loading();

    /// 진행 중인 모델 적재가 하나라도 있는가. 옛 `DrawStatus` 첫 두 줄이
    /// 비어 있으면 곧바로 돌아가던 판단이고, 그것이 존재 조건이 되었다.
    /// 이 술어는 `EditorModelPlacement.cpp` 가 답한다 — 큐가 그 파일 것이다.
    bool model_loading_has_visible();

    /// 위 술어와 본문이 걸렸는지를 함께 본다. 선언이 부르는 것은 이쪽이다.
    bool model_loading_in_progress();
}

// ── 저작 창 셋 ────────────────────────────────────────────────────────────
struct editor_authoring_windows
{
    static consteval auto for_editor()
    {
        using namespace editor;
        return window_set(
            // 도크 빌더가 이 둘을 가운데 노드에 넣는다(BuildInitialDockLayout).
            panel<&windows::draw_behavior_tree>(
                EditorWindowName::kBehaviorTree, EditorWindowName::kBehaviorTree)
                .dock(dock_slot::center)
                .open_by_default(false)
                .available(&windows::has_behavior_tree),

            panel<&windows::draw_black_board>(
                EditorWindowName::kBlackBoard, EditorWindowName::kBlackBoard)
                .dock(dock_slot::center)
                .open_by_default(false)
                .available(&windows::has_black_board),

            panel<&windows::draw_input_action_maps>(
                EditorWindowName::kInputActionMaps, EditorWindowName::kInputActionMaps)
                .initial_size(800.f, 600.f)
                .open_by_default(false)
                .available(&windows::has_input_action_maps));
    }
};

// ── 진단 창 셋 ────────────────────────────────────────────────────────────
struct editor_diagnostic_windows
{
    static consteval auto for_editor()
    {
        using namespace editor;
        return window_set(
            // 프레임 프로파일러만 앞으로 끌어올린다. 본문 첫 줄이
            // BringWindowToFocusFront + BringWindowToDisplayFront 를 부르던 것이
            // 표시 순서 선언으로 왔다.
            panel<&windows::draw_frame_profiler>(
                EditorWindowName::kFrameProfiler, EditorWindowName::kFrameProfiler)
                .stacking(window_stacking::focus_front)
                .open_by_default(false)
                .available(&windows::has_frame_profiler),

            panel<&windows::draw_output_log>(
                EditorWindowName::kOutputLog, EditorWindowName::kOutputLog)
                .open_by_default(false)
                .available(&windows::has_output_log),

            panel<&windows::draw_render_pass_debug>(
                EditorWindowName::kRenderPassDebug, EditorWindowName::kRenderPassDebug)
                .initial_size(400.f, 300.f)
                .open_by_default(false)
                .available(&windows::has_render_pass_debug));
    }
};

// ── 대화 창 넷 ────────────────────────────────────────────────────────────
struct editor_dialog_windows
{
    static consteval auto for_editor()
    {
        using namespace editor;
        return window_set(
            // 높이 0은 `AlwaysAutoResize` 와 짝이다 — 너비만 정하고 높이는
            // 내용이 정한다. 조건이 `Appearing` 인 유일한 창이다.
            panel<&windows::draw_about>(
                EditorWindowName::kAbout, EditorWindowName::kAbout)
                .traits(window_trait::no_docking |
                        window_trait::no_collapse |
                        window_trait::auto_resize)
                .initial_size(440.f, 0.f, size_policy::on_appearing)
                .open_by_default(false)
                .available(&windows::has_about),

            panel<&windows::draw_build_scene_setting>(
                EditorWindowName::kBuildSceneSetting, EditorWindowName::kBuildSceneSetting)
                .initial_size(460.f, 340.f)
                .open_by_default(false)
                .available(&windows::has_build_scene_setting),

            panel<&windows::draw_grid_settings>(
                EditorWindowName::kGridSettings, EditorWindowName::kGridSettings)
                .traits(window_trait::auto_resize)
                .open_by_default(false)
                .available(&windows::has_grid_settings),

            // 유일한 일시 표시다. 적재가 끝나면 스스로 사라지므로 닫기 단추가
            // 없고 배치를 남기지 않는다. 성질은 옛 `Begin` 인자 그대로라
            // 역할 기본값의 `no_docking` 은 빼고 적었다.
            transient<&windows::draw_model_loading>(
                EditorWindowName::kModelLoading, EditorWindowName::kModelLoading)
                .traits(window_trait::auto_resize | window_trait::no_saved_layout)
                .available(&windows::model_loading_in_progress));
    }
};
