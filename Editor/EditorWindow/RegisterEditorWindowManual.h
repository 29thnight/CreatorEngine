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
// 예: #include "Windows/ViewportWindows.h"

// 목록 항목은 여기에 더한다. 예:
//     X(viewport_windows) \
//     X(inspector_windows) \
//
// M4 1단계는 배선만 세운다 — 표가 비어 있으므로 셸은 아무것도 그리지 않고,
// 기존 25개 창은 그대로 제 자리에서 돈다. 이관은 2단계(ContextRegister 10개)와
// 3단계(직접 Begin 15개)가 맡는다.
#define EDITOR_WINDOW_LIST(X) \
    /* 아직 없음 — M4 2단계가 채운다 */

namespace editor
{
    /// 부팅 때 한 번 부른다(2단계가 EditorMain 초기화에 잇는다).
    inline void register_editor_windows()
    {
#define EDITOR_WINDOW_REGISTER_ONE(T) ::editor::register_declarer_windows<T>(#T);
        EDITOR_WINDOW_LIST(EDITOR_WINDOW_REGISTER_ONE)
#undef EDITOR_WINDOW_REGISTER_ONE
    }
}
