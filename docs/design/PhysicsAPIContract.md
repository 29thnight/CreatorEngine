# PhysX API 계약 — PHASE 19

작성: 2026-10-01 · 실행 계획: [PhysicsRedesignPlan](../plans/PhysicsRedesignPlan.md)
상태: 계약 초안과 현재 SDK/빌드/언어 검증. 제품 baseline/corpus 증거 한계는 아래에 명시한다.

## P1 구현 증거와 현재 경계

공개 PhysicsScene/PhysicsTypes는 PhysX SDK 헤더를 노출하지 않는다. create/begin_step/finish_step은
expected를 반환하고 scene은 복사·이동 불가다. 생성 스레드가 begin/finish/destruction을 소유하며
다른 스레드의 begin/finish는 wrong_phase 오류, 다른 스레드의 destruction은 terminate다.
status는 살아 있는 scene의 원자적 진단 snapshot이며 SDK 오류 수는 공유 SDK 범위의 누적 값이다.
body/character 핸들의 타입 분리만 구현했고 slot/generation registry 검증은 P2에 남는다.

CPU dispatcher는 기본 max(1, logical processors-4), 최대 256 workers와 bounded queue를 사용한다.
queue 포화 시 SDK task를 호출 스레드에서 즉시 실행하고 inline_tasks로 기록한다. 각 task는 run/release를
정확히 한 번 수행한다. scene은 fetch/drain 후 scene→dispatcher→CUDA context→공유 SDK 순서로 해제한다.
공유 SDK의 마지막 해제가 끝나기 전에 새 Foundation을 생성하지 않도록 registry를 동기화한다.
prefer_gpu는 CUDA context 생성/유효성을 확인하고 GPU dynamics·PCM·GPU broadphase를 설정한다.
실제 생성된 scene의 flags/broadphase/context를 대조한 뒤에만 GPU backend로 표시한다.
실패 시 GPU scene/context를 해제하고 새 CPU descriptor로 재생성한다. CPU 생성도 실패하면 expected 오류다.
status는 backend, gpu_requested/gpu_unavailable, context_unavailable/scene_rejected/unsupported_build 원인과
실제 GPU dynamics/broadphase 설정을 구분한다. 기본 execution은 계속 CPU다.
테스트 배포에는 PhysXGpu_64.dll과 PhysXDevice64.dll이 모두 필요하며 기존 제품 deploy-runtime도 둘을 포함한다.
GPU 초기화 marker는 CPU scope이며 GPU solver 실행 시간 계측을 대신하지 않는다.

verify-physics-p1.ps1 -Configuration All: Debug/Release 각각 checks=402, sdk_tasks=4160, gpu_verified=true;
Shipping checks=4, sdk_tasks=51. 단계별 초기화 실패 5종과 복구, 동시 scene 생성/종료,
중복 step·잘못된 dt·다른 owner 호출·in-flight 해제를 확인했다. Shipping은 진단 소스/라이브러리 없이 링크했다.
Physics.vcxproj Debug/Release 단독 빌드도 통과했다. 제품 전체 빌드 통과 증거는 아니다.
RTX 2080 Ti/driver 595.97에서 실제 GPU 씬 초기화/10 step/in-flight 종료를 확인했다.
CUDA 생성 실패와 GPU 씬 거부를 주입하여 GPU 설정 제거·CPU step·SDK 해제를 검증했다.
GPU 본체 DLL만 배포한 첫 실행의 Device DLL 누락도 CPU 전환됐으며, 실제 GPU 검증은 둘을 배포한 후 수행했다.
GPU 바디 충돌/cook/성능 검증은 P2/M이며 이번 빈 씬 초기화 검증으로 완료를 주장하지 않는다.
Build/Obj/Phase19P1에 JSONL/log와 Debug/Release baseline.ceprof를 보존한다.
marker 실제 이벤트와 worker stream을 검증했고 종료 후 capture complete=1/unacked=0/droppedEvents=0이다.
실행 중 idle worker freeze는 미검증이며 PhysicsTick 계층·CPU tick/task 상관 메타데이터는 P3 필수 작업이다.

## P2 구현 계약과 검증 범위

PhysicsGeometry.h가 SDK 타입 없는 공개 형상/바디/쿼리 정의다. body_desc의 shapes는 생성 호출 동안
빌린 span이며 SDK actor/shape와 공유 cooked 자산은 생성 후 자체 소유한다. 외부 배열의 수명에 의존하지 않는다.
shape ID는 바디 내 nonzero/unique이며 userdata는 안정적인 별도 record에 저장한다.
바디 슬롯 저장소의 재할당은 actor/shape 식별자 주소를 바꾸지 않는다.
슬롯 발급·재사용은 intrusive free list로 O(1)이며 실패 시 무료 슬롯에 남아 재사용 가능하다.
destroy는 재사용 전 같은 handle에 멱등적이고 재사용 후 구 handle은 stale 오류다.
read/mutation/query-ignore의 stale/다른 scene 핸들도 거부하며 generation wrap 전에 슬롯을 폐기한다.

body pose/local pose는 단위 quaternion과 m 위치이며 capsule은 local +Y축이다.
box는 half extent, capsule half_height는 cap을 제외한 원통 반길이다. 질량 kg에서 solid shape만으로
자동 관성과 COM을 계산하고 sensor를 제외한다. non-static body는 최소 한 solid shape를 요구한다.
read_body는 local COM pose와 그 principal axes의 inertia를 함께 반환한다.
dynamic의 초기 속도·감쇠·중력 enabled·translation/rotation 축 잠금과 kinematic target을 지원한다.
형상/재질/필터는 생성 정의에 속하며 동적 재설정 커맨드와 이벤트는 P3에서 확장한다.

convex/triangle mesh cook은 buildGPUData를 설정하고 convex는 실제 GPU 호환 여부를 노출한다.
zero-area 검사 실패는 cooking_failed와 SDK cooking condition을 보존하며 임의의 얇은 hull로 승계하지 않는다.
복잡한 형상은 shared_ptr<const CollisionGeometry>로 공유하고 양수 scale만 지원한다.
primitive scale은 치수에 미리 적용한다. heightfield scale=(row 간격,height 단위,column 간격)이다.
triangle mesh/heightfield는 static solid 전용이며 dynamic/kinematic 및 sensor 정의는 unsupported_geometry다.
스케일/형상 교체 후 질량 재계산·재cook의 제품 저작 흐름은 B1에서 연결한다.

simulation filter는 양쪽 belongs_to/collides_with 비트 모두 일치해야 접촉/trigger pair를 허용한다.
query_enabled와 query_layers는 simulation filter와 독립이며 sensor query는 명시적으로 포함한다.
query geometry는 primitive/convex만 지원하며 mesh/heightfield 입력 sweep/overlap은 거부한다.
raycast/sweep은 단위 방향과 양수 유한 거리를 요구한다. 세 query는 SDK 기본 face 정책의 shape hit를
순서 없이 반환하며 overlap은 body/shape 신원만 유효하다. no-hit는 written=required_capacity=0 성공이다.
호출자 output span에 기록하며 작은 버퍼/빈 버퍼도 모든 hit를 세어 정확한 required_capacity를 반환한다.
truncated 결과는 가까운 hit를 보장하지 않는다. 충분한 버퍼의 결과에서 nearest를 선택할 수 있다.
동기 query는 owner idle 경계에 한정한다. 동시 query/batch 읽기 창과 immutable 결과 publication은 T2/P3 범위다.

verify-physics-p2.ps1 -Configuration All [-RequireGpu]는 Debug/Release/Shipping/ASan을 새 소스로 빌드한다.
현재 RTX 2080 Ti 실행은 Debug/Release/ASan checks=1065, Shipping checks=366, 모두 gpu_verified=true다.
CPU/GPU 실제 기본 형상·convex·mesh·heightfield 접촉, 센서/필터·감쇠·축 잠금·중력 비활성,
compound mass/COM, query identity/no-hit/overflow(70 hits), 실패 rollback·핸들/phase·공유 수명을 확인했다.
Shipping은 진단 소스/라이브러리 없이 새 cook/body/query API를 실행했다. ASan의 외부 PhysX DLL은 비계측이다.
Build/Obj/Phase19P2/{Debug,Release,Shipping,ASan}에 JSONL/log, non-Shipping baseline.ceprof를 보존한다.
종료 후 capture complete=1/unacked=0/droppedEvents=0이며 실행 중 idle-worker freeze 증거는 아니다.
제품 전체 연결/성능과 contact/trigger 이벤트 전달은 아직 완료로 표시하지 않는다.

