# 엔티티 헤더: 이름·태그·Physics Layer 배선

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 등록 경로를 2026-10-15에 대조했다. 이 개정에서는 빌드·실행·화면 검증을 하지 않았다. 과거 결과와 현재 구현의 존재를 구분한다.

## 헤더의 현재 구성

Inspector 헤더는 icon 열과 properties 열을 가진 표로 구성한다. 왼쪽 preset 이미지는 40 logical px이며 오른쪽에는 enabled, 이름과 tag 버튼, Static 및 Physics Layer 같은 속성을 배치한다. 좁은 폭에서는 Static을 다음 줄로 보내고 이름 필드 뒤의 tag 버튼 폭을 예약해 긴 이름이 아이콘 아래로 흐르지 않게 한다.

이름은 InputText의 Enter commit에서 `EditorObjectOperations::Rename`으로 전달된다. enabled는 `SetEntityEnabled`, icon은 `SetIcon`으로 보낸다. tag picker는 TagManager에서 기존 membership을 제거하고 새 tag membership을 추가한다. 동일하게 보이는 control이라도 서로 다른 mutation 경로를 쓰므로 모든 필드에 같은 Undo 정책이 있다고 일괄 설명하지 않는다.

Physics Layer 표시는 프로젝트 layer snapshot에서 현재 ID를 찾는다. 단순 문자열 라벨의 변경으로 충돌 설정이 복제되는 것이 아니며 실제 layer 변경의 사전조건/consumer는 EditorObjectOperations와 project layer 서비스가 담당한다. 이전 “Layer” 이름을 새 데이터 필드의 생성으로 해석하지 않는다.

## 이 문서가 보존하는 회귀 의도

이전 polish는 tag와 이름 겹침, + 버튼 padding, 제목 로고 뒤의 불필요한 gap, browser splitter의 두꺼운 선 같은 시각 문제를 다뤘다. 현재 치수 정본은 EditorTheme와 각 draw 함수다. screenshot에 있던 좌표를 하드코딩해 복구하지 않는다.

검증할 것은 긴 UTF-8 이름 입력/취소/Enter, 선택된 tag 표시, 없는 layer의 처리, 잠금 시 mutation 거부, 좁은 폭에서 버튼 클릭 영역의 분리다. style screenshot만으로 membership·Undo·저장 결과를 입증하지 않는다. 선택·잠금·preset의 영속 상태는 [엔티티 탐색 계약](EditorEntityNavigationValidation.md)에 둔다.

## 구현 근거

- [Editor/EngineGUIWindow/InspectorWindow.cpp](../../Editor/EngineGUIWindow/InspectorWindow.cpp)
- [Editor/EngineGUIWindow/EditorTheme.h](../../Editor/EngineGUIWindow/EditorTheme.h)
- [Editor/EngineEntry/EditorObjectOperations.cpp](../../Editor/EngineEntry/EditorObjectOperations.cpp)
- [Editor/EngineGUIWindow/HierarchyWindow.cpp](../../Editor/EngineGUIWindow/HierarchyWindow.cpp)
- [Editor/EngineGUIWindow/ContentsBrowserWindow.cpp](../../Editor/EngineGUIWindow/ContentsBrowserWindow.cpp)
