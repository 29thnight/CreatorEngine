# Hierarchy 형제 순서·드롭·Undo 계약

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 등록 경로를 2026-10-15에 대조했다. 이 개정에서는 빌드·실행·화면 검증을 하지 않았다. 과거 결과와 현재 구현의 존재를 구분한다.

## 순서의 정본

Scene의 children 목록이 형제 순서를 소유한다. Entity 슬롯은 재사용 가능한 저장 구현이며 화면 정렬 키가 아니다. `HierarchyFlatten`은 root와 DDOL 그룹의 발견에도 계층 순회를 사용한다. Play/Stop으로 객체가 재생성되어 슬롯 배치가 바뀌어도 저작 snapshot에서 복원된 계층 순서를 보여 주어야 한다.

`Scene::ReorderChildren(parent, orderedHandles)`는 살아 있는 parent와 모든 자식을 확인한다. 입력 개수가 현재 자식 수와 같고 현재 자식을 중복 없이 정확히 한 번씩 포함해야 한다. 자식 추가·누락·다른 부모의 엔티티·stale handle을 순서 변경으로 수용하지 않는다. 실제 순서가 바뀌면 HierarchyStore를 갱신하고 topology mutation을 게시하며 UI layout을 dirty로 만든다. 유효하지만 같은 순서인 요청은 불필요한 topology 변경을 만들지 않는다.

## GUI와 CLI의 공통 작업

행 높이의 위 25%는 대상 형제 앞, 아래 25%는 뒤에 놓는 영역이다. 삽입 위치는 선으로 표시한다. 가운데 50%는 그 엔티티의 자식으로 이동한다. 빈 영역은 Scene root로 parenting한다. Scene root 행 자체는 형제 삽입 anchor가 아니다. `SCENE_OBJECT` drag payload의 index를 delivery 시 다시 조회하고 현재 handle로 바꿔 공통 작업에 넘긴다.

`EditorObjectOperations::MoveRelative(target,sibling,after)`는 게임 스레드가 아니면 mutation queue로 위임한다. 두 객체가 살아 있고 서로 다르며 같은 Scene에 있어야 한다. anchor 잠금, 대상/자손 및 목적지 잠금, Scene root 이동, 자기 자신으로의 이동, 조상 아래로의 순환 이동을 거부한다. `ParentAt`은 목적지 children에서 대상을 제외한 뒤 anchor 앞/뒤에 넣는다. anchor가 더 이상 목적지 자식이 아니면 `object.order.stale`이다.

`object.order <target> <sibling> <before|after>`가 같은 작업을 호출하고 `scene.order <parent|->`가 children 순서와 실제 평탄화된 visible roots를 노출한다. CLI 결과는 작업 배선과 순서 규칙을 검사할 수 있지만 마우스가 예상 영역을 맞히는지는 별도다.

## Undo와 Play 경계

한 번의 reparent와 배치는 하나의 custom change command다. 작업은 target, 이전/새 parent, 각 parent의 이전/이후 children 순서를 `Meta::EntityReference`로 보유하고 Undo/Redo 때 다시 resolve한다. raw Entity 포인터나 당시 slot 번호만 저장하지 않는다. 순서 복원 시 현재 살아 있는 기록 엔티티를 우선 복원하고 기록 이후의 나머지 자식은 뒤에 유지한 후 `ReorderChildren`의 완전한 입력으로 제출한다.

RectTransform이 있고 부모가 바뀌면 처음 적용할 때 world 위치를 유지하는 parent 변환을 수행하고 직렬화한 before/after 상태를 Undo/Redo에 사용한다. 이를 모든 종류의 Transform에 대한 무조건 world-preserving API라고 일반화하지 않는다. 부모나 target이 사라진 뒤의 복원은 reference 해석 및 공통 Undo 수명 규약의 영향을 받는다.

Play 진입 성공 시 편집 Undo 이력을 비우는 정책은 별개다. Stop은 저작 계층 snapshot을 복원하지만 진입 전 Undo stack까지 복구하지 않는다. Stop 이후 새 편집의 Undo/Redo를 별도로 확인해야 한다.

## 검증 자료의 사용

`verify-hierarchy-order.ps1`은 같은 부모/서로 다른 부모 이동, Undo/Redo, 두 번의 Play/Stop과 runtime mutation, visible roots 일치, cycle/self 거부를 검사하는 제품 실행 시나리오다. 기존 문서의 21 snapshots 및 Debug/Release 성공 문구는 2026-10-08 실행 기록이며 이번 개정의 결과가 아니다. 당시 `Build/Verification/HierarchyOrder/{Debug,Release}/results.jsonl` 등은 이 checkout의 보증된 첨부물이 아니다.

남은 UI 수용 요구는 실제 pointer drop의 경계, 빠른 Scene 교체 중 drag, 잠금 변경, 깊은 트리의 스크롤과 삽입선 위치다. 현재 선언·공통 연산의 존재만으로 그 사용성까지 통과 처리하지 않는다.

## 구현 근거

- [Editor/EngineGUIWindow/HierarchyFlatten.cpp](../../Editor/EngineGUIWindow/HierarchyFlatten.cpp)
- [Editor/EngineGUIWindow/HierarchyWindow.cpp](../../Editor/EngineGUIWindow/HierarchyWindow.cpp)
- [Editor/EngineEntry/EditorObjectOperations.cpp](../../Editor/EngineEntry/EditorObjectOperations.cpp)
- [Editor/EngineEntry/Commands/SceneObjectCommands.cpp](../../Editor/EngineEntry/Commands/SceneObjectCommands.cpp)
- [Editor/EngineEntry/EditorPlayModeController.cpp](../../Editor/EngineEntry/EditorPlayModeController.cpp)
- [Engine/SceneRuntime/Scene.cpp](../../Engine/SceneRuntime/Scene.cpp)
- [Tools/regression/verify-hierarchy-order.ps1](../../Tools/regression/verify-hierarchy-order.ps1)