## 1. 고정 의존과 현재 증거

- HEAD: 12f970c7ed3a4408d268479f5d5acb5f80372b8c. 작업 시작의 기존 dirty 변경은 보존한다.
- PhysX: 5.5.0, vcpkg package 5.5.0#1, manifest baseline 9e593bb18ea69cc5095e012465dcd675a822ed0d.
  SDK version header와 installed package 기록을 대조했다. 실제 triplet 트리는 vcpkg_installed/x64-windows/x64-windows.
- Mathematics: ThirdParty/Mathematics/PROVENANCE.md의 pin 1f43e080f180db1afbf6e18cb3849b758858a496.
  외부 최신 checkout이 아니라 vendored 헤더를 compile probe에 사용한다.
- 기존 Physics 프로젝트 Debug/Release x64 재빌드 통과. manifest 복원으로 reflgen 설치 후 재실행이 필요했다.
  추가로 현재 HEAD CreatorEditor Release 전체 빌드와 HTTP 저작/재생을 실행했다. Player 전체 빌드는 미검증이다.
- C++23 probe: expected monadic 연산, flat_map/set, mdspan, concepts, deducing this,
  enumerate/zip/chunk/filter/transform, variant visit, pmr, format/source_location와 Mathematics view를
  Debug/Release에서 실제 컴파일·실행했다. 실제 compiler 버전은 cpp23.jsonl에 기록한다.
- 소비자 inventory: 43개 파일, 626개 일치 줄. 이는 주석/선언 포함 lexical 참조이며 호출 횟수가 아니다.
  C# native 함수 포인터는 Cct 17 + Rigid 23 + Collider 15 + Physics query 3 = 58개.
  구 계획의 45개/82곳을 현재 확정 소비자 수로 사용하지 않는다.
- 로컬 Dynamic_CPP/Assets의 scene/prefab과 추가 Resources/TrainAsis 검색에서 물리 타입 UUID/이름을 가진
  파일은 처음에 0개였고 PhysicsDrop.creator도 없었다. 사용자 지시에 따라 HTTP CLI로 아래 기준선 씬/프리팹을 새로 저작했다.

## 2. API 소유권과 오류

PhysicsScene은 이동 불가·복사 불가이며 엔진 Scene이 소유한다. 물리 모듈은 Entity/Component를 모르고
SceneRuntime이 opaque 엔티티 식별자를 연결한다. 정적/키네마틱/동적 모두 완결된 body_desc로 생성한다.
BodyHandle/CharacterHandle은 scene identity, slot, generation을 구분하고 슬롯 generation은 최소 32비트로
시작한다. wrap 직전 슬롯은 재사용에서 제외한다. invalid sentinel은 명시하고 GameObject InstanceID를 재사용하지 않는다.
ShapeId는 바디 안에서 안정적인 ID이며 재배열된 shape 배열 index와 구분한다.

create_scene/cook_geometry/create_body/create_character/query는 [[nodiscard]] expected로 결과를 반환한다.
오류 분류는 invalid_argument, unsupported_geometry, stale_handle, wrong_scene, wrong_phase,
backend_initialization, cooking_failed, capacity_exceeded, out_of_memory다. SDK 오류 코드와 진단 위치를 보존한다.
query의 no-hit는 성공 결과다. 결과 버퍼 부족은 truncated/required_capacity를 명시하고 조용히 누락하지 않는다.
일부 생성 실패는 rollback한다. destroy는 중복 요청에 멱등적이나 stale/다른 scene 오용은 진단한다.

Foundation/SDK/dispatcher → scene → actor/controller → shape/geometry/material 참조 관계를 소유 타입으로 표현한다.
실제 종료는 반대 의존 순서로 수행하고 in-flight simulate/query/콜백을 회수한 뒤 userData와 actor를 해제한다.
컨트롤러와 controller manager는 scene보다 먼저 해제한다. 공유 geometry/material은 마지막 소비자까지 보유한다.
CPU 기본, GPU는 명시적 opt-in. CUDA 실패 시 CPU 초기화로 전환하고 선택 결과를 진단/계측에 기록한다.

## 3. 형상과 단위

primitive box/sphere/capsule, compound, convex, static triangle mesh, heightfield를 P2의 지원 목록으로 정한다.
각 조합은 설치 SDK에서 검증하고 미지원 dynamic geometry는 expected 오류로 거부한다.
형상 정의는 variant의 종류별 구조체, 복잡한 geometry는 불변 cooked 자산, ShapeInstance는 pose/material/filter/solid-sensor.
행렬 scale을 pose에 섞지 않는다. shape별 scale 지원 여부·재cook·질량/관성 재계산을 명시한다.
각 센서/필터/재질은 shape 단위이고 body aggregate 이벤트에도 ShapeId 쌍을 보존한다.

새 API는 길이 m, 시간 s, 질량 kg, 각도 rad, 속도 m/s, 가속도 m/s², 각속도 rad/s를 사용한다.
기존 씬의 1 engine unit은 자동으로 1m였다고 단정하지 않는다. 일회성 변환 도구에 length_scale과
legacy_reference_dt를 필수 metadata로 기록한다. 신규 기본 단위는 1 unit=1m로 정의한다.
기존 boxExtent는 half extent, sphere radius는 radius, capsule height와 CCT height는 실제 SDK 전달 값을 확인해 변환한다.
slopeLimit은 현재 cosine 계열 값(기본 0.7)이며 새 공개 값은 radians로 표현하고 cos 변환한다.

현재 CharacterController::Update는 currentFrameVelocity를 dt 곱 없이 PxController::move의 displacement로 넘긴다.
일반 이동·강제 이동 모두 해당한다. maxSpeed/jumpSpeed와 속도 상태는 legacy_reference_dt로 나눠 새 속도로
변환하고, velocity에 dt를 곱해 누적하던 acceleration/gravity 역시 그 상태 변환을 반영한다.
lerp 마찰은 스텝당 계수이므로 alpha(dt)=1-(1-alpha_ref)^(dt/reference_dt)로 기준점 일치를 검증한다.
reference_dt는 기록이 없으므로 임의 60Hz를 과거 골든으로 주장하지 않는다. 이동 골든 없이는 C1 완료 불가.

## 4. 스케줄링·동시성

Runtime이 고정 clock을 단독 소유한다. 신규 기본 fixed dt는 1/60s이며 설정 가능하고 scene 실행 중 변경은 거부한다.
catch-up 기본 상한은 프레임당 4틱으로 시작한다. 상한 도달 시 남은 시간을 누산기에 보존하고 지연을 계측한다.
시간을 조용히 버리지 않는다. 지속적인 지연의 제품 정책은 기준선 실측 후 조정한다.
frame과 tick은 별도 64비트 ID, scene session identity를 함께 사용한다.

1. tick N 입력 마감과 필수 AI 작업 회수.
2. 생성/파괴/설정/형상 커맨드 commit, query 구조 갱신.
3. 캐릭터 충돌 이동·동기 query·kinematic target 적용.
4. simulate(dt), SDK solver 병렬 실행.
5. 실제 물리/Transform/컴포넌트 상태를 참조하지 않는 독립 작업만 overlap.
6. fetch 완료, actor/shape 이벤트와 active pose 수집.
7. 부모 Transform 환산/dirty 전파, 이벤트 전달, 불변 snapshot과 보간 pose 발행.
8. batch query가 필요하면 별도의 읽기 창에서 실행·join 후 다음 mutation을 허용.

전용 물리 스레드는 이번에 도입하지 않는다. PhysX SDK dispatcher를 초기 기본으로 유지한다.
공용 enkiTS는 AI/독립 게임 작업에 사용하며 SDK task adapter는 검증된 경우에만 도입한다.
기본 solver workers는 max(1, logical_processors-4)로 시작하고 override를 허용한다. 이것은 성능 최적값 주장 아닌 초기 정책이다.
전체 엔진 worker 예산과 함께 계측·조정한다. 워커의 미완료 job_handle::wait는 금지한다.

AI는 tick N snapshot에서 N+1 결과를 만든다. 필수 AI는 N+1 입력 마감에서 대기해 완료시각에 따라 적용 틱이
달라지지 않게 한다. 선택적 작업은 마감 실패 시 이전 target 유지 및 late-result count를 기록하고 늦은 결과를 폐기한다.
커맨드는 scene/tick/producer/sequence를 소유하며 병합은 (tick,producer,sequence) 순으로 결정한다.
메인 producer와 AI producer 우선순위는 계약에 고정한다. force/impulse 및 frame 표현이 다르면 합산하지 않는다.
유효 phase는 idle/commit/query_read/simulating/publish/closing. phase 위반은 디버그 진단과 오류로 거부한다.
solver callback은 worker-safe 수집만 수행하며 CLR/컴포넌트 호출·재진입 mutation을 금지한다.

