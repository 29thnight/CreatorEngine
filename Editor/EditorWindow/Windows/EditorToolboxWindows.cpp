// 저작·진단·대화 창 진입점 (PHASE 21 M4 3단계).
//
// 이 TU도 유니티에서 뺀다 — EditorWindowRegistry.cpp와 같은 이유.

#include "EditorToolboxWindows.h"

namespace editor::windows
{
    EDITOR_DEFINE_WINDOW_ENTRY(behavior_tree,       EditorWindowName::kBehaviorTree)
    EDITOR_DEFINE_WINDOW_ENTRY(black_board,         EditorWindowName::kBlackBoard)
    EDITOR_DEFINE_WINDOW_ENTRY(input_action_maps,   EditorWindowName::kInputActionMaps)

    EDITOR_DEFINE_WINDOW_ENTRY(frame_profiler,      EditorWindowName::kFrameProfiler)
    EDITOR_DEFINE_WINDOW_ENTRY(output_log,          EditorWindowName::kOutputLog)
    EDITOR_DEFINE_WINDOW_ENTRY(render_pass_debug,   EditorWindowName::kRenderPassDebug)

    EDITOR_DEFINE_WINDOW_ENTRY(about,               EditorWindowName::kAbout)
    EDITOR_DEFINE_WINDOW_ENTRY(build_scene_setting, EditorWindowName::kBuildSceneSetting)
    EDITOR_DEFINE_WINDOW_ENTRY(grid_settings,       EditorWindowName::kGridSettings)
    EDITOR_DEFINE_WINDOW_ENTRY(model_loading,       EditorWindowName::kModelLoading)

    bool model_loading_in_progress()
    {
        // 본문이 걸려 있고 보여 줄 적재가 남아 있을 때만 프레임을 연다.
        // 옛 `DrawStatus` 는 큐가 비면 `Begin` 앞에서 돌아갔으므로 같은 뜻이다.
        return has_model_loading() && model_loading_has_visible();
    }
}
