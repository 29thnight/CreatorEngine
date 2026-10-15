# Transform 계층·dirty resolve·렌더 게시 계약

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 호출부. 이 문서는 현재 구현을 설명하며, 이번 문서 갱신에서 빌드·테스트·제품 실행을 수행하지 않았다.

## 데이터 정본

Scene의 HierarchyStore가 slot별 parent/root/children/occupied 관계를 보관한다. Entity에 별도 계층 사본을 만들지 않는다. Transform의 local position/rotation/scale은 저작 값이고 Scene의 TransformStore는 local/world matrix, world scale/quaternion/position, dirty/worldChanged 파생 배열을 가진다. slot allocation/release와 store grow/reset은 함께 움직인다.

EntityHandle은 sceneId/index/generation이다. compiled execution graph의 dense 위치는 실행 투영이며 핸들이나 디스크 identity가 아니다. Scene 수명과 topology가 바뀌면 이를 다시 만든다. 모든 Entity가 spatial Transform을 가진다고 가정하지 않는다. UI는 별도 rect 투영을 사용한다.

## 쓰기 경계

Transform setter와 local matrix 변경은 `PublishLocalWrite(handle,reason)`로 합류한다. Scene은 dirty queue와 epoch를 통해 변경을 수집하고 physics transform과 render proxy의 필요한 invalidation도 연결한다. 단순 bool 하나를 세워 모든 derived domain을 같은 상태로 처리하지 않는다.

`ApplyWorldWriteBatch`는 physics와 같은 world-space 결과를 계층 규약에 맞춰 local/store로 반영한다. Animator worker는 자기 pose/socket staging을 쓰며 join 후 owner가 Scene에 게시한다. queue가 동시 제출을 받는다고 Scene의 모든 mutation을 worker에서 해도 되는 것은 아니다.

worldChanged는 local dirty와 별개다. 즉시 read가 ancestor chain을 먼저 resolve하면 local dirty가 이미 내려갈 수 있다. 그때 아직 순회하지 않은 자식에게 변경을 전파하려면 실제 world write를 표시하는 worldChanged가 필요하다.

## topology 변경

`Scene::Reparent`는 invalid/stale/cross-scene/root/self/cycle/corrupt hierarchy를 구분해 거부한다. 정상 hierarchy mutation이 cycle을 막아도 CompileExecutionGraphs가 손상된 입력을 다시 fail-close한다. parent-before-child 순서는 cycle 검증의 결과이지 검증의 대체물이 아니다.

creation/destruction/reparent는 topology version을 갱신한다. bulk build는 반복 compile을 피하는 창을 제공하지만 완료 시 mutation을 게시해야 한다. 표시용 HierarchyStore::Revision과 transform execution projection의 수명도 목적에 따라 구분한다.

## resolve와 즉시 읽기

AllUpdateWorldMatrix는 sync point 이름을 받는 진입점이며 내부 derived-state 조정은 spatial과 UI를 구분한다. `ResolveSpatialTransformsSparse`는 compiled topology와 dirty roots로 영향을 받은 영역을 resolve한다. dirty queue를 꺼낸 이후 새 publish가 들어와도 새 epoch가 사라지지 않게 처리한다. 기존 dirty traversal toggle은 진단/대조 경로이며 일반 소비자가 각각 순회를 선택하는 API가 아니다.

`EnsureResolved(EntityHandle)`는 world getter가 즉시 최신 값을 요구할 때 ancestor chain을 앞당겨 계산하는 경계다. 이 read가 이후 형제 subtree의 propagation을 없애면 안 된다. animated bone read는 준비된 pose materialization을 확인하고 같은 resolve 계약으로 이어진다.

UI layout은 screen rect·canvas scale·rect dirty를 별도로 확인한다. 공간 Transform이 정적이라고 UI 해상도 변경을 skip하면 안 된다. 반대로 UI만 바뀌었다고 spatial tree를 전부 재계산할 필요도 없다.

## 프레임과 렌더

물리 전에 FixedUpdate sync가 있고 Scene::Update의 앞/뒤에도 PreUpdate/LateUpdate sync가 있다. animation pose는 GameLogic의 animation event에서 계산되어 owner에 게시된다. Scene::LateUpdate의 UpdateRenderData가 렌더 proxy로 값을 전달한다. physics interpolation은 렌더 읽기용 pose이며 simulation Transform 정본과 구분한다.

proxy dirty mask는 Transform뿐 아니라 payload/material/visibility 같은 변경 원인을 다룬다. “이동한 slot 목록”만으로 모든 render 갱신을 대체할 수 없다. sealed frame 이후 renderer가 live Scene pointer를 따라 최신 값을 읽는 방식은 frame 수명 계약을 어긴다.

## 성능과 잔여 확장

sparse movement, full movement, 깊은 hierarchy, UI-only, topology churn, 즉시 world read, animated bone, physics bulk writer를 따로 측정해야 한다. compiled order만 생겼다고 data locality가 개선됐다는 결론은 나오지 않는다. X9의 병렬 depth band/static partition/world TRS 같은 선택 확장은 필요성과 비용을 다시 측정한 후 결정한다.

이 문서화는 현재 source 경계만 확인했다. 과거 µs·속도 배수는 재실행 값이 아니며 topology compile 비용·GPU frame 결과도 새로 측정하지 않았다. [실행 그래프 이전 안내](TransformExecutionGraphPlan.md)와 [파이프라인 보기](TransformUpdatePipeline.html)는 이 문서를 보조한다.

## 구현 근거

- [Engine/SceneRuntime/HierarchyStore.h](../../../Engine/SceneRuntime/HierarchyStore.h)
- [Engine/SceneRuntime/TransformStore.h](../../../Engine/SceneRuntime/TransformStore.h)
- [Engine/SceneRuntime/Transform.cpp](../../../Engine/SceneRuntime/Transform.cpp)
- [Engine/SceneRuntime/Scene.h](../../../Engine/SceneRuntime/Scene.h)
- [Engine/SceneRuntime/Scene.cpp](../../../Engine/SceneRuntime/Scene.cpp)
- [Engine/SceneRuntime/AnimationScheduler.cpp](../../../Engine/SceneRuntime/AnimationScheduler.cpp)
- [Engine/SceneRuntime/RenderProxyDirty.h](../../../Engine/SceneRuntime/RenderProxyDirty.h)