## 5. 생명주기와 스키마 이전

OnInitialized는 정의 검증, OnAddedToScene은 scene 연결, OnBeginSimulation은 실행 admission을 담당한다.
OnEndSimulation은 입력 중지, OnRemovingFromScene은 실행 연결 회수, OnUninitializing은 저작 참조 정리다.
실제 바디 생성/제거는 안전 commit 경계에 실행한다. 중복 축소는 멱등적이고 callbacks에는 엔티티 raw pointer를 저장하지 않는다.
DDOL은 origin 회수 완료 후 목적 scene에 새 handle을 발급한다. pose/속도/이동 상태 이전은 소유 payload로 수행한다.
scene identity 변경으로 구 handle은 즉시 무효다. pending 요청/이벤트는 목적 scene에 자동 재사용하지 않는다.

구 타입 UUID 8종과 현재 헤더 스키마는 source-corpus.json 및 pre-r0 ZIP에 보존한다.
일회성 변환은 구 Rigidbody/Collider/CCT 조합을 새 body/character 정의로 출력하며 구 runtime loader나 호환 adapter를 남기지 않는다.
실제 저장 corpus가 없어 저장/로드 호환은 현재 미검증이다. reflgen 생성 descriptor와 실제 구 제품에서 저장한
최소 물리 corpus를 추가 확보하고 hash/왕복 결과를 보존한 뒤 R0를 열어야 한다.

## 6. 프로파일러 계약과 선행 격차

현 ce::profile_scope/marker, profiler_service::register_thread/unregister_thread,
register_counter와 counter sample 경로를 재사용한다. 물리 category와 physics_worker track을 추가한다.
PhysicsTick 밑에 CommandCommit/CharacterMovement/KinematicTargets/SimulateSubmit/FetchWait,
QueryStructureUpdate/QueryBatch/ActivePoseCollect/TransformApply/EventCollect/EventDispatch/SnapshotPublish를 기록한다.
SDK dispatcher submit/실행/완료에는 PhysXTask scope와 tick/task 상관을 연결한다. 세부 solver 단계는 관찰 가능할 때만 표시한다.
워커 시간은 game FetchWait의 자식으로 집계하지 않는다. 워커 자체에서 stream을 등록·봉인·종료한다.

현 profile_event는 40B 고정이고 frame, GPU submission/view만 있으며 CPU tick/task metadata가 없다.
GPU submission 필드를 물리 ID로 재사용하지 않는다. P1~P3에서 typed CPU context/flow record와 캡처 schema를
명시적으로 확장하고 writer/reader/aggregate/UI/버전/Shipping compile-out을 함께 검증한다.
이는 기존 profiler에 API가 있다고 가정하고 생략할 수 없는 선행 작업이다.
총/활성/변경 바디, shape/character, command applied/late/overflow, query count/time/overflow,
contact/event 수, scratch 현재/최대 bytes, workers, catch-up/lag를 tick 집계로 발행한다.

## 7. 삭제 전 증거와 재현

- Tools/regression/capture-physics-p0.py: 현 소스·소비자·물리 자산 inventory와 SHA256 ZIP 보존.
- Tools/regression/verify-physics-p0.ps1 [-Configuration Debug|Release|All]: 언어와 SDK CPU reference 실행.
- Build/Obj/Phase19P0/{Debug,Release}/{cpp23,cpu}.jsonl: fresh 실행 결과.
- Physics 프로젝트 Debug/Release 빌드 로그는 같은 디렉터리의 physics-*-build.log.

CPU reference는 SDK 직접 호출이며 기존 엔진의 push/pull·CCT·콜백·필터·GPU 경로를 포함하지 않는다.
100/1000/10000 상자를 분리된 위치에서 낙하, workers=4, dt=1/60, sleep 비활성,
warmup 60 + 측정 180틱, 독립 scene 3회 반복. 평균/샘플 p99와 submit/fetch 벽시계 및 실제 바닥 pose를 검증한다.
순서는 작은 N부터 고정이며 order 효과·엔진 프레임·메모리·CPU 점유와 기존 GPU 작동은 미검증이다.
benchmark와 제품 build를 동시에 실행한 최초 결과는 탐색용이며 최종 Release 기준선은 빌드 종료 뒤 재측정한다.
최종 Release SDK 반복 평균 중앙값: 100/1000/10000 bodies = 205.817/431.98/3839.88 us.
이는 SDK CPU reference이며 제품 수치와 비교해 성능 개선을 주장하지 않는다.

### HTTP CLI 제품 기준선

Tools/regression/author-physics-p0.ps1로 현재 HEAD Release 제품에 인증된 HTTP 명령을 전송했다.
P0Floor 정적 box, P0Drop dynamic box, P0Character CCT를 저작했다.
Dynamic_CPP/Assets/Scenes/PhysicsP0Baseline.creator와 Assets/Prefabs/PhysicsP0Drop.prefab을 저장했고,
동일 바이트를 Tools/regression/fixtures/physics-p0에도 보존한다. 실제 JSON 요청/응답은
Build/Obj/Phase19P0/http/results.jsonl, 실행 파일/씬 SHA256과 관찰은 baseline.json에 있다.
저장/재로드 Transform digest 596d43edf8b07940 일치, committed PlayingPossessed 확인,
dynamic box y=5에서 약 1.015로 낙하, stop 전이 Stopped 및 이름별 Transform 복원을 검증했다.
정지는 엔티티 index 순서를 바꾸므로 raw digest가 아니라 이름별 의미 값으로 대조한다.
CCT 위치는 (4,5,0)으로 유지됐다. CCT 이동 입력·접지·점프가 정상이라는 증거로 사용하지 않는다.

제품 capture 20261001-080244.ceprof: 600 frames, droppedEvents=0, physxUpdate 599 complete scopes,
mean 2.6447ms, p50 2.5613ms, p95 3.4072ms. GPU 기반 구 wrapper와 비고정 프레임 경로의 수치다.
capture 전체는 complete=0/unacked=1(RHIThread)이다. 전체 thread/gpu 완전성은 미검증이다.
RHI producer idle 안전 지점과 pause 응답 계약 검증은 사용자 지시로 별도 profiler backlog로 이관한다. P0/R0 진입 차단 조건이 아니다.
최초 CCT 관찰은 저작된 씬의 RigidBodyComponent 누락 때문이었다. 제품 CCT 결함의 증거로 사용하지 않는다.
physics 세부 hierarchy는 새 API P1~P3에서 구현하며 구 capture에는 기존 scope만 있다.

제품 최초 실행은 stale forest.ceibl recipe로 실패했다. 실행 배치 디렉터리만 recook했으며
저장소 Resources 원본은 수정하지 않았다. RuntimeLauncher는 std::exception 내용을 출력하도록 보완했다.

**P0 상태: 완료(아래 최종 기준선과 범위 적용).** RHI freeze 보완은 별도 backlog이며 R0 소유자 철거는 실행했으며 소비자 이전은 진행 중이다.

### RenderThread 완료 대기 후 재캡처 (2026-10-01)

HTTP render.live.fence를 async operation으로 실행하고 completed/succeeded를 확인한 뒤 profile.pause/save했다.
afterFrame=2503, completedFrame=7844, waitedMs=14590.3. 새 capture는
Build/Obj/Phase19P0/http/20261001-081353.ceprof이며 600 frames, droppedEvents=0,
complete=0, unacked=1(RHIThread)이다. RenderThread 완료 대기만으로 freeze 미응답이 해소되지 않았다.
이 명령은 TickLive의 completedFrameId만 확인하며 GPU fence 완료/전체 RHI retirement를 보장하지 않는다.
원인은 RHI producer 봉인 응답 경로를 별도로 검증해야 한다. 위 이전 캡처와 성능 수치도 그대로 보존한다.

### 최종 P0 기준선 및 완료 범위 (2026-10-01)

