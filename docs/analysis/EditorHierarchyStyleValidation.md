# Hierarchy 행 표시와 평탄화 경계

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 등록 경로를 2026-10-15에 대조했다. 이 개정에서는 빌드·실행·화면 검증을 하지 않았다. 과거 결과와 현재 구현의 존재를 구분한다.

## 현재 표시 구조

Hierarchy는 Scene의 저작 계층을 `HierarchyFlatView`의 선형 행으로 투영하고 `ImGuiListClipper`로 보이는 구간만 그린다. 평탄화 결과는 entity index, depth, bandIndex, kind, 자식 존재 여부, 펼침 상태 같은 파생 정보다. 이름·부모·자식·활성·잠금·선택의 원본 저장소를 복제하지 않는다. 행 순서는 엔티티 저장 슬롯 번호가 아니라 저작 계층의 children 순서를 따른다. 순서 변경·Undo 계약은 [Hierarchy 순서](HierarchyOrder20261008.md)에 둔다.

`HierarchyStyleScope`는 이 창 안의 색과 간격을 적용하고 종료 시 되돌린다. 패널 배경은 RGB(50,53,52), 일반/교대 행은 0x171818/0x1E201F, 선택 행은 0x494D4C이며 선택 상태의 hover는 0x535856이다. 엔티티 라벨은 파란 계열 0x7095D0, 선택 라벨은 0xA8C9FA다. 선택 배경 자체를 밝은 파랑으로 칠하지 않는다. Scene 및 DDOL 그룹은 entity 띠 번호에 포함되지 않아 그룹 행 삽입으로 아래 stripe가 밀리지 않는다.

## 소유권과 갱신

평탄화 캐시는 Hierarchy 창이 소유한다. 계층 revision, entity/DDOL 개수와 검색 활성 상태가 재구축 판단에 쓰인다. 이름 변경에 대응하는 별도 revision이 없으므로 검색 중에는 매 프레임 재구축한다. 따라서 검색 중 대형 씬 비용을 비검색 대기 수치로 설명하면 안 된다. 펼침 override는 UI 파생 상태이고 토글 결과는 다음 프레임의 행 목록에 반영된다. 선택 위치를 드러낼 때는 `ExpandAncestors`로 조상을 펼친다.

각 행을 그릴 때 엔티티를 다시 조회한다. 트리 깊이는 행 데이터로 계산하고 clipper 밖의 보이지 않는 노드마다 `TreePush`를 유지하지 않는다. Scene/DDOL 그룹의 펼침과 일반 엔티티 펼침을 혼동하지 않는다. 잠금된 엔티티나 자손을 포함한 편집 제약은 공통 편집 계층이 재확인한다. 비싼 자손 잠금 검사는 실제 drag 시작 후보 행에서 수행하며 모든 보이는 행의 상시 검사로 되돌리지 않는다.

Presentation 중 읽은 Scene/Entity 포인터는 그 프레임의 보호 구간에서만 유효하다. `queue_hierarchy_edit`는 Scene root의 generation-checked handle과 소유된 작업 값을 큐에 담고 게임 스레드에서 현재 Scene을 다시 확인한다. 그 사이 씬이 교체되면 `scene.stale`로 거부한다. 시각 상태만 바꾸는 작업과 실제 Scene mutation을 같은 직접 포인터 콜백으로 처리하지 않는다.

## 검증 범위

`verify-hierarchy-flatten-contract.ps1`은 평탄화·클리핑·순서 관련 소스 계약, `verify-hierarchy-convention.ps1`은 계층 접근 관례, `verify-hierarchy-read-boundary.ps1`은 읽기 경계를 검사한다. 소스 검사는 각 DPI에서의 문자 정렬·선택 대비·스크롤 사용성을 입증하지 않는다. 실제 UI 수용은 깊은 트리, 이름 검색 중 rename, DDOL 혼합, 잠금 drag, 선택 reveal, 접기/펼치기와 화면 밖 행의 비용을 함께 확인해야 한다.

현재 배치와 토큰 정본은 [워크스페이스 설계](../plans/EditorWorkspaceRedesignPlan.md)다. 과거의 특정 FPS·p95 또는 체크 개수는 현재 코드의 재측정 결과로 옮기지 않는다.

## 구현 근거

- [Editor/EngineGUIWindow/HierarchyWindow.cpp](../../Editor/EngineGUIWindow/HierarchyWindow.cpp)
- [Editor/EngineGUIWindow/HierarchyFlatten.h](../../Editor/EngineGUIWindow/HierarchyFlatten.h)
- [Editor/EngineGUIWindow/HierarchyFlatten.cpp](../../Editor/EngineGUIWindow/HierarchyFlatten.cpp)
- [Editor/EngineGUIWindow/EditorTheme.h](../../Editor/EngineGUIWindow/EditorTheme.h)
- [Editor/EngineGUIWindow/EditorTheme.cpp](../../Editor/EngineGUIWindow/EditorTheme.cpp)
- [Tools/regression/verify-hierarchy-flatten-contract.ps1](../../Tools/regression/verify-hierarchy-flatten-contract.ps1)
- [Tools/regression/verify-hierarchy-read-boundary.ps1](../../Tools/regression/verify-hierarchy-read-boundary.ps1)
