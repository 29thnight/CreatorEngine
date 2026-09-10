#pragma once
// 에디터 창 선언자 중앙 목록 (PHASE 21 M4 · 계획서 부록 B.3).
//
// `RegisterEditorMenuManual.h`와 **같은 모양이고 같은 이유**다. 선언자를 새로
// 만들면 헤더 include 한 줄과 EDITOR_WINDOW_LIST 항목 한 줄을 같이 늘린다.
//
// ★ 목록이 include까지 맡는 것이 핵심이다. Editor는 네 구성 전부 StaticLibrary이고
//   `/WHOLEARCHIVE`가 없어서, 아무도 참조하지 않는 TU는 링커가 끌어오지 않는다.
//   그래서 "선언만 해 두면 알아서 등록된다"가 성립하지 않는다. 여기서 include하면
//   선언자의 for_editor()가 **링크되는 TU 안으로** 들어오고, 열거가 등록을 돌린다.
//
// ★ 이미 목록에 있는 선언자에 창을 하나 더 다는 것은 **그 선언자 헤더 한 줄**이다.
//   이 파일을 건드릴 일이 없다. 그것이 이 배선의 요점이다.

#include "EditorWindowRegistry.h"

// 선언자 헤더는 여기에 include한다.
#include "Windows/EditorStandardWindows.h"
#include "Windows/EditorViewportWindows.h"
#include "Windows/EditorToolboxWindows.h"
#include "Windows/EditorAnimatorWindows.h"

// 목록 항목은 여기에 더한다.
//
// 2단계까지 옛 `ContextRegister` 열 곳이 왔고, 3단계가 직접 `ImGui::Begin`을
// 부르던 열다섯을 모두 옮겼다. 이제 에디터의 창 스물다섯이 전부 여기 있다.
#define EDITOR_WINDOW_LIST(X) \
    X(editor_panel_windows) \
    X(editor_tool_windows) \
    X(editor_viewport_windows) \
    X(editor_authoring_windows) \
    X(editor_diagnostic_windows) \
    X(editor_dialog_windows) \
    X(editor_animator_windows)

namespace editor
{
    /// 부팅 때 한 번 부른다. **창 객체들보다 먼저**여야 한다 — 생성자가 자기
    /// 본문을 걸면서 `open_window`/`close_window`로 초기 표시 상태를 정하는데,
    /// 그때 표에 항목이 있어야 그 호출이 닿는다.
    inline void register_editor_windows()
    {
#define EDITOR_WINDOW_REGISTER_ONE(T) ::editor::register_declarer_windows<T>(#T);
        EDITOR_WINDOW_LIST(EDITOR_WINDOW_REGISTER_ONE)
#undef EDITOR_WINDOW_REGISTER_ONE
    }
}