사용자는 RHI freeze 미응답을 이번 진행에서 보류했다. 전체 capture 완전성은 여전히 미확인이며
P0 완료의 의미를 물리 계약/빌드/언어/CPU reference와 최소 제품 baseline 확보로 한정한다.
구 CCT는 RigidBodyComponent에 의존한다(OnFixedUpdate 및 PhysicsManager pose push/pull).
HTTP 저작 씬에 이를 추가하고 재실행한 결과 CCT y=5 -> 2.15, dynamic box y=5 -> 1.01658을 관찰했다.
강체 entity off/on 상태, 저장/재로드 digest, stop 이름별 Transform 복원, prefab 생성/인스턴스화,
RigidBodyComponent 제거 및 임시 entity 삭제(objects=4)를 검증했다. 모든 HTTP 결과는 succeeded였다.
최종 capture: Build/Obj/Phase19P0/http/20261001-082802.ceprof, 600 frames, droppedEvents=0,
complete=0/unacked=1(RHIThread). 이 수치로 전체 renderer/GPU 완전성을 주장하지 않는다.
수평 이동/점프/정확한 grounded flag, DDOL, mesh/terrain/ragdoll, 쿼리/콜백 조합은
M 통합 검증에 남으며 최소 baseline 통과로 이 기능들의 정상성을 주장하지 않는다.
R0에서 새 character는 기존 RigidBodyComponent 의존을 제거한다. 새 런타임 계측 계층은 P1~P3 필수다.

### P3 tick/task 계측 계약 (2026-10-01)

- 성공한 begin_step마다 scene 내부의 강한 tick_id가 1부터 증가한다. 잘못된 owner/phase/duration은
  tick을 소비하지 않는다. UINT64_MAX 이후는 capacity_exceeded이며 status.last_tick으로 조회한다.
- PhysicsTick은 begin_step부터 finish_step의 fetch/drain 완료까지의 소유 스레드 wall time이다.
  SimulateSubmit과 FetchWait는 그 자식이다. 두 호출 사이의 caller 시간도 포함하며 solver 실행 시간으로 해석하지 않는다.
  진행 중 씬 파괴도 finish_step을 거쳐 열린 스코프를 닫는다.
- CPU 이벤트의 cpu_span_context는 session=scene_id, tick=tick_id, task=SDK 작업 ID의 독립된 u64 필드다.
  task는 scene dispatcher 수명 동안 재사용하지 않는다. 0은 task 없는 소유 스레드 구간이며 초기화 작업의 tick은 0일 수 있다.
  SDK dispatcher가 ID를 소진하면 종속 작업을 버리거나 ID를 재사용할 수 없어 terminate한다.
- TaskSubmit/PhysXTask/TaskComplete는 동일한 (session,tick,task)로 연결된다. inline 실행도 같은 계약이며
  워커 스코프는 실제 작업 실행과 SDK release를 잰다. SDK 작업 종속성 DAG의 edge 자체는 아직 수집하지 않는다.
  이 CPU 시간으로 GPU solver timing을 주장하지 않는다. GPU submission/view/queue는 별도 필드로 유지한다.
- native 이벤트는 40 -> 64 bytes, producer page version은 2다. .ceprof 파일 version 2의 frame chunk는
  이벤트마다 세 u64를 추가해 38 -> 62 wire bytes이며 thread chunk version 2는 physics_worker=7을 추가한다.
  기존 track 값은 유지한다. reader는 v1/38-byte 이벤트를 읽고 CPU 상관 정보를 0으로 초기화한다.
  세 CPU ID를 가진 GPU 이벤트는 malformed로 거부한다. 메모리 예산의 byte 한도는 유지되므로 같은 예산의 보존 이벤트 수는 감소한다.
- profile_context_scope는 중첩 컨텍스트를 복원한다. scope는 시작 시 ID를 저장하여 캡처 중 잘리거나
  종료 시 TLS가 바뀌어도 원래 ID를 유지한다. Shipping은 TLS 컨텍스트와 profiler 호출을 제거한다.
- Timeline tooltip은 CPU ID를 표시한다. reader/file/요약 도구에 새 정보가 전달되며 UI translation unit 컴파일을 검증한다.
  실제 제품 UI 실행은 미검증이며 아래 수치 계측 배선을 후속 완료했다.


### P3 명령·이벤트·스냅샷 계약 (2026-10-01)

- submit_command는 SDK를 호출하지 않는 bounded 다중 생산자 입력이다. 명령은 이동 전용이며
  body_definition이 형상 배열과 cooked 자산 참조를 소유한다. 입력의 borrowed shapes span은 비운다.
  살아 있는 PhysicsScene에 submit/latest_snapshot만 다른 스레드에서 호출할 수 있다.
  Scene 파괴 전 호출자를 종료해야 하며 이미 받은 스냅샷은 Scene 없이 유지할 수 있다.
- stamp는 scene/tick/producer/sequence다. producer 0=main, 1=required AI, 2..255=명시 우선순위다.
  begin_step은 다음 tick 입력을 mutex 아래 닫고 (tick,producer,sequence) 순으로 적용한다.
  닫힌 tick은 late_command, 중복 키 또는 이미 적용한 sequence 이하는 duplicate_command다.
  future 요청은 다음 tick들에 남는다. 생산자는 적용 tick 순서에 맞춰 sequence를 증가시켜야 한다.
  실패한 명령도 sequence를 소비한다. 한 명령 실패로 나머지를 롤백하지 않고 개별 outcome을 발행한다.
- 생성/파괴/전체 정의 교체/pose/velocity/kinematic target/force를 지원한다.
  전체 정의 교체는 새 actor를 먼저 완성하며 실패하면 기존 바디가 남는다. 성공 시 구 handle은 무효이고
  outcome.created/retired로 대응을 전달한다. force/impulse/acceleration/velocity_change는 dynamic 전용이다.
- callback은 안정적인 body/shape record에서 신원을 복사하고 SDK/컴포넌트 포인터를 공개하지 않는다.
  contact begin/persist/end, sensor enter/exit와 소유 접촉점을 bounded 저장소에 수집한다.
  persist는 SDK가 보고하는 awake 접촉이며 sleeping 접촉의 매 tick 통지를 보장하지 않는다.
  삭제된 shape는 해제 전 retired record의 주소 대조로 식별한다. SDK owner는 성공한 fetch까지 유지한다.
  슬롯 재사용 뒤에도 종료 이벤트는 구 generation을 보존한다.
- endpoint는 (body,shape) 순으로 정규화하고 normal/impulse는 second에서 first 방향이다.
  이벤트는 endpoint/kind 순으로 정렬하지만 solver 결과의 비트 단위 결정성을 보장하지 않는다.
  각 이벤트는 접촉점 최대 16개와 required_contacts를 가진다. tick별 required/dropped events/contacts와
  unresolved_identities를 노출하여 버퍼 부족을 정상적인 완전 결과로 오인하지 않게 한다.
- finish_step은 fetch/drain 뒤 outcome/events/active poses를 atomic shared_ptr<const tick_snapshot>으로 발행한다.
  독자는 완성된 불변 값만 읽는다. active poses는 변경된 SDK active actor이며 전체 바디 목록이 아니다.
  idle → commit → simulating → publish → idle을 status.phase로 노출하고 owner 쿼리 창은 query_read다.
  Runtime 고정 tick 누산과 query batch overlap은 후속 B/T 작업이다.
- snapshot_capacity는 기본 8, 허용 2..256이며 published/pending/독자 보유 버퍼를 모두 포함한다.
  마지막 strong reader가 반환한 배열 저장소를 재사용하고 acquire마다 새 shared control block을 사용한다.
  이전 weak_ptr은 재사용된 tick에 다시 연결되지 않는다. 풀 포화/OOM은 입력을 닫거나 tick을 소비하기 전에
  expected 오류로 반환한다. 배열 용량은 해당 버퍼의 최고 사용량까지 유지하며 전체 바디 수의 메모리 한도는 아니다.
  shared control block 할당은 남는다. snapshot_buffers/in_use는 발행 직전의 풀 통계다.
- fetch 실패는 terminal failed로 전환한다. 실패 스냅샷은 step_succeeded=false/failure이며 유효 pose를 발행하지 않는다.
  미적용 future 명령은 cancelled outcome으로 반환한다. 이후 SDK 호출/입력을 거부하고 Scene 재생성으로 복구한다.
  실패 때 retired owners는 Scene 해체까지 유지한다. 주입 검사는 실제 fetch 이후 오류를 강제한 API 경로 검증이며
  실제 GPU 장치 장애를 재현했다는 증거가 아니다.
- PhysicsTick 아래 CommandCommit/EventCollect/ActivePoseCollect/SnapshotPublish를 실제 구간에 계측한다.
  SnapshotPrepare는 입력 종료 전 독립 scope이며 candidate tick을 사용한다. 실패 재시도의 ID는 같을 수 있다.
  수치 통계는 snapshot에 있으며 아래 수치 계측 배선을 후속 완료했다.


### P3 capsule CCT 계약 (2026-10-01)

