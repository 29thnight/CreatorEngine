# 행동 트리 관리 실행과 native AI 경계

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 호출부. 이 문서는 현재 구현을 설명하며, 이번 문서 갱신에서 빌드·테스트·제품 실행을 수행하지 않았다.

## 현재 실행 소유자

native BehaviorTreeComponent는 authoring graph/blackboard asset 참조와 managed tree instance ID·revision을 보관한다. 실제 BTNode 인스턴스와 Running 상태는 C# BehaviorTreeRegistry가 소유한다. `BTGraphBuilder`가 전달된 평면 node description으로 트리를 조립하고 BTNodeFactory가 타입을 찾는다. 실패는 음수 instance ID와 진단으로 드러난다.

## worker에서 하는 일

AIManager::CaptureAIUpdate는 owner thread에서 entity handle, registration ID, tree instance/revision, bounds, enabled 상태와 camera frustum을 값 snapshot으로 만든다. worker는 CullAIUpdate만 수행하고 Scene/CLR/컴포넌트 포인터를 직접 순회하지 않는다. ApplyAIUpdate는 owner에서 handle·registration·tree revision을 재검증한 후 결과를 반영한다.

Scene::LateUpdate가 완료 결과를 적용하고 ClrHost의 AI tick batch는 RuntimeFrame post 경계에서 관리 영역으로 전달된다. BT 내부 recursive tick은 관리 영역에서 끝난다. node마다 native/managed crossing을 만들지 않는다. 늦은 결과는 EndFrame의 join으로 회수하고 다음 소비 경계에 사용하며 미소비 batch를 무한 누적하지 않는다.

## 수명과 블랙보드

OnAddedToScene/OnRemovingFromScene은 AI 등록을 관리한다. graph rebuild/clear는 tree revision을 올려 이전 visibility 결과가 새 tree ID에 적용되지 않게 한다. OnUninitializing은 managed tree를 파괴한다. scene unload에서 전체 registry Clear를 호출하면 DDOL tree까지 잃으므로 owner 생존 기반 orphan sweep과 assembly reload의 Clear를 구별한다.

blackboard 초기화는 bool/int/float/vector2/vector3와 문자열을 지원한다. Entity/Transform 저작 값은 초기에는 이름/경로 문자열로 남으며 필요할 때 resolve한다. Vector4/None은 대응 저장소가 없어 경고를 남긴다. native enum에 값이 있다는 이유만으로 관리 지원을 가정하지 않는다.

## 실패·미래 요구

없는 instance ID나 죽은 owner는 tick을 건너뛰고 skipped count에 반영한다. tree Tick 자체의 사용자 node 오류 정책은 managed dispatch 경계와 함께 검토해야 한다. random selector는 전체 simulation 결정성 보장이 아니다. network rollback/effect recording은 향후 계약이다.

이전 B 단계 계획은 위 구현으로 치환한다. 새 node를 추가할 때 generator 등록, graph validation, blackboard 초기값, 재로드·DDOL·orphan을 확인한다. 이번에는 tree를 실행하거나 기존 tick count를 새로 측정하지 않았다.

## 구현 근거

- [Engine/SceneRuntime/BehaviorTreeComponent.h](../../../Engine/SceneRuntime/BehaviorTreeComponent.h)
- [Engine/SceneRuntime/BehaviorTreeComponent.cpp](../../../Engine/SceneRuntime/BehaviorTreeComponent.cpp)
- [Engine/SceneRuntime/AIManager.h](../../../Engine/SceneRuntime/AIManager.h)
- [Engine/SceneRuntime/AIManager.cpp](../../../Engine/SceneRuntime/AIManager.cpp)
- [Engine/SceneRuntime/Scene.cpp](../../../Engine/SceneRuntime/Scene.cpp)
- [ScriptCore/BehaviorTreeRegistry.cs](../../../ScriptCore/BehaviorTreeRegistry.cs)
- [ScriptCore/BTGraphBuilder.cs](../../../ScriptCore/BTGraphBuilder.cs)
- [ScriptCore/BehaviorTree.cs](../../../ScriptCore/BehaviorTree.cs)
