# 본문·탭·컴포넌트·메뉴의 밀도 계약

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 등록 경로를 2026-10-15에 대조했다. 이 개정에서는 빌드·실행·화면 검증을 하지 않았다. 과거 결과와 현재 구현의 존재를 구분한다.

## 하나의 padding으로 해결하지 않는 이유

EditorThemeTokens는 BodyFontSize=16, ControlHeight/RowHeight=20, TabHeight=24, ItemGapY=4 logical px을 사용한다. 본문 control을 조밀하게 만들면서 같은 FramePadding을 쓰는 dock tab까지 줄이면 창 손잡이와 본문이 함께 변한다. TabStyleScope는 NewFrame의 floating dock tab, dockspace 및 window Begin 경계에서 tab padding을 적용하고 본문으로 돌아갈 때 복원한다.

컴포넌트 header는 EditorInspectorPanel의 별도 metrics를 따른다. enabled checkbox는 제목 텍스트와 맞는 16 logical px 슬롯을 갖고 toggle 클릭과 fold 영역을 분리한다. Add Component는 문구 실제 폭/padding과 남은 영역으로 배치해 좁으면 줄바꿈하며 고정 physical width를 신뢰하지 않는다.

## 메뉴와 제목 행

MenuGapX/Y=10/6, MenuPaddingX/Y=8/6은 popup의 읽기 간격이다. 제목 행의 조밀한 ItemSpacing을 열린 popup에 그대로 상속하지 않는다. 현재 title row는 최소 45 physical px이며 과거의 20 logical px 제목 높이를 density 문서에서 재도입해서는 안 된다. [제목표시줄 계약](EditorTitleBarValidation.md)을 따른다.

## 검증 기준

실제 control 높이, 글자 clipping, checkbox와 fold의 클릭 분리, tab grab area, menu popup 간격을 DPI/user scale 조합별로 확인한다. theme token 값의 일치만으로 interaction 수용을 선언하지 않는다. 과거 2026-09-12 캡처와 build log는 그때의 밀도 수정 증거이며 현재 host/Inspector 변경의 통과를 뜻하지 않는다. 기능 mutation은 표준 ImGui와 기존 component operation을 사용하고 새 input framework를 도입하지 않는다.

## 구현 근거

- [Editor/EngineGUIWindow/EditorTheme.h](../../Editor/EngineGUIWindow/EditorTheme.h)
- [Editor/EngineGUIWindow/EditorTheme.cpp](../../Editor/EngineGUIWindow/EditorTheme.cpp)
- [Editor/ImGuiHelper/EditorInspectorPanel.cpp](../../Editor/ImGuiHelper/EditorInspectorPanel.cpp)
- [Editor/EngineGUIWindow/InspectorWindow.cpp](../../Editor/EngineGUIWindow/InspectorWindow.cpp)
- [Editor/EngineGUIWindow/MenuBarWindow.cpp](../../Editor/EngineGUIWindow/MenuBarWindow.cpp)
- [Editor/EngineGUIWindow/EditorRenderer.cpp](../../Editor/EngineGUIWindow/EditorRenderer.cpp)