- PhysicsCharacter.h는 SDK 타입 없는 character_desc/move/state를 제공한다. +Y up capsule이며 position은
  중심 위치(m), cylinder_height는 두 반구를 제외한 원통 높이(m)다. radius/height/contact_offset은 양수,
  step_offset은 비음수, slope_limit_cosine은 0..1(0=제한 해제)이다. box CCT는 현재 지원 표면이 아니다.
- Scene이 manager/controller/material 수명을 소유한다. 캐릭터는 바디 생성이나 강체 컴포넌트를 요구하지 않는다.
  typed character_handle의 scene/slot/generation을 검증하고 O(1) free list를 사용한다.
  create 실패 시 controller를 회수하며 handle을 발급하지 않는다. destroy는 재사용 전 멱등적이며
  generation 상한의 슬롯을 폐기한다. controller → manager → SDK scene 순으로 해제한다.
- create/destroy/move/teleport는 owner idle/commit, read는 완료한 owner 읽기 경계만 허용한다.
  simulate 중에는 모두 거부한다. 생성/삭제/이동/teleport 명령도 기존 stamped queue에서 순서대로 적용하며
  outcome의 character_created/retired로 typed handle을 반환한다.
- move는 변위(m), 경과시간(s), minimum_distance(m)를 받으며 중력·속도 적분·점프를 자동 적용하지 않는다.
  반환값은 소유 position/foot_position/actual_displacement와 마지막 move의 sides/above/below다.
  아래 충돌 flag만으로 이후 tick의 영구 grounded를 보장하지 않는다. 위 정책은 C0/C1에서 구현한다.
  teleport는 충돌을 확인하지 않는 명시적 위치 변경이며 캐시와 마지막 move 결과를 초기화한다.
- CCT 이동은 query-enabled solid body와 다른 CCT에 대해 양방향 belongs_to/collides_with를 검사한다.
  센서는 이동 장애물에서 제외한다. CCT manager의 capsule 간 필터도 동일한 양방향 규칙이다.
  query-disabled body는 SDK CCT sweep 대상에서 제외된다. scene query 표면은 현재 body hit 전용이며
  CCT 내부 actor/shape 프록시를 반환하지 않는다.
- CCT 내부 actor는 query-only다. 동적 바디의 solver가 CCT를 자동으로 밀거나 CCT가 힘을 전달하는 경로는 없다.
  dynamic body에 대한 CCT sweep은 가능하며 명시적 push 정책은 C0의 별도 이동 정책이다.
  CCT 프록시가 body 접촉/센서 이벤트로 나타나지 않는다. 캐릭터 센서/상호작용 정책은 C0의 후속 작업이다.
- 성공한 finish_step은 현재 모든 캐릭터의 소유 state를 tick_snapshot.characters에 담는다.
  불변 캐릭터 결과는 manager/Scene 종료 후에도 유지된다. 실패 snapshot은 캐릭터 pose도 발행하지 않는다.
  CharacterCreate/Destroy/Movement/Teleport/PoseCollect를 실제 CPU profiler scope로 기록하며
  명령 이동은 PhysicsTick/CommandCommit 아래 실행된다. counter 그래프는 아래 절에서 후속 배선했다.


### P3 수치 계측 계약 (2026-10-01)

카운터 category physics(1<<6)는 기본 활성화이며 profiler Physics 토글로 차단할 수 있다.
35개 고정 descriptor/ID(26..60)를 사용하여 tick마다 이름 등록·문자열 생성·registry mutex 조회를 하지 않는다.
Shipping은 카운터 발행과 profiler 참조를 제거하되 API snapshot.statistics는 계속 제공한다.

finish_step의 실제 완료 engine frame에 (scene,tick,task=0) 표본을 발행한다. 한 프레임의 여러 씬/틱을
합산하거나 덮어쓰지 않는다. 동일 ID/scene/tick/task의 재발행만 값 교체다. 0/N tick 프레임에서
없는 물리 표본은 미측정이며 0으로 보간하지 않는다. CounterPublish는 Physics.Counters CPU scope다.
GPU backend에서도 이 SDK 작업 수는 CPU dispatcher 작업 수이며 GPU kernel 수나 실행 시간은 아니다.

| 카운터 | 단위 | 집계·리셋 |
|---|---|---|
| Physics.Bodies | bodies | 발행 직전 gauge |
| Physics.Shapes | shapes | 발행 직전 gauge |
| Physics.Characters | characters | 발행 직전 gauge |
| Physics.Active bodies | bodies/tick | 완료 tick 결과 |
| Physics.Active shapes | shapes/tick | 완료 tick 결과 |
| Physics.Changed bodies | bodies/interval | 직전 발행 이후 증분; 발행 시 리셋 |
| Physics.Changed shapes | shapes/interval | 직전 발행 이후 증분; 발행 시 리셋 |
| Physics.Changed characters | characters/interval | 직전 발행 이후 증분; 발행 시 리셋 |
| Physics.Applied commands | commands/tick | 완료 tick 결과 |
| Physics.Failed commands | commands/tick | 완료 tick 결과 |
| Physics.Cancelled commands | commands/tick | 완료 tick 결과 |
| Physics.Queued commands | commands | 발행 직전 gauge |
| Physics.Stored events | events/tick | 완료 tick 결과 |
| Physics.Required events | events/tick | 완료 tick 결과 |
| Physics.Dropped events | events/tick | 완료 tick 결과 |
| Physics.Stored contact points | points/tick | 완료 tick 결과 |
| Physics.Required contact points | points/tick | 완료 tick 결과 |
| Physics.Dropped contact points | points/tick | 완료 tick 결과 |
| Physics.Unresolved identities | pairs/tick | 완료 tick 결과 |
| Physics.Queries | queries/interval | 직전 발행 이후 증분; 발행 시 리셋 |
| Physics.Required query hits | hits/interval | 직전 발행 이후 증분; 발행 시 리셋 |
| Physics.Query overflows | queries/interval | 직전 발행 이후 증분; 발행 시 리셋 |
| Physics.SDK workers | workers | 발행 직전 gauge |
| Physics.SDK tasks submitted | tasks/interval | 직전 발행 이후 증분; 발행 시 리셋 |
| Physics.SDK tasks completed | tasks/interval | 직전 발행 이후 증분; 발행 시 리셋 |
| Physics.Inline SDK tasks | tasks/interval | 직전 발행 이후 증분; 발행 시 리셋 |
| Physics.Snapshot buffers | buffers | 발행 직전 gauge |
| Physics.Snapshot buffers in use | buffers | 발행 직전 gauge |
| Physics.Tick buffer capacity | B | 발행 직전 gauge |
| Physics.Peak tick buffer capacity | B | Scene 수명 최대값 |
| Physics.Peak query stack scratch | B | Scene 수명 최대값 |
| Physics.Step failed | 0/1 | 완료 tick 결과 |
| Physics.Rejected input commands | commands/interval | 직전 발행 이후 증분; 발행 시 리셋 |
| Physics.Input queue overflows | commands/interval | 직전 발행 이후 증분; 발행 시 리셋 |
| Physics.Maximum command residence | ticks | 완료 tick 결과 |

전체 바디/형상/캐릭터 수는 성공한 등록/해제 시 O(1)로 갱신한다. 변경량은 동일 대상 tick에서
한 번 이상 API 쓰기가 성공한 handle generation의 수다. 생성/파괴 및 교체 중 실제로 생성했다가
회수한 generation도 포함하고, 중복 쓰기는 한 번만 센다. 변경 형상은 해당 바디의 형상 수이며
shape별 데이터 비교 결과가 아니다. 활성 형상 수는 수집한 active body에 대해서만 조회한다.

입력 거부에는 invalid/foreign/stale sequence/late/포화/terminal rejection이 포함된다.
accepted payload의 commit 실패와 구분하며 queue overflow는 거부 중 capacity_exceeded다.
명령 waited_ticks는 적용 tick - 입력 승인 당시 last_tick이며 미래 tick에 예약한 시간도 포함한다.
Maximum command residence는 적용한 명령의 최대 waited_ticks이고 wall-clock queue latency가 아니다.
카운터/큐 메모리 자체의 수집 손실은 capture.dropped_counters로 별도 진단한다.

Tick buffer capacity는 이번 발행의 commands/events/active_poses/characters vector 용량×요소 크기 합이다.
객체/공유 control block, 다른 독자가 보유한 snapshot, event callback 버퍼, queued geometry, SDK/GPU heap은 제외한다.
Peak tick buffer capacity는 이 값의 Scene 수명 최대치다. Query stack scratch는 실제 실행한
raycast/sweep/overlap의 32-element SDK hit 배열 중 최대 크기이며 예약한 스택 공간이다.
SDK 내부 scratch/heap 사용량을 측정했다고 주장하지 않는다.

