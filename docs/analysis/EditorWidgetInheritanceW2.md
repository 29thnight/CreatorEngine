# 공통 위젯의 책임과 관측 계약

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 등록 경로를 2026-10-15에 대조했다. 이 개정에서는 빌드·실행·화면 검증을 하지 않았다. 과거 결과와 현재 구현의 존재를 구분한다.

## 현재 공통 구현

EditorSectionHeader는 enabled checkbox, 접기 영역과 menu 버튼이 겹치지 않게 배치하고 ItemAdd/ButtonBehavior로 hover/held/press를 읽는다. 배경을 먼저 그린 뒤 아직 계산되지 않은 hover를 사용하는 방식이 아니다. label은 ID의 씨앗이며 click-on-press 동작을 유지한다.

EditorPropertyRow는 label/value 열과 표준 숫자 편집을 결합한다. EditorAxisField3는 X/Y/Z badge와 개별 float 입력을 가지며 stacked 여부를 공통 layout 결과로 받는다. EditorInspectorPanel은 컴포넌트 header의 icon/toggle/buttons/경계선을 맡는다. EditorModeButton은 활성 모드를 아래 marker로, hover/held를 배경으로 표현해 선택 상태와 누름 상태가 사라지지 않게 한다.

이 위젯은 Scene mutation·Undo·파일 저장의 소유자가 아니다. caller가 stable ID 범위를 제공하고 실제 값 변경 경로를 선택한다. HorizontalLayout과 node editor pin용 widgets는 별도 용도다. 과거 ToggleUI 후보의 존재를 현재 UI 구현으로 설명하지 않는다.

## 세 관측 경계

- EditorNavContract: item 신고, nav cursor draw, frame 관측으로 실제 NavId가 custom item에 도달했는지와 cursor가 그려졌는지를 맞댄다. 키 주입은 다음 NewFrame의 event queue에 들어간다
- EditorClipContract: 텍스트 폭과 실제 영역, clipping, 원문 tooltip 및 clip-stack 균형을 기록한다. truncation을 한 번도 자극하지 않은 overflow 0은 유효한 음성 검사가 아니다
- EditorStateContract: hover/active/focus/nav/disabled/mixed/error의 선언, notApplicable 사유와 observed bit를 비교한다. 선언됐지만 관찰되지 않은 상태는 미자극 또는 죽은 분기일 수 있다

게시/신고는 ImGui presentation 스레드, read/reset/request는 게임 스레드의 mailbox/snapshot 경계다. CLI가 ImGui 내부 상태를 직접 변경하지 않는다.

## 한계와 유지보수

개별 texture validator가 생겼다고 모든 공통 위젯의 error 상태가 구현된 것은 아니다. Inspector의 다중 선택과 실제 multi-object mixed 편집도 같지 않다. notApplicable은 소비자 맥락을 근거로 유지해야 하며 과거 주석의 “오류 원천 없음”을 전체 Editor의 현재 사실로 확대하지 않는다.

`verify-editor-keyboard-nav.ps1`, `verify-editor-widget-clipping.ps1`, `verify-editor-state-matrix.ps1`이 각각 다른 축을 검사한다. 본 개정은 코드 및 관측 API를 대조했으며 실행하지 않았다. header 변경 시 조기 return의 style/clip/ID stack 복원, active edit, disabled input, tooltip과 실제 nav 도달을 함께 확인한다.

## 구현 근거

- [Editor/ImGuiHelper/EditorSectionHeader.cpp](../../Editor/ImGuiHelper/EditorSectionHeader.cpp)
- [Editor/ImGuiHelper/EditorPropertyRow.cpp](../../Editor/ImGuiHelper/EditorPropertyRow.cpp)
- [Editor/ImGuiHelper/EditorAxisField3.cpp](../../Editor/ImGuiHelper/EditorAxisField3.cpp)
- [Editor/ImGuiHelper/EditorInspectorPanel.cpp](../../Editor/ImGuiHelper/EditorInspectorPanel.cpp)
- [Editor/ImGuiHelper/EditorModeButton.cpp](../../Editor/ImGuiHelper/EditorModeButton.cpp)
- [Editor/ImGuiHelper/EditorNavContract.h](../../Editor/ImGuiHelper/EditorNavContract.h)
- [Editor/ImGuiHelper/EditorClipContract.h](../../Editor/ImGuiHelper/EditorClipContract.h)
- [Editor/ImGuiHelper/EditorStateContract.h](../../Editor/ImGuiHelper/EditorStateContract.h)
- [Tools/regression/verify-editor-state-matrix.ps1](../../Tools/regression/verify-editor-state-matrix.ps1)
