# Scene·Entity·Component 소유권과 이전 계약

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 호출부. 이 문서는 현재 구현을 설명하며, 이번 문서 갱신에서 빌드·테스트·제품 실행을 수행하지 않았다.

## 현재 소유 모델

SceneManager가 한 GameThread GCCE domain을 보유하고 Scene root를 관리한다. Scene과 Entity는 gc::managed이며 Scene의 entity 목록과 Entity의 component 목록은 gc::trace_ref로 연결한다. 외부에서 root를 넘겨받는 AdoptScene/RetireScene과 frame boundary collection이 수명 경계다. 옛 unique_ptr 컴포넌트 단계의 주석을 현재 소유 구조로 다시 서술하지 않는다.

asset generation의 own::shared_owner와 scene graph의 tracing domain은 다른 체계다. raw Component* 조회값은 빌린 관찰자이며 외부가 delete하거나 shared ownership을 새로 만들지 않는다. ComponentFactory는 domain을 명시해 생성하며 reflection descriptor의 generic factory는 managed 객체에 사용하지 않는다.

## 정체성과 hierarchy

EntityHandle은 sceneId/index/generation으로 Scene slot을 가리킨다. C# ObjectHandle의 전역 ScriptObjectRegistry는 별도 세대 수명이다. DDOL detach/attach 중 Scene slot이 없어도 script identity를 보존해야 하므로 두 핸들을 하나로 합치지 않는다. instance ID, component UUID, asset FileGuid도 서로 다른 용도다.

parent/root/children은 HierarchyStore가 canonical이다. Entity의 authoring adapter는 디스크 hierarchy를 이 store에 연결하며 별도 반영 필드를 정본으로 유지하지 않는다. Reparent는 cycle·stale·cross-scene·root 조건을 검증한다. transform 실행 투영은 [TransformUpdatePlan](TransformUpdatePlan.md)을 따른다.

## 생명주기

ScenePhase는 Detached→Attached→InScene→Simulating이다. component 훅은 Initialized→AddedToScene→BeginSimulation, 역방향 EndSimulation→RemovingFromScene→Uninitializing이다. enable/disable은 별도 축이다. native lifecycle override mask는 남아 있지만 매 프레임 virtual Update/LateUpdate/FixedUpdate 목록은 제거되어 subsystem 전용 배열이 tick한다.

장기 등록은 최초 initialize만이 아니라 scene enter/exit에 연결한다. DDOL 객체는 재초기화 없이 다른 Scene에 붙으므로 Animator/AI/UI 같은 등록을 initialize에만 두면 새 Scene에서 빠진다. destroy mark와 실제 구조 제거를 분리하고 EndFrame의 cleanup 경계를 지킨다.

## Scene 전환과 비동기 load

SceneManager의 pending structure change는 Host가 안전 경계에서 적용한다. Editor의 simulation session은 authoring snapshot/restore, Player는 runtime 정책이다. Play 요청과 IsPlayCommitted는 다르며 snapshot/start가 실패하면 committed 실행을 시작하지 않는다.

scene load는 owner가 admission하고 worker가 parsing/assets를 준비한 뒤 owner boundary에서 Entity를 만든다. future는 결과 채널이며 owner가 pump 전에 blocking get하면 진행을 막을 수 있다. Ready는 Scene construction 완료이지 요청한 activation이 이미 확정됐다는 뜻이 아니다. canceled/stale/failed result와 abandoned future의 성공 객체 소유권도 manager가 정리한다.

## 프리팹·직렬화

Prefab authoring document와 instance override는 저장 표현이고 live hierarchy/managed graph와 다르다. spatial archetype은 생성 시 필요한 Transform/RectTransform 조합을 정한다. 기존 저장 field가 있다고 임의 컴포넌트 조합을 추가하지 않는다. component UUID를 통해 리네임 호환을 찾고 unknown type은 진단한다.

## 이전 계획에서 남는 의미

EnTT/Flecs 도입이나 옛 Entity/Object rename은 현재 구조의 필수 단계가 아니다. network identity·fixed managed tick·effect runtime은 별도 미래 계획이다. 이 archive는 현재 canonical ownership와 lifecycle을 찾기 위한 연결 문서이며 과거 회귀 개수·성능 결과를 새로 검증했다고 주장하지 않는다.

## 구현 근거

- [Engine/SceneRuntime/SceneManager.h](../../../Engine/SceneRuntime/SceneManager.h)
- [Engine/SceneRuntime/SceneManager.cpp](../../../Engine/SceneRuntime/SceneManager.cpp)
- [Engine/SceneRuntime/Scene.h](../../../Engine/SceneRuntime/Scene.h)
- [Engine/SceneRuntime/Entity.h](../../../Engine/SceneRuntime/Entity.h)
- [Engine/SceneRuntime/SceneGC.h](../../../Engine/SceneRuntime/SceneGC.h)
- [Engine/SceneRuntime/EntityHandle.h](../../../Engine/SceneRuntime/EntityHandle.h)
- [Engine/SceneRuntime/ScriptObjectRegistry.h](../../../Engine/SceneRuntime/ScriptObjectRegistry.h)
- [Engine/SceneRuntime/ComponentFactory.cpp](../../../Engine/SceneRuntime/ComponentFactory.cpp)
- [Engine/SceneRuntime/ScenePhase.h](../../../Engine/SceneRuntime/ScenePhase.h)
- [Engine/SceneRuntime/Prefab.h](../../../Engine/SceneRuntime/Prefab.h)