CPU 카운터 native sample은 16 -> 40 bytes다. .ceprof envelope version은 2를 유지하고
counter chunk version을 2로 확장하여 sample wire 10 -> 34 bytes(id/value/세 u64 ownership)다.
reader는 counter chunk v1을 읽고 CPU ownership을 0으로 초기화한다. 기존 frame/thread chunk v2와
나머지 chunk v1은 유지한다. 구 reader는 counter chunk v2를 unsupported로 거부한다.
고정 byte capture 예산은 유지되므로 표본 증가에 따른 보존 메모리 비용은 숨기지 않는다.

Physics UI는 불변 capture의 Scene/metric을 선택하여 tick 축 그래프를 표시하고 completion frame을 tooltip에 보인다.
한 engine frame의 여러 tick도 유지하고 누락 tick 사이 선을 연결하지 않는다.
Timeline 실행 계층과 수치 그래프는 서로 다른 표면이다. UI translation unit 컴파일을 검증했으며
현재 제품 UI 실행은 소비자 이전 후 M 단계에서 검증한다. 계측 on/off 비용·p99 측정도 M3 잔여다.

## B0 Scene 물리 세션과 에디터 복원 계약

ScenePhysicsSimulation은 생성 스레드가 소유한다. 편집 membership과 런타임 SDK 소유자를
분리하여 Register/Define은 에디터 상태에 SDK를 만들지 않는다. Start는 등록 순서로 candidate
SDK scene/body를 준비한 뒤 전부 성공해야 공개한다. Define은 재생 중 거부한다.
Stop은 진행 중 fetch/drain을 포함하여 SDK를 해제하고 캐시를 초기 정의로 되돌린다.
이 물리 캐시 초기화만으로 플레이 중 생성/삭제나 DDOL을 복원했다고 판정하지 않는다.

SceneManager는 host가 시작 전에 선택한 SimulationSessionPolicy를 사용한다. 기본/runtime
정책의 Player는 authoring 백업·복원 콜백을 호출하지 않는다. Editor만 Play 직전 문서와
DDOL 지정을 함께 백업한다. Editor Stop에는 일반 씬
이동과 달리 DDOL까지 해체하고, 합성 루트의 추가 컴포넌트를 지운 뒤 원래 루트/객체 정의와
Transform을 복원한다. 타입/역직렬화 오류가 나면 백업을 유지하고 성공 이탈 통지를 보내지
않는다. runtime binding/handle/SDK 내부 속도·관성은 저작 문서에 저장하지 않는다.

Advance는 finite/nonnegative 초만 받는다. 60 Hz, 최대 4 tick, 초과 시간 폐기이며 폐기 집계는
유한 double 최댓값에서 포화한다. 활성 결과는 frame publication generation/index로 O(active)
중복 제거하여 마지막 pose를 제공한다. Scene은 기존 world-write batch로 반영하고
Physics.ApplyTransforms에 마지막 scene/tick 문맥을 부여한다. N3 공통 스케줄러와 B2 보간은
별도 계약이다. 비활성 바디 재활성화는 현재 pose/속도를 유지하고 새 Play는 초기 정의로 시작한다.

독립 SDK 세션 CPU/GPU/ASan과 네 제품 TU Debug 컴파일은 확인했다. 전체 제품 소비자
이전이 끝나지 않아 실제 에디터의 Play/Stop 저장 왕복, 씬 전환/DDOL 속도 이전,
compound/제약 저작·관리 이벤트 배선은 미검증 또는 미구현이다. B0를 완료로 판정하지 않는다.

Read는 활성 바디의 질량/관성을 첫 tick 이전에도 SDK에서 읽는다. SDK 시뮬레이션 중에는
wrong_phase를 반환하며 독자는 불변 snapshot을 사용한다. 에디터/비활성 membership은
정의/마지막 상태 캐시를 읽고, 에디터 정의의 관성은 아직 SDK에서 계산되지 않은 0이다.

2026-10-01 씬 활성화 경계: 기존 SDK drain → DDOL 소속 이전 → 목적 씬 SDK 시작.
현재 선형/각속도는 transient body_state로 이송하고, pose는 이송된 현재 Transform을 사용한다.
기존 SDK handle은 재사용하지 않는다. 시작 실패는 committed를 해제하고 Stop을 요청한다.
독립 실제 SDK CPU/GPU 상태 이전 및 같은 실행 정책의 실패/재시도 게이트를 검증했다.
Debug/Release/ASan 4341, Shipping 4337; 제품 Scene 네 TU와 host 두 TU 컴파일 통과.
실제 Player/Editor 바이너리 실행 및 씬 메타데이터까지 포함한 Editor travel 복원은 미검증이다.

B0 runtime control: PhysicsBodyComponent::ReadState/SetVelocity/ApplyForce는 Scene 소유 membership으로
위임한다. SetVelocity의 linear/angular는 m/s, rad/s. ApplyForce는 linear force/torque와
force_mode를 받는다. 소유 스레드의 idle SDK만 변경하며, 비활성/편집 상태는 wrong_phase,
해제된 binding은 stale_handle, static/kinematic과 비유한 입력은 invalid_argument다.
저작 translation/rotation axis_lock은 모든 축 조합을 제공한다. runtime 제어는 저작 초기 속도를
덮어쓰지 않는다. C# 엔트리와 kinematic target/Transform 일관성은 M0/B2 잔여다.

B0 제어 추가 최종 검증: Debug/Release/ASan 각 4381, Shipping 4377, 실제 GPU 통과.

## 공통 레이어 책임 분리 (L0 기반 구현, 제품 연결 잔여)

이번 빅뱅에서 TagManager는 태그만 남기고 레이어 API를 제거한다. 공통 LayerCatalog가
안정 layer_id/명시 0~31 slot을 소유하며 SceneLayerIndex는 Scene 수명의 EntityHandle
소속을 관리한다. PhysicsCollisionPolicy가 단일 충돌 행렬 원본을 소유하고 PhysicsScene은
불변 revision을 SDK 필터로 적용한다. 물리 전용 레이어 이름 체계를 추가하지 않는다.
이름/순서 변경은 ID와 mask 의미를 바꾸지 않는다. 자동 slot 재사용과 미등록 Default
fallback을 금지한다. Entity m_collisionType을 제거하고 Scene/Prefab/쿼리/Editor/C#/
Player cooked 참조를 일회성 변환한다. 자세한 수명·변경·오류·검증 계약은
PhysicsRedesignPlan의 공통 레이어 정리 L0를 따른다. 현재 바디 필터와 Entity 안정 ID 저장/Scene 소속을 프로젝트 정책에 연결했다.

L0 기반 현재 증거: core 각 7602 checks(Debug/Release/Shipping/ASan), SDK 각
568 checks(Debug/Release/ASan)/Shipping 564, 실제 GPU 및 FilterCommit/Refilter capture.
LayerCatalog Replace는 ID high-water와 사용된 slot tombstone을 유지하며 revision을
단조 증가시킨다. SceneLayerIndex Members는 owner-only span이고 다음 membership 변경
전에만 유효하다. SDK set_shape_filter는 idle owner에서 같은 body/shape identity를 유지하고
resetFiltering으로 기존 contact pair를 재평가한다. Query 필터는 다음 tick 이전에도 보인다.
프로젝트 host는 ProjectLayerSettings를 소유하며 카탈로그/정책의 원자적 쌍을 발행한다.
Editor Play는 이 쌍을 백업/복원하고 Player는 엄격한 native asset 로드만 수행한다.
`Layers.celayers`는 CLYR v1 little-endian 형식: magic/version, next_id, slot 순서의
ID/slot/retired/UTF-8 이름 레코드, 1024개 대칭 bool, FNV-1a 64-bit checksum이다.
크기는 64KiB, 이름은 1024 bytes로 제한하며 잘린 입력·잘못된 UTF-8·중복 ID/이름·
비대칭 행렬을 거부한다. Editor만 legacy N×N/32×32 파일 변환과 저작 트랜잭션을 제공한다.
패키저는 asset을 필수로 검증하고 그대로 복사하며 Player는 전체 의미 검증을 수행한다.
실제 프로젝트 import/Entity 스키마 각 1161 checks와 패키징 오류 게이트 6 checks 통과.
TagManager 레이어 책임은 제거했고 Entity 안정 ID 저장/Scene index/Inspector/CLI를 연결했다.
CCT와 실제 Editor/Player 실행 검증은 남아 있으며,
실제 Editor/Player 제품 실행은 전체 소비자 교체 이후 검증한다.

