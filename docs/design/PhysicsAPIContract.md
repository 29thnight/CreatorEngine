# 물리 런타임 API와 실행 계약

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 호출부. 이 문서는 현재 구현을 설명하며, 이번 문서 갱신에서 빌드·테스트·제품 실행을 수행하지 않았다.

## 진입점과 계층

물리 SDK의 공개 경계는 `ce::physics::PhysicsScene`이다. 구현 TU가 SDK를 숨기며 SceneRuntime은 `ScenePhysicsSimulation`으로 씬 저작 멤버십과 실제 물리 세션을 연결한다. `Scene::FixedUpdate`가 레이어·변환을 반영하고 세션을 진행한 뒤 접촉과 변경 포즈를 소비한다. Editor와 Player의 공통 진입점은 `Runtime::TickSimulationFrame`이다.

`PhysicsScene`은 복사·이동 불가 소유 스레드 객체이며 `create`는 `result<unique_ptr<PhysicsScene>>`를 돌려준다. 엔진 Scene이 실행 객체를 소유한다. 편집 모드의 컴포넌트 등록은 SDK Scene을 만들지 않는다. 런타임 조작은 실행 중인 활성 바디와 올바른 owner/idle 구간을 요구한다.

## 정체성·수명·오류

- `body_handle`과 `character_handle`은 scene ID, slot, generation을 가진 값이다. 포인터도 저장 자산 ID도 아니다. 파괴·교체 후 이전 세대는 재사용하지 않는다
- `shape_id`는 바디 안의 형상 정체성이다. 형상 정의는 nonzero 고유 ID를 요구한다. 접촉 role UUID와 충돌 layer는 다른 축이다
- `result<T>`는 `std::expected<T,error>`다. 실패를 빈 성공으로 바꾸지 말고 `invalid_argument`, `unsupported_geometry`, `stale_handle`, `wrong_scene`, `wrong_phase`, `backend_initialization`, `cooking_failed`, `capacity_exceeded`, `out_of_memory`, `late_command`, `duplicate_command`를 구분한다
- `error.message`는 정적 문자열 뷰이며 SDK 코드와 source location을 함께 갖는다. 메모리 부족 보고 자체가 새 문자열 할당을 요구하지 않는다
- `replace_body`는 완전한 정의를 교체하는 트랜잭션이다. 성공하면 이전 핸들이 무효가 되므로 장기 캐시는 새 결과를 받아야 한다

## 고정 스텝과 실행 창

`ScenePhysicsSimulation::fixed_seconds`는 1/60초, 프레임당 `max_catchup_ticks`는 4다. `Advance`가 frame delta를 누산하고 수용 예산을 넘는 시간을 `DroppedSeconds`에 기록한다. 바디의 previous/current pose와 남은 누산 비율은 렌더 보간에 쓰인다. 이 고정 스텝이 C# 전체를 고정 틱으로 돌리는 것은 아니다. 관리 `PrePhysics`와 `PostPhysics`는 공통 프레임 함수에서 각 한 번 호출된다.

저수준 순서는 `begin_step(seconds)`와 `finish_step()`로 나뉜다. 공개 상태는 idle, commit, query_read, simulating, publish, failed, closing을 구분한다. fetch 실패는 terminal 상태이며 해당 Scene의 파괴·재생성이 복구 경계다. `Scene::FixedUpdate`의 필수 실패는 `ReportSimulationFailure`에 전달되고, 구조 안전 경계에서 teardown을 요청한다. 스텝 중 즉시 Scene을 해제하지 않는다.

## 명령과 작업자

작업자는 빌린 Scene 포인터 대신 `PhysicsSceneChannel` 값을 받는다. 채널은 요청·게시 저장소만 소유하며 SDK Scene을 소유하지 않는다. Scene이 닫힌 후 살아남은 채널의 제출은 `wrong_phase`로 실패한다. 직접 `submit_command`를 다른 스레드에서 부르는 사용자는 객체 소멸 전에 생산자를 중단해야 한다.

명령은 `(scene,tick,producer,sequence)` stamp와 소유 payload다. producer 0은 main, 1은 required AI, 2~255는 명시적 우선순위다. sequence는 0이 아니며 producer의 확정 이력에서 증가해야 한다. 정렬 키는 tick→producer→sequence다. `body_definition`은 shape span을 복사한 저장소를 소유하고 `view()`에서 다시 연결한다. 호출자의 임시 span을 큐에 보관하지 않는다.

