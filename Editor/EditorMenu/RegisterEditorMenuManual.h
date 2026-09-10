#pragma once
// 에디터 메뉴 선언자 중앙 목록 (PHASE 21 M0 · 계획서 부록 A.5).
//
// `RegisterReflectManual.h`와 **같은 모양이고 같은 이유**다. 선언자를 새로 만들면
// 헤더 include 한 줄과 EDITOR_MENU_LIST 항목 한 줄을 같이 늘린다. 누락은 기동
// 게이트가 잡는다(M2).
//
// ★ 목록이 include까지 맡는 것이 핵심이다. Editor는 StaticLibrary이고
//   `/WHOLEARCHIVE`가 없어서, 아무도 참조하지 않는 TU는 링커가 끌어오지 않는다.
//   그래서 "선언만 해 두면 알아서 등록된다"가 성립하지 않는다. 여기서 include하면
//   선언자의 for_editor()가 **링크되는 TU 안으로** 들어오고, 열거가 등록을 돌린다.
//   목록 하나가 두 일을 한다.
//
// ★ 이미 목록에 있는 선언자에 동작을 하나 더 다는 것은 **그 선언자 헤더 한 줄**이다.
//   이 파일을 건드릴 일이 없다. 그것이 이 배선의 요점이다.

#include "EditorMenuRegistry.h"

// 선언자 헤더는 여기에 include한다.
// 예: #include "Menus/AssetMenus.h"

// 목록 항목은 여기에 더한다. 예:
//     X(asset_menus) \
//     X(scene_menus) \
//
// M0은 배선만 세운다 — 실제 동작은 M1이 그리기 배선과 함께 태운다(부록 A.7:
// 기존 상단 19항목·팝업 68항목의 이관은 W3 이후 별도 정리다).
#define EDITOR_MENU_LIST(X) \
    /* 아직 없음 — M1이 채운다 */

namespace editor
{
    /// 부팅 때 한 번 부른다(M1이 EditorMain 초기화에 잇는다).
    inline void register_editor_menus()
    {
#define EDITOR_MENU_REGISTER_ONE(T) ::editor::register_declarer<T>(#T);
        EDITOR_MENU_LIST(EDITOR_MENU_REGISTER_ONE)
#undef EDITOR_MENU_REGISTER_ONE
    }
}