ScenePhysicsSimulation::CommitLayers는 전체 binding 정렬 목록을 요구한다. owner/idle,
coherent revision, 전체 membership과 ID를 검증한 후 변경 shape만 SDK에 적용한다.
실패 시 이전 shape 필터를 복원하며 rollback 불가능 시 세션을 종료한다. 성공 이후에만
정의와 LayerRevision을 갱신한다. disabled 정의도 갱신하여 재등록 시 필터가 일치한다.
Scene은 시작/틱 경계에서 Entity 안정 ID를 공통 카탈로그로 엄격히 검증하며 미등록 ID는
오류다. 실패 시 해당 틱을 생략하고 구조 경계에서 Play 중단을 요청한다. 현재 ID 검증/
전체 목록 정렬 비용은 남아 있으며 dirty 소비자 연결 이후 실측한다.
Profiler: LayerMembership → LayerPolicyCommit → FilterCommit → Refilter. 독립 CPU/GPU
회귀 각 Debug/Release/ASan 4974, Shipping 4967; 하위 계층 depth/시간 구간 검증 포함.

EntityLayerSchema는 m_layer/m_collisionType을 포함하거나 m_layerId가 누락/미등록인
입력을 거부한다. uint64 저장을 강한 layer_id로 읽으며 Entity::SetLayer는 Scene 소속
갱신을 먼저 성공시킨 뒤 값을 발행한다. Scene::LayerMembers는 owner-only 핸들 span이며
다음 membership 변경 전까지 유효하다. 소비자는 Resolve와 파괴 표시를 확인한다.
프로젝트 Change/Restore의 publication callback은 준비된 snapshot을 받는다. 모든 할당을
마친 뒤 Editor 저장을 완료하고 atomic snapshot을 교체한다. 저장 실패 시 이전 pair/
revision을 유지하고 재진입 변경은 거부한다. Undo/Redo도 같은 저장 경로를 사용한다.
CLI의 layerId/revision은 decimal 문자열이고 slot은 정수다. 기존 cooked 자산은 재쿠킹한다.
Release 4978 CPU/실제 GPU 회귀로 rename 후 slot과 SDK body identity 보존을 추가 확인했다.

레이어 Undo 기록은 소유 프로젝트의 weak 참조를 보유하며 현재 프로젝트가 달라졌거나
소유자가 종료됐으면 적용을 거부한다. Entity Undo는 기존 EntityReference의 수명 검증을 사용한다.

## B1 shared cook/source 계약 (2026-10-01)

geometry_asset_key는 자산 UUID+revision이며, 동일 key의 내용 변경은 거부한다.
CollisionGeometrySource는 owned convex/triangle/heightfield 입력 variant다. CECG v1은
이를 little endian 네이티브 record로 검증/왕복하고 SDK ABI/pointer를 저장하지 않는다.
CollisionGeometryIO는 read-only이고 expected key를 검증한다. 전체 형상 preflight 성공 후
Scene-owned CollisionGeometryLibrary가 loader/cook을 호출한다. 캐시 hit는 I/O/cook을 반복하지 않는다.

Publish는 prepared flat_map과 immutable canonical bytes로 새 자산을 준비하고, host
callback 성공 뒤 swap한다. cook/할당/host false/exception/재진입은 이전 cache를 보존한다.
동일 revision에 다른 payload를 덮어쓰지 않는다. Resolve/Publish/Stats는 owner thread 계약이다.
파일 staging/meta/revision archive는 Host 책임이다. 생성 전용 geometry.create Editor adapter는
연결했고 geometry.update의 revision 수정/Undo와 불변 CECG source archive도 연결했다.
SDK cook blob과 packaged artifact closure는 잔여다.
새 자산은 UUIDv4/revision1, Assets canonical 경계 및 기존 source/meta/staging 부재를 요구한다.
cook 후 meta 먼저 게시하고 본문 게시 실패 시 새 meta를 복구하며 cache는 유지한다.
Play 중 Scene 게시를 거부한다. 성공 결과의 catalogRegistered=false는 durable 파일/cache가
성공했으나 catalog 알림은 watcher/재기동의 재시도가 필요함을 뜻한다.
여러 파일의 crash 원자성을 뜻하지 않는다. revision 수정은 expected source bytes 및 sidecar
UUID/revision으로 stale 이력을 거부하고, rename 실패 시 backup을 복구한다. Undo/Redo는
고정 before/after revision을 복원하며 번호 발급은 archive 최대값 뒤에서 계속한다.
Editor는 이전 고정 revision을 Derived/CollisionGeometry/UUID에서 읽고 Player는 탐색하지 않는다.
Physics.GeometryRevisionPublish와 목적 Undo stack 사전 할당으로 게시/이력 실패를 구분한다.

바디와 shape는 shared_ptr<const CollisionGeometry>를 소유한다. 임시 cook PhysicsScene/cache
해제 후에도 SDK session은 자산이 유지한다. GeometryCook scope는 SDK cook만 포함하며
Host 게시 scope는 GeometryPublish 안에서 분리한다. native source와 SDK 사전 cook artifact,
독립 SDK 검증과 실제 Editor/Player 제품 실행을 구분한다.

## B1 offline SDK artifact 계약 (2026-10-01)

CECG는 Editor의 portable 원본/이력이며 CEPG v1은 Player의 PhysX SDK cook bundle이다.
CEMF UUID당 artifact 한 개에 오름차순으로 최대 1024개 고정 revision을 담는다.
CEPG는 SDK 버전, Windows x64 little endian, cook 정책 1, UUID, revision, 형상 종류,
원본 canonical hash와 SDK blob을 기록한다. GPU cook data를 포함한다.
최대 256MiB, 전체 FNV64와 CEMF SHA-256을 검증하고 호환되지 않는 SDK/정책은 거부한다.

AssetCooker는 현재 CECG와 UUID별 불변 archive를 cook한 뒤 SDK import 왕복을 확인한다.
Scene/Prefab의 geometryAsset은 CEMF 의존성이며 geometryRevision이 bundle에 없으면
패키지 게시 전에 실패한다. BuildTool은 새 원본/이력도 cook 입력에 복사하고, cook 이후
배포용 merged 입력에서 CECG와 그 sidecar를 제거한다. Editor 원본은 수정하지 않는다.

Player PhysicsBodyComponent는 CEMF cooked entry만 ResolveCooked로 읽는다.
원본·Editor archive fallback이나 runtime cook은 없다. 현재 제품 경로는 runtime loose
cooked tree를 사용하고, 공용 byte-source의 mounted pak 읽기는 독립 회귀로 검증한다.
SDK blob import 뒤 cache를 swap하며 loader/decode/import 실패 시 기존 cache를 유지한다.
Physics.CookGeometryBlob, Physics.LoadGeometryBlob, Physics.GeometryArtifactResolve,
Physics.GeometryArtifactImport를 profiler 계층에 노출한다. 실제 제품 capture는 M3 게이트다.

## B1 shape layer override·idle replacement 계약 (2026-10-01)

PhysicsShapeDefinition.layerOverride는 프로젝트 공통 layer_id의 uint64 값이다.
0은 Entity 레이어 상속이고 별도 물리 레이어/slot/이름 체계를 만들지 않는다.
BuildPhysicsShapes는 override가 있으면 project_layer_snapshot을 요구하며,
모든 형상의 활성 ID/스케일/형상 preflight가 끝난 뒤에만 geometry resolver를 호출한다.
ScenePhysicsSimulation.CommitLayers는 형상별 override를 사용해 정책 변경을 refilter한다.
unknown/retired ID는 전체 게시 전에 실패하며 disabled 바디 정의에도 같은 정책을 적용한다.

SetShapes/Define는 Play 중 저작 변경을 계속 거부한다. ReplaceShapes는 별도 runtime API다.
owner/idle Play에서만 complete shape list를 교체하며 RefreshRuntimeShapes는 현재 목록을
현재 world scale로 다시 구축한다. Scene.EnsureResolved로 부모 Transform을 먼저 확정한다.
자동 scale 감지/보간은 B2 잔여 범위이며 이 API는 명시적 갱신이다.

ScenePhysicsSimulation.Replace는 현재 simulated pose/linear/angular velocity와 enabled 상태를
유지한다. map/node/bucket 준비 후 PhysicsScene.replace_body 성공 시에만 definition/map/handle을
게시하고, 기존 핸들은 stale이 된다. invalid definition 또는 simulating 상태는 기존 바디를 보존한다.
disabled 바디는 private SDK validation Scene에서 정의를 검사해 live Scene에 임시 actor를 남기지 않는다.
정적 바디의 속도 의미는 SDK 종류 규칙을 따른다. 질량/관성은 새 형상으로 SDK가 재계산한다.

