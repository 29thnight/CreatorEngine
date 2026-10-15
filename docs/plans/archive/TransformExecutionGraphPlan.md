# Transform 실행 그래프 이전 안내

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 호출부. 이 문서는 현재 구현을 설명하며, 이번 문서 갱신에서 빌드·테스트·제품 실행을 수행하지 않았다.

## 통합 위치

이전 Sparse Compiled Transform Execution Graph 제안은 [Transform 갱신 계약](TransformUpdatePlan.md)에 통합했다. 현재 Scene에는 CompileExecutionGraphs, sparse resolver, EnsureResolved, local/world batch publication이 존재한다. 이 문서를 별도의 미착수 executor 설계로 해석하지 않는다.

## 실행 graph가 나타내는 것

HierarchyStore의 canonical 관계를 topology version에 맞춰 spatial/UI 실행 투영으로 바꾼다. node의 실행 위치와 EntityHandle은 별개다. reparent는 cycle·cross-scene을 검증하고 compiler도 손상을 다시 검사한다. topology가 변하지 않은 frame은 기존 projection을 재사용할 수 있다.

dirty root와 epoch는 sparse resolve의 입력이다. parent world 변경은 child에 전파되고 EnsureResolved가 chain을 먼저 계산한 뒤에도 worldChanged가 남아 다른 branch의 변경을 잃지 않는다. 단순 preorder 배열만 만드는 것과 이 mutation/invalidaton 계약을 구현하는 것은 다르다.

## 사용자가 변경할 때의 경계

새 writer는 local setter/PublishLocalWrite 또는 owner의 ApplyWorldWriteBatch에 합류해야 한다. 새로운 Scene traversal을 subsystem마다 추가하지 않는다. UI screen resize는 별도 dirty domain이고 animation worker는 scene storage를 직접 갱신하지 않는다.

성능 확장인 depth-band 병렬화나 static partition은 현재 source 구조만으로 효과를 보장할 수 없다. topology churn과 sparse/full movement를 같은 fixture에서 재측정한 뒤 결정한다. 이번 문서화에서는 실행 그래프 테스트·벤치마크를 수행하지 않았다.

## 구현 근거

- [Engine/SceneRuntime/Scene.cpp](../../../Engine/SceneRuntime/Scene.cpp)
- [Engine/SceneRuntime/Scene.h](../../../Engine/SceneRuntime/Scene.h)
- [Engine/SceneRuntime/HierarchyStore.h](../../../Engine/SceneRuntime/HierarchyStore.h)
- [Engine/SceneRuntime/TransformStore.h](../../../Engine/SceneRuntime/TransformStore.h)