`tick_snapshot`은 값 배열과 접촉점을 소유한다. 명령 성공/실패/취소, 생성·퇴역 핸들, 변경 바디·캐릭터, required/dropped 계수를 읽을 수 있다. 기본 scene config는 command/event/task 용량 1024와 snapshot buffer 용량 8이다. reader가 snapshot을 오래 붙들면 게시 버퍼 예산에 포함된다. 용량은 무제한 큐를 뜻하지 않는다.

## 형상과 바디

primitive는 box, sphere, capsule이며 cooked 형상은 convex, triangle mesh, heightfield다. `PhysicsShapeDefinition`을 SDK 값으로 바꾸기 전에 전체 목록을 먼저 검증한다. 양수 유한 스케일, 단위 quaternion, 마찰·반발 범위, 형상 ID 중복, role UUID, geometry asset UUID/revision을 확인한다.

sphere/capsule은 균일 body scale을 요구한다. 비균일 스케일과 회전된 형상의 조합은 shear를 근사하지 않고 거부한다. mesh/heightfield는 static solid body에 제한된다. moving body의 모든 형상이 sensor인 정의도 거부된다. cooked blob은 SDK 버전과 artifact codec 계약을 함께 고려해야 하며 원본 메시 파일이 런타임 SDK 데이터와 동일하지 않다.

## 쿼리

네이티브는 all-hit raycast, sweep, overlap과 `query_batch`를 제공한다. no-hit은 성공이다. 출력 span 용량이 부족하면 필요한 용량을 알리며 결과를 완전한 집합처럼 취급하면 안 된다. overflow의 부분집합은 가장 가까운 hit를 보장하지 않고 정렬 계약도 없다.

batch는 owner/idle read window에서 수행하고 입력·출력은 반환까지 빌린다. hit 출력 범위는 서로 겹치면 안 된다. batch 전체 검증 실패는 출력을 쓰지 않으며 개별 요청 실패가 뒤 요청을 취소하지 않는다.

C# 공개 표면은 더 좁다. `Physics.Raycast`, `OverlapSphere`, `QueryBatch`는 살아 있는 Entity를 scene anchor로 사용한다. 단일 쿼리 최대 256 hit, batch 최대 64 요청·4096 hit이며 `Written`, `RequiredCapacity`, `Truncated`, `PhysicsError`를 확인해야 한다. ray 방향은 단위 벡터다. 공개 네이티브 sweep이 존재한다고 C# sweep API도 있다고 추정하지 않는다.

## GPU·계측 한계

기본 실행 요청은 CPU다. `prefer_gpu`는 요청일 뿐이며 status의 actual backend, gpu dynamics/broadphase, fallback reason을 확인해야 한다. unsupported build, context unavailable, scene rejected를 구분한다. worker 수 0은 하드웨어 스레드 수에서 4를 뺀 값과 1 중 큰 값을 선택한다.

`CE_PHYSICS_RESOURCE_PROBE=1`의 통계는 외부 소유권 장부다. SDK 내부 모든 allocation의 수가 아니며 생산자를 join한 뒤에만 최종 balance를 판정한다. 코드에 카운터와 회귀 도구가 있다는 사실은 현재 플랫폼의 처리량·p99·무누수 통과 증거가 아니다.

컴포넌트 저작은 [PhysicsComponentDesign](PhysicsComponentDesign.md), 접촉 수신은 [PhysicsContactExecutionContract](PhysicsContactExecutionContract.md), 통합 확인 순서는 [PhysicsRedesignPlan](../plans/PhysicsRedesignPlan.md)을 따른다.

## 구현 근거

- [Engine/Physics/PhysicsScene.h](../../Engine/Physics/PhysicsScene.h)
- [Engine/Physics/PhysicsTypes.h](../../Engine/Physics/PhysicsTypes.h)
- [Engine/Physics/PhysicsStep.h](../../Engine/Physics/PhysicsStep.h)
- [Engine/SceneRuntime/ScenePhysicsSimulation.h](../../Engine/SceneRuntime/ScenePhysicsSimulation.h)
- [Engine/SceneRuntime/ScenePhysicsSimulation.cpp](../../Engine/SceneRuntime/ScenePhysicsSimulation.cpp)
- [Engine/SceneRuntime/PhysicsShapeDefinition.h](../../Engine/SceneRuntime/PhysicsShapeDefinition.h)
- [Engine/SceneRuntime/Scene.cpp](../../Engine/SceneRuntime/Scene.cpp)
- [ScriptCore/Physics.cs](../../ScriptCore/Physics.cs)
