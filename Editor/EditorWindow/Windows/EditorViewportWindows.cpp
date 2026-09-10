// 중앙 뷰포트 창 진입점 (PHASE 21 M4 3단계).
//
// 이 TU도 유니티에서 뺀다 — EditorWindowRegistry.cpp와 같은 이유.

#include "EditorViewportWindows.h"

namespace editor::windows
{
    EDITOR_DEFINE_WINDOW_ENTRY(scene_view, EditorWindowName::kScene)
    EDITOR_DEFINE_WINDOW_ENTRY(game_view,  EditorWindowName::kGame)
}