Physics.ShapeRuntimeReplace → ShapeAuthoring/BodyDefinitionReplace → BodyReplace 계층을
제품 profiler에 노출한다. Editor Stop의 pre-Play 문서 복원 및 Player의 비복원 계약은 유지한다.
실제 Editor 복원/Inspector/HTTP 호출은 제품 게이트로 따로 검증해야 한다.


## B1 Editor 형상 목록 게시 계약 (2026-10-01)

Inspector 값 초안과 physics.shapes 파일 입력은 EditorObjectOperations::PhysicsShapes를
공유한다. live m_shapes 직접 reflection 변경은 숨기고 일반 property 쓰기도 거부한다.
완전한 목록 검증·현재 문서 비교·프로젝트/씬/컴포넌트 신원 확인 후 SetShapes와 prefab
m_shapes override를 한 Undo 명령으로 게시한다. 실패 시 Undo 이력과 기존 목록을 보존한다.
Undo/Redo도 현재 목록이 예상 문서와 같은지 확인하며 Play/잠금/프로젝트 교체를 거부한다.
형상 종류와 레이어는 Inspector에서 이름으로 선택하고 문서에는 기존 enum/공통 안정 ID를 저장한다.
Physics.ShapeAuthoringPublish를 계측한다. reader 37 checks는 Debug/Release/ASan 통과했으나
실제 Editor UI·HTTP·Undo/Prefab·Stop 복원은 새 Editor 빌드 이후 제품 게이트로 확인해야 한다.


## M0 primitive 표시 경계 (2026-10-01)

BuildPhysicsPrimitivePreview는 box/sphere/capsule만 읽기 전용 variant와 unit-scale pose로
변환한다. 형상 변환 검증을 재사용하며 asset resolver/cook/SDK를 호출하지 않는다.
치수와 local position에 scale을 한 번 적용한다. 레이어 선택은 표시 기하와 무관하다.
Scene 기즈모 수집은 Scene 소유 컴포넌트를 GT에서 읽고 정점 값을 packet에 복사한다.
Cooked 와이어와 CCT 표시는 잔여 구현이며 unsupported/invalid primitive 수치를 구분한다.


## M0 모델 충돌 저작 경계 (2026-10-01)

Core 모델 생성은 triangle 값을 worker에서 준비하고 owner-thread host 콜백으로
공유 geometry_asset_key를 받는다. 파일 게시·UUIDv4/meta·catalog 등록은 Editor Host 소유다.
파일명 내용 hash는 재사용 후보만 정하고 전체 encoded CECG 및 meta identity를 검증한다.
변경 입력은 새 자산이며 Undo에 필요한 기존 자산을 삭제하지 않는다. 프로젝트/Play guard는
GT에서 캡처한 request identity를 비교한다. Player에 source 게시/cook fallback을 추가하지 않는다.
바디는 생성 전에 static + complete triangle shape를 검증하고 resolved owner pose로 등록한다.
스킨 자동 triangle collision은 거부한다. 실패 시 모델 인스턴스 회수와 생성 자산 보존을 구분한다.
제품 placement/Undo/Stop/Player 실행은 CLR 소비자 교체 뒤 검증해야 한다.


### C0/C1 Scene 캐릭터 단위 및 이동 정책 (2026-10-02)

저수준 move는 m 단위 변위, 상위 CharacterMovementComponent 입력은 월드 m/s다.
Scene은 1/60s 고정 스텝에서 desired velocity + 누적 Y 중력을 적분하여 SDK begin_step
이전에 이동한다. gravity 단위는 m/s²이며 낮은/높은 계단 판정은 동일 stepOffset을 쓴다.
SDK capsule은 constrained climbing과 prevent-climbing 정책이다. slope cosine=0은 제한 해제,
경사 제한의 검증 범위는 static triangle mesh다. SDK 계약상 non-walkable 처리는 static actor
대상이고 sphere/capsule에는 지원되지 않는다. 동적 바디 전반의 경사 제한을 보장하지 않는다.

강제 위치는 Teleport로 명시하며 중력 속도와 이전 collision flags를 초기화한다.
teleport는 접촉 해결을 보장하지 않으며 원하는 속도 입력과 SDK identity는 유지한다.
강제 속도는 SetDesiredVelocity로 교체하고 호출자가 종료 시점에 다시 입력한다.
자동 duration/감쇠/점프/입력 혼합을 legacy 함수 이름으로 복원하지 않는다.
단위 보고서는 offline이며 runtime deserializer는 schema 1만 허용한다.


### C1 고정 스텝 이동 정책과 ABI31 (2026-10-02)

Scene의 캐릭터 정의는 acceleration(m/s²), braking_decay(1/s), jump_speed(m/s),
max_fall_speed(m/s)를 소유한다. CharacterMovementComponent의 저작 기본값은
20, 8, 5, 55다. 저수준 Scene 정의의 acceleration=0은 직접 입력 모드로,
매 스텝 motion velocity를 desired velocity로 바꾸고 가속/감쇠를 우회한다.
acceleration>0이면 벡터 크기 기준으로 목표 속도에 접근하고 입력 0에서
`v *= exp(-braking_decay*dt)`를 적용한다. 이 모드의 decay=0은 감쇠 없음이다.
movement_velocity는 접촉 해결 이전 정책 속도이며 실제 속도/변위는 SDK의
actual_displacement와 구분한다. max_fall_speed는 누적 중력 속도의 하한이며
명시적 desired/forced Y 속도까지 제한하는 값은 아니다.

Jump는 마지막 완료 이동의 below 접촉과 비상승 상태에서만 한 번 예약한다.
강제 이동 중이나 이미 예약된/공중/비활성 점프는 wrong_phase다. 예약은 다음
실제 fixed step에서 jump_speed로 중력 속도를 교체하며 frame만 흐르면 소비하지 않는다.

ForceVelocity(v, seconds)는 양의 유한 시뮬레이션 시간 동안 일반 속도를 대체한다.
중력은 함께 적용되며 원하는 입력과 정상 이동의 정책 속도는 계속 갱신한다.
시간이 스텝보다 적게 남으면 그 시간만 forced 속도로 적분하고 나머지는 정상
속도로 적분한다. 시작 요청은 queued jump를 지운다. 종료/Cancel은 원하는 입력으로
복귀한다. 비활성/paused/no-step 시간에는 타이머가 줄지 않는다. Cancel은 멱등적이다.
Teleport는 정책 속도·forced timer/velocity·queued jump·낙하와 이전 충돌을 지우고
원하는 입력은 유지한다. Stop은 authored 초기 상태로 복귀한다. DDOL은 SDK 핸들
대신 motion memory를 전달하지만 제품 DDOL 실행 증거는 별도다.

CharacterMotionPolicy.h의 순수 준비 함수는 유효값/overflow를 검사하며 Scene은
모든 활성 입력을 검증한 후 SDK 이동을 시작한다. 실패한 SDK 이동은 그 캐릭터의
정책 메모리를 게시하지 않는다. 전체 이동 실패는 Scene의 기존 fail-closed Stop 경계로 간다.
`Physics.CharacterJumpRequest/ForceRequest/ForceCancel` scope와 기존 FixedStep→SDK
Movement 계층에 연결한다.

ABI31은 Character_Jump/Force/CancelForce를 더한 167슬롯이다. 상태 wire는 88 bytes:
기존 tick offset56, movement velocity offset64, double forced_remaining offset80이다.
flags의 bit16=forced, bit32=jump queued. C# 구조/실제 bound 호출을 함께 검증한다.
HTTP의 character.jump/force/cancel과 state의 movementVelocity/forcedRemaining/forced/
jumpQueued가 같은 native 경계를 사용한다. 정책 저작값 변경은 Play에서 거부하며
유효하지 않은 새 값은 원복하고 Undo를 만들지 않는다.


#### Character DDOL identity

Scene-scoped EntityHandle은 Scene 이송으로 만료된다. C#의 ScriptObjectHandle은 동일 Entity의
수명을 따르므로 DDOL 이송으로 만료하지 않는다. 기존 managed character wrapper는 같은
component instance의 새 Scene 상태를 읽는다. SDK character_handle은 승계하지 않고 새
Scene에서 생성한다. 입력과 motion memory만 전달한다. Editor Stop으로 Entity를 복원하면
기존 managed wrapper는 StaleHandle을 반환하며 복원된 component에 재지정하지 않는다.
