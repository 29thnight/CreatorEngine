# Inspector 스타일과 값 편집의 분리

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 등록 경로를 2026-10-15에 대조했다. 이 개정에서는 빌드·실행·화면 검증을 하지 않았다. 과거 결과와 현재 구현의 존재를 구분한다.

## 현재 스타일 소유자

InspectorThemeTokens는 패널 배경, Field/Label/AxisBackground, 10 logical px 수평 padding, row/axis gap을 정의한다. InspectorStyleScope가 본문 style을 적용·복원하므로 조기 return 뒤 다른 창에 색이나 spacing이 새지 않아야 한다. window 선언의 padding은 host에서 배율을 한 번 적용하고 본문이 다시 DPI를 곱하지 않는다.

색을 바꾸는 것과 값 배치를 바꾸는 것은 다르다. 값 배치는 [반응형 Inspector](EditorInspectorLayoutW2I.md), 컴포넌트 헤더는 EditorInspectorPanel, 축 입력은 EditorAxisField3가 담당한다. 축 badge는 문자 X/Y/Z를 함께 보여주며 의미 색(Error/Positive/Primary)과 별도의 색을 사용한다. 색만으로 축이나 오류 상태를 전달하지 않는다.

## 숫자와 좁은 폭

compact_property_number는 표시용 정규화다. 단위·지수·비숫자를 보존해야 하며 저장값이나 입력 정밀도를 줄이는 함수로 쓰지 않는다. scalar 값 최소폭과 세 축이 나눠 쓰는 최소폭은 다르게 측정한다. 긴 라벨은 clip하고 tooltip으로 원문을 돌려주며 값 영역을 0폭으로 만들지 않는다.

기존 normal/narrow/transform-inline screenshot은 당시 visual acceptance 자료다. 현재 style token을 확인했지만 과거 pixel 위치를 최신 theme의 golden으로 간주하지 않는다. 회귀에서는 정상/좁은 폭, active input, disabled/locked, tooltip, 뒤따르는 다른 창의 style 복구를 함께 확인해야 한다. appearance 일치가 serialization·Undo·physics 값 변경의 정확성을 보장하지 않는다.

## 구현 근거

- [Editor/EngineGUIWindow/EditorTheme.h](../../Editor/EngineGUIWindow/EditorTheme.h)
- [Editor/EngineGUIWindow/EditorTheme.cpp](../../Editor/EngineGUIWindow/EditorTheme.cpp)
- [Editor/ImGuiHelper/EditorInspectorPanel.cpp](../../Editor/ImGuiHelper/EditorInspectorPanel.cpp)
- [Editor/ImGuiHelper/EditorAxisField3.cpp](../../Editor/ImGuiHelper/EditorAxisField3.cpp)
- [Editor/ImGuiHelper/EditorPropertyRow.cpp](../../Editor/ImGuiHelper/EditorPropertyRow.cpp)
- [Editor/EngineGUIWindow/InspectorWindow.cpp](../../Editor/EngineGUIWindow/InspectorWindow.cpp)
