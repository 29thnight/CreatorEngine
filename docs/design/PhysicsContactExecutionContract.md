# ContactStream 구독·게시·소비 계약

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 호출부. 이 문서는 현재 구현을 설명하며, 이번 문서 갱신에서 빌드·테스트·제품 실행을 수행하지 않았다.

## 등록 표면

게임 스크립트는 `OnBeginSimulation`에서 `Physics.ObserveContacts(owner, selfRole, otherRole, phases, capacity, grouping)`를 호출한다. 현재 실행 중인 subscriber는 `ScriptRegistry`가 이 훅을 호출하는 동안 설정한 `ContactRouter.Current`에서 얻는다. 임의 프레임이나 background Task에서 구독하면 `InvalidOperationException`이다.

기본 phase는 Begin, capacity는 256, grouping은 ShapePairs다. owner가 유효해야 하고 self/other role은 빈 GUID가 아니어야 한다. phase는 Begin/Persist/End의 nonzero 조합, capacity는 1~65536, grouping은 정의된 enum이어야 한다. 구독은 subscriber의 `SimulationScope`에 묶이며 scope 취소 시 Dispose된다.

저장 role이 없는 shape만 `Physics.BindContactRole(body, shapeId, role)`로 임시 역할을 부여할 수 있다. 이 호출도 OnBeginSimulation 전용이며 shape가 실제 body에 있어야 한다. 저장된 role은 스크립트 binding보다 우선하며 이미 저작된 역할을 덮어쓰려 하면 거부한다. 역할 binding은 충돌 레이어를 바꾸지 않는다.

## 네이티브에서 관리 코드까지

1. PhysicsScene은 body/shape/role과 tick을 가진 값 이벤트를 snapshot에 게시한다. contact manifold는 최대 16점이며 실제 required count를 별도로 보관한다
2. ScenePhysicsSimulation이 SDK handle을 stable component binding으로 변환한다. 교체·제거 직전 actor의 마지막 이벤트를 위해 retired 매핑을 보관한다
3. `Scene::FixedUpdate`는 소유 Entity의 script handle, component ID, shape ID, body slot/generation, role을 `ClrHost::QueueContact`에 넣는다. 관리 경계는 manifold 전체가 아니라 count/required count와 첫 point를 전달한다
4. `RuntimeFrame`의 post-physics 경계가 등록 변경을 먼저 flush하고 physics events를 관리 라우터에 전달한다
5. C# `PostPhysics`에서 `stream.Read()`를 소비한다. PostPhysics 완료 뒤 `ContactRouter.EndFrame`이 그 프레임의 touched buffer를 비운다

물리 catch-up 이벤트에는 각각 원래 tick ID가 남는다. 한 렌더 프레임에 여러 물리 tick 이벤트가 함께 도착할 수 있다. 이전 프레임 span을 다음 프레임에 저장해 읽거나, 네이티브 callback 안에서 CLR gameplay를 직접 호출하는 계약이 아니다.

## 라우팅과 정체성

라우팅 키는 owner, self role, other role, phase다. 역할을 매 접촉마다 전체 스크립트에서 검색하지 않는다. 두 endpoint 각각의 관점으로 `SelfEntity`/`OtherEntity`를 만든다. 이벤트는 component ID와 shape ID뿐 아니라 body slot/generation도 포함한다. 동일 Entity에 물리 컴포넌트가 재생성되거나 바디가 여러 번 교체돼도 오래된 쌍을 새 쌍으로 합치지 않기 위한 정보다.

solid 접촉은 shape-pair 단위다. `SensorTargets`는 sensor에만 target Entity 집계를 적용한다. 한 target의 여러 shape pair 중 첫 진입에 Begin, 마지막 이탈에 End를 만들고 tick당 target의 중복 관측을 억제한다. pair 상태는 프레임 buffer clear 이후에도 유지된다. 늦게 구독해 최초 관측이 Persist인 sensor도 관측 상태에 따라 Begin으로 승격할 수 있다. Persist-only 구독은 최초 overlap을 잃지 않도록 처리한다.

## 읽기·overflow·수명

`Read`는 owner thread에서만 호출한다. dispose된 stream은 `ObjectDisposedException`, 용량 초과 stream은 `InvalidOperationException`이다. `Overflowed`와 `RequiredCapacity`를 확인할 수 있지만 부분 결과로 gameplay를 계속하도록 허용하지 않는다. 출력 buffer뿐 아니라 sensor pair 추적도 정해진 capacity를 넘으면 overflow가 된다.

subscriber가 disabled이거나 Begin 단계가 성공하지 않은 동안은 전달하지 않는다. suspend는 기존 pair/target 관측 상태도 비운다. Dispose는 라우팅 목록, subscriber 목록, touched 목록에서 제거하고 buffer와 관측 상태를 비운다. scope cleanup은 native object가 사라지기 전에 진행해야 한다.

`Contact.Point`는 현재 ABI가 제공하는 대표 위치다. 모든 normal·impulse·manifold를 제공하는 API로 확장해서 해석하지 않는다. role GUID가 양쪽 모두 해결되지 않으면 그 접촉은 role stream에 전달되지 않는다. layer 충돌 허용과 stream 구독은 각각 필요하다.

## 실패가 발생하는 위치

shape 검증 실패는 저작/교체 경계, SDK 오류는 물리 step, binding 누락은 Scene publication, capacity 초과는 stream read에서 드러난다. Scene은 필수 publication 실패를 simulation failure로 보고한다. 관리 훅의 예외는 ScriptRegistry의 해당 인스턴스 격리 규칙을 따른다. 모든 실패를 “충돌 없음”으로 변환하면 실제 누락과 정상 no-contact를 구별할 수 없다.

## 유지할 회귀 시나리오

single/compound solid, sensor 초기 overlap, 늦은 구독, Begin/Persist/End 조합, SensorTargets의 다중 pair, disable/enable, subscriber 제거, 동일 owner 재생성, fetch 전 연속 교체, scene reload, hierarchy DDOL, role 변경, overflow를 분리한다. Editor/Development/Shipping 도구가 있더라도 동일 commit·ABI·실행 환경의 산출물을 확인해야 한다. 이 갱신에서는 제품 회귀를 실행하거나 과거 PASS를 현재 결과로 승계하지 않았다.

기본 SDK와 쿼리는 [PhysicsAPIContract](PhysicsAPIContract.md), 미래 효과 실행기와의 구분은 [SimulationEffectContractPlan](../plans/SimulationEffectContractPlan.md)을 참조한다. ContactStream은 현재 C#/Scope 계약이며 PrSM 도입 증거가 아니다.

## 구현 근거

- [ScriptCore/Physics.cs](../../ScriptCore/Physics.cs)
- [ScriptCore/ContactStream.cs](../../ScriptCore/ContactStream.cs)
- [ScriptCore/ScriptRegistry.cs](../../ScriptCore/ScriptRegistry.cs)
- [Engine/Physics/PhysicsStep.h](../../Engine/Physics/PhysicsStep.h)
- [Engine/SceneRuntime/ScenePhysicsSimulation.cpp](../../Engine/SceneRuntime/ScenePhysicsSimulation.cpp)
- [Engine/SceneRuntime/Scene.cpp](../../Engine/SceneRuntime/Scene.cpp)
- [Engine/SceneRuntime/RuntimeFrame.cpp](../../Engine/SceneRuntime/RuntimeFrame.cpp)
- [Engine/SceneRuntime/ClrHost.cpp](../../Engine/SceneRuntime/ClrHost.cpp)
