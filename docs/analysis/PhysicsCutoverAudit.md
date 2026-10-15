# 물리 전환 감사와 레거시 경계

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 호출부. 이 문서는 현재 구현을 설명하며, 이번 문서 갱신에서 빌드·테스트·제품 실행을 수행하지 않았다.

## 이 문서의 역할

이 경로는 과거 R0 전환 보고서 링크를 유지한다. 현재 실행 규약은 [물리 API](../design/PhysicsAPIContract.md)와 [컴포넌트 설계](../design/PhysicsComponentDesign.md)가 정본이다. 과거 날짜의 “완료”를 현재 checkout의 실행 결과로 읽지 않는다.

## 현재 확인할 소유자

`Scene`이 `ScenePhysicsSimulation`을 가지며, 그 세션이 `unique_ptr<PhysicsScene>`을 소유한다. Host가 `Runtime::TickSimulationFrame`을 호출하고 SceneManager::Physics→Scene::FixedUpdate로 진입한다. shape 정의는 PhysicsBodyComponent 한 곳에서 완결된다. public SDK pointer나 전역 옛 물리 manager를 새로운 소비자에 노출하지 않는다.

worker 제출은 `PhysicsSceneChannel`, 결과는 immutable tick snapshot으로 전달한다. character 이동과 query batch도 같은 Scene 세션에 귀속된다. 스크립트 접촉은 ClrHost의 값 큐를 거쳐 ContactStream으로 도착한다. 이름만 새 API로 바꾸고 기존 콜백에서 gameplay를 호출하는 방식은 현재 경계를 우회한다.

## 감사 도구와 해석

`audit-physics-cutover.ps1`과 `physics-legacy-files.json`은 옛 파일/기호의 잔존을 확인하는 source gate다. `verify-physics-sdk-boundary.ps1`은 SDK 격리를 별도로 확인한다. 소스 감사가 통과해도 SDK 바이너리 링크 폐포, 실제 actor 수명, Player packaging, 렌더 결과를 증명하지 않는다.

마이그레이션은 legacy component/file 검출과 새 authoring schema 검증을 함께 수행해야 한다. 오래된 문서의 파일 경로를 런타임 파일로 복원하거나 compatibility singleton을 만드는 작업은 이 감사의 목적이 아니다. alias가 필요한 경우 저장 데이터 reader와 실행 소유자를 구분하고 정해진 migration 정책으로 처리한다.

## 재검증 경계

실제 확인은 current binary에서 Play/Stop, scene reload, DDOL, component removal, shared cooked geometry, CCT, query, ContactStream을 수행해야 한다. 이번에는 source와 연결부만 검토했으며 audit script도 실행하지 않았다.

## 구현 근거

- [Engine/SceneRuntime/RuntimeFrame.cpp](../../Engine/SceneRuntime/RuntimeFrame.cpp)
- [Engine/SceneRuntime/Scene.cpp](../../Engine/SceneRuntime/Scene.cpp)
- [Engine/SceneRuntime/ScenePhysicsSimulation.h](../../Engine/SceneRuntime/ScenePhysicsSimulation.h)
- [Tools/regression/audit-physics-cutover.ps1](../../Tools/regression/audit-physics-cutover.ps1)
- [Tools/regression/physics-legacy-files.json](../../Tools/regression/physics-legacy-files.json)
- [Tools/regression/verify-physics-sdk-boundary.ps1](../../Tools/regression/verify-physics-sdk-boundary.ps1)
