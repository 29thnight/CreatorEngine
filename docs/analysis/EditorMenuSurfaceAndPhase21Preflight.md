# 메뉴 선언·실행 경계와 PHASE 21 현재 점검

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 등록 경로를 2026-10-15에 대조했다. 이 개정에서는 빌드·실행·화면 검증을 하지 않았다. 과거 결과와 현재 구현의 존재를 구분한다.

## 옛 사전 조사에서 달라진 것

이전의 “메뉴 registry 없음”, “Window 메뉴 없음”, “chrome 관측 명령 없음”은 현재 상태가 아니다. 메뉴는 typed 선언과 명시 등록, 창은 stable ID registry와 host로 연결되어 있다. 현재 전체 방향은 [워크스페이스 정본](../plans/EditorWorkspaceRedesignPlan.md#phase21-current-status)에 둔다. 과거 API 98개·메뉴 19개 같은 분모를 새 기능의 누락률로 재사용하지 않는다. API 제품 경계는 [공통 편집 표면](CommandSurfaceImplementation.md)과 함께 읽는다.

## 선언과 링크

`EditorMenuSurface.h`는 상단 root를 File/Edit/Settings/Tools/Window/Help의 닫힌 enum으로 정의한다. popup host는 Hierarchy, Content Browser folder/asset, Inspector component, Behavior Tree node, Animator node다. root 뒤의 하위 경로만 문자열이다. Scene View의 오른쪽 버튼은 카메라 조작과 충돌하므로 현재 popup host에 Scene View는 없다.

선언자는 `static consteval for_editor()`에서 `menu_set(...)`을 반환한다. 자유/정적 함수의 서명이 전역, 선택 엔티티 또는 host별 popup context를 결정한다. 다른 host의 context를 붙이는 오류는 타입 검사 대상이다. action ID는 라벨이 아니라 함수 이름에서 추출하고 `__FUNCSIG__` 표기 가정을 canary assertion으로 검사한다. `.shortcut()`은 표시 힌트이며 키 입력을 등록하는 API가 아니다.

`RegisterEditorMenuManual.h`의 include와 `EDITOR_MENU_LIST`가 실제 링크와 등록을 보장한다. 현재 중앙 목록은 `editor_core_menus`를 포함한다. 이미 등록된 선언자 안의 항목 추가와 새 선언자의 중앙 목록 추가는 다른 작업이다. StaticLibrary의 미참조 TU에 self-registration만 두면 링커가 버릴 수 있다. 감사의 기대 선언자도 같은 목록에서 파생하며 빈 집합을 성공으로 보지 않아야 한다.

## 실행과 대상 수명

registry는 global/selection/popup 항목을 문맥 타입별로 보유한다. popup context에는 원시 Entity 포인터 대신 generation이 포함된 identity나 소유 경로를 담는다. 메뉴는 Presentation에서 그려지지만 Scene mutation은 게임 스레드의 공통 편집/명령 큐로 넘겨 그 시점의 대상을 다시 확인한다. “선택이 있었으므로 실행까지 살아 있다”는 가정은 금지한다.

enabled/confirm/shortcut 같은 선언 정보, 실제 callback, host의 draw 연결을 모두 보아야 기능이 도달 가능하다고 판정할 수 있다. `editor.menu`, `editor.windows`, `editor.selftest`와 감사는 등록·draw 위치·본문 결속을 관측하는 표면이다. 메뉴 하나가 있다고 CLI에 같은 이름이 자동 생성되거나 모든 명령이 메뉴로 노출되지는 않는다.

창의 open 상태는 registry가 소유한다. UI에서 여는 것과 CLI의 queued open/close/focus는 같은 stable ID에 도달하며 이름 번역·라벨 변경으로 다른 창을 만들지 않는다. Profiler의 외부 process launch는 명시 요청이고 workspace 복원이나 rendering_debug preset의 자동 부작용으로 만들지 않는다.

## 현재 남은 요구

상태바 Cmd는 입력 UI만 있고 Enter로 명령을 실행하지 않는다. Revision은 Lore 연결이 없어 비활성이다. C# 메뉴 attribute, 사용자 shortcut rebinding, 동적 플러그인 메뉴 수명, 모든 CLI 기능의 GUI 동등성은 현재 registry만으로 완료되지 않는다. 필요하면 action ID, 권한/실행 문맥, 취소·오류 보고와 unload 수명을 먼저 설계해야 한다.

PHASE 21 수용은 registry 존재 확인, 실제 조작, 저장/복원, UI frame 관측, Release 비용과 입력 소유권으로 나누어 판단한다. 이 문서는 옛 사전 조사 수치를 보존하는 보고서가 아니라 현재 배선과 남은 확장 경계를 설명하는 호환 안내다.

## 구현 근거

- [Editor/EditorMenu/EditorMenuSurface.h](../../Editor/EditorMenu/EditorMenuSurface.h)
- [Editor/EditorMenu/EditorMenuSchema.h](../../Editor/EditorMenu/EditorMenuSchema.h)
- [Editor/EditorMenu/EditorMenuRegistry.h](../../Editor/EditorMenu/EditorMenuRegistry.h)
- [Editor/EditorMenu/EditorMenuDraw.cpp](../../Editor/EditorMenu/EditorMenuDraw.cpp)
- [Editor/EditorMenu/RegisterEditorMenuManual.h](../../Editor/EditorMenu/RegisterEditorMenuManual.h)
- [Editor/EditorMenu/Menus/EditorCoreMenus.h](../../Editor/EditorMenu/Menus/EditorCoreMenus.h)
- [Editor/EditorWindow/EditorWindowRegistry.h](../../Editor/EditorWindow/EditorWindowRegistry.h)
- [Editor/EngineGUIWindow/MenuBarWindow.cpp](../../Editor/EngineGUIWindow/MenuBarWindow.cpp)
- [Editor/EngineEntry/Commands/CoreCommands.cpp](../../Editor/EngineEntry/Commands/CoreCommands.cpp)
