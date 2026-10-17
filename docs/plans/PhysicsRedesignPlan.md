# 물리 재설계 — C++23 PhysX API 선행 재작성 (PHASE 19)

수립일: 2026-08-18 · 빅뱅 범위 개정: 2026-10-01
접촉 저작 개정(2026-10-08): [물리 접촉 작업·C# 계약](../design/PhysicsContactExecutionContract.md). E0 추가, ContactStream 기본 구현 진행 중. 기존 완료 기록은 당시 범위의 증거이며 접촉 제품 배선 완료를 뜻하지 않는다.

관련: [컴포넌트 설계](../design/PhysicsComponentDesign.md) · [잡 시스템 계약](../design/JobSchedulerDesign.md) · [직렬화](SerializationPlan.md) · [고정 Simulation Tick](NetworkFrameworkPlan.md)

## 최신 완료 상태 (2026-10-09)

**B1 형상 저작·공유 자산 및 C0 CharacterMovementComponent 완료.** 고정한 필수 종료 조건7개 제품 게이트를 통과했다. 아래 이전 날짜의 progress 기록은 당시 증거의 이력이다. 상세 증거는 문서 끝 B1 종료 기록 및 `Build/Verification/ContactStream/B1Closure/result.json`에 있다. Phase19 전체는 완료되지 않았다.

## P0 실행 기록 (2026-10-01)

상세 계약과 fresh 증거: [PhysicsAPIContract](../design/PhysicsAPIContract.md).
P0는 **최소 삭제 전 기준선/계약 범위에서 완료**다. C++23/Mathematics view Debug·Release probe,
PhysX SDK CPU reference, Physics Debug·Release 및 HEAD Editor Release 빌드를 확보했다.
HTTP 저작으로 box/rigid/CCT corpus, 저장/재로드·낙하·CCT 하강·강체 off/on·stop pose 복원,
prefab 인스턴스화·컴포넌트 제거·entity 삭제를 검증하고 fixture/JSONL/해시/ZIP을 보존했다.
RHI freeze incomplete는 사용자 지시로 별도 profiler backlog로 이관하며 P0 진입 차단에서 제외한다.
수평 CCT 입력·점프·DDOL·복잡한 형상·쿼리/콜백의 포괄 검증은 M 단계에 남는다. R0는 아직 실행하지 않았다.

## R0 실행 기록 (2026-10-01)

[철거 감사](../analysis/PhysicsCutoverAudit.md): 해시 대조한 구 구현 55파일, 프로젝트 편입,
타입 등록과 bootstrap 물리 초기화를 제거했다. Owners gate 통과, Complete gate는 소비자 12파일 때문에 미통과다.
현재 제품 빌드는 Scene 물리 타입 미이전으로 실패하며 P0 바이너리를 철거 후 검증에 재사용하지 않는다.
R0 전체 완료는 P1~P3/B/C/T/M의 새 계약 연결 후 확정한다.

## P1 실행 기록 (2026-10-01)

새 PhysicsScene의 CPU 기반을 구현했다. SDK 타입은 공개 헤더에서 제거하고 RAII,
expected monadic 합성, 강한 scene/body/character 핸들, 소유 스레드 검사와 bounded SDK dispatcher를 배선했다.
초기화 단계별 실패 rollback, 공유 SDK 마지막 해제 경합, in-flight 종료와 워커 수명을 검증했다.
새 Physics 프로젝트 Debug·Release 단독 빌드 통과. verify-physics-p1.ps1의 Debug·Release는 각각
402개 검사/SDK task 4,160개, Shipping은 진단 라이브러리 없이 4개 검사/task 51개를 통과했다.
SceneCreate/SceneDestroy/CpuInitialize/GpuInitialize/SimulateSubmit/FetchWait/PhysXTask marker와 워커 stream을 실제 캡처했다.
워커 종료 후 capture는 complete=1/unacked=0/droppedEvents=0이며 실행 중 idle 워커 freeze 응답 증거는 아니다.
CUDA context 유효성·실제 GPU dynamics/broadphase 설정을 확인한 opt-in을 구현했다.
RTX 2080 Ti/driver 595.97에서 Debug·Release GPU 씬 초기화, 10 step와 in-flight 종료를 검증했다.
CUDA 생성 실패와 GPU 씬 거부 주입은 새 CPU descriptor로 전환하며 GPU 설정 제거와 CPU step을 확인했다.
검증 배포는 PhysXGpu_64.dll뿐 아니라 PhysXDevice64.dll도 포함한다. GPU 선택/전환 이유는 status로 노출한다.
**P1 기반 완료**다. 이 GPU 증거는 빈 씬 초기화/스텝 범위이며 실제 바디 충돌·GPU cook·성능은 P2/M에 남는다.
P2의 바디/형상/쿼리와 P3의 PhysicsTick 계층·tick/task 상관 메타데이터는 아직 구현하지 않았다.
제품 전체 빌드는 구 소비자 교체가 필요하며 단독 모듈/probe 통과로 제품 통과를 주장하지 않는다.

## P2 실행 기록 (2026-10-01)

PhysicsGeometry.h의 variant 형상, 불변 공유 CollisionGeometry, ShapeInstance와 body_desc를 구현했다.
static/kinematic/dynamic, compound 질량·관성·질량중심, 감쇠·축 잠금·중력 설정,
shape별 sensor/material/양방향 simulation filter/query layer를 새 API에 배선했다.
box/sphere/Y축 capsule, convex, static triangle mesh와 heightfield를 지원한다.
mesh/heightfield의 비정적 바디·센서 조합은 명시적으로 거부하며 센서는 질량 계산에서 제외한다.
slot/generation registry는 O(1) free list로 재사용하고 generation 상한에서 슬롯을 폐기한다.
일부 shape 생성 후 실패는 actor/shape/material 참조를 회수하며 handle을 공개하지 않는다.

raycast/sweep/overlap은 owner idle 경계에서만 실행하고 body/shape ID를 값으로 반환한다.
순서 없는 shape hit를 반환하며 작은 버퍼·빈 버퍼·SDK scratch보다 많은 hit에서도
written/required_capacity/truncated를 명시한다. nearest 보장은 별도이며 overflow subset에 적용하지 않는다.
convex/triangle cooking은 GPU data를 생성하고 shared 자산이 SDK 수명을 보유한다.
heightfield는 row-major int16 샘플을 mdspan으로 읽고, 비핫패스 값 검증은 Mathematics components view를 사용한다.

verify-physics-p2.ps1 -Configuration All -RequireGpu: Debug/Release/ASan 각각 1,065개 검사,
Shipping 366개 검사. CPU 및 RTX 2080 Ti GPU에서 실제 낙하·기본 형상/convex/mesh/heightfield 접촉,
센서 통과·필터 차단·compound·kinematic·쿼리·stale/다른 scene/phase 거부·generation 폐기·rollback을 검증한다.
Debug/Release 모듈 빌드와 P1 회귀도 확인한다. PhysXCooking_64.dll을 probe 배포에 추가했다.
Cook/BodyCreate/BodyDestroy/Raycast/Sweep/Overlap 이벤트를 실제 profiler capture에서 확인하며
종료 후 complete=1/unacked=0/droppedEvents=0이다. ASan은 엔진/probe 소스를 계측하며 외부 SDK DLL은 비계측이다.
**P2 저수준 API 범위 완료.** 다음은 P3 커맨드·이벤트·CCT와 tick/task 계측 스키마다.
제품 Scene/컴포넌트/C#/Editor/파일 이전, batch query 읽기 창, 실제 제품 성능은 후속 게이트다.

## P3 실행 기록 (2026-10-01, 저수준 API 범위 완료)

첫 작업은 살아 있는 SDK 워커의 유휴 캡처 경계다. profiler_service::publish_thread는
현재 producer만 자기 꼬리를 봉인하며 새 이벤트·엔진 프레임·암묵적 스레드 등록을 만들지 않는다.
dispatcher는 각 워커가 유휴 대기로 들어가기 전 자기 꼬리를 봉인한다. drain은
SDK outstanding=0뿐 아니라 모든 참여 워커의 active batch=0도 기다리므로,
finish_step이 반환할 때 유휴 워커에 완료 작업의 미공개 꼬리가 남지 않는다.
collector가 다른 스레드의 writer를 만지지 않으며 유휴 폴링이나 추가 스레드도 없다.

verify-physics-p1.ps1 -Configuration All: Debug/Release 각각 523개 검사, Shipping 4개 검사.
Debug/Release에서 씬과 워커를 유지한 채 sleep 없이 step 완료 직후 pause/capture를 20회 반복하여
complete=1/unacked=0, 해당 프레임의 실제 PhysXTask 이벤트와 droppedEvents=0을 확인했다.
최초 Release 검사에서 다른 워커가 마지막 큐 작업을 가져가는 경쟁을 검출했으며,
각 워커의 active batch 봉인까지 drain에 포함한 뒤 재검증을 통과했다.
verify-profile-core.ps1 -Configuration All -Only close-open-scopes: 각각 474개 검사와
선택 변이 1/69 통과. publish_thread의 암묵적 등록 금지와 열린 스코프 보존을 확인한다.
verify-physics-p2.ps1 -Configuration All -RequireGpu 재검증: Debug/Release/ASan 각각 1,065개,
Shipping 366개 검사와 실제 CPU/GPU 실행 통과. 외부 SDK DLL은 ASan 비계측이다.
로그: Build/Obj/Phase19P1/p3-worker-publication.log, p3-profile-core.log 및
Build/Obj/Phase19P2/p3-worker-regression.log. 제품 전체 빌드 통과를 의미하지 않는다.

이후 tick/task 상관 스키마와 PhysicsTick 기반 계층을 구현했다(아래 기록).
명령·이벤트·불변 스냅샷·CCT와 수치 계측을 후속 완료했다(아래 최종 게이트).
P3 API의 B/C/T 선행 조건을 충족한다. RHI 미응답 문제는 별도 보류다.

### P3 tick/task 계측 (2026-10-01)

status.last_tick의 강한 tick_id, PhysicsTick -> SimulateSubmit/FetchWait 계층,
전용 physics_worker track과 TaskSubmit/PhysXTask/TaskComplete의 session/tick/task ID를 구현했다.
CPU ID는 GPU submission/view/queue와 분리한다. 실제 CPU wall time이며 GPU solver timing은 아니다.
진행 중 파괴도 finish_step을 통해 스코프를 닫는다. 잘못된 호출은 tick을 소비하지 않는다.

.ceprof writer는 version 2, frame/thread chunk version 2다. reader는 v1/v2를 지원한다.
native 이벤트 64 bytes/page version 2, wire 이벤트 v1=38/v2=62 bytes다.
기존 고정 byte 메모리 예산의 보존 이벤트 수는 줄어든다. scope 시작 ID를 저장하고
중첩 TLS 컨텍스트 복원과 pause truncation에서도 보존한다. Shipping에는 컨텍스트 TLS/profiler 호출이 없다.
Timeline tooltip에 CPU ID를 표시하고 summary 도구는 v1/v2와 CPU ID 개수를 해독한다.

P1 All: Debug/Release 각각 34,305개 검사, Shipping 4개 검사 통과.
살아 있는 씬 즉시 캡처 각 20회, tick 증가, task ID별 submit/run/complete 정확히 1회,
complete=1/unacked=0/drop=0, 캡처 재로드의 CPU ID/worker track 보존을 확인한다.
profile core: Debug/Release 각각 498개 검사 및 선택 변이 4개 통과(전체 71개 중 선택).
원래 v1 writer의 이벤트를 보존한 1,028-byte fixture 읽기, 64-bit ID와 잘린 스코프/중첩 복원을 검증한다.
ProfilerTimeline.cpp 단독 컴파일과 summary v1/v2 실행을 확인한다. 실제 제품 UI 실행은 미검증이다.
Physics 모듈 Debug/Release 빌드도 통과했다. 구 소비자 이전이 남아 제품 전체 빌드 통과를 주장하지 않는다.
P2 All -RequireGpu 회귀: Debug/Release/ASan 각각 1,065개, Shipping 366개 검사와 실제 CPU/GPU 통과.
로그는 Phase19P1/p3-context*.log와 Phase19P2/p3-context-regression.log다.

### P3 명령·이벤트·불변 스냅샷 (2026-10-01)

이동 전용 소유 명령과 bounded 다중 생산자 큐를 구현했다. begin_step에서 입력 tick을 닫고
(tick,producer,sequence) 순으로 적용하며 미래 요청·중복·지연·개별 실패 outcome을 구분한다.
전체 바디 정의 교체는 실패 시 기존 바디를 보존하고 성공 시 새 handle 대응을 반환한다.
SDK 접촉/센서 callback은 body/shape 신원과 접촉점을 값으로 복사하며 삭제 후 종료 이벤트의
구 generation을 보존하기 위해 actor/shape 소유자를 fetch까지 유예한다.
16개 접촉점/이벤트와 bounded 이벤트 버퍼의 손실·필요 용량은 명시 통계다.

fetch/drain 뒤 active poses/명령 결과/이벤트를 불변 스냅샷으로 발행한다. bounded buffer pool은
배열을 재사용하고 독자가 오래 보유하면 tick 종료 전 capacity_exceeded를 반환한다.
새 control block으로 이전 weak reader의 부활을 막고 Scene 종료 후 스냅샷 값 수명을 유지한다.
terminal fetch 실패는 진단 스냅샷과 future 명령 취소를 발행하며 이후 SDK 작업을 거부한다.
CommandCommit/EventCollect/ActivePoseCollect/SnapshotPublish와 독립 SnapshotPrepare를 실제 캡처한다.
상세 의미와 제약은 [API 계약](../design/PhysicsAPIContract.md)의 P3 절을 따른다.

verify-physics-p3.ps1 -Configuration All -RequireGpu: Debug/Release/ASan 각각 669개 검사,
Shipping 641개 검사와 실제 CPU/GPU 실행 통과. 동시 4 producer/100명령, 스냅샷 독자,
접촉 begin/persist/end·센서 enter/exit·삭제/슬롯 재사용·손실·형상 교체·force 4종·실패 경로를 검증한다.
ASan은 엔진/probe만 계측하며 외부 SDK DLL은 비계측이다. fetch 실패 주입은 실제 GPU 장애 재현이 아니다.
P1 회귀: Debug/Release 각각 35,350개 검사/task 4,160개, Shipping 4개 검사/task 51개 통과.
P2 회귀: Debug/Release/ASan 각각 1,065개, Shipping 366개 검사와 실제 CPU/GPU 통과.
Physics 모듈 Debug/Release 빌드도 통과했다.
로그: Build/Obj/Phase19P1/p3-pool-all.log, p3-pool-runtime.log, p3-pool-module-*.log,
Build/Obj/Phase19P2/p3-pool-regression.log. 프로브 출력/캡처는 Build/Obj/Phase19P3/<configuration>이다.
검증 스크립트 compiler PDB를 각 출력 디렉터리로 분리하여 서로 다른 gate의 ASan 빌드 경합을 제거했다.

이 시점의 CCT·수치 계측 잔여는 아래 최종 게이트에서 후속 완료했다.
현재 API begin/finish는 호출자가 지정한 dt를 실행한다. 제품 고정 스케줄링·컴포넌트·C#/Editor 이전과
전체 제품 실행은 아직 검증하지 않았다. 계획 공수 8인일과 전체 68인일은 유지한다.

### P3 capsule CCT (2026-10-01)

강체에 의존하지 않는 character_desc/move/state, typed registry와 RAII manager/controller를 구현했다.
생성/이동/teleport/삭제의 owner/phase 검증과 stamped command를 연결했다. SDK CCT는 query-only capsule이며
중력·점프·속도 적분·동적 바디 push·캐릭터 센서 정책은 C0/C1의 상위 이동 기능으로 남는다.
이동 입력은 변위(m)/시간(s), 위치는 중심, 높이는 원통 구간, 경사는 cosine이다.
마지막 move 충돌 flag와 실제 변위/발 위치를 반환하고 완료 tick의 불변 캐릭터 목록에 복사한다.
캐릭터 간 및 body 양방향 필터, body 쿼리의 CCT 프록시 제외, controller→manager→scene 해체를 검증한다.

P3 All -RequireGpu: Debug/Release/ASan 각각 989개, Shipping 951개 검사 통과.
CPU/GPU에서 바닥·벽·천장·낮은/높은 계단·센서/필터·CCT 간 충돌,
명령 생성/이동/teleport/삭제·owner/phase·rollback·stale/generation 소진·반복 수명·in-flight 해체를 확인한다.
CharacterMovement/PoseCollect의 tick 연관 scope를 실제 캡처한다. GPU solver 시간 측정 증거는 아니다.
전체 경사/점프/이동 정책과 제품 저작 회귀는 C1/M에서 검증한다. 외부 SDK DLL은 ASan 비계측이다.
로그: Build/Obj/Phase19P1/p3-cct-all.log, p3-cct-runtime.log, p3-cct-module-*.log,
Build/Obj/Phase19P2/p3-cct-regression.log. P3 각 구성의 capture/result도 새로 생성했다.
이 시점에 남았던 수치 counter 배선은 아래 최종 게이트에서 후속 완료했다. 8인일 추정은 유지한다.

### P3 수치 계측 및 저수준 완료 게이트 (2026-10-01)

35개 고정 Physics counter를 실제 등록/변경/쿼리/입력 거부/SDK task/발행 지점에 연결했다.
전체 수량은 등록/해제로 O(1) 갱신하며 tick마다 전체 바디를 순회하지 않는다.
표본은 completion engine frame과 scene/tick을 모두 보존하여 동일 프레임의 다중 씬/틱이 덮어써지지 않는다.
버퍼 용량·생애 최대치·구간 증분·tick 결과의 단위/리셋/제외 범위는 [API 계약](../design/PhysicsAPIContract.md)에 명시한다.
query scratch와 API publication 배열을 계상하며 SDK/GPU heap을 계측했다고 주장하지 않는다.
Physics UI에 Scene/metric 선택과 tick 그래프, 카운터 토글을 연결했고 Python summary도 표본 소유 정보를 읽는다.
.ceprof counter chunk v2는 reader v1 호환과 64-bit 소유 정보 왕복을 검증한다.

P3 All -RequireGpu: Debug/Release/ASan 각각 3,971개, Shipping 987개 검사 통과.
Release capture: 79 engine frames/84 scene-ticks, complete=1/unacked=0, event/counter drop=0.
P1 회귀: Debug/Release 각각 35,768개/SDK task 4,160개, Shipping 4개/task 51개 통과.
P2 회귀: Debug/Release/ASan 각각 1,065개, Shipping 366개와 실제 CPU/GPU 통과.
Profile core 최종 Debug/Release 각각 504개 검사와 owned-counter 선택 변이 2개/전체 73개 통과.
별도 선택 실행에서 기존 counter mask/vocabulary 변이 2개도 통과했다. 전체 73개 실행을 주장하지 않는다.
Physics/EngineDiagnostics 모듈 Debug/Release 및 ProfilerTelemetry/ProfilerWindow translation unit 컴파일 통과.
로그: Build/Obj/Phase19P1/p3-counter-all.log, p3-counter-core*.log, p3-counter-runtime.log,
p3-counter-ui.log, p3-counter-module-*.log, p3-counter-diagnostics-*.log,
Build/Obj/Phase19P2/p3-counter-regression.log. fresh summary는 Phase19P3/Release 아래에 보존한다.

**P3 저수준 API 범위 완료.** 스텝 경계·소유 명령·이벤트·CCT·불변 결과·프로파일러 배선의 단독 게이트를 통과했다.
다음은 B0 PhysicsBodyComponent/Scene 연결이며 B/C/T 착수의 P3 API 선행 조건을 충족한다.
제품 fixed tick 누산·0/N tick/catch-up 계측은 B/T/N3 연결 시 검증한다. 실제 UI 실행,
제품 회귀·계측 on/off 비용·p99·메모리 비교는 M3이며 단독 프로브로 제품 완료를 주장하지 않는다.
RHI freeze 미응답은 사용자 지시에 따라 별도 보류한다. 추정 8인일/전체 68인일은 유지한다.

## B0 실행 기록 (2026-10-01, 에디터 Play/Stop 경계 구현 중)

`PhysicsBodyComponent`와 Scene 소유 `ScenePhysicsSimulation`을 추가했다. 런타임 바인딩/SDK
핸들은 비직렬화이며 편집 모드에는 SDK scene/body가 없다. 현재 저작 표면은 box 기본 형상,
운동 종류·질량·중력·감쇠·초기 속도이며 compound/cooked 저작은 B1에 남는다.
신규 UUID·생명주기·typed 직렬화/Inspector·Editor Physics 분류·프로젝트 편입을 연결했다.
Scene의 구 매니저/콜라이더 등록표·전량 배선·캐릭터 시스템 호출을 제거했다.

Play는 **문서/기존 DDOL 지정 백업 → 새 물리 세션 준비 성공 → Simulating → 진입 통지**다.
준비 실패는 부분 SDK 세션을 폐기하며 에디터 Transform과 진입 통지를 변경하지 않는다.
물리는 committed Play이며 Pause/구조 전이 중이 아닌 때만 진행한다. 현재 세션 누산기는
60 Hz, 프레임당 최대 4 tick, 초과 시간 폐기/집계를 사용한다. 이는 물리 경계의 구현이며
관리 Pre/PostPhysics·네트워크 N3와 공통 고정 tick 스케줄러의 완료 증거가 아니다.
완료 active pose를 기존 Transform world-write batch에 반영하고 마지막 tick 문맥으로 계측한다.
B2 보간/kinematic target/static 명시 변경과 부모 변환 제품 회귀는 별도 잔여다.

2026-10-01 B0 바디 제어를 연결했다. 이동/회전 축 잠금을 반사 저작값으로 저장하고,
none/x/y/z/xy/xz/yz/all 조합을 지원한다. ReadState, SetVelocity, ApplyForce는 씬 membership을
통해 SDK에 접근한다. 속도 단위는 m/s와 rad/s이며 force/impulse/acceleration/velocity_change를
지원한다. 실행 중인 활성 dynamic 바디만 변경하고, 소유 스레드·idle 경계를 요구한다.
편집 상태/비활성은 wrong_phase, 제거된 binding은 stale_handle, 비유한 값/비동적 바디는
invalid_argument로 거부한다. 실행 제어는 최초 저작값을 변경하지 않는다. kinematic target과
pose 변경의 Transform 동기화는 B2, 관리 스크립트 엔트리는 M0에서 연결한다.
축 잠금 및 실제 질량에 따른 충격량, 비활성/삭제/다른 스레드/in-flight 거부를 독립 SDK로 검증했다.
반사 재생성 로그 b0-controls-reflgen.log, 제품 네 TU 로그 b0-controls-product-tus.log.

2026-10-01 Player/Editor 실행 정책을 분리했다. 기본값과 Player 진입점은 runtime이며
백업·복원·백업 폐기 콜백을 전혀 호출하지 않는다. Editor 진입점만 editor_restore를 선택하며
문서 백업 성공 후 SDK를 시작하고, 복원 실패 시 원본 백업을 보존한다.
일반/즉시/비동기 씬 활성화는 이전 SDK 세션을 먼저 종료하고 목적 씬 세션을 재시작한다.
DDOL 바디는 현재 선형/각속도를 값으로 이송하고 목적 씬에서 새 scene-scoped handle을 받는다.
같은 씬 재활성화는 해체 없이 종료한다. 목적 SDK 시작 실패는 committed를 내리고 다음
구조 경계에 Stop을 요청한다. Editor의 최초 백업은 씬 이동 중 다시 만들지 않는다.

Stop은 **SDK fetch/drain 및 소유자 해제 → DDOL을 포함한 플레이 객체/루트의 추가 컴포넌트
해체 → 원래 루트/객체/컴포넌트/Transform·루트 형제 순서·DDOL 지정 복원 → 성공 통지**다.
DDOL은 일반 씬 이동에서 보존되지만 에디터 Stop에서는 Play 직전 정의로 재생성한다.
DDOL 비소유 포인터는 실제 해제 전에 제거한다. 복원 시 알 수 없는 타입/역직렬화 예외를
실패로 처리하고, 성공해야 백업을 해제한다. 실패 시 백업과 미완료 복원 상태를 보존한다.

`verify-physics-b0.ps1 -Configuration All -RequireGpu`는 실제 SDK를 사용해 Debug/Release/ASan
각 **4,381**, Shipping **4,377** 검사를 통과했다. 각 구성에서 CPU 32회/GPU 4회 반복
Play/Stop, 실제 중력, 편집 시 무시, 재생 초기화, 비활성/재활성 속도, 플레이 중 생성/삭제,
실패한 준비의 롤백, 최대 catch-up, 외부 스레드 거부, **simulate 진행 중 Stop**,
Stop 후 독자 스냅샷 수명을 검증했다. 실제 GPU backend를 확인했다.
이전 B0 capture(`b0-all.log`)는 512 engine frames/620 counter scene-ticks, complete=1, unacked=0,
event/counter drop=0이며 `Physics.PlayStart/PlayStop` 실행 계층을 확인했다.
증거: `Build/Obj/Phase19P1/b0-controls-all.log` 및 최종 Debug `b0-controls-debug.log`, `Build/Obj/Phase19B0/<configuration>/`.

변경한 Scene/SceneManager/PhysicsBodyComponent/ScenePhysicsSimulation 네 TU를 현재 생성
reflection 주입과 함께 Debug 컴파일했다(`b0-player-product-tus.log`). 전체 SceneRuntime 빌드는
모델/기즈모 등 미이전 소비자에서 실패했다(`b0-runtime.log`). Owners gate는 통과,
EditorMain/PlayerMain 두 진입점도 Debug 컴파일했다(`b0-host-tus.log`).
실제 구 소비자는 10파일이다. 새 CharacterMovement profiler 문자열의 감사 오탐도 제거했다.
**실제 에디터 HTTP 저작 → Play → Stop → 저장 문서/Transform/생명주기 대조는 미검증**이며
M0 제품 배선 이후 필수 게이트다. 씬 전환·DDOL 상태 이전/물리 재시작은 배선 및 독립 SDK
검증을 했으나 실제 제품 경로와 Editor 씬 이동 후 원래 씬 메타데이터 복원은 M2 잔여다.
완결된 제약 저작/실행 제어, 이벤트 전달 및 M1 파일 왕복도 아직 완료로 판정하지 않는다.
B0 상태는 progress로 유지한다. 기존 P0 바이너리로 신규 구현을 검증했다고 주장하지 않는다.

## 0. 확정 방향과 범위

**기존 PhysX 래퍼 계층을 전면 교체하고, C++23 기반 PhysX API를 먼저 완성한다. 그 API 위에 새 컴포넌트와 씬 연결을 구현한다.**

- 이번 백엔드는 **PhysX로 확정**한다. 기존 `PhysicX` 싱글턴, 래퍼 상속 구조, GameObject ID 기반 컨테이너, 전량 push/pull 구조를 최종 실행 경로에 남기지 않는다. PhysX SDK 자체는 유지한다.
- **Jolt 스파이크·비교·도입·어댑터는 이번 리팩토링 범위 밖**이다. 미래 별도 계획으로만 남긴다. 백엔드 선택 게이트 X0/X1은 현재 실행 순서에서 제거한다.
- 저작환경의 익숙함을 구조의 근거로 삼지 않는다. 바디 정의의 완결성, 단일 소유권, 형상 자산 공유, 변경량 기반 처리와 명시적 스텝 계약을 우선한다.
- 명명은 `PhysicsScene`, `PhysicsBodyComponent`, `CharacterMovementComponent`, `CollisionGeometry`, `ShapeInstance`를 사용한다. `PhysicsWorld`와 `CharacterMotorComponent`는 사용하지 않는다.
- 기존 씬·프리팹·스크립트 호환은 **마이그레이션 경계**에서 처리한다. 구 필드명이나 컴포넌트 분할을 새 런타임의 제약으로 삼지 않는다.
- **빅뱅 교체**: 구 물리 계층과 새 API를 병렬 배선하지 않는다. 호환 어댑터·구 경로 fallback·선택 스위치·dual-write를 만들지 않는다. 작업 중 제품 빌드가 일시적으로 깨지는 것은 허용하되 최종 통합 변경은 모든 필수 게이트를 통과해야 한다.
- 이번 변경은 계획이다. 구현·성능·회귀 완료를 뜻하지 않는다. 아래 과거 실측의 줄 번호와 소비자 수는 현재 기준선으로 다시 계측한다.

## 현재 생명주기·실행 기반 대조 (2026-10-01)

아래 초판 실측의 경로·줄 번호·소비자 수는 역사적 기준선이다. 현재 물리 컴포넌트와
PhysicsManager는 `Engine/SceneRuntime/`, 백엔드는 `Engine/Physics/`에 있다.

- 신규 컴포넌트 초기화는 `OnInitialized → OnAddedToScene → OnBeginSimulation`,
  파괴는 `OnEndSimulation → OnRemovingFromScene → OnUninitializing` 순서다.
  DDOL 이송은 별도의 소속 제거/재편입 경로이며 초기화를 반복하지 않는다.
  기존 등록 훅은 이 구조와 맞는다. 새 연결 수명 설계에서는 시뮬레이션 종료,
  컴포넌트 해체, 씬 소속 제거를 구분하고 중복 파괴 없이 멱등적으로 처리해야 한다.
  컴포넌트 가상 틱은 은퇴했으므로 CCT 스텝은 `CharacterControllerSystem::FixedUpdate`
  경로를 유지한다. LifecycleRegistry는 훅 판정표이며 물리 시뮬레이션 소유자가 아니다.
- AI는 `std::async`/future가 아니라 공용 enkiTS `job_scheduler`의 `job_handle`이다.
  `Scene::DrainAIUpdate`는 토큰을 회수하고 `wait()`한다. EndFramePass의 실제 파괴 전,
  DDOL 분리 및 씬 해체 경계의 대기는 이미 있다. 그러나 FixedUpdate 입구는
  `valid() && is_complete()`일 때만 회수하므로 **구 실행 경로의 Z0는 미해결이며 빅뱅 재작성의 필수 회귀 조건**이다.
  파괴 수명 보호와 다음 프레임 물리/Transform의 읽기·쓰기 보호를 혼동하지 않는다.
- Z2의 static 스케일 조회는 이미 `staticBody->GetScale()`이다. 추가 수정 대상으로
  중복 산입하지 않으며 static 바디 실행 회귀는 별도로 확인한다.
- T1은 옛 `WorkerPool::Enqueue/NotifyAllAndWait`를 대상으로 구현하지 않는다.
  현재 `job_scheduler`는 개별/그룹 대기와 의존 제출을 지원한다.
  이번에는 PhysX 실행 경계와 공용 스케줄러의 결합만 검증한다. 미완료 토큰의 워커
  내부 대기는 금지한다. 상세 계약은
  [JobSchedulerDesign](../design/JobSchedulerDesign.md)을 따른다.
- A0의 Terrain 삭제 판정은 보류한다. `PhysicsManager::AddCollider(Terrain)`은 실제
  heightfield 정적 바디를 만든다. 동기화 분기의 `continue`만으로 전체 경로를 죽은
  자산으로 판정할 수 없다. reflgen 등록·LifecycleRegistry·타입 UUID·기존 씬 참조를
  현재 소스에서 확인한 뒤 삭제/이전 범위를 정한다.

이번 대조는 생명주기 코드와 계획의 정합성 확인이다. 물리 장시간 구동·누수 및
기존 게임 플레이 회귀 통과를 의미하지 않는다.


---

## 1. 실측 — 2026-08-18 전수 조사

### 1.1 자산 지도

| 계층 | 위치 | 규모 | 내용 |
|---|---|---|---|
| L1 백엔드 래퍼 | `Physics/` (독립 vcxproj) | **5,979줄** | `PhysicX` 싱글턴(`Physx.cpp` 2,597줄 단일 파일), RigidBody 3종, CCT+Movement, Ragdoll 3종, Resource 3종, EventCallback |
| L2 컴포넌트 | `ScriptBinder/` | **~3,100줄** | `PhysicsManager` 싱글턴(267+1,100), 컴포넌트 8종 |
| L2.5 소유권 | `Scene.cpp:1525-1810`, `2147-2231` | ~370줄 | 콜라이더 7개 병렬 벡터 + `m_colliderContainer` + 생성 콜백표 |
| L3 스크립트 경계 | `ScriptCore/*.cs` + `ClrHost.cpp` | 374줄 + API 45개 | C# 표면 |

합계 **약 9,500줄**. PhysX 5.5.0 (vcpkg).

### 1.2 과거 결함 목록 — 새 API가 반드시 제거할 회귀 위험

**Z-①  AI 스레드 ↔ 물리 데이터 레이스 (CRITICAL)**

`Scene.cpp:1271`이 `std::async`로 AI를 띄우고, `Scene.cpp:1135`가 다음 프레임에 `wait_for(0s) == ready`일 때만 회수한다. **안 끝났으면 기다리지 않고 물리 스텝으로 진행한다.** 그런데 BT 액션이 AI 스레드에서 물리를 직접 만진다:

- `ChaseAction` · `Idle` · `WaitAction` → `CharacterControllerComponent::Move()`
- `DamegeAction` → `StopForcedMove()` → `Physics->StopForcedMoveOnCCT()` (`CharacterControllerComponent.cpp:391`) — PhysicX의 `m_characterControllerContainer`를 직접 변경
- `MageActtack` → `RigidBodyComponent` 접근

메인 스레드는 같은 시각 `PhysicX::Update`에서 그 컨테이너를 순회하며 `simulate()`를 돌린다. `sceneDesc.flags`에 `eREQUIRE_RW_LOCK`은 **없다**. 보호되지 않은 레이스다.

**Z-②  `CollisionData` 누수**

`new CollisionData()` 4곳(`Physx.cpp:359, 957, 995, 1656`). `delete`는 **생성 실패 경로에만** 있다(`:964`, `:1002`). 성공 경로는 `RemoveCollisionData`가 id를 목록에 넣고 `Update`가 맵에서 `erase`만 한다 — 해제 없음. 바디마다 하나씩, 씬 전환마다 누적.

**Z-③  `GetRigidBodyData` static 분기 널 역참조**

`Physx.cpp:1124` — `staticBody` 분기 안에서 `dynamicBody->GetScale()`. 그 시점 `dynamicBody`는 반드시 `nullptr`이다. 현재는 `GetPhysicData`가 DYNAMIC만 호출해 안 터지고 있을 뿐인 지뢰.

**Z-④  CUDA 실패 시 폴백 부재**

`Physx.cpp:199-210`이 CUDA 초기화 실패 시 `m_cudaContextManager = nullptr`로 두고 *"continuing without CUDA"* 로그만 찍는데, `:244-251`의 `eENABLE_GPU_DYNAMICS` + `broadPhaseType = eGPU` 플래그는 **그대로 남는다**. 그리고 `createScene` 반환값을 널 검사 없이 `PxCreateControllerManager(*m_scene)`에 역참조한다. 폴백이라 부를 수 없다.

**Z-⑤  솔버 워커 8 하드캡**

`Physx.cpp:165-172`:

```cpp
UINT MaxThread = 8;
UINT core = std::thread::hardware_concurrency();
if (core < 4) core = 2;
else if (core > MaxThread + 4) core = MaxThread;   // 12 초과면 무조건 8
else core -= 4;
```

32스레드 워크스테이션에서도 8개만 쓴다.

**Z-⑥  `AngularDamping`이 직렬화되지 않는다**

`RigidBodyComponent::reflect()`에 `LinearDamping`은 있는데 `AngularDamping`이 **빠져 있다**. 인스펙터에서 각 감쇠를 조정해도 저장되지 않고 로드 시 기본값(0.05)으로 되돌아온다. 한 줄 누락이고 저작 데이터가 지금 조용히 유실되고 있다.

**갱신(2026-09-30, reflgen 전환)**: 레시피는 사라졌고 누락은 그대로 옮겨졌다 — 전환 codemod가 레시피에 없던
필드에 `[[reflgen::ignore]]` 를 붙였으므로 `RigidBodyComponent.h` 의 `AngularDamping` 에 `ignore` 가 달려 있다.

### 1.3 구조적 결함 — 재작성이 뒤집어야 할 것

| # | 결함 | 근거 |
|---|---|---|
| S-1 | **소유권 4분할.** PhysX 액터=`PhysicX::m_rigidBodyContainer`, 컴포넌트=Scene 7개 병렬 벡터, 등록표=`Scene::m_colliderContainer`, 변경 큐=`PhysicsManager::m_pendingChanges`. 파괴 단일점 없음 — `bIsDestroyed`를 세 군데서 각자 검사 | `Scene.h:420-432` |
| S-2 | **ID = GameObject InstanceID → 오브젝트당 콜라이더 1개.** `m_ColliderTypeLinkCallback`이 `unordered_map<GameObject*, ...>`이고 `insert`라 두 번째 콜라이더가 조용히 무시된다 | `Scene.h:420`, `Scene.cpp:1566` |
| S-3 | **전량 푸시/풀.** `SetRigidBodyData`가 매 프레임 모든 바디에 `setActorFlag`×2, `setRigidBodyFlag`, 셰이프 순회 + `setFlag`×2, `setLinearVelocity/Angular`를 **조건 없이** 호출. `isDirty`는 mass/damping/lock 묶음에만 걸림. `getShapes`용 `std::vector` 힙 할당이 바디마다 프레임마다 | `Physx.cpp:1156-1284`, 할당 `:1206` |
| S-4 | **핫패스 RTTI.** `GetRigidBodyData`·`SetRigidBodyData`·`PhysicX::Update` 전부 `dynamic_cast<DynamicRigidBody*>`로 분기 | `Physx.cpp:1058, 1180, 330` |
| S-5 | **RigidBody 없는 콜라이더는 죽는다.** `SetPhysicData`가 `rigidbody == nullptr`이면 `continue`. "콜라이더만 = 정적 지오메트리" 개념이 없어 바닥 하나에도 RigidBody가 필요 | `PhysicsManager.cpp:742` |
| S-6 | **CCT가 RigidBodyComponent에 기생.** CCT 속도를 `rigidbody->GetLinearVelocity()`로 읽고 되돌려 쓴다. CCT는 리지드바디가 아니다 | `PhysicsManager.cpp:795, 924` |
| S-7 | **충돌 매트릭스 2중 진실.** `PhysicsManager`는 `vector<vector<uint8_t>>`(32×32), `PhysicX`는 `unsigned int[32]`. setter 시점에만 동기화, 레이어 수 32 하드코딩 | `PhysicsManager.h:145`, `Physx.h:188` |
| S-8 | **계층 침범.** ScriptBinder 12개 헤더가 `../Physics/Physx.h` / `PhysicsCommon.h`를 직접 include → 유니티 빌드에서 PhysX 헤더가 엔진 전역에 전이 | 12개 파일 |
| S-9 | **싱글턴 접근자가 헤더의 `static auto`.** `static auto Physics = PhysicX::GetInstance();`(Physx.h 말미), `static auto PhysicsManagers = ...`(PhysicsManager.h 말미) — TU마다 사본 | 헤더 말미 |
| S-10 | **생성이 3단계 지연.** `OnInitialized` → `Scene::CollectColliderComponent`(정보만 채우고 람다 저장) → 다음 `FixedUpdate`의 `SetInternalPhysicData`가 람다 호출. `PhysicsManager::AddCollider`는 이름과 달리 아무것도 만들지 않고 오프셋만 계산 | `PhysicsManager.cpp:430` |
| S-12 | **되쓰기가 분해→합성→재분해를 돈다.** `GetPhysicData`가 scale·quat·pos로 행렬을 조립(`CreateScale`×`CreateFromQuaternion`×`CreateTranslation`, 곱 2회)한 뒤 `SetAndDecomposeMatrix`에 넘기고, 그 안에서 행렬 비교(16 float) → `XMMatrixDecompose`(축마다 sqrt) → `XMVector4Normalize` → 부모 조회를 한다. **이미 분해된 형태로 들고 있던 데이터를 옮기려고** 동적 바디마다 프레임마다. 물리는 스케일을 바꾸지 않으므로 스케일은 왕복할 이유조차 없다 | `PhysicsManager.cpp:955-962`, `Transform.cpp:364` |
| S-11 | **주석 소실.** `PhysicsManager.cpp`·`Physx.cpp` 상당 부분이 이중 mojibake(`占쏙옙`)로 **복구 불가**. 기존 의도를 주석에서 읽어낼 수 없다 | — |

부수: `OnUnloadScene`이 언로드 중에 `Physics->Update(1.0f)` — **1초짜리 스텝을 한 번 시뮬레이션**한다(`PhysicsManager.cpp:107`).

### 1.4 죽은 자산

| 자산 | 상태 |
|---|---|
| `ColliderDebugData.h` | 참조 0. 유일한 함수가 static 아니면 `throw`로 끝남 |
| `PhysicsDebug.cpp` | **0바이트** |
| `RagdollComponent` | 필드 1개, `ICollider` override 전부 빈 몸통. 리플렉션·Lifecycle에는 등록됨 |
| `Physics/Ragdoll*` 3종 (736줄) | 컴포넌트가 비어 있어 도달 불가. `PxArticulation*` 15회 사용처가 전부 여기 |
| `TerrainCollider` 경로 | `SetPhysicData`가 heightField id면 데이터 읽고 즉시 `continue` |
| `TriangleMeshResource` | `CreateStaticBody(TriangleMesh…)` 호출자 0 |
| GPU/CUDA 경로 | 켜져 있으나 실측된 적 없음. Z-④ 참조 |
| `eENABLE_ACTIVE_ACTORS` | 켜두고 활성 액터 목록을 안 씀(전수 순회 유지) |
| `PxDeformableSurface` | 코드 0줄. **주석 5곳에만** 존재 |

### 1.5 병렬화 가능성 — 층별 판정

| 층 | 백엔드 지원 | 현재 | 재작성 후 |
|---|---|---|---|
| ① 솔버 내부 병렬 | ○ | **켜져 있음**, 워커 8 하드캡 | ○ 즉시 (Z-⑤) |
| ② GPU 다이내믹스 | PhysX ○ / Jolt ✕ | 켜놓고 폴백 없음 | 백엔드 판정에 종속 |
| ③ simulate↔게임로직 오버랩 | PhysX ○ / Jolt은 잡 모델 | **전혀 안 씀** | 백엔드에 따라 형태가 다름 |
| ④ 씬 쿼리 병렬 | ○ (락 정책 필요) | 안 씀 | ○ |
| ⑤ 동기화 루프 병렬 | — | **자료구조가 막음** | ○ (트랙 B 선결) |
| ⑥ 외부 스레드 접근 | ○ (락 필요) | **락 없이 하고 있음** ⚠ | 계약으로 봉인 (T0) |

③에 대한 주의: 현재 `simulate()`와 `fetchResults(true)` 사이에 아무것도 없어(`Physx.cpp:442-451`) 메인 스레드가 시뮬레이션 내내 논다. 그런데 **Jolt에는 이 분할이 없다** — `PhysicsSystem::Update()`는 잡 시스템 위에서 내부 병렬로 돌지만 호출은 동기다. 백엔드 선택이 프레임 구조 설계를 바꾼다.

### 1.6 소비자 표면 — 재작성이 깨뜨릴 것

- **C++ 게임 스크립트**: `GetComponent<CharacterControllerComponent>` 56, `RigidBodyComponent` 17, Sphere/Box 콜라이더 9 → **82곳**
- **PhysicsManager 직접 호출**: `SphereOverlap` 16, `BoxSweep` 3, `CapsuleSweep` 1, 기타 4 → **24곳**
- **C# 경계**: Native 테이블 물리 항목 45개 (Cct 17 · Rigid 21 · Collider 12 · 질의 3)
- **직렬화**: 컴포넌트 8종이 `RegisterReflectManual.h`·`ComponentTypeUUID.h`에 등록, 기존 `.creator` 씬/프리팹이 이 필드명으로 저장됨

`Colliders.cs`가 이미 *"트리거 여부와 콜라이더 켜고 끄기는 여기가 아니라 RigidBodyComponent에 있다(엔진 구조가 그렇다)"* 고 기형을 주석으로 기록해 두었다 — 재설계에서 정상화할 지점.

---

## 2. 목표 구조

```text
Engine/Physics/
  PhysicsTypes                  값 타입·핸들·오류·설정
  PhysicsScene                  씬 하나의 물리 실행·수명 소유
  CollisionGeometry             불변 지오메트리와 cooked 자산
  ShapeInstance                 로컬 pose·재질·필터·solid/sensor·ShapeId
  Body records                  엔티티/바디 연결·세대·수명
  Character records             캐릭터 이동 상태
  PhysicsCommand / PhysicsEvent 변경 요청·스텝 결과
  backend/physx/                SDK 헤더·RAII 소유·cook·dispatcher

Engine/SceneRuntime/
  PhysicsBodyComponent          완결된 바디 정의 + BodyHandle
  CharacterMovementComponent    캐릭터 정의 + CharacterHandle
  Scene 연결 코드               생명주기·Transform·이벤트 전달
```

엔진 Scene이 `PhysicsScene`을 소유한다. 별도 공개 PhysicsWorld/PhysicsManager 연결 계층을 두지 않는다.
물리 모듈은 Scene/Entity/Component 정의에 의존하지 않고, SceneRuntime이 핸들을 엔티티와 연결한다.
백엔드 헤더는 구현에 격리한다. 이는 PhysX SDK를 안전하게 사용하는 엔진 API이며, 다중 백엔드 플러그인 추상화를 만들지 않는다.

강체는 `PhysicsBodyComponent` 하나가 static/kinematic/dynamic 종류, 형상 집합, 질량 특성과 제약을 정의한다.
Box/Sphere/Capsule/Mesh별 컴포넌트를 새 모델의 기본 단위로 두지 않는다. 콜라이더 부재/존재로 바디 종류를 추론하지 않는다.
캐릭터는 별도 `CharacterMovementComponent`로 이동하며 강체 컴포넌트와 동시 부착을 기본적으로 거부한다.
형상은 바디 내부 항목이고, 복잡한 지오메트리는 불변 cooked 자산으로 공유한다.
독립 갱신 센서·자식 엔티티 형상의 바디 귀속 규칙은 B1에서 명시한다.

### B1 형상 목록 저작 연결 (2026-10-01, progress)

`PhysicsBodyComponent`의 단일 `m_halfExtent`를 `m_shapes` 소유 목록으로 교체했다.
반영되는 `PhysicsShapeDefinition`은 바디 내부 ShapeId, Box/Sphere/Capsule 종류와 치수,
로컬 position/rotation, 마찰/반발, sensor/queryEnabled를 저장한다. 저작 경계에서
`BuildPhysicsShapes`가 소유된 SDK `geometry` variant/`ShapeInstance` 목록으로 변환한다.
형상 목록은 Entity 레이어를 상속한다. 값 확인은 ranges와 Mathematics components view,
오류/값 조합은 expected의 transform을 사용한다. `Physics.ShapeAuthoring` marker를 배선했다.

소유 규칙은 명시적이다. 형상은 해당 바디 목록 안에만 있고, 자식 Entity에서 자동 수집하거나
가장 가까운 부모 바디에 편입하지 않는다. 같은 바디에 붙는 hitbox는 해당 목록의 sensor 항목이다.
독립 Entity의 sensor는 독립 바디를 명시적으로 저작한다. 현재 SDK의 sensor-only 바디는
static만 지원하며, 움직이는 바디에는 solid mass shape가 필요하다. 자식 Transform의
연속 갱신/독립 이동 sensor 정책 확장은 이 슬라이스의 완료 주장에 포함하지 않는다.

양수·유한 스케일만 허용한다. 회전 없는 Box는 비균일 스케일을 치수/로컬 위치에 반영한다.
Sphere/Capsule 및 회전된 primitive는 균일 스케일을 요구한다. 타원체·shear를 임의 근사하지 않는다.
0/중복 ID, 미지원 종류, 비유한/overflow 치수·위치, 비단위 quaternion, 잘못된 재질,
움직이는 sensor-only 목록은 발행 전에 거부한다. 부모 Transform의 shear 검출은 B2 잔여다.

`SetShapes`는 새 목록 검증/Scene 정의 변경 성공 후 swap한다. Play 중 저작 변경은 거부하고
기존 정의를 유지한다. 기존 B0 박스 스키마는 fallback하지 않는다. shape schema 1과
`m_shapes`를 요구하는 typed/ComponentFactory 후처리 경로를 추가했다. 구 씬/프리팹 파일의
일회성 변환은 M1에서 닫는다. 실제 프로젝트에 물리 컴포넌트가 없던 P0 기준선은 보존한다.

검증: `Tools/regression/verify-physics-b1.ps1 -Configuration All -RequireGpu` 통과.
Debug/Release/ASan 각 68 checks, Shipping 66 checks, 네 구성 모두 실제 GPU 확인.
compound 각 형상 ID/바디 핸들 query, sensor 기본 제외/명시 포함, Entity query mask,
query-disabled solid, Play 정의 변경 거부, Stop 후 정의 변경/재시작/step/해제를 확인했다.
증거는 `Build/Obj/Phase19B1/{Debug,Release,Shipping,ASan}/{result.jsonl,baseline.ceprof,build.log}`
(Shipping은 profiler 산출물 없음). PhysicsBodyComponent 및 생성 리플렉션 코드의 Debug/Release
제품 TU 컴파일도 통과했다(`Product/compile-{debug,release}.log`). 제품 Editor/HTTP 실행 증거는 아니다.

**B1은 progress다.** 아래 shared cook/source와 생성·revision 수정/Undo Host/CLI까지 구현했다.
BuildTool/CEMF/pak 및 SDK 사전 cook, per-shape 공통 layer override와 명시적 idle 교체는
아래 후속 절에서 연결했다. 실제 Inspector/HTTP 저작·저장/로드·Undo/Prefab/cooked
제품 왕복 검증은 남았다.

#### B1 공유 지오메트리 source/cook 연결 (2026-10-01)

PhysicsShapeKind를 Box/Sphere/Capsule/Convex/TriangleMesh/Heightfield로 확장했다.
형상은 geometryAsset(UUID 문자열 경계), geometryRevision, geometryScale을 저장하고
내부에서는 geometry_asset_key(Uuid16+uint64 revision)로 식별한다. primitive의 기존
값/필드 의미는 유지하며 새 cooked 종류는 실제 자산의 kind와 일치해야 한다.

Scene이 CollisionGeometryLibrary를 소유한다. UUID/revision당 한 번 cook하고 모든
ShapeInstance가 shared_ptr<const CollisionGeometry>를 공유한다. cook용 PhysicsScene은
즉시 해제하며 전용 cook 스레드/상주 cook Scene을 추가하지 않는다. SDK 자산 자체가
SDK 세션을 유지하므로 cache/원본/cook Scene 해제 후에도 바디와 Stop/재시작 정의가 유효하다.

CECG v1 `.cegeometry`는 UUID/revision과 owned convex points, triangle indices,
heightfield samples의 variant를 담는 네이티브 cook **source**다. little endian·크기 제한·
checksum·정확한 payload 길이·유한 입력/indices/dimensions를 검증한다. 디스크에 SDK 포인터나
SDK ABI blob을 저장하지 않는다. 이것을 오프라인 SDK cook/최종 Player 자산 파이프라인 완료로
읽지 않는다. 현재 file reader는 DataSystem의 UUID catalog 경로에서 읽고 expected UUID/revision을
검증한다. Player의 누락/오래된 CEMF를 source 경로로 fallback하지 않는다.

전체 형상 preflight가 loader/cook보다 먼저 실행된다. Publish는 prepared flat_map에서
할당과 SDK cook을 끝내고 Host 게시 성공 후 swap한다. 동 revision의 내용 변경, cook 실패,
Host false/exception/재진입, 다른 owner thread는 기존 cache와 기존 공유 자산을 바꾸지 않는다.
파일 staging/원자적 게시/meta 소유는 Host callback 경계에 두었다. 새 자산 생성 Editor/CLI는
아래 후속 작업으로 연결했다. revision 수정 및 CECG source archive는 아래 후속 작업으로 연결했다. SDK 사전 cook 산출물은 잔여다. Shape SetShapes는
CaptureDefinition 실패 시 기존 바디 저작/Scene 정의를 유지하고 Play 중에는 여전히 거부한다.

GeometryResolve → GeometryPublish → GeometryCook → SDK CookConvex/CookTriangleMesh/
CookHeightfield를 profiler에 노출했다. GeometryCook은 Host 파일 게시 시간을 포함하지 않는다.
cache hits/cook attempts/failures/assets는 owner-only Stats로 읽는다. Scene 범위 카운터를
제품 profiler counter UI/성능 게이트에 합치는 것은 M3 잔여다.

검증: `verify-physics-b1-geometry.ps1 -Configuration All -RequireGpu` 통과.
Debug/Release/ASan 각 741 checks, Shipping 736 checks, 네 구성 모두 실제 GPU 확인.
모든 source 바이트 손상/절단, 유효 checksum의 oversized/trailing/version 실패, 실제 파일
roundtrip/ID·revision 불일치, 한 번 cook 공유, 동 revision 충돌, cook/Host 실패와 재진입,
cache/cook Scene 종료 후 CPU/GPU의 convex·mesh·heightfield query, shared convex dynamic
solver step, Stop/재시작 수명을 확인했다. primitive Release 68 checks도 재확인했다.
증거 `Build/Obj/Phase19B1Geometry/{all.log,primitive-release.log}`와 구성별
`{result.jsonl,build.log,baseline.cegeometry,baseline.ceprof}`(Shipping profiler 없음).
제품 PhysicsBodyComponent/Scene/SceneManager 및 생성 리플렉션 코드의 Debug/Release
TU 컴파일은 `Product/*.log`에 기록한다. 전체 Editor/Player 실행 검증은 아니다.

#### B1 새 지오메트리 자산 생성 Host/CLI (2026-10-01)

`geometry.create <Assets 상대 경로.cegeometry> <convex|mesh|heightfield> <입력.txt>`를
Editor HTTP/CLI 명령 registry와 descriptor에 등록했다. destination의 부모 폴더는 미리 존재해야 한다.
EngineService 자산 생성 명령이며
Undo를 지원한다고 표시하지 않는다. 기존 자산과 sidecar를 덮어쓰지 않는 생성 전용 경로다.

입력은 공백 구분 숫자다. convex는 `pointCount xyz...`, mesh는
`pointCount xyz... triangleCount abc...`, heightfield는 `rows columns signed16Samples...`다.
음수/범위 초과 count, 누락/추가 토큰, 비유한 값, 잘못된 인덱스·차원을 거부한다.
텍스트는 Editor 입력 경계이며 런타임에 새로운 텍스트 parser를 추가하지 않았다.

새 UUIDv4/revision 1을 발급하고 Scene의 owner cache에서 SDK cook을 먼저 검증한다.
EditorAssetDatabase의 watcher mutex 안에서 canonical Assets 경계를 확인하고, staging한
meta를 먼저 게시한 뒤 native CECG 본문을 게시한다. 각 rename은 overwrite 없는
MoveFileExW/WRITE_THROUGH다. 본문 게시 실패는 이번 작업이 만든 meta와 staging을 제거하며
기존 자산/캐시는 보존한다. 기존 파일·고아 sidecar·staging 파일이 있으면 생성 자체를 거부한다.
여러 파일에 대한 전원 차단 시 원자성이나 revision 수정 transaction 완료를 주장하지 않는다.

파일 성공 후 cache commit 및 catalog 알림을 수행한다. catalog 알림 실패는 성공 결과의
`catalogRegistered=false`로 명시하고 durable 파일/cache를 되돌리지 않는다. watcher/재기동이
재등록하며, 본문 UUID와 sidecar UUID가 다르거나 source가 손상되면 등록을 거부한다.
sidecar 최초 생성 시 본문 UUID를 유지한다. Play 중 Scene 게시와 CLI 생성은 거부한다.

독립 검증 `verify-physics-geometry-authoring.ps1 -Configuration All`: Debug/Release/Shipping/ASan
각 47 checks. 입력 거부, 실제 cook→파일→native decode, 기존 자산/sidecar/staging 보존,
meta/본문 rename 실패 복구, cache/기존 파일 보존을 검증했다. 제품 EditorAssetDatabase/
AssetAuthoringCommands/CommandDescriptorSeeds/Scene의 Debug/Release 8 TU 컴파일과
descriptor 정렬 단정 통과. 저작 소유권 SourceOnly와 대시보드 432행 검증 통과.
Physics.GeometryAuthoringPublish를 기존 GeometryPublish 계층 아래 노출한다.
증거는 `Build/Obj/Phase19GeometryAuthoring/{구성/result.jsonl,Product/*.final.log}`다. 실제 Editor HTTP 실행/Undo/Prefab,
기존 revision 수정/보존은 아래 후속 절에서 연결했다. 오프라인 SDK blob 및 CEMF/pak 연결은 아래 후속 절에서 구현했다. 실제 제품 실행 게이트는 남았다.

#### B1 지오메트리 revision 수정·Undo·source archive (2026-10-01)

`geometry.update <Assets 상대 경로.cegeometry> <convex|mesh|heightfield> <입력.txt>`를
Undo 가능한 EditorOperation으로 등록했다. 기존 UUID를 유지하고 현재 source와
`Assets/Derived/CollisionGeometry/<UUID>/<revision>.cegeometry` 기록의 최대 revision+1을
사용한다. uint64 상한·손상 archive·동 revision 내용 충돌은 거부한다. Undo 뒤 새 수정도
이미 게시/보존된 번호를 재사용하지 않는다. revision은 CLI에서 decimal 문자열로 반환한다.

SDK cook과 cache 준비 후 Editor watcher mutex 안에서 현재 source의 canonical bytes와
예상 source를 비교한다. sidecar UUID 및 geometryRevision도 예상 값과 같아야 한다.
기존 sidecar의 다른 필드는 유지하면서 revision만 갱신한다. 수정 전후 source를 불변
archive로 보존한 뒤 live meta/source를 backup하고 staging→meta→source 순서로 게시한다.
각 rename 단계의 일반 실패는 backup에서 기존 파일 쌍을 복구하고 cache를 유지한다.
실패 시 만들어진 유효 archive는 삭제하지 않으며 후속 번호 발급에 포함한다.
영구 파일 잠금·디스크 오류로 복구 자체가 실패하면 backup을 보존하고 다음 수정을 거부한다.
다중 파일의 전원 차단 시 원자성/자동 recovery 완료를 주장하지 않는다.

Undo/Redo는 before/after source 값과 예상 revision을 사용한다. 다른 수정이 먼저 적용된
stale 이력은 파일을 덮어쓰지 않는다. project weak owner와 canonical Assets root를 검증하며
프로젝트 교체·Play·active Scene 부재 시 거부한다. UndoManager는 Execute/Undo/Redo의
목적 스택 공간을 파일 변경 전에 확보하고, 명령 실패 시 원래 이력을 유지한다.
형상 정의의 고정 revision을 자동 변경하지 않는다. Editor의 이전 revision 소비는 UUID
catalog 확인 후 archive를 읽어 새 Scene/cache에서도 유효하다. Player는 source archive를
탐색하지 않으며 새 패키지 closure가 필요하다. archive는 watcher catalog/meta 생성에서 제외한다.

GeometryRevisionPublish를 GeometryPublish 아래 계측한다. source archive는 CECG 입력이고
SDK cook blob이 아니다. 실제 Inspector/HTTP·프로젝트 교체/Play Undo 제품 실행, package
closure·오프라인 SDK cook·기존 shape의 명시적 revision 변경 UI는 잔여다.

검증 증거: `Build/Obj/Phase19GeometryRevision/{구성/{result.jsonl,build.log},Product/*.log}`.
Debug/Release/Shipping/ASan 각 107 checks. EditorAssetDatabase/AssetAuthoringCommands/
CommandDescriptorSeeds/PhysicsBodyComponent Debug/Release 8개 제품 TU 컴파일 통과.
저작 소유권 SourceOnly, 대시보드 432행, 프로젝트 XML 및 scoped diff 검사 통과.
독립 source/cook/revision/파일 실패·이력 할당 실패와 실제 제품 TU 컴파일은 전체 Editor 실행과 구분한다.

#### B1 offline SDK cook·CEMF/pak 연결 (2026-10-01)

`CookedCollisionGeometry` CEPG v1 bundle과 PhysX SDK blob cook/import API를 추가했다.
UUID당 하나의 CEMF kind=CollisionGeometry artifact에 현재/과거 고정 revision을 보존한다.
SDK 버전·플랫폼/cook 정책·UUID/revision·형상 종류·길이·checksum을 import 전에 검사하고
CEMF SHA-256으로 artifact 무결성을 확인한다. convex/triangle/heightfield 모두 GPU cook data를 포함한다.

AssetCooker는 portable 원본과 불변 source archive를 읽어 cook→SDK import 왕복 후 staging에
게시한다. Scene/Prefab geometryAsset 의존성과 고정 revision 존재를 검증한다. BuildTool은
untracked native 원본/이력도 cook 입력에 넣고 배포 merged 입력에서는 CECG 및 sidecar를 제거한다.
Player 바디는 CEMF cooked entry를 SDK import하며 source/archive fallback이나 runtime cook을 하지 않는다.
현재 제품 Player는 loose cooked runtime tree를 읽는다. 공용 mounted pak reader는 독립 실제 pak 회귀로 검증한다.

독립 gate는 생산자 UUID 불일치/손상 archive, 모든 byte 변조/truncation, checksum을 다시 맞춘
잘못된 header/record, CEMF digest 불일치, 실제 chunked pak 읽기, CPU/실제 GPU import/쿼리,
cache hit 및 실패 후 cache 유지, runtime cook=0을 확인한다. 증거는
`Build/Obj/Phase19CookedGeometry/<configuration>/{build.log,result.jsonl,stderr.log}`다.
Debug/Release/Shipping/ASan 각각 1801 checks와 실제 GPU 검증을 통과했다. ASan은
사전 빌드 ryml과 맞추기 위해 STL vector/string annotation을 끄고 주소 검사기를 유지한다.
BuildTool 회귀 48 checks 및 Debug/Release의 CEMF/catalog/scene producer/바디/AssetCooker
제품 TU 총 10개 컴파일이 통과했다. 전체 AssetCooker 실행·패키지 Player·HTTP Editor·제품 profiler
capture는 독립 gate와 구분하며 아직 완료를 주장하지 않는다. B1은 progress를 유지한다.
shape별 공통 layer override와 명시적 idle 교체/스케일 정책은 아래 후속 절에서 연결했다. Inspector/제품 왕복은 남았다.

#### B1 형상 공통 layer override·idle 교체/스케일 (2026-10-01)

형상 저작에 layerOverride(안정 공통 layer_id, 0=Entity 상속)를 추가하고 generated reflection에
반영했다. project snapshot으로 solid/sensor/query 필터를 형상별로 만들며 unknown/retired ID를
geometry I/O/cook 이전 preflight에서 거부한다. CommitLayers는 Entity 상속 및 각 override를
같이 계산하므로 matrix/revision 갱신 때 override가 덮이지 않는다. retired override는 전체 transaction을 거부한다.

Play 저작 freeze는 유지한다. 별도 ReplaceShapes/RefreshRuntimeShapes와 Simulation.Replace를
owner/idle runtime API로 추가했다. 현재 pose/속도/활성 상태를 유지하고 신규 shape 전체와
binding map 준비 후 SDK 교체를 게시한다. 실패 시 기존 바디/핸들을 유지하고 성공 시 구 핸들은 stale이다.
disabled 교체도 private SDK Scene에서 검증하며 live Scene에 임시 actor를 게시하지 않는다.
현재 world scale은 부모 Transform lazy resolve 뒤 사용한다. uniform sphere/capsule,
nonuniform axis-aligned box 및 cooked scale 규칙은 기존 preflight를 따른다.
자동 Transform scale 변경 감지·보간은 B2에 남고 갱신은 명시적 API 호출이다.

verify-physics-b1 All/RequireGpu: Debug/Release/ASan 129 checks, Shipping 127 checks와 실제 GPU 통과.
형상별 query mask, 정책 재게시/retired rollback, 2배 scale query 범위, pose/속도 보존,
구 핸들 stale, invalid/in-flight/disabled 교체 및 teardown을 확인했다.
비Shipping profiler capture complete 및 baseline.ceprof 저장을 확인했다.
기존 shared geometry Release 회귀 741 checks 및 실제 GPU도 재검증했다.
제품 바디/Simulation Debug/Release 4개 TU 컴파일 및 generated layerOverride 확인 증거는
Build/Obj/Phase19B1Replacement/Product, 실행 증거는 Build/Obj/Phase19B1/<configuration>이다.
전체 Editor/Player 실행, Inspector/HTTP 형상 목록 저작·Undo/Prefab/cooked 왕복은 미검증이다.
B1은 progress로 유지하고 다음 작업은 저작 surface와 제품 왕복 게이트다.

### 공통 레이어 정리 — L0, 이번 빅뱅의 선행 작업

TagManager의 레이어 책임 분리를 이번 리팩토링에서 구현한다. 물리 전용 레이어 이름/ID
체계를 추가하지 않는다. 기존 태그·레이어 파일은 일회성 변환하고 모든 소비자를 새 책임에
직접 연결한다. 구 TagManager 레이어 API와 Entity::m_collisionType은 제거하며 위임용
호환 facade를 남기지 않는다. 기존 데이터 재사용은 기존 책임 혼합 구조의 존치를 뜻하지 않는다.

| 소유자 | 책임과 명명 이유 |
|---|---|
| Utility_Framework/LayerTypes.h | SDK/Entity 의존 없는 강한 layer_id와 layer_slot. ID는 의미 식별, slot은 32비트 필터 위치이며 서로 혼용하지 않는다 |
| Utility_Framework/LayerCatalog | 프로젝트 공통 레이어 정의·이름 조회·불변 revision snapshot. Catalog는 정의 목록이며 객체나 물리 실행을 관리하지 않는다 |
| SceneRuntime/SceneLayerIndex | Scene 수명의 EntityHandle 소속 인덱스. Scene 간 DDOL 이송은 제거/등록하고 원시 Entity 포인터를 전역 보관하지 않는다 |
| SceneRuntime/TagManager | 태그 정의와 태그 조회만 관리한다. 레이어 저장·조회·객체 소속 기능을 전량 제거한다 |
| Physics/PhysicsCollisionPolicy | 공통 레이어 사이의 충돌 허용 정책. SDK/Scene/Entity 의존 없이 단일 행렬 저장소와 revision을 소유한다 |
| Scene 소유 PhysicsScene | 정책의 불변 snapshot을 받아 SDK simulation/query 필터로 반영한다. 프로젝트 원본 행렬을 소유하거나 중복 편집하지 않는다 |

프로젝트 bootstrap이 LayerCatalog와 PhysicsCollisionPolicy의 수명을 소유한다. Editor는
저작 명령으로 정의/정책을 변경하고 Player는 cooked 정의를 로드한다. 런타임 소비자는 const
snapshot을 읽으며 mutable vector나 새 singleton 전역 접근자를 공개하지 않는다.

- layer_id는 직렬화되는 강한 64비트 식별자이며 이름 변경/정렬로 바뀌지 않는다. Entity는
  이 ID 하나를 저장한다. 레이어 이름은 Catalog 조회 결과이며 별도 mutable 진실로 저장하지 않는다.
- filter slot은 명시적으로 저장되는 0~31 위치다. ID/화면 순서/slot을 분리하고 Default를
  예약한다. 삭제된 slot은 tombstone으로 보존하고 자동 재사용하지 않는다. 압축/재사용은
  모든 씬·프리팹·쿼리 마스크·cooked 참조를 변환하는 명시적 프로젝트 migration으로만 허용한다.
- 기존 이름/목록 위치/32비트 마스크는 동일 slot으로 변환하여 기존 의미를 유지한다.
  중복 이름, 누락 참조, 33개 이상 레이어, 유효하지 않은 slot/행렬은 진단하고 변환을 실패시킨다.
  알 수 없는 이름을 묵시적으로 Default에 대응시키지 않는다. 기존 AddLayer의 33번째 허용과
  Entity의 index > 32 경계 검사는 새 계약으로 제거하고 31/32/33 경계 회귀를 둔다.
- 충돌 정책은 고정 연속 32×32 저장소와 std::mdspan view를 사용한다. 대칭 행렬로 저장하고
  비대칭 legacy 입력은 오류로 진단한다. 이름 정의와 충돌 허용 여부는 서로 다른 책임이다.
- ShapeInstance는 기본적으로 Entity 레이어를 상속한다. 명시적 형상 override도 같은 Catalog의
  ID를 사용한다. query mask는 동일 slot 공간이며 simulation matrix와 query 선택은 구분한다.
- 레이어/정책 변경은 GT 구조 경계에서 revision snapshot을 확정한다. SDK in-flight 중
  필터 쓰기를 금지하며 다음 물리 tick 전에 모든 영향 형상/CCT 필터와 재필터링을 적용한다.
  변경 직후 정지 SDK에서 수행하는 query에도 새 revision을 반영한다. 실패 시 기존 committed
  revision을 유지하고 pending 변경을 진단한다. tag 변경은 물리 필터 변경을 일으키지 않는다.
- Entity의 레이어 변경/파괴/씬 이동/DDOL/Editor Stop은 SceneLayerIndex와 바디 필터를 함께
  일관되게 갱신한다. Editor Play는 정의·충돌 정책까지 백업하고 Stop에 원상 복원한다.
  전역 Catalog/정책 revision 변경을 한 씬의 SDK 또는 객체 인덱스 갱신으로 오인하지 않는다.

2026-10-01 L0 기반 구현 증거: LayerTypes/LayerCatalog, SceneLayerIndex,
PhysicsCollisionPolicy를 SDK/Entity 포인터 의존 없이 추가했다. Catalog는 안정 ID와
명시 slot, tombstone, Default 예약, owner-only 변경과 독자 보유 불변 snapshot을 제공한다.
Restore 시 Play-created slot도 retired로 남기고 ID high-water를 유지하여 자동 재사용을
막는다. 이름/배열 위치 기반 legacy 정의 및 32×32 행렬의 값 변환·검증도 구현했다.
정책은 단일 연속 저장소+mdspan으로 대칭 행렬을 편집하고 공통 ID를 simulation/query
필터로 변환한다. SceneLayerIndex는 scene-scoped EntityHandle만 보유하고 중복 등록,
소속 변경 실패 보존, 다른 씬/다른 소유 스레드 접근을 거부한다.

`verify-layer-catalog.ps1 -Configuration All`: Debug/Release/Shipping/ASan 각 5,248 checks.
증거 `Build/Obj/Phase19L0-all.log`. SDK set_shape_filter는 body/shape handle을 유지하며
idle 상태에서 기존 쌍을 refilter한다. 실패 시 기존 simulation/query 필터를 복원한다.
실제 SDK 충돌 허용→차단→재허용, tick 전 query 반영, in-flight/foreign/stale 거부와
Physics.FilterCommit/Refilter capture를 검증했다. `verify-physics-l0.ps1 -Configuration All
-RequireGpu`: Debug/Release/ASan 각 568, Shipping 564 checks, 각 구성 실제 GPU 통과.
증거 `Build/Obj/Phase19L0-sdk-all.log`, `Phase19L0Sdk/<configuration>/baseline.ceprof`.
Physics Debug 제품 모듈 빌드도 통과했다(`Phase19L0-product.log`).

2026-10-01 프로젝트 연결: EditorMain/PlayerMain이 ProjectLayerSettings를 소유하고,
카탈로그와 정책을 하나의 불변 revision으로 발행한다. SceneManager는 weak 참조를 보유하며
Editor Play 백업/복원에 두 설정을 함께 포함한다. Player는 복원 세션 없이 엄격한 바이너리
로드만 수행한다. Editor는 기존 TagManager/CollisionMatrix를 일회성 변환하고 기존 저작
트랜잭션으로 저장한다. 실제 Dynamic_CPP의 16개 레이어와 16×16 규칙을 보존한
`ProjectSetting/Layers.celayers`(1,376 bytes)를 생성했다. 입력은 레이어 수와 일치하는
N×N 또는 기존 32×32이며 미사용 슬롯은 기존 로더처럼 허용으로 채운다.

현재 검증: core Debug/Release/Shipping/ASan 각 7,597, 실제 프로젝트 import 각 1,058,
BuildTool 포맷/손상/누락 게이트 6 checks 통과. BuildTool 빌드와 변경 관련 제품 8 TU
컴파일도 통과했다. 증거 `Build/Obj/Phase19L0-project-all.log`,
`Phase19L0-import-all.log`, `Phase19L0Package/result.log`, `Phase19L0-buildtool.log`,
`Phase19L0-host-tus.log`, `Phase19L0-port-tus.log`, `Phase19L0-project-tus.log`.

2026-10-01 바디 정책 연결: PhysicsBodyComponent의 정의 캡처는 프로젝트 카탈로그에서
현재 Entity 안정 ID를 엄격히 검증하여 simulation/query 필터를 생성한다. Scene은
Play 시작과 고정 틱 직전에 비활성 바디를 포함한 전체 binding/ID 목록을 commit한다.
등록·DDOL 재등록의 새 정의도 같은 경로를 사용한다. 동일 revision에서 Entity 레이어가
변경돼도 반영하고, 미등록/retired ID·중복/누락 binding·구 revision·in-flight·외부 스레드를
거부한다. 변경이 없으면 SDK refilter와 변경 버퍼 할당을 생략한다. 현재 ID 검증과
전체 목록 정렬은 틱마다 수행하며 dirty 연결 이후 최적화·계측한다.

Commit은 전체 검증/할당 후 SDK 필터를 적용하며 마지막 바디 실패 시 앞선 변경을
되돌린다. rollback까지 실패하면 SDK 세션을 종료해 혼합 정책으로 진행하지 않는다.
Scene은 적용 실패 시 해당 틱을 생략하고 렌더 안전 구조 경계의 Play 중단을 요청한다.
성공 후에만 정의/정책 revision을 발행한다. 비활성 바디의 정의 필터도 갱신하므로
재활성화 시 같은 정책을 사용한다. 바디/형상 identity와 현재 속도는 보존한다.
Profiler 계층은 Physics.LayerMembership → LayerPolicyCommit → FilterCommit → Refilter다.

검증 `verify-physics-b0.ps1 -Configuration All -RequireGpu`: Debug/Release/ASan 각
4,974, Shipping 4,967 checks와 실제 GPU 통과. 기존 B0 생명주기/제어 검사에 전체 정책
허용→차단→재허용, 즉시 query mask, disabled 재등록, 잘못된 마지막 입력의 원자적 거부,
두 번째 SDK 쓰기 실패의 첫 번째 필터 rollback, Play 복원 후 재시작을 추가했다.
독립 capture에서 Refilter의 정책 commit 하위 depth/시간 구간도 확인했다.
증거 `Build/Obj/Phase19L0-binding-all.log`, `Phase19B0/<configuration>/baseline.ceprof`.
관련 제품 4 TU 컴파일 증거 `Phase19L0-binding-product-tus.log`.

2026-10-01 소비자 이전: Entity는 숨김 uint64 `m_layerId`를 직렬화하고 강한 layer_id
조회/설정 API로 SceneLayerIndex에 소속을 갱신한다. `m_layer`/`m_collisionType`과
TagManager의 레이어 정의·전역 포인터 목록·API를 제거했다. 생성/로드/복제/프리팹 갱신/
Undo 복원/DDOL 재부착은 ID를 재등록하고 ReleaseSlot은 세대 증가 전에 원 Scene 소속을
해제한다. detached 입력은 소속 발행 전에 검증한다. 이름 입력은 경계에서만 ID로 변환한다.
EntityLayerSchema와 Deserialize 사전 훅은 구 필드·누락/미등록/retired ID를 거부한다.

Inspector는 공통 Layer 선택과 Undo를, 충돌 행렬은 단일 정책의 즉시 저장·Undo를 사용한다.
중복 UI 행렬 캐시는 제거했다. CLI `layer.list/add/rename/collision`, `entity.layer`를
추가하고 object 응답에 layerId/name/slot을 실었다. ID는 JSON 문자열로 전달해 64비트
정밀도를 보존한다. prefab Entity 레이어 변경은 m_layerId override를 기록한다.
프로젝트 저장은 준비된 불변 쌍의 할당과 파일 트랜잭션 완료 후에만 발행하며 저장 실패/
재진입은 기존 pair와 epoch를 보존한다. 새 레이어 시스템의 가변 singleton은 추가하지 않았다.

실제 Dynamic_CPP의 22개 Scene/Prefab, 74 Entity를 ID로 변환하고 중복 충돌 필드를
제거했다. `Tools/regression/migrate-entity-layer-ids.py`는 전체 입력 사전 검증, 원본 ZIP/
SHA-256 manifest, 파일 단위 atomic replace와 도중 실패의 원본 복원을 제공한다.
반복 실행은 변경 0이다. 원본은 `Build/Obj/Phase19L0Consumer/Migration/`에 보관했다.
기존 cooked Scene/Prefab은 새 스키마로 재쿠킹해야 하며 Player에서 구 필드 fallback은 없다.
기존 TagManager.asset의 layer 구역과 CollisionMatrix.asset은 최초 이전 입력으로만 남아
있으며 활성 저장/정책 편집은 native Layers.celayers를 사용한다.

현재 증거: core Debug/Release/Shipping/ASan 각 7,602, native import+실제 22개 자산/
74개 Entity 스키마 각 1,161, migration 1,409 checks. Release B0 4,978 checks와 실제
GPU에서 레이어 rename 후 slot/SDK body identity 보존도 통과했다. 관련 SceneRuntime
9 TU, Editor 6 TU와 EditorMain/PlayerMain 2 TU의 Debug 제품 컴파일을 확인했다.
증거 `Build/Obj/Phase19L0Consumer/{core-all,import-all,migration-tests,b0-release,
product-tus,runtime-tus,editor-core-tus,editor-ui-tus,host-tus}.log`.

**L0는 progress다.** CCT, dirty 기반 정책 commit/성능 계측, 정책 revision/실패 카운터,
실제 Editor Play/Stop·Undo·DDOL·HTTP와 새 cooked Player 실행은 잔여다. R0 Owners는
통과하고 Complete는 구 물리 소비자 7파일 때문에 미통과다. 별도 저작 소유권 게이트의 초기 실패(`ownership.log`)는 후속 정리했다.
ModelAssetGeneration은 CEIM 캐시 payload만 만들고 기존 AssetAuthoringPort::WriteModelCache로
Editor의 staging/원자적 게시에 위임한다. 삭제된 PhysicsManager 및 TagManager 레이어 검사도
현재 LayerSettings/tags-only 계약으로 갱신했다. `-SourceOnly`와 MBC 동결 게이트 통과
증거는 `Build/Obj/AssetAuthoringOwnership/{source,mbc-freeze}.log`다. 변경 모델 로더의 Debug/Release 제품 TU 컴파일도 통과했다(`compile-{debug,release}-selected.log`).
제품 탐침은 최신 실행 파일을 요구하며 현재 오래된 Editor 때문에 미실행이다(`product.log`).
소스/TU/독립 네이티브 검증과 전체 제품 실행을 구분하며 전체 제품 완료를 선언하지 않는다.

L0 완료 게이트: 정의/행렬 파일 변환·왕복, 이름 변경/정렬/삭제 후 ID/마스크 의미 보존,
32비트 경계/미등록/중복 거부, Scene 소속/파괴/DDOL 인덱스, CPU/GPU 실제 충돌 허용/차단과
query mask, 런타임 revision 변경·in-flight 거부·CCT 갱신, Editor 복원 및 Player cooked 로드.
Profiler에 Physics.FilterCommit/Refilter, 적용 revision·변경 형상 수·실패 수를 노출한다.
단위/독립 SDK 게이트와 실제 제품 게이트를 구분한다. B1은 L0 정의/변환 계약이 완료된 뒤
진행하고, CCT/C#/제품 실행 증거는 C0/M0~M3에서 닫는다. L0 단독 완료로 제품 완료를 선언하지 않는다.

## 3. C++23 PhysX API 계약

C++23을 새 물리 API의 기본 설계 언어로 사용한다. 오류·형상·명령·배치·데이터 뷰·제약을 타입으로 표현하며 구 C++ 관용구를 그대로 옮긴 래퍼를 만들지 않는다. P0에서 MSVC와 제품 빌드의 실제 기능 지원을 compile probe로 확인한다. 미지원 기능은 해당 계약의 대안을 명시하며 언어 기준을 낮추거나 구 래퍼로 fallback하지 않는다.

- **RAII**: `Px*::release()` 전용 deleter와 소유 타입. shape/material/geometry/actor/scene/dispatcher/CUDA/foundation의 해제 순서 및 공유 참조를 명시한다. raw SDK 포인터는 구현 내부의 비소유 관찰에만 사용한다.
- **`std::expected<T, PhysicsError>`**: 초기화, cook, 바디 생성, 쿼리 실패를 표현한다. 오류 코드와 진단 정보를 분리하고 일부 생성 실패도 rollback한다. 빈 쿼리 결과와 쿼리 실패를 구분한다.
- **강한 값 타입·`enum class`**: BodyHandle/CharacterHandle/ShapeId, 운동 종류, 응답 종류, 힘 모드를 구분한다. 핸들의 슬롯 재사용·세대 순환·씬 간 오용 정책을 정하고 24/8 비트 배치를 미리 고정하지 않는다.
- **`std::span`**: batch 입력과 호출자 제공 결과 버퍼. 임시 span을 지연 커맨드에 보관하지 않고 필요한 데이터를 소유하거나 자산 수명을 보유한다. cook 자산은 형식·버전·유효성 검증을 갖춘다.
- **`std::variant`**: primitive/convex/mesh/heightfield 형상 정의에 명시적 타입을 사용한다. 저작 리플렉션·파일 스키마는 별도 DTO/tagged schema로 지원한다. 직렬화 제약 때문에 런타임 형상을 Vector3 슬롯에 몰아넣지 않는다.
- **`std::pmr`**: 커맨드·접촉점·쿼리 scratch의 스텝 수명 할당에 제한적으로 사용한다. 결과 소비 완료와 잡 join 전에 allocator를 reset하지 않는다. 전 타입에 일괄 적용하지 않는다.
- **소유 스레드와 단계**: create/destroy/mutate는 소유 스레드의 명시된 단계에서만 수행한다. 공용 enkiTS 워커는 불변 스냅샷을 읽고 요청을 적재한다. SDK solver 워커 콜백은 별도 동시성 계약으로 처리한다.
- **요청과 확정 상태 분리**: target pose/velocity 요청과 simulated pose/velocity 조회를 구분한다. 모든 세터에 '즉시 실제 상태 반영'을 약속하지 않는다. 커맨드 적용 tick을 명시한다.
- **데이터 저장**: 엔진은 백엔드 상태 전체를 복제하지 않는다. 연결/세대는 작은 AoS, 활성 목록·보간 pose·스냅샷은 접근 패턴에 맞춰 분리한다. 전량 SoA를 선결 규칙으로 강제하지 않는다.

### 적극 적용할 언어·라이브러리 구조

- **합성 가능한 오류 흐름**: std::expected의 and_then/transform/or_else로 초기화·cook·생성을 구성한다. bool+out pointer, sentinel ID와 로그만 남기는 실패 API를 제거한다. [[nodiscard]]로 결과 무시를 막는다.
- **타입 안전 명령 합집합**: PhysicsCommand는 TeleportCommand/VelocityCommand/ForceCommand/DestroyCommand 등의 std::variant로 정의한다. std::visit으로 처리하며 opcode+aux+무타입 union 조합을 사용하지 않는다. shape/body 타입별 유효 명령과 병합 규칙을 검증한다.
- **concepts·requires와 if constexpr**: typed batch 입력, 핸들 종류, geometry 생성기의 요구조건을 명시한다. 백엔드 종류를 템플릿화하는 대신 유효 입력과 정책을 제약한다. 공개 mutation은 강한 타입의 비템플릿 표면도 제공해 ABI 경계를 통제한다.
- **명시적 객체 매개변수(deducing this)**: 내부 핸들/뷰/descriptor builder의 const/non-const 접근 및 value-category 보존 중복을 제거한다. 지원은 P0 compile probe로 확인한다.
- **ranges를 기본 선택으로 적극 수용**: std::ranges 알고리즘과 filter/transform/enumerate/zip/chunk 파이프라인을 저작 형상 정규화·검증, cook 입력 준비, 자산 탐색, 마이그레이션, 진단, 이벤트 선택과 batch 구성에 우선 사용한다. 수동 루프를 기본값으로 고정하지 않는다. 핫패스도 ranges를 일괄 금지하지 않고 생성 코드·할당·실측으로 판정한다. lazy view를 프레임/잡 경계를 넘어 저장하지 않고 실행 입력은 소유 버퍼로 확정한다.
- **Mathematics view 적극 도입**: 저장소에 고정된 ThirdParty/Mathematics/include/mathematics/views.hpp의 math::components, math::rows, math::views::transform_fixed와 ranges.hpp의 fixed terminal을 비핫패스에서 우선 활용한다. 벡터 성분 유효성 검사·단위 변환, 행렬 진단·직렬화 준비, cook/마이그레이션 수학 데이터 처리에서 임시 배열 복사와 중복 루프를 줄인다. view는 원본을 빌리는 표현이므로 소유 객체/람다 capture의 수명을 확인한다. 비핫패스 도입마다 성능 벤치를 요구하지 않는다. 활성 바디 pull·스텝당 대량 pose 변환·solver 콜백 등 핫패스에서는 직접 수학 연산과 비교 측정해 선택한다. 라이브러리 갱신은 별도 범위이며 외부 checkout의 최신 API를 암묵적으로 요구하지 않는다.
- **std::mdspan**: 레이어 충돌 매트릭스와 연속 batch 데이터의 비소유 다차원 뷰. 중첩 vector와 중복 배열 표현을 단일 저장소+뷰로 바꾼다. 프로젝트 충돌 정책 원본은 PhysicsCollisionPolicy가 소유하고 PhysicsScene은 적용 snapshot만 소유한다.
- **std::flat_map / flat_set**: 저빈도 변경의 정렬된 자산/형상 lookup에 사용한다. 바디 핫 경로는 직접 인덱스와 세대 검사로 유지한다. 조회마다 unordered_map을 기본 선택하지 않는다.
- **std::pmr + 소유 epoch**: 커맨드·접촉점·query batch scratch를 명시적 tick arena에 묶는다. 반환 데이터가 arena를 벗어나면 소유 결과로 이동하거나 epoch를 보유한다. 잡 완료 전 reset 금지.
- **std::format과 source_location**: 초기화/cook/잘못된 핸들/단계 위반 진단의 공통 오류 컨텍스트를 구성한다. 문자열 생성은 핫 경로의 정상 실행에서 제외한다.

coroutine은 공용 스케줄러의 완료/취소/소유 계약과 연결될 때만 추가한다. C++23 활용을 이유로 별도 스케줄러·전용 비동기 물리 스레드를 만들지 않는다. STL 기능은 실측 없이 성능 우위로 주장하지 않는다.

### 프로파일러 실행 계층 — 필수 구현·완료 조건

물리 실행 계층을 기존 엔진 프로파일러에 노출한다. 계측은 완료 뒤 붙이는 선택 작업이 아니라
P1~P3 API 구현과 함께 배선하며 M3의 성능 판정에 사용한다. PhysicsScene 전체 시간 하나로 완료 판정하지 않는다.

- 소유 스레드의 PhysicsTick 아래에 CommandCommit(생성/파괴/변경), CharacterMovement,
  KinematicTargets, SimulateSubmit, FetchWait, QueryStructureUpdate, QueryBatch,
  ActivePoseCollect, TransformApply, EventCollect/Dispatch, SnapshotPublish를 실제 실행 구간별로 기록한다.
  쿼리 등 tick 밖 작업은 실제 부모 구간에 기록하며 가짜 중첩 scope를 만들지 않는다.
- PhysX solver 작업은 실행 중인 dispatcher 워커에 thread stream과 task scope로 노출한다.
  PhysicsTickId와 task 연관 ID로 제출/실행/완료를 연결한다. 워커 실행 시간을 게임 스레드
  FetchWait의 중첩 시간으로 합산하지 않는다. SDK 작업 세부 구분이 불가능한 곳은
  PhysXTask 등 실제 관찰 가능한 범위로 표시하고 solver 세부 단계가 계측됐다고 주장하지 않는다.
- FrameId와 PhysicsTickId를 구분하고 프레임당 0/N tick, catch-up 횟수·누산 지연,
  커맨드 대기/적용 지연과 dropped/overflow, query 대기/실행 시간을 기록한다.
- 총/활성/변경 바디·형상·캐릭터 수, 이벤트/접촉점 수, 쿼리 수, scratch 할당/최대 사용량과
  dispatcher 워커 수를 카운터로 제공한다. 단위·집계 주기·리셋 시점을 명시한다.
- 기존 profiler marker/thread stream 계약을 사용하고 hot path에서 동적 이름·문자열 할당을
  만들지 않는다. 워커 등록/종료, 캡처 종료 시 전달 drain과 이벤트 버퍼 수명을 검증한다.
- Exit: Debug/Release 실제 캡처에서 소유 스레드와 solver 워커의 실행 계층·tick 연관을 확인하고,
  submit 비용/solver 실행/fetch 대기를 분리해 판독할 수 있어야 한다. 별도 stopwatch만으로 대체하지 않는다.
  계측 on/off 비용도 기준선에 기록한다. 물리 전용 스레드의 필요성은 이 캡처로 판단한다.

## 4. 실행 순서와 완료 게이트

**P0 기준선/계약 → R0 구 계층·배선 제거 → P1~P3 새 PhysX API → B/C/T 전량 구현 → M 전량 이전·통합 검증**.
이 순서는 내부 구현 의존성이지 단계별 제품 출시가 아니다. 전체를 하나의 빅뱅 변경으로 완결한다.
기준선은 삭제 전 실행 결과·스키마 corpus·소스 스냅샷으로 보존한다. 이전 구현을 실행 경로로 남기지 않는다.
Z의 결함은 별도 구 래퍼 지혈 구현 없이 새 API의 필수 정확성 게이트로 흡수한다. API 단독 프로브는 새 계층만 검증하며 기존 제품에 나란히 붙이지 않는다.

| 단계 | 작업 | 완료 근거 |
|---|---|---|
| P0 | 현재 소비자/SDK/빌드/생명주기 기준선과 API 계약 | 사용 버전 고정, 필요한 기능·단위·스레드·수명·지원 형상 목록, CPU 기준선. 기존 PhysicsDrop 자산 존재부터 확인 |
| R0 | 기존 물리 계층·제품 배선 제거 | PhysicX/PhysicsManager/구 래퍼·컴포넌트·전역 접근자·프로젝트 편입 제거. 관련 호출부를 새 계약으로 전량 교체하며 legacy fallback 없음 |
| P1 | C++23 PhysX API 기반 | SDK RAII·expected 오류·강한 핸들·씬 소유·CPU 초기화, 실패 경로/해제 순서 테스트, profiler marker와 워커 stream 기반 배선. CUDA는 검증된 opt-in이며 실패 시 CPU fallback |
| P2 | 완결된 바디·형상·cook·쿼리 API | compound, static/kinematic/dynamic, shape별 sensor/filter/material, cooked geometry 공유, stale handle 거부, 실제 충돌·쿼리 확인 |
| P3 | 스텝·커맨드·이벤트·캐릭터 저수준 API | 적용 tick·콜백 동시성·접촉점 소유·body/shape 신원, CCT 생성/이동/해제. **B/C/T 착수의 하드 선행** |
| B0 | PhysicsBodyComponent와 Scene 연결 | 완결된 바디 정의·등록/해제 단일점·생명주기. 에디터 Play/Stop은 SDK 종료 후 문서/Transform/DDOL을 원상 복원하고 실패 시 백업 보존. 제품 왕복은 M 단계 필수 게이트 |
| L0 | 공통 레이어·TagManager 책임 분리 | LayerCatalog/SceneLayerIndex/PhysicsCollisionPolicy, 안정 ID·slot, 전 소비자 교체·일회성 파일 변환·필터 revision 게이트 |
| B1 | CollisionGeometry와 ShapeInstance 저작 | 값 형상+공유 cooked 자산, 센서/자식 귀속, 스케일·교체·cook 실패 정책 |
| B2 | 차등 동기화와 보간 | 활성 dynamic pull, kinematic target, static 명시 변경, 부모 Transform 환산과 렌더 이전/현재 pose |
| C0 | CharacterMovementComponent | 강체 의존 제거, 입력→충돌 이동→결과, 소유 스레드 계약 |
| C1 | 기존 이동값 이전 | 초당 단위와 legacy 스텝당 값 변환 정책, 중력/경사/계단/강제 이동 회귀 |
| T0 | 스레딩 계약 강제 | 게임 요청과 solver 콜백 분리, 스냅샷 수명, 위반 진단, deterministic merge 규칙 |
| T1 | PhysX 실행 최적화 | simulate/fetch 창에서 안전한 작업만 배치. 공유 dispatcher 도입 여부는 task 수명·의존성 검증 후 결정. 전용 SDK dispatcher 유지도 허용 |
| T2 | 쿼리 배치 | 동시 쓰기 없는 query 창, 구조 갱신 정책, 결과 버퍼·overflow와 실패 계약 |
| M0 | C++/C# 소비자 이전 | 새 표면에 맞춘 전량 이전, 구 물리 시그니처·엔트리 제거, 호환 어댑터 없음 |
| M1 | 씬/프리팹/cooked 스키마 이전 | 구 Rigidbody+Collider 파일을 일회성 도구로 새 바디 정의로 변환, UUID/형상 ID/누락 자산·미지원 데이터 진단 |
| M2 | 경계/생명주기 게이트 | backend include 격리, 파괴/씬 전환/DDOL 회귀. 기존 include 검사기의 프로젝트 배치 정합성부터 복구 |
| E0 | ContactStream·C# 접촉 소비 | 역할 신원/Prefab·cook, Scope 구독 인덱스, 네이티브 이벤트→준비된 span, PostPhysics 소비·취소/drain·CLR ABI·구 콜백 제거 및 실제 profiler 게이트 |
| M3 | 제품 회귀·측정 | 형상별 실제 충돌·sensor/filter·CCT·실패/해제, 실제 profiler 계층 캡처·tick/task 연관, CPU 평균/p99·메모리·변경/활성 비율별 비용 |
| M4 | 빅뱅 완료 감사 | R0 제거 항목의 소스/링크/프로젝트 재유입 0, 구 배선·선택 스위치·호환 어댑터 0, 필수 제품 게이트 전항 통과 |

### 계획 공수 (2026-10-01 재산정)

단위는 인일(1인 기준 8시간)이며 구현·리뷰·해당 단계 검증을 포함한 계획 추정치다.
완료 단계의 값도 비교 가능한 기준 공수이며 실제 소요시간을 사후 측정한 값이 아니다.
여러 작업을 병렬 수행해도 합계 인일이 달력상 소요일과 같지는 않다.
R0는 제거·감사 자체만 산정하고 소비자 교체는 B/C/T/M에 배정하여 중복 산정하지 않는다.

| 단계 | 추정 인일 | 산정 범위 |
|---|---:|---|
| P0 | 2 | 계약·SDK/언어 확인·HTTP 기준선·corpus 보존 |
| R0 | 2 | 구 소유자 제거·프로젝트 정리·최종 제거 감사 연결 |
| P1 | 3 | RAII·오류·SDK/워커 수명·CPU/GPU 초기화·기반 계측 |
| P2 | 7 | 바디/형상/재질/필터·cooking·공유 자산·쿼리·핸들 검증 |
| P3 | 8 | 고정 step·커맨드·이벤트·CCT·프로파일러 tick/task 메타데이터 |
| B0 | 3 | 바디 컴포넌트·씬 등록/해제·생명주기 |
| L0 | 4 | 태그/레이어 분리·ID/slot·행렬·기존 소비자/파일 전환·독립 필터 회귀 |
| B1 | 4 | compound/센서 저작·공유 cooked 자산·교체/스케일 |
| B2 | 4 | 차등 pose 동기화·부모 Transform·보간 |
| C0 | 4 | 독립 캐릭터 입력·충돌 이동·결과 |
| C1 | 3 | 단위 변환·이동값 이전·경사/계단/강제 이동 |
| T0 | 3 | 소유권 강제·스냅샷/콜백 수명·커맨드 병합 |
| T1 | 3 | 안전 overlap·워커 실행/대기 측정·실행 최적화 |
| T2 | 3 | 쿼리 읽기 창·배치·버퍼/overflow 정책 |
| M0 | 5 | C++/C# 엔트리·모델/terrain·Editor 소비자 전량 교체 |
| M1 | 4 | 씬/프리팹/cooked 변환·오류 진단·왕복 검증 |
| M2 | 3 | SDK 경계·씬 전환/DDOL·제품 생명주기 회귀 |
| E0 | 6 | 역할 저작·라우팅·C# stream/ABI·PostPhysics 소비·생명주기·제품 gate |
| M3 | 5 | 제품 기능·CPU/GPU 회귀·평균/p99/메모리·실제 캡처 |
| M4 | 2 | 제거/배선 재유입 감사·필수 게이트 통합 확인 |
| **합계** | **78** | 기본 범위, 별도 위험 여유 제외 |

통합·스키마 이전·GPU 검증 불확실성에 대한 여유는 기본 공수의 20~30%(약 16~23인일)로
별도 관리한다. 여유 포함 계획 범위는 약 94~101인일이며 단계 진행 중 새 증거로 갱신한다.
대시보드는 기본 72인일을 사용하고 완료 상태의 공수 가중 집계는 실제 시간 기록과 구분한다.
P2의 형상/cook 검증과 P3의 계측 스키마가 확정되면 잔여 공수를 다시 산정한다.

워커 예산은 새 API에서 설정화하고 기준선으로 검증한다. 스레드 수 증가 자체를 개선으로 판정하지 않는다.
Terrain은 실제 생성 경로가 있으므로 A0 일괄 삭제에서 제외하며 새 heightfield 경로로 이전 여부를 결정한다.
Ragdoll 등 삭제 후보는 실사용/파일 참조를 다시 확인한 후 스키마 이전과 함께 처리한다.


#### B1 Inspector·HTTP 형상 목록 트랜잭션 (2026-10-01)

Inspector는 바디의 live 형상 목록을 직접 수정하지 않고 값 초안에서 추가·삭제·종류·공통
레이어를 편집한다. Apply와 `physics.shapes <target> <component-or-#id> <input-file>`은
같은 검증/Undo 명령을 사용한다. 전체 형상 목록을 한 Undo 항목으로 게시하고 prefab의
m_shapes override를 함께 복원한다. 프로젝트 교체·stale 문서·Play·잠금 상태는 거부한다.
형상 문서 reader는 JSON/YAML, canonical 벡터 map, 정확한 uint64 ID/revision을 읽으며
중복/누락/알 수 없는 키·잘못된 UUID·비유한 수·잘못된 형상 정의를 게시 전에 거부한다.
Physics.ShapeAuthoringPublish를 profiler 계층에 연결했다.

`verify-physics-shape-document.ps1 -Configuration All`: Debug/Release/ASan 각 37 checks
통과. 변경 관련 제품 TU 5개를 Debug/Release에서 실제 선택 컴파일했다. 증거는
`Build/Obj/Phase19ShapeAuthoring/{Debug,Release,ASan,Product}`다.

**제품 실행은 미확인이다.** 새 전체 Editor Debug 빌드는 ModelSceneInstantiation.cpp,
ClrHost.cpp, EnhancedGizmoSceneBinding.cpp의 구 물리 소비자에서 실패했다. Owners 감사는
통과하지만 C++/C# 구 소비자 7파일이 남는다. `verify-physics-shape-http.ps1`은 Undo/Redo,
prefab override, 저장/로드, Play 중 저작 거부와 Stop 위치 복원 게이트를 준비했으며
오래된 Editor 바이너리를 거부했다. 실제 Inspector·HTTP·패키지 Player 성공으로 기록하지 않는다.
다음 선행 작업은 M0 소비자 교체와 C0 캐릭터 경로 정리이며 B1은 progress를 유지한다.


### M0 기즈모 소비자 교체 (2026-10-01, progress)

EnhancedGizmoSceneBinding은 삭제된 Collider/CCT 캐시 목록 대신 Scene 소유 Entity의
PhysicsBodyComponent 목록과 compound 형상 값을 읽는다. 새 캐시나 raw SDK 포인터를
추가하지 않고 GT packet 수집 경계에서 선 정점으로 복사한다. Physics.GizmoCollect를
profiler 계층에 노출하고 파괴 예약 Entity/Component를 제외한다.

PhysicsPrimitivePreview는 같은 형상 변환·검증을 재사용하고 asset 조회/cook/SDK 호출 없이
box/sphere/capsule 표시 값을 만든다. 치수와 local offset에 world scale을 한 번만 적용하고
unit-scale body/local pose를 합성한다. 구 max-axis sphere/capsule 근사는 제거하며 비균일
sphere/capsule 또는 회전 형상의 shear는 SDK 저작 계약처럼 거부한다. Capsule 축은 +Y,
halfHeight는 반구를 제외한 값이다. 센서 형상은 별도 색상으로 표시한다.

Cooked 형상 와이어는 아직 지원하지 않으며 unsupportedColliderShapes에 집계한다.
잘못된 primitive는 invalidColliderShapes에 집계한다. CCT 미리보기는 C0 구현 뒤 연결한다.
`verify-physics-primitive-preview.ps1 -Configuration All`: Debug/Release/ASan 각 21 checks.
실제 line collector의 box 정점 경계·회전 capsule 축과 크기, 중복 스케일 방지·실패 계약을
검증한다. SceneRuntime 기즈모 TU는 Debug/Release 선택 컴파일로 확인한다. 증거는
`Build/Obj/Phase19PrimitivePreview`와 `Build/Obj/Phase19M0Gizmo`다.

M0 전체 완료나 새 Editor 제품 실행 성공은 아니다. ModelSceneInstantiation의 충돌 자산
생성과 CLR/C# 바디·형상·query·캐릭터 엔트리 교체가 남는다. 다음 작업은 이 소비자의
명시적 자산/신원 계약과 C0 캐릭터 연결이며 구 API 호환 어댑터를 추가하지 않는다.


### M0 모델 자동 충돌 소비자 교체 (2026-10-01, progress)

ModelSceneInstantiation의 구 RigidBodyComponent/MeshColliderComponent 생성을 제거했다.
CreateMeshCollider 저작 옵션은 Editor Host의 shared CollisionGeometry 게시 콜백을 요구한다.
작업 스레드는 immutable 모델 vertex layout 표로 position/triangle 값만 추출하고 전체
개수·stride·layout hash·비유한 수·범위/중복 index를 검사한다. SceneRuntime은 파일을 쓰지 않는다.

Editor GT가 모델 UUID·mesh UUID·입력 내용 discriminator별 cegeometry/UUIDv4/revision 1을
게시한다. 동일 파일 재사용 시 전체 CECG 내용과 meta UUID/revision을 비교하며 파일명 hash만
신뢰하지 않는다. 변경 입력은 새 불변 자산이고 Undo/Redo·씬/프리팹 저장에 필요한 기존
생성 자산은 보존한다. SDK cook이 성공한 뒤 atomic source/meta 게시와 cache commit을 수행한다.
GT에서 캡처한 project weak identity와 asset root를 worker 요청에 보관하고 callback 실행 시
프로젝트 교체·Play를 거부한다. worker가 SceneManager의 mutable project 상태를 읽지 않는다.

각 mesh 엔티티는 명시적 static PhysicsBodyComponent + triangle_mesh ShapeInstance를 갖는다.
완전한 정의를 생성자에서 검사하고 owner Transform을 EnsureResolved한 뒤 등록하므로
기본 dynamic/box를 중간 게시하지 않는다. 기존 마찰·반발 0을 보존하며 등록 실패는 전체
PendingInstance::Cancel 및 기존 Editor cleanup/EndFramePass 경로로 회수한다. 스킨 변형에
따라갈 수 없는 정적 triangle 자동 생성은 Prepare에서 거부한다. 콜백 없는 runtime 요청도
명시적으로 거부하며 Player에 source writer나 runtime cook fallback을 추가하지 않는다.
Physics.ModelCollisionPrepare/Publish/Attach를 profiler 계층에 노출한다. 최초 SDK cook/파일 게시가
GT activation 안에서 실행되므로 기존 2ms deadline은 이 작업을 선점하지 못한다. 실제 maxApplyUs
계측과 worker cook blob 준비/GT import 분리는 제품 성능 게이트의 잔여 작업이다.

검증: `verify-physics-model-collision.ps1 -Configuration All` Debug/Release/ASan 각각
129 checks. 8개 제품 vertex mask의 추출/실패 계약과 실제 atomic CECG/meta 생성·UUID
재사용·변경 입력·내용 충돌·metadata 불일치를 검사했다. 증거
`Build/Obj/Phase19ModelCollision/{Debug,Release,ASan}`. 변경 제품 TU 3개를 Debug/Release
컴파일했다(`Build/Obj/Phase19M0Model`). Owners 및 저작 소유권 SourceOnly 게이트 통과,
lexical 구 소비자는 ClrHost + C# 5파일의 6파일이다.

새 전체 Editor Debug 빌드는 exit 1이며 오류 발생 TU는 ClrHost.cpp 하나다.
구 CCT/query/body/shape 엔트리 오류는 남아 있다. 증거 `Phase19M0Model/editor-full-debug.log`.
실제 모델 placement Undo/Redo·실패 회수·Play/Stop·새 패키지 Player·제품 profile capture는
미검증이다. 다음은 CLR/C# 표면과 C0 캐릭터 연결이며 M0 progress를 유지한다.

## 5. 생명주기와 고정 스텝

새 컴포넌트는 초기화·씬 편입·시뮬레이션 시작, 종료·씬 제거·해체를 구분한다.
DDOL 이송은 목적 PhysicsScene에 새 런타임 바디를 연결하고 초기화 훅 반복 없이 상태 이전 정책을 적용한다.
콜백/쿼리/잡이 참조하는 데이터가 남아 있는 동안 actor/userData/cooked geometry를 해제하지 않는다.

고정 Simulation Tick은 NetworkFrameworkPlan N3와 공통 계약으로 구현·검증하며, Scene::FixedUpdate라는 이름만으로 실제 고정 스텝이라고 판정하지 않는다.
관리 PrePhysics/PostPhysics와 애니메이션 이벤트의 프레임 축은 유지한다. CCT 저작값은 기준 tick과 변환을 명시한다.
스텝은 요청 수집 → 변경 적용 → 캐릭터/키네마틱 목표 처리 → simulate/fetch → 활성 pose/이벤트 수집 → 전달/스냅샷 발행 순서다.
렌더 보간은 스텝 완료 pose 사이에서 수행한다.

## 6. 이번 범위 밖과 미래 계획

**Jolt 도입은 미래 별도 계획**이다. 이번에는 Jolt 의존성·콘솔 스파이크·비교 벤치·JobSystem 어댑터·이중 백엔드 실행을 만들지 않는다.
미래 검토 시 목표 바디 수, CCT/cook/접촉 의미 호환, CPU/GPU 사용 실측, 마이그레이션 비용을 평가한 뒤 별도로 승인된 계획에서 결정한다.
현재 PhysX API는 SDK 경계를 격리하지만 미래 교체를 위해 불필요한 범용 backend 인터페이스를 미리 만들지 않는다.

랙돌/아티큘레이션 신규 구현, 차량·클로스·디포머블, ECS 전환, 전용 비동기 물리 스레드, 캐릭터 네트워크 예측은 이번 기본 완료 조건 밖이다.


### M0 CLR/C# ABI 교체 (2026-10-01, progress)

Script API version 29로 일괄 변경했다. 구 Rigid/Collider/Cct 엔트리와 관리 래퍼를
제거하고 PhysicsBodyComponent의 캡처된 component instance ID로 바디를 조회한다.
동일 Entity에 바디가 여러 개면 일반 GetComponent 조회는 모호성을 거부하며,
Find(Entity, componentId)로 명시적으로 선택한다. 제거·교체된 바디로 자동 재연결하지 않는다.
바디 상태/속도/힘과 형상 조회/센서·query flag 교체는 PhysicsError를 반환한다.
관리 Component.Enabled의 스크립트 수명 필드를 물리 활성 상태로 가장하지 않도록
PhysicsBodyComponent에서 사용을 명시적으로 거부한다. 현재 활성 제어는 Entity.SetEnabled다.

쿼리는 Entity를 씬 anchor로 받아 Scene의 소유 API를 통해 실행한다. 런타임 상태를
CLR에 공개하지 않는다. 실제 hit distance, object generation, component/shape identity,
공통 stable layer ID, face, location 유무를 반환한다. Written/RequiredCapacity/Truncated를
분리하며 빈 span은 용량 확인이다. 잘림 시 최근접 hit 보장은 없다. 오류와 no-hit를
분리하고 GT 밖 호출은 상태에 접근하기 전에 거부한다. Physics.ScriptQuery 계층을 계측한다.

GameScripts의 물리/바디 probe도 새 API로 교체하고 무조건 참인 검사와 빈 fixture의
성공 처리를 제거했다. 구 CctProbe는 삭제했다. CharacterMovementComponent의 고정 스텝,
레이어 갱신, 수명·이동 결과와 새 스크립트 probe는 C0/C1 미구현 항목으로 남는다.
호환 어댑터나 성공을 반환하는 캐릭터 stub은 없다.

검증: 실제 ClrHost/Scene/ScenePhysicsSimulation TU Debug/Release 컴파일,
ScriptCore Debug/Release 및 GameScripts Debug 빌드 통과. verify-physics-script-abi.ps1은
양측 160개 슬롯 순서와 8개 신규 엔트리 초기화를 검사하며, 실제 관리 어셈블리의
layout/offset/enum/version 및 unbound 오류·출력 초기화·enable 거부를 Debug/Release
각 17 checks로 검증했다. native DTO static_assert도 제품 TU에서 컴파일된다.
이 게이트는 실제 bound callback/SDK 실행 또는 Play/Stop 제품 증거를 대체하지 않는다.
증거: Build/Obj/Phase19M0Script. Cutover Complete lexical 감사는 구 소유 파일 55개 삭제,
구 소비자 0파일로 통과했고 저작 소유권 SourceOnly도 통과했다. lexical complete는
M0/C0/R0 제품 완료 판정이 아니다. 전체 Editor 빌드 및 HTTP 제품 게이트 결과는 별도 기록한다.


M0 제품 진입 검증에서 추가로 발견한 결함:
- 구 타입 삭제 후 ComponentTypeUUID 표의 고정 크기 33에 빈 항목이 남아 초기화가
  크래시했다. std::to_array로 실제 25항목을 추론하고 비어 있는 항목을 compile-time 거부한다.
- prefab.overrides는 콘솔에만 목록을 출력했다. HTTP data에도 isInstance와
  componentType/componentSlot/property/value 배열을 반환한다.
- Prefab 형상 override의 중첩 YAML 문자열 저장에서 ryml 0.16의 자동 scalar style
  선택이 noexcept assertion을 발생시켰다. AuthoringWriteNode는 CR/LF 문자열에
  double-quote 스타일을 지정한다. 줄바꿈/들여쓰기/따옴표/backslash의 byte 왕복을
  추가한 형상 문서 게이트는 Debug/Release/ASan 각 43 checks로 통과했다.
- HTTP 게이트는 예상한 400 거부 응답을 JSON으로 읽고 argument/precondition status를
  검사하며, launcher EXE 대신 실제 구현 runtime DLL의 갱신 시각으로 stale build를 판정한다.

전체 Editor Debug 빌드는 통과했다. 초기 제품 실행은 기본 forest cook recipe가 오래되어
막혔고, 소스 자산 대신 Bin/x64-Debug/Resources의 실행 산출물만 재생성했다.
EnvironmentCooker는 validation=0, roundtrip=exact로 통과했다. 전체 빌드의 리소스 배포가
이 산출물을 덮어쓸 수 있으므로 이 작업의 재생성본과 로그를 Phase19M0Script에 보존한다.
HTTP 최종 결과는 아래 후속 기록을 정본으로 한다.


**최종 제품 결과:** `editor-multiline-debug.log` 전체 Editor Debug 빌드 exit 0.
`verify-physics-shape-http.ps1`은 새 Editor에서 27개 HTTP 명령으로 통과했다.
Undo/Redo, invalid 형상 거부와 Undo 깊이 유지, Prefab shape override의 실제 HTTP 조회,
저장/로드 후 override 배열 동일성, Play 중 저작 거부, 시뮬레이션 이동, Stop 위치 복귀를
검증했다. 관측 위치는 `[0,5,0] → [-0.655997,4.87975,-0.0479685] → [0,5,0]`다.
증거는 `Build/Obj/Phase19ShapeAuthoring/http-6ee4c29729b143c280ff7acaed90a0ea`
(`result.json`, `results.jsonl`, Editor stdout/stderr)이며 최신 fixture는
`Dynamic_CPP/Assets/Scenes/PhysicsShapeGate-6ee4c29729b143c280ff7acaed90a0ea.creator`다.
이전 실패/중간 실행 fixture는 해당 evidence의 fixtures 디렉터리에 보존했다.

이 결과는 Editor 원시 형상 저작/씬·Prefab 저장/Play-Stop 위치 복귀의 제품 증거다.
CLR bound callback의 실제 게임 스크립트 실행, Player, cooked geometry 배포/왕복,
모델 배치 Undo/Stop, C0/C1 캐릭터, profiler capture/성능과 Inspector 직접 UI 조작은
별도 게이트다. M0와 B1은 progress를 유지하며 다음 의존 작업은 C0 캐릭터 실행 경로다.


### 2026-10-02 C0 native 캐릭터 실행 슬라이스

`CharacterMovementComponent`는 SDK 객체를 소유하지 않는다. Scene의 단일 물리 세션이
캐릭터 등록·SDK 핸들·입력·중력·충돌 결과를 소유한다. 월드 속도(m/s)는 1/60초 고정
스텝에서 변위로 적분하며, 캐릭터 이동을 SDK simulation begin 이전에 수행한다.
최대 4회 catch-up, 초과 시간 계측, 바닥/천장 충돌의 낙하 속도 취소를 검증했다.
캡슐은 +Y 기준이며 양의 균일 world scale만 허용한다. 시각 회전은 유지한다.

공통 LayerCatalog/PhysicsCollisionPolicy 변경은 body와 character를 함께 검증하고
SDK 적용 전체 성공 후 게시한다. 다른 레이어 간 CCT 접촉을 SDK query mask가 먼저
제외하던 결함을 수정했다. 캐릭터 접촉은 양방향 collision policy 콜백이 판정한다.
동일 Entity의 PhysicsBodyComponent/CharacterMovementComponent 동시 부착은 Entity
추가 경계에서 거부한다. 비활성화는 SDK generation을 퇴역시키며 재활성화는 보유한
상태에서 재생성한다. Stop은 authored 위치·입력·낙하 속도·tick을 복원한다.
DDOL 전달은 상태만 복사하며 Scene SDK 핸들을 승계하지 않는다.

`verify-physics-c0.ps1 -Configuration All -RequireGpu`: 실제 CPU/GPU에서
Debug/Release/ASan 각 752 checks, Shipping 746 checks 통과.
`Physics.CharacterFixedStep` 아래 `Physics.CharacterMovement`의 부모/자식 capture도 검증했다.
`verify-physics-p3.ps1 -Configuration Debug -RequireGpu`: 3971 checks/GPU 통과.
증거는 `Build/Obj/Phase19C0/c0-all.log`, `Build/Obj/Phase19C0/p3-debug.log`와
`Build/Obj/Phase19C0/<configuration>/baseline.ceprof`다.

C0 상태는 progress다. native standalone 결과는 Editor HTTP·Player·cooked·C#·직접
Inspector·DDOL 제품 실행의 완료 증거가 아니다. Editor 명령 `character.state`,
`character.velocity`, `character.teleport`와 HTTP 게이트를 추가했다.
C# 캐릭터 ABI/wrapper와 C1 단위 이전·경사/계단/강제 이동, 점프/낙하 속도 제한·동적
바디 push·센서 정책은 후속 작업으로 남는다. 기존 CCT 호환 표면은 복원하지 않는다.


#### C0 Editor HTTP 제품 검증

전체 Editor Debug 빌드가 통과했다. 새 타입은 생명주기·UUID·reflection typed 정본에
등록했고 Editor Physics 목록에도 추가했다. `verify-physics-character-http.ps1`은
41개 명령을 통과했다. 저장/로드, 바디·캐릭터 동시 부착 거부와 Undo 깊이 불변,
Editor 제어 거부, Play 속도 입력/NaN 거부/저작값 동결, 바닥 접촉, teleport 결과 초기화,
Entity 비활성화 상태 유지/재활성화 SDK 생성, Stop 위치·입력·낙하 속도·tick 초기화,
컴포넌트 제거 뒤 접근 거부를 실제 HTTP 제품 경로에서 확인했다.

위치는 `[0,3,0]` → `[0.683333,1.05,0]` → `[0,3,0]`, 마지막 발 위치 Y는
`8.9407e-8`, tick은 41이었다. 증거:
`Build/Obj/Phase19C0/http-1a0cd530a3784c89b88fe9308443984f/result.json` 및 `results.jsonl`.
재사용 fixture는
`Dynamic_CPP/Assets/Scenes/PhysicsCharacterGate-1a0cd530a3784c89b88fe9308443984f.creator`다.
기존 `verify-physics-shape-http.ps1`도 같은 새 Editor에서 통과했다.
이는 Editor native C0의 제품 증거이며 C#·Player·cooked·직접 UI·DDOL·C1의 완료 판정은 아니다.


### 2026-10-02 C0/M0 C# 캐릭터 실행 경계

Script ABI를 30으로 올리고 `Character_Find/Read/Velocity/Teleport` 4슬롯을 추가했다.
네이티브/C# API table 164슬롯의 순서와 초기화가 일치한다. 상태 wire는 64 bytes이며
위치·발 위치·실제 변위·원하는 속도·낙하 속도·충돌/활성 flags·64-bit tick을 반환한다.
C++ size/offset static_assert와 C# size/offset 검사를 함께 둔다.

`Entity.GetComponent<CharacterMovementComponent>()`와 명시적 ID Find를 지원한다.
래퍼는 owner generation과 component instance identity를 보존하고 매 호출에서 재검증한다.
제거·Play/Stop 교체 뒤 새 컴포넌트로 재지정하지 않는다. native activation은
`Entity.SetEnabled`로 통일하고 `Component.Enabled`를 통한 잘못된 제어는 예외로 거부한다.
게임 스레드 밖 호출·비활성/비실행 이동·잘못된 숫자는 명시적 PhysicsError로 반환한다.
상태 read 실패 출력은 0으로 초기화한다. 입력은 m/s이며 teleport는 낙하 속도와 이전
충돌 flags를 초기화하고 원하는 속도는 유지한다. SDK 핸들은 C#에 노출하지 않는다.

검증:
- 전체 Editor Debug 빌드와 실제 CLR TU Release 컴파일 통과.
- ScriptCore/GameScripts Debug/Release 빌드 통과.
- ABI gate Debug/Release 각 25 checks, API table 164슬롯 통과.
- `verify-physics-character-http.ps1 -ScriptProbe`: 실제 Editor HTTP 45명령 통과.
- GameScripts `CharacterMovementProbe`의 실제 PostPhysics 12 assertions 통과:
  조회·실행 상태·identity·잘못된 identity·속도/상태 왕복·NaN 거부/입력 유지·teleport/상태
  왕복·foreign-thread 거부·activation 경계. 최초 완료 tick까지 기다려 검증한다.
- Stop 뒤 보관한 wrapper의 read/velocity/teleport는 모두 StaleHandle, read 출력 tick=0.
  기존 Editor 위치·입력·tick 복귀 및 enable/disable 회귀도 같은 HTTP에서 통과했다.

증거는 `Build/Obj/Phase19C0/character-abi.log`, `character-editor-debug.log`,
`character-clr-release.log`, `character-gamescripts-{debug,release}.log`,
`http-6a31c335bb074130a79ed3a9abdd9746/result.json`과 `results.jsonl`다.
fixture는 `Dynamic_CPP/Assets/Scenes/PhysicsCharacterGate-6a31c335bb074130a79ed3a9abdd9746.creator`다.
HTTP 서비스는 이 스크립트 게이트에서만 `--allow-user-code`로 시작한다.

이 항목은 앞의 C# 연결 미완료 기록을 갱신한다. C0/M0 전체는 progress를 유지한다.
다음은 C1의 단위 이전 정책·경사/계단·강제 이동 회귀다. Player/cooked/직접 Inspector/
DDOL 제품 회귀, 캐릭터 기즈모 및 점프/낙하 속도 제한·push·센서 정책은 별도 미검증이다.


### 2026-10-02 C1 단위 정책·경사/계단/강제 이동 회귀

구 `CharacterController::Update`는 이름이 velocity인 `GetOutVector`와 forced velocity를
`PxController::move`의 displacement에 그대로 전달했다. `CharacterMovement`는 내부 값을
`acceleration*dt`, `gravityWeight*dt`로 갱신한다. 기준 스텝 시간 h를 반드시 명시하여:

| 구 값 | 새 단위/환산 |
|---|---|
| maxSpeed/jumpSpeed/forced velocity | m/s = 구 값 / h |
| acceleration/gravityWeight | m/s² = 구 값 / h; gravity는 음의 Y |
| static/dynamic friction lerp 계수 f | 초당 감쇠율 = -ln(1-f)/h; f=1은 즉시 정지 |
| controller height/radius/step/contact/minDistance | m 유지; height는 원통 구간 |
| slopeLimit | cosine 유지, degree로 해석하지 않음 |
| forced duration | s 유지, 속도 입력 유지/종료 시점은 명시적 gameplay 정책 |

`Tools/regression/report-character-unit-migration.py`는 읽기 전용 offline 보고서다.
`--baseline-seconds`는 필수이며 모든 필드/변환이 유한하고 유효해야 output을 만든다.
Scene/Prefab을 덮어쓰지 않으며 입력 SHA256과 source path/instance ID를 보존한다.
Python+PyYAML이 필요하다. `verify_character_unit_migration.py`는 22 checks를 통과했다.

P0 baseline의 캐릭터 1건을 명시적 60Hz 기준(h=1/60)으로 보고했다.
현재 TimeSystem 기본값은 `TicksPerSecond/60`이나 사용자의 프로젝트 설정을 추론하지 않는다.
`maxSpeed=1.025`는 61.5m/s인 반면 구 LateUpdate는 `baseSpeed=.025 * multiplier=1`로
정상 최대 속도를 다시 덮어써 1.5m/s가 된다. 두 값을 분리하며 자동 채택하지 않는다.
중력은 -12m/s², jump 속도 3m/s, 가속도 60m/s²다. 초기 입력은 0이다.
contact .1m/step .001m/slope .7/minDistance .01m은 구 SDK 생성/이동 소스 기본값으로
표시하며 별도 runtime override가 있었는지 검토해야 한다. 새 기본값 .3m step으로
무조건 승계하지 않는다. 마찰·가속·점프·자동 회전·시간제 forced move·offset/scale 및
동일 Entity의 구 RigidBody 소유권은 explicit 정책 검토 항목이다. 자동 이전 결과가 아니다.
P0 원본 해시 `ae03fa05b90e7602bb9b0497aaab3ea7449baa20e94140e99b5251ebf945c021`은 유지됐다.
보고서: `Build/Obj/Phase19C1/p0-unit-report.json`.

`verify-physics-c1.ps1 -Configuration All -RequireGpu`는 실제 ScenePhysicsSimulation과
PhysX에서 CPU/GPU를 실행했다. Debug/Release/ASan 각 2020 checks, Shipping 2018 checks.
20° static triangle mesh는 45° 제한 아래 상승(x=3m, footY=1.124m), 60°는 막힘(x=-.3m),
cosine=0이면 60°를 상승(x=3m, footY=5.69615m). stepOffset=.3m에서 .2m 계단을 넘고
.8m 계단은 x=-.511598m에서 막힌다. 구 body 없이 독립 capsule이 수행한다.
명시적 6m/s 입력의 .5초 변위=3m와 중력 누적 -4.905m/s, 입력 취소 뒤 수평 정지,
NaN teleport 거부/상태 유지, 정상 teleport의 handle 유지/낙하·충돌 초기화,
다음 fixed tick의 중력 재개 및 Stop authored 위치 복귀를 검증했다.
C1 캡처는 완료/저장까지 검사하며 SDK 내부 GPU 시간의 계측 증거가 아니다.
로그/캡처: `Build/Obj/Phase19C1/c1-all.log`, `<configuration>/baseline.ceprof`, `stderr.log`.

`verify-physics-character-http.ps1 -StepProbe`는 실제 새 Editor에서 80명령을 통과했다.
Scene 저장/로드 후 낮은 계단 위 발 위치와 통과, 높은 계단 측면 충돌/정지를 검증했다.
고계단 앞 x=4.954m, 실제 수평 변위 .000341415m였고 Stop 뒤 `[0,3,0]`으로 복원됐다.
증거: `Build/Obj/Phase19C0/http-ba5a75a82fe9411a8136a64cee953592/result.json` 및 `results.jsonl`.

C1은 progress다. 이 회귀는 native 경사·계단과 Editor 계단 제품 증거다. 실제 콘텐츠의
legacy 동작 채택/저작 파일 변경, 점프·감쇠·시간제 강제 이동 정책, Editor 경사 mesh
저작/제품 게이트·Player·cooked·직접 UI·DDOL는 완료로 판정하지 않는다.


### 2026-10-02 C1 가속·감쇠·점프·시간제 강제 이동 구현

CharacterMotionPolicy.h의 순수 적분 정책을 Scene 세션에 연결했다. 입력/실제 정책
속도를 분리하며 가속도 벡터 한계, 초당 지수 감쇠, grounded one-shot jump,
누적 낙하 속도 제한, 시간제 forced override/취소 및 마지막 부분 스텝을 구현했다.
모든 시간은 완료 fixed step의 시뮬레이션 시간이다. disabled/no-step 타이머 정지,
Teleport/Stop 초기화와 DDOL motion memory 전달을 연결했다. 실제 DDOL 제품 게이트는 미검증이다.
새 저작값은 reflection 저장/로드와 검증에 연결했고 invalid 값의 property transaction은
원복·Undo 불변으로 거부한다. 구 runtime 호환 adapter를 추가하지 않는다.
C# ABI31 및 HTTP jump/force/cancel을 연결했다. 세부 단위와 동작은 API 계약의 C1 절을 따른다.

이 구현은 앞 절에서 후속으로 남긴 가속/감쇠/점프/시간제 forced 기능을 갱신한다.
offline 이전 보고서에도 새 m_acceleration/m_brakingDecay/m_jumpSpeed/m_maxFallSpeed
제안을 추가했다. 보고서는 여전히 review_required다. 새 최대 낙하 속도 55m/s는
legacy에서 추론한 값이 아니며 dynamic lerp·내부 unclamped 속도의 구 동작을 복제하지 않는다.
기준 tick과 serialized/steady maxSpeed 불일치, companion body 소유권은 실제 이전 시
명시적 선택이 필요하다. 원본 P0 fixture는 변경하지 않았다.


#### C1 motion 구현 검증 결과

`verify-physics-character-motion.ps1 -Configuration All -RequireGpu`:
Debug/Release/ASan 각 2276 checks, Shipping 2274 checks. CPU와 실제 GPU를 함께 실행했다.
가속 한계/반초 변위, 감쇠 반감기/분할 동등성/zero decay, 낙하 속도 제한, 완료 ground
점프/중복·공중 거부/no-step 요청 유지, partial final forced step의 정확한 .25m 변위,
시간 만료/취소/disabled timer 동결/재개, invalid 입력·Teleport/Stop 상태 초기화,
기존 static mesh 경사와 낮은/높은 계단을 검사했다.

전체 Editor Debug 빌드, 실제 CLR Release TU, ScriptCore/GameScripts Debug/Release 빌드 통과.
ABI gate는 167슬롯 순서/초기화와 Debug/Release 각29 checks를 통과했다.
`verify-physics-character-http.ps1 -ScriptProbe -MotionProbe`: HTTP67명령, C#18 assertions.
정책 저작/invalid 원복/Undo 불변·저장/로드·grounded C# Jump→상승→착지·공중 거부,
C#/HTTP Force/Cancel·자동 만료·teleport·disable/enable·Stop 복귀 및 Stop 뒤
captured wrapper의 Read/Velocity/Teleport/Jump/Force/Cancel 전부 StaleHandle을 확인했다.
위치는 시작/Stop 모두 `[0,3,0]`이다.
증거: `Build/Obj/Phase19C1Motion/{motion-all,abi,editor-debug,clr-release,scripts-debug,scripts-release}.log`,
`Build/Obj/Phase19C0/http-11f1b03761ab4128bac7316af8bd0c27/result.json` 및 `results.jsonl`.
C0 Debug 회귀 752 checks/GPU도 통과했다. 구 소비자 lexical 0파일과 저작 소유권 SourceOnly 통과.

C0/C1/M0는 progress를 유지한다. 남은 것은 실제 구 콘텐츠의 정책 채택/소유권 이전,
Player·cooked·직접 Inspector·DDOL 제품 게이트, 캐릭터 preview/push/센서와 자동 회전 정책이다.
이동 기능 구현을 하지 않은 항목으로 되돌려 표기하지 않는다.


### 2026-10-02 캐릭터 Inspector·기즈모 연결

캐릭터 Inspector는 일반 리플렉션 직접 변경 대신 공통 `EditorObjectOperations::Property`를
사용한다. 필드별 단위와 +Y·양의 균일 월드 스케일 계약을 표시하며, 포커스 이탈 시
검증 후 Undo/Redo에 기록한다. Play와 저작 잠금 중에는 필드 및 개별 enable 편집을 막는다.
캡슐 치수·중력·최소 이동 거리·초기 속도까지 전체 정의 검증 대상으로 확장했다.
실패 시 이전 문서로 복구하고 Undo 기록을 추가하지 않는다.

런타임과 표시가 `CaptureCapsule()`의 월드 치수 계산을 공유한다. 기즈모는 Entity 회전과
무관한 실제 +Y 캡슐, contact 여유, SDK 계약의 foot 위치와 step 높이를 표시한다.
표시는 SDK 조회·등록·cook 없이 수행하며 `Physics.GizmoCollect → Physics.CharacterPreview`
계층을 계측한다. 공통 레이어 설정 유무가 기하 표시를 막지 않는다.

SDK 없는 실제 선 수집기 회귀는 Debug/Release/ASan 각각 29 checks를 통과했다.
직접 마우스 Inspector 검증 및 렌더 완료 후 캡처, DDOL·Player·cooked 제품 수명 게이트는
별도 잔여이며 C0/C1 완료로 판정하지 않는다.

최종 제품 연결 검증: 전체 Editor Debug 빌드 성공
(`Build/Obj/Phase19PrimitivePreview/editor-debug.log`). 새 런타임 HTTP 74명령 및 실제
PostPhysics C# 18 assertions 성공. 캡슐 저작 오류 5종의 거부/Undo 불변, 초기 속도
문자 입력, Play 중 저작 거부, 이동·점프·force, Stop `[0,3,0]` 복귀와 이전 wrapper
StaleHandle을 확인했다. 증거는
`Build/Obj/Phase19C0/http-125caf6a26e24c42966eaa77ddf6eff5/result.json`이다.
이는 Inspector가 사용하는 공통 편집 경로의 제품 검증이며 직접 UI 입력·렌더 픽셀 검증은 아니다.


### 2026-10-02 캐릭터 DDOL 제품 수명 게이트

`verify-physics-character-http.ps1 -ScriptProbe -MotionProbe -DdolProbe`가 서로 다른
저작 Scene과 빈 목적지를 생성/저장해 실제 Editor 전환 경로를 검증한다. 활성 캐릭터의
component instance·원하는 속도·motion velocity·시간제 force 잔여를 이송한 뒤 새 Scene에서
고정 tick/이동/중력/타이머 감소가 재개된다. 목적지에는 이전 바닥이 없으므로 낙하를 확인한다.
두 번째 전환은 비활성 캐릭터가 이동하지 않으며 새 Scene에서 다시 활성화되는지 확인한다.
Stop은 원래 `[0,3,0]`과 초기 runtime 상태를 복원한다.

Scene-scoped CLI EntityHandle은 소유 Scene 변경 시 만료한다. C# ScriptObjectHandle은
Entity 수명을 따르므로 동일 DDOL Entity의 기존 CharacterMovementComponent wrapper가
새 Scene 상태를 계속 읽어야 한다. 이송 시 stale을 강제하지 않는다. Stop으로 Entity가
교체된 후에는 이전 wrapper의 Read/Velocity/Teleport/Jump/Force/Cancel이 모두 stale이다.

최종 새 Editor HTTP 96명령·기본 C# 18 assertions 및 captured-wrapper DDOL 검사 통과.
증거: `Build/Obj/Phase19C0/http-b1f358f2e29347389b60c43fbbbd7878/result.json`,
`Build/Obj/Phase19PrimitivePreview/character-ddol.log`.
GameScripts Debug/Release 빌드와 Player 전체 Debug 빌드도 통과했다.
Player는 시작 전 runtime session policy를 선택하고 공통 fixed simulation 경로를 사용한다.
이 빌드/소스 확인을 새 cooked 패키지의 캐릭터 실행 증거로 세지 않는다. Player/cooked 제품
실행과 DDOL 자식 계층·실제 콘텐츠 이전·직접 UI 검증은 잔여이며 C0/C1 progress 유지.

최종 gate는 레이어 ID 보존, 강제 속도×경과 simulation time과 위치 연속성, 누적 속도,
목적지의 바닥 부재에 따른 낙하까지 단정한다. 명령 수에는 고정스텝 완료를 기다리는 상태
조회가 포함되어 실행마다 달라질 수 있다. Player 빌드 직후 공용 forest 배포 recipe 불일치로
Editor 기동이 한 번 실패했으며, 기존 검증용 cooked 환경 자산 복원 후 위 새 실행이 통과했다.
렌더링 원본 자산은 변경하지 않았다.


### 2026-10-02 Debug cooked Player 캐릭터 제품 검증

고정 Scene `Tools/regression/fixtures/PhysicsCharacterPlayer.creator`와
`CharacterPlayerProbe`를 격리한 프로젝트에 복사하고 새 Debug 배포본으로 CEDO1/CEMF/pak를
만드는 `build-physics-character-player-fixture.ps1`을 추가했다. 입력은 추적된 Prim_Cube
모델·기본 셰이더·공통 레이어 설정이며 기존 게임 프로젝트/엔진 pin을 변경하지 않는다.
Primitive 바닥·독립 캐릭터·카메라의 4 Entity/8 component Scene이다.

실제 PostPhysics에서 완료 tick, 바닥 접촉, 입력·가속 이동, grounded jump·상승·공중 재점프
거부, 착지, 시간제 force·만료 이동량, teleport와 motion reset을 12 checks로 검증한다.
일반 상태 조회의 반복 횟수는 검사 수에 포함하지 않는다.
`player.scene`의 읽기 전용 진단에 simulating/editorSceneLoaded/hasAuthoringSnapshot을 추가했다.
`verify-physics-character-player.ps1`은 소유 PID/token endpoint만 사용하고, 시작/완료 시
Editor restoration 상태와 snapshot이 명시적으로 false임을 확인한다. normal quit exit=0과
immutable package 전체 파일 집합/해시도 대조한다.

검증: Player·AssetCooker·AssetPacker·BuildTool Debug 및 GameScripts Debug/Release 빌드 통과.
새 로컬 배포 buildId `38cc3d85-0f0f-48f7-97ae-827d4e00220a`에서 package smoke 통과:
CEDO1 runtime documents 4개, CEMF cooked catalog, authoring text-parser calls=0.
동일 최종 package의 smoke 및 독립 Player 실행 각각 12 checks/failed=0/tick=138.
독립 실행은 simulating=true, editorSceneLoaded=false, hasAuthoringSnapshot=false를 시작/완료
모두 확인하고 exit=0, package 243 files byte-identical을 검증했다.

증거: `Build/Obj/Phase19Player/package-final.log`,
`Build/Obj/Phase19Player/Fixture4/Staging/Game-ed0be4b7a0fb4582b564d3a898f4daea`,
`Build/Obj/Phase19Player/run-32b02568625f42a1a90ab95c7919f926/result.json`.
첫 후보의 필수 셰이더 입력 누락은 fixture 입력 수정으로 해결했다. 배포 직전 forest recipe는
기존 검증된 cooked artifact를 사용했다. 제품 원본 렌더 자산은 변경하지 않았다.

범위는 Debug CPU primitive/CCT cooked Player다. Release/Shipping, cooked mesh/geometry 충돌,
Player DDOL 전환, 실제 콘텐츠 이전 및 DDOL 자식 계층/직접 Inspector는 잔여다.
C0/C1/M0 전체를 완료로 판정하지 않으며 progress를 유지한다.


### 2026-10-02 Editor DDOL 자식 계층 회귀

`verify-physics-character-http.ps1 -ScriptProbe -MotionProbe -HierarchyProbe`를 추가했다.
HierarchyProbe는 DdolProbe를 포함하며 HTTP로 회전30°/균일2배 스케일의 PersistentParent,
활성 CharacterGateActor 자식, 중간 PersistentBranch, 비활성 캐릭터/강체 손자를 저작한다.
손자 캐릭터는 Enemy, 강체는 Ground 공통 레이어를 사용한다.

자식 CharacterGateActor에 DDOL을 지정해 상위 부모 승격·서브트리 이송을 실제 실행한다.
모든 노드의 새 Scene 소속, 이전 CLI 핸들 거부, component instance/공통 레이어/활성 상태,
parent remap과 children 정확한 신원·개수를 검사한다. 정적 부모·손자의 월드 위치·회전·스케일
보존, 활성 자식의 motion 연속성과 목적지 낙하, 기존 C# wrapper 유지도 함께 확인한다.
비활성 손자 캐릭터는 입력 2m/s와 위치가 보존되고 새 Scene에서 SDK controller를 활성화할
수 있다. 두 번째 씬 이송도 기존 DDOL gate를 통과한다.

Stop 후 전체 저작 계층의 월드 Transform·레이어·활성 상태와 부모 관계를 복원한다.
비활성 저작 컴포넌트는 Stop 직후 물리 세션에 미등록일 수 있으므로 재Play에서 tick=0/
simulating=false/initial input=2를 확인하고, 재활성화 및 두 번째 Stop의 disabled 복원을 검증한다.
회전 부모의 왕복 좌표는 float 환산 차이를 허용하는 0.0001m 허용 오차로 비교한다.
약1e-7m 차이를 상태 손실로 판정하지 않는다.

최종 Editor Debug HTTP 145명령·실제 PostPhysics C# 기본18 assertions 성공.
증거: `Build/Obj/Phase19C0/http-b61dd1be206b411e829ace6b7a167c83/result.json`,
`Build/Obj/Phase19Hierarchy/http.log`. 제품 런타임 수정 없이 회귀 gate를 확장했다.
Editor의 위 계층 범위이며 Player DDOL 전환, 동적 물리 부모의 중첩 제어·지원 정책,
직접 UI/렌더 캡처, Release/Shipping/cooked mesh 및 실제 콘텐츠 이전은 잔여다.
C0/C1 전체 완료로 판정하지 않는다.
### 2026-10-02 C0/C1 — 실제 Inspector 입력·폭 전환·기즈모 화면 검증

실제 Editor UI에서 긴 CharacterMovement 필드 이름이 오른쪽으로 넘치고 초기 속도가
`{x: 0, y: 0, z: 0}` 문자열로 표시되는 문제를 확인했다. 공통 property_sheet 배치로
12개 필드의 라벨·값 폭과 좁은 폭의 세로 배치를 통일했다. 복합 직렬화 스칼라는 문서
수명을 유지하며 읽어 초기 속도를 Property 입력과 같은 `x, y, z`로 표시한다.
안내·실패 문구도 가용 폭 안에서 줄바꿈한다. 물리 핫패스 변경은 없다.

전체 Editor Debug 빌드 성공. Sky 실제 마우스·키 입력으로 Radius 변경을 커밋하고
저장 파일/Undo/Redo를 확인했다(0.5 → 입력 75 → Undo 0.5 → Redo 75).
UI의 -1 입력은 Invalid character capsule을 표시하고 값 75를 복원하며 Undo 깊이 6을
유지했다. Play 중 필드/개별 enable가 비활성이고 실제 클릭·키 입력에도 값/Undo가
변하지 않았다. simulating=true를 읽었으며 Stop 후 radius 0.5와 편집 가능 상태를 확인했다.

uiScale 2.25의 실제 패널 및 논리 폭 240에서 12개 property lines, overflow=0,
최소 값 폭 각각 410.5/495px를 확인했다. Scene 화면에서 cyan 물리 캡슐, contact 경계,
green foot, amber step 표시를 직접 확인했다. `render.live.fence`의 afterFrame 38548보다
completedFrame 38549가 커진 뒤 새 화면을 캡처했다. 좁은 배치 캡처도 별도 완료 대기 후
보관했다. 초기 카메라 전환 시 sync 서비스 5초 제한을 만난 요청은 완료 증거로 사용하지
않았으며 최종 async fence 성공을 사용했다. 보류한 RHI 내부 capture 이슈의 해결 주장은 없다.

증거: `Build/Obj/Phase19InspectorUI/result.json` 11개 증거 단정 통과,
`commands.jsonl`, `build.log`, `inspector-gizmo.png`, `inspector-narrow.png`.
이는 실제 UI 회차와 저장된 증거에 대한 검증이며 무인 UI 자동화 gate 전체를 뜻하지 않는다.
C0/C1은 진행 상태 유지. Release/Shipping cooked Player, Player DDOL, cooked mesh,
실제 콘텐츠 이전과 지원 정책 확정은 잔여다.
최종 수정 빌드에서 `verify-physics-character-http.ps1 -ScriptProbe -MotionProbe`도 재통과했다. 증거: `Build/Obj/Phase19C0/http-f0d9d91d1fe54f07aaa8402624ff4e31/result.json`.
### 2026-10-02 C0/C1 — cooked Player Release / Shipping 실행 검증

고정 `PhysicsCharacterPlayer.creator`를 Release와 Release+EngineShipping=true의 각각
검증된 엔진 배포본으로 cook/pack했다. Release Editor/Player/AssetCooker/AssetPacker,
Shipping Player 및 BuildTool 빌드 성공. Jolt나 별도 물리 레이어 경로는 추가하지 않았다.
`build-physics-character-player-fixture.ps1`에 Shipping 패키징 분기를 추가했고 단일
`--shipping` 인자가 문자별로 splat되지 않도록 string[]으로 전달한다.

`verify-physics-character-player.ps1`의 Development 경로는 실제 HTTP player.scene으로
simulating=true/editorSceneLoaded=false/hasAuthoringSnapshot=false를 확인하고 quit한다.
Shipping 경로는 서비스가 컴파일에서 빠지므로 HTTP를 기다리지 않고 실제 PostPhysics
C# 12개 검사, 요청한 2000 GT 프레임/최소8 display promotions 이후 smoke 종료,
서비스 compiled=no/enabled=no, endpoint 없음, text-parser calls=0을 확인한다.
Shipping의 저작 스냅샷 여부를 HTTP로 관측했다고 주장하지 않는다.

두 패키지 모두 CEMF/cooked Scene, jump/airborne rejection/landing/force expiration/
teleport reset 등을 실제로 실행해 각12/0, tick138, 종료0, 238파일 집합·바이트 보존 통과.
Release의 개발용 서비스와 Shipping 서비스 부재는 별도 import/string 격리 gate에서도
확인했다(Development WS2_32 있음 / Shipping 없음; WSAStartup/endpoint.json/
CommandService 문자열도 Development에 있고 Shipping에 없음).

Release 배포 ID df7f2fac-fdb4-4376-86cc-2c621dad5929,
최종 Shipping 배포 ID db6d4d8c-71cd-42bc-9bae-065236d7eedd.
Shipping 첫 패키징 시도의 인자 전달 오류 및 긴 .NET 배포 경로의 파일 매핑 오류는
최종 성공에 포함하지 않았다. 새 `Build/Obj/P19RS`의 짧은 격리 경로에서 재실행했다.
로컬 --no-pointer 배포본이며 저장소 push/공식 배포 포인터 변경은 없다.

증거: `Build/Obj/Phase19PlayerRelease/result.json`(최종 묶음), 같은 폴더의 빌드/
package-release/package-shipping-short/gate-release/gate-shipping/shipping-isolation 로그.
독립 실행: `Build/Obj/Phase19Player/run-f92825a607a3495993e07675ec13bf11/result.json`,
`Build/Obj/Phase19Player/run-bddea89074bb401ba67bfd5280b9e327/result.json`.
기존 Debug 패키지도 수정 verifier로 재검증해12/0·종료0·243파일 보존을 통과했다.

이번 증거는 DX12·CPU CCT·primitive 충돌·고정 cooked 캐릭터 씬의 D/R/Shipping 실행이다.
Player DDOL 전환, cooked mesh 물리, 동적 물리 부모 정책, 실제 콘텐츠 이전과 성능 수용은
잔여이며 C0/C1/M0 전체 완료로 올리지 않는다. 다음은 Player DDOL 전환의 실행 증거다.

### 2026-10-02 Player DDOL cooked 재로드 실행 증거

Debug Player의 기존 --smoke-reload 경로에 --smoke-ddol-character를 연결했다.
첫 display promotion 이후 같은 cooked startup Scene을 비동기로 준비하고,
활성화 직전 대상의 Teleport/desired velocity/10초 force를 설정해 DDOL 루트 계층을 이송한다.
새 씬의 native binding과 30 fixed ticks, 활성화 이후 completed display를 기다린 뒤 정상 종료한다.
새 공개 C# API나 Editor rollback 경로를 추가하지 않았다.

실제 패키지의 기본 motion 12/0(tick138), DDOL 8/0(tick30) 통과.
기존 C# wrapper를 재조회하지 않고 사용해 OnAdded 2회/OnRemoving 1회,
Simulating, desired X=1.25, force 잔여 시간, 강제 이동/중력/낙하와 새 fixed tick을 검증했다.
종료0, 243파일 집합 및 SHA256 보존, cooked Scene 2회 로드, CEMF,
서비스 compiled=yes/enabled=no와 text-parser calls=0을 확인했다.
DDOL 검증은 HTTP를 켜지 않으므로 snapshot 상태를 HTTP로 관측했다고 주장하지 않는다.

증거: Build/Obj/Phase19Player/run-e5daef0567c84a5fa0d92288a13a1f5b/result.json,
player.out 및 Runtime 로그. 빌드/격리 배포/패키징 로그는
Build/Obj/Phase19Player/ddol-{build,publish,package,gate}.log.
로컬 배포 d64b66ac-b62e-4cd6-a638-12bceca33f3d, Player.runtime SHA256
779E83BB742C5B0EDDBC57B18579C738EE66344363FE06EB59A78353F7809ACA.

범위는 Debug/DX12/CPU CCT/primitive/동일 cooked 씬 재로드의 활성 DDOL 루트다.
서로 다른 목적지, Player 비활성 자식 계층 및 stale Scene CLI handle,
Release/Shipping DDOL은 별도 실행 증거가 필요하다. C0/C1/M0는 progress를 유지한다.

### 2026-10-02 서로 다른 cooked 목적지로 Player DDOL 전환 — D/R/Shipping

Editor HTTP CLI scene.new/object.create/component.add/object.property/object.transform/
scene.save로 PhysicsCharacterDestination 씬과 primary DestinationCamera를 저작했다.
목적지 GUID 2ef81ba4-399d-4170-bd85-931c3264b423은 출발지 GUID와 다르며,
목적지에는 PhysicsBody/CharacterMovement 컴포넌트가 없다. fixture와 meta를 고정하고
격리 프로젝트 패키징에 포함했다. --smoke-reload-destination으로 기존 비동기 준비/
owner 활성화 경로를 사용한다. C# 공개 API/ABI 변경은 없다.

Debug/Release/Shipping을 새로 빌드하고 --no-pointer 로컬 배포본으로 패키징했다.
각 실제 PostPhysics 기본12/0(tick138) + DDOL8/0(tick30), 다른 cooked GUID 로드,
전환 후 completed display, 종료0 통과. 패키지 파일 집합과 SHA256도 각각
243/238/238개 보존했다. CEMF entries13/sources111, runtime text-parser calls=0.
기존 C# wrapper를 사용해 OnAdded2/OnRemoving1, Simulating, desired X=1.25,
force 잔여9~9.51초, X=1.4~1.8/Y=3.3~4.2 범위, 낙하와 새 physics tick을 확인했다.

Shipping은 서비스 compiled=no/enabled=no, endpoint 없음 및 최소8 display promotions
뒤 정상 종료를 추가로 확인했다. 새 Development/Shipping 바이너리의 WS2_32/import와
WSAStartup/endpoint.json/CommandService 문자열 격리 gate도 통과했다.
이번 DDOL 실행은 모든 구성에서 HTTP를 켜지 않았다. 저작 snapshot 여부에 관한
기존 Development 관측은 앞선 검사 증거이며 이번 실행의 관측값은 null이다.

첫 목적지 fixture에는 카메라가 없어 physics8/0 이후 표시 대기 timeout이 발생했다.
Object::SetDontDestroyOnLoad는 synthetic Scene root(index0)를 이송하지 않는다.
이 출발지의 캐릭터는 Scene root 직속이므로 캐릭터만 이송되며, 출발지의 바닥과
카메라는 이전 씬과 함께 정리된다. 목적지 카메라를 CLI로 저작한 뒤 재검증했다.
실패/진단 실행은 최종 성공에 포함하지 않았다. 저작 Editor 종료도 timeout 후
소유한 프로세스만 정리했으므로 Editor 정상 종료 검증으로 계산하지 않는다.

최종 묶음: Build/Obj/P19DD2/result.json. 개별 실행:
- Debug: Build/Obj/Phase19Player/run-5ad7919c947e40c2bcf7a4f3342ccba8/result.json
- Release: Build/Obj/Phase19Player/run-d472809deec44c369749ac226d4efa7c/result.json
- Shipping: Build/Obj/Phase19Player/run-bd641ad77a114af4b531b6bd366b5c91/result.json
빌드/저작/격리 로그는 Build/Obj/P19DD2, Debug/Release 배포·패키징 로그는
같은 폴더의 D2/R, Shipping은 긴 Windows .NET 경로를 피한 Build/Obj/DD/S에 있다.

범위는 DX12/CPU CCT/primitive와 활성 top-level 캐릭터 1개의 전환이다.
Player 비활성 자식 계층, stale Scene CLI handle, cooked mesh 물리,
실제 콘텐츠 이전과 성능 수용은 잔여다. C0/C1/M0는 progress를 유지한다.

### 2026-10-02 Player DDOL 비활성 자식 계층·Scene 핸들 — D/R/Shipping

Editor HTTP CLI로 PhysicsCharacterHierarchy를 저작했다. GUID는
af55790d-53c0-43b6-b4dc-6515b414e0e1이며 회전·균일 스케일 부모 아래의 활성 캐릭터,
중간 부모, 비활성 CharacterMovement/PhysicsBody를 포함한 5개 노드를 이송한다.
기존 Enemy/Ground 레이어를 사용한다. 목적지는 앞선 별도 cooked 카메라 씬이다.
synthetic Scene root는 이송하지 않으며 실제 부모 계층의 pointer identity를 유지한다.

최종 소스의 Debug/Release/Shipping 빌드, 격리 배포와 패키징 뒤 각 76/0 통과:
기본 이동12/0(tick154), 기존 C# wrapper 이송8/0(tick30), native 계층56/0(tick90).
5개 노드의 owner, 이전 Scene EntityHandle 거부, 새 핸들 resolve, 컴포넌트 identity,
레이어·enabled, world pose와 부모·순서 있는 자식 remap을 검증했다.
비활성 자식 입력/상태 및 SDK 핸들 부재, 재활성화 시 controller/body 재생성,
중력·이동과 force 시간 진행, 다시 비활성화 후 자세·tick·양수 force 잔여 시간의
정지를 확인했다. Scene별 SDK 핸들 숫자가 반드시 달라야 한다고 가정하지 않는다.

기존 --smoke 2000 / 최소8 promotions와 slot rotation, 목적지 활성화 이후
completed display를 유지했다. Offscreen을 사용하지 않았다. 세 구성 모두 종료0,
243/238/238개 패키지 파일 집합·SHA256 보존, CEMF entries14/sources112,
text-parser calls=0. Shipping 서비스 compiled=no/enabled=no, endpoint0 및
최종 바이너리의 Development/Shipping 소켓·서비스 import/문자열 격리도 통과했다.
DDOL 실행은 HTTP를 켜지 않으므로 저작 snapshot 관측값은 null이다.

첫 계층 baseline은 절대 X좌표를 이동 거리로 사용해 실패했다. 시작 X 대비 변위로
수정했다. 이후 Development 계층 verifier가 Hierarchy→Ddol 정규화 이전에 실행
인자를 만들어 --smoke를 누락한 오류를 수정했다. 이 진단 timeout을 렌더러/RHI
실패로 계산하지 않는다. 실제 인자를 launch.json에 기록하며 실패/임시 진단 배포본은
최종 성공에 포함하지 않는다. 저작 Editor quit timeout도 정상 종료 증거가 아니다.

최종 묶음: Build/Obj/P19DH/result.json. 개별 실행:
- Debug: Build/Obj/Phase19Player/run-0a62075c972e4112ad2d1fda8edb1937/result.json
- Release: Build/Obj/Phase19Player/run-7cc031c9436a4fa0b53c3a89bb9b20ba/result.json
- Shipping: Build/Obj/Phase19Player/run-39359aa592d8475fb60c1260d1db8398/result.json
빌드/저작/격리 로그는 Build/Obj/P19DH, 배포·패키징은 D3/R 및 Build/Obj/DH/S.
범위는 DX12/CPU CCT/primitive와 고정 계층 fixture다. cooked mesh 물리,
실제 콘텐츠 이전·캐릭터 전체 회귀·성능 수용은 잔여이며 C0/C1/M0는 progress다.

### 2026-10-02 cooked triangle mesh 바닥 — 실제 Player D/R/Shipping

Editor HTTP CLI geometry.create/physics.shapes/object.transform/scene.save로
PhysicsPlayerFloor.cegeometry(revision1)와 PhysicsCharacterMesh를 저작했다.
geometry UUID 578cd3b3-bd67-450c-a1d1-786ee613f24f, 씬 GUID
40e1f75b-57d2-40d9-9e6f-1fc882869740. 바닥은 static PhysicsBody의 kind4
triangle mesh 1개이며 primitive 바닥을 남기지 않았다. 저장한 source/meta를
fixture로 고정하고 격리 프로젝트 cook→CEMF→Pak→Player 경로에 포함했다.

최종 Debug/Release/Shipping 배포본에서 각각 기존 실제 PostPhysics 이동·접지·
가속·force·낙하12/0(tick138), 해당 cooked Scene GUID 로드, 종료0 통과.
실행 중 RuntimeContent의 .cepg 1개와 SHA256, 저작용 .cegeometry 0개를 확인했다.
종료 시 임시 mount가 정리되므로 artifact 검사는 종료 전에 수행한다.
패키지 파일 집합·SHA256은 243/238/238개 보존. CEMF entries16/sources113.
Development 두 구성은 HTTP player.scene의 초기/최종 simulating=true,
editorSceneLoaded=false/hasAuthoringSnapshot=false를 관측했다.
Shipping은 서비스 compiled=no/enabled=no, endpoint0, parser0 및 기존
--smoke 2000/최소8 promotions와 정상 종료를 확인했다. HTTP 관측은 null이다.

기반 cooked geometry 검사도 Debug/Release/Shipping/ASan을 새로 빌드·실행하여
각1809 checks, gpu_verified=true 통과. convex/triangle/heightfield의 producer
출력, revision/source identity, 손상·잘림·SHA256 거부, source/loose artifact 제거
후 CEMF/Pak 읽기·CPU/GPU SDK import·ray hit·해제, import3/cook0를 검증했다.

첫 기반 Release/Shipping 실행은 별도 두 cook의 SDK blob 전체 바이트 동일성에서
실패했다. 검사에 source_location 진단을 추가하고 실제 producer 출력의
revision/종류/source hash를 검증한 뒤 그 출력 자체를 CEMF/Pak/import에 사용했다.
개별 artifact의 SHA256 및 손상 거부 검사는 유지했다. 재cook 간 blob 바이트
결정성을 확인했다고 주장하지 않는다. 첫 CLI geometry.create는 존재하지 않는
부모 디렉터리 때문에 거부됐으며 Assets 루트의 새 파일로 저작했다.
wrapper의 첫 검사는 Scene 로그 위치/종료 후 mount 정리를 고려하지 못해 실패했다.
stdout을 포함하고 실행 중 artifact를 검사하도록 수정했다. 실패 로그는 성공에
포함하지 않으며 저작 Editor quit timeout도 정상 종료 증거가 아니다.

최종 묶음: Build/Obj/P19Mesh/result.json. 개별 Player 실행:
- Debug: Build/Obj/Phase19Player/run-deeb3d9c92eb4e608999254bbfc4d92d/result.json
- Release: Build/Obj/Phase19Player/run-5119ef78da614901862305fc67e7351e/result.json
- Shipping: Build/Obj/Phase19Player/run-04dd5a43656c4032ac0e56d28e80afb7/result.json
저작/배포·패키징/실행 로그는 Build/Obj/P19Mesh 및 Shipping Build/Obj/Mesh/S.
기반 최종 로그는 P19Mesh/cooked-final.log, 개별 로그는 Phase19CookedGeometry.

범위는 DX12/CPU CCT의 고정 triangle mesh 바닥이다. 기반 SDK 검사와 실제 Player
범위를 구분한다. Player convex/heightfield·mesh DDOL, 실제 콘텐츠 이전,
캐릭터 전체 회귀와 성능 수용은 잔여이며 B1/C0/C1/M0는 progress를 유지한다.

### 2026-10-02 Player convex·heightfield 및 mesh DDOL — D/R/Shipping

HTTP CLI geometry.create/physics.shapes/script.add/object.parent/scene.save로
PhysicsCharacterGeometry(GUID 9e8090b1-8f12-46ee-b554-610bc247dd08)를 저작했다.
triangle mesh 바닥 외에 별도 위치의 convex 상자 바닥과 평평한 2×2 heightfield,
각각의 CharacterMovement/CharacterPlayerProbe를 배치했다. 모든 geometry는
revision1이며 새 source/meta 두 쌍과 씬/meta를 fixture로 고정했다.
GeometryPersistentParent 아래에 mesh 바닥·기존 캐릭터를 묶어 부모 계층을 DDOL한다.
목적지는 기존 별도 cooked 카메라 씬이며 convex/heightfield 계층은 이송하지 않는다.

최종 소스 Debug/Release/Shipping 빌드 및 격리 배포·패키징 뒤 각71/0 통과:
세 바닥의 실제 PostPhysics 접지·이동·점프·착지·force·teleport 각각12/0=36/0,
기존 C# 참조의 이송 lifecycle/input/force/gravity/tick8/0,
native 캐릭터·mesh 바닥·부모 3노드 계층/핸들24/0과 목적지 body/import/cook3/0.
이전 Scene EntityHandle 거부, 새 핸들 resolve, 컴포넌트 identity, layer/enabled,
world pose, 부모·순서 있는 자식 remap을 확인했다. SDK 핸들 숫자 차이는 가정하지 않는다.
목적지의 mesh RuntimeHandle 유효성 및 cache imports1/assets1/cooks0을 확인했고,
출발지도 imports3/cooks0을 이송 전 필수 조건으로 확인했다.

Scene 캐시는 비공개로 유지하고 ReadCollisionGeometryStatistics()가 owner-thread
검사를 거친 수치 사본만 반환하도록 했다. 변경 가능한 cache/asset 접근이나 C# API를
추가하지 않았다. cold regression 진단이며 물리 scheduling/solver 설정 변경은 없다.

실행 중 CEPG3개·각 SHA256과 저작용 .cegeometry0개, CEMF entries19/sources114,
서로 다른 cooked GUID 로드, 목적지 활성화 이후 completed display, 종료0 통과.
패키지 파일 집합·SHA256은 243/238/238개 보존했다. Offscreen 없이 기존
2000 GT frames/최소8 promotions·slot rotation을 유지했다. parser calls=0,
Shipping service compiled=no/enabled=no·endpoint0 및 최종 바이너리의
Development/Shipping 소켓·서비스 import/문자열 격리도 통과했다.
이번 DDOL 실행은 HTTP를 켜지 않으므로 snapshot 관측값은 null이다.
저작 Editor quit timeout은 별도 기록하며 정상 종료 검증으로 세지 않는다.

최종 묶음: Build/Obj/P19Geo/result.json. 개별 실행:
- Debug: Build/Obj/Phase19Player/run-2df96f26eda342b9b5aacb52f111d169/result.json
- Release: Build/Obj/Phase19Player/run-32b947c2c3664b5889dd8bd232d5cbcb/result.json
- Shipping: Build/Obj/Phase19Player/run-e3e8693a669840c4aac97296ab917101/result.json
빌드/저작/격리 로그는 P19Geo, 배포·패키징은 같은 폴더 D/R 및 Build/Obj/GD/S.

범위는 DX12/CPU CCT의 static convex·평평한 heightfield·triangle mesh와 mesh DDOL이다.
목적지 mesh body 재생성까지 검증했으며 목적지에서 재착지하는 추가 contact gate는
이번 검사에 포함하지 않았다. 실제 Player의 missing/corrupt/revision closure 실패,
동적 convex·경사/계단·실제 콘텐츠 이전·전체 회귀·profiler/성능 수용은 잔여다.
B1/C0/C1/M0는 progress를 유지한다.

### 2026-10-02 목적지 재착지·실제 Player geometry 실패 — D/R/Shipping

기존 geometry DDOL gate를 목적지 tick90까지 연장했다. mesh 바닥에서 Below와
foot Y≈0, 계속된 X 이동과 양수 force 잔여 시간을 추가 검증한다. 세 바닥 motion36,
C# 이송8, native 계층/재생성/재착지29로 최종 정상 경로는 각73/0이다.

정상 배포본의 격리 사본만 변형하는 C++23 Pak mutator를 추가했다. 공식
Pak::Archive/Builder와 CEMF reader/writer/SHA256을 사용해 Pak index는 정상으로
유지한다. 대상은 mesh UUID 578cd3b3-bd67-450c-a1d1-786ee613f24f 하나다.
- missing: CEMF 참조를 유지한 채 해당 CEPG entry만 제거.
- corrupt: CEPG byte를 변경하고 기존 CEMF hash를 유지.
- revision: Scene의 요청1은 유지하고 CEPG record를2로 변경; CEPG checksum과
  CEMF artifact hash를 갱신해 단순 checksum 실패와 구분.

최종 Debug/Release/Shipping 실제 Player에서 세 사례 각각5/0, 총15/0 통과.
잘못된 mesh body와 캐릭터 controller의 RuntimeHandle 부재, 유효한 다른 geometry
imports2/assets2, runtime cooks0, character tick0을 확인했다. 잘못된 body를
제외한 부분 시뮬레이션이 아니라 Scene::StartPhysicsSimulation의 전체 시작 거부다.
정상 접지12 성공은 나오지 않았고 runtime text-parser calls0 및 변형 사본 불변을
확인했다. 원본 배포본의 전체 파일 집합·SHA256도 변형/실행 전후 보존했다.

실패 selftest는 거부 상태를 확인하고 창을 닫아 종료0으로 판정한다. 일반 Player의
필수 물리 자산 오류를 프로세스 fatal exit로 올리는 정책을 변경하거나 검증한 것은 아니다.
첫 진단은 부분 시뮬레이션/낙하를 잘못 가정해 tick 대기 상태가 됐다. 소유한 프로세스를
정리하고 현재 전체 시작 거부 계약에 맞춰 수정했다. 실패 진단은 최종 성공에서 제외했다.

최종 세 구성 모두 정상73+실패15=88 checks. 정상 경로의 cooked Scene/CEMF,
목적지 completed display·종료0·243/238/238파일 보존과 기존2000/min8/slot rotation,
CEPG3/source geometry0을 유지했다. 최종 Shipping 바이너리 서비스/소켓 격리도 통과.
DDOL/실패 실행은 HTTP를 켜지 않으므로 snapshot 관측값은 null이다.

최종 묶음: Build/Obj/P19Fail/result.json. 정상/실패 증거 쌍:
- Debug: Phase19Player/run-e7a39a5ed58343e698d8fdb6e64a3c66,
  PhysicsGeometryFailure/run-f41de83a5c0547d9a8ece40b367e9562
- Release: Phase19Player/run-439711cf5e474aac8e4b2c0415a1af0a,
  PhysicsGeometryFailure/run-c1b469b458564326b20e4f121b6995f1
- Shipping: Phase19Player/run-4746767397994b27bf3a6e67b2c141e3,
  PhysicsGeometryFailure/run-9cab56876dc2492f88d2e83c856e6a9f
개별 경로는 Build/Obj 아래이며 result.json/player.out/Runtime 로그를 포함한다.
최종 빌드/배포·패키징은 P19Fail/D2/R 및 Build/Obj/FG/S, mutator 재현 빌드는
Tools/regression/build-physics-geometry-pak-mutator.ps1을 사용한다.

범위는 고정 fixture의 CPU CCT/DX12, mesh 자산1개와 Scene 물리 시작 거부다.
프로세스 fatal 정책·SDK include 경계 전체 감사·동적 convex·경사/계단·실제 콘텐츠
이전·전체 회귀·profiler/성능 수용은 잔여다. B1/C0/C1/M0는 progress 유지,
M2는 검증된 생명주기/실패 범위를 부분 진행으로 기록하며 완료로 계산하지 않는다.


### 2026-10-02 Player 필수 시뮬레이션 실패 종료 정책 — D/R/Shipping

Player는 프레임의 시뮬레이션·구조 경계 뒤 SceneManager의 PlayFailureCount와
LastPlayFailure를 확인한다. 시작 또는 씬 전환의 거부가 기록되면
`[player.simulation.failed] exit=3 reason=...`를 stderr에 출력하고,
실행 전제조건 실패 코드3을 설정한 뒤 WM_CLOSE로 기존 Finalize 경로를 탄다.
Core/Editor의 실패·스냅샷 복원 정책은 변경하지 않았다. 진단 플래그는 관찰만 하며
제품 종료 정책을 우회하지 않는다. 진단 자체의 단정 실패는 기존 selftest 코드4다.

실제 최종 Debug/Release/Shipping 배포본마다 누락·손상·요청 revision 불일치의
일반 실행(인자 없음)3건과 진단 실행3건, 총6건을 통과했다. 모든 자산 오류는
종료3·비어 있지 않은 오류 사유·runtime text-parser0, fallback 접지 성공 없음,
원본/변형 패키지의 파일 집합과 SHA256 보존을 검증했다. 진단3건은 각각
body/controller 핸들 없음·유효 geometry import2/assets2·cook0·character tick0의
5개 계약도 유지한다. 정상 geometry/DDOL/목적지 재착지는 각73/0·정상 종료0,
completed display/2000 frames/min8/slot rotation을 유지했다.
Shipping 서비스·소켓 격리도 최종 바이너리로 다시 통과했다.

최종 묶음: `Build/Obj/P19Fatal/result.json`.
- Debug 정상: Phase19Player/run-99c8e45c7fb54c24af7fd35764fc6157,
  실패: PhysicsGeometryFailure/run-ad4329084ce94bda9c50243beb714e50.
- Release 정상: Phase19Player/run-975eb2b86a414b44bb9ca1e39a7e2ad4,
  실패: PhysicsGeometryFailure/run-c92a30a9fe554211ab06bbaa33e55e73.
- Shipping 정상: Phase19Player/run-c729c0a8d96f4023a6ff9d09c7cadb25,
  실패: PhysicsGeometryFailure/run-d3d0791c718a4f798332be6dd1c1838e.
경로는 Build/Obj 아래다. 최종 빌드·배포·패키징/격리 로그는 P19Fatal 및 FT/S.

앞 절의 일반 Player fatal 정책 미검증 항목을 이 시작 실패 범위에서 갱신한다.
전환 실패도 동일 실패 기록을 소비하도록 배선했지만, 잘못된 목적지로 이동하는
실제 Player 실패 회귀는 별도 잔여다. SDK include 전체 감사·동적 convex·제품 경사/계단,
실제 콘텐츠 이전·전체 회귀·profiler/성능 수용도 잔여이며 Phase19 전체 완료가 아니다.
B1/C0/C1/M0/M2는 progress를 유지한다.


### 2026-10-02 잘못된 cooked 목적지의 Player 전환 실패 — D/R/Shipping

fixture builder의 Transition 모드는 기존 PhysicsCharacterPlayer primitive 씬을 시작
씬으로 선택하고 PhysicsCharacterGeometry를 cooked 목적지로 함께 포함한다.
출발 씬에는 문제 geometry 참조가 없다. 정상 Pak 또는 목적지 mesh UUID
578cd3b3-bd67-450c-a1d1-786ee613f24f만 missing/corrupt/revision으로 변형한 Pak을
격리 사본에서 실행한다. 백엔드/Player 실패 정책을 우회하거나 수정하지 않았다.

Debug/Release/Shipping 각각 정상 목적지1건과 오류 목적지3건, 총4건 통과.
매 실행에서 출발 cooked GUID cf654e3e-050c-412f-81b2-1df250d4c806의 실제 이동/접지
12/0을 확인한 뒤 목적지 GUID 9e8090b1-8f12-46ee-b554-610bc247dd08 로드를 확인한다.
정상 전환은 gameStart=true/pending=false, 목적지 completed display와 종료0을
검증한다. 오류 목적지는 비어 있지 않은 simulation 실패 사유와 종료3, 목적지
fallback 접지 성공 및 displayedAfterActivation 성공의 부재를 검증한다.
모든 경우 runtime text-parser0, 원본/실행 사본의 파일 집합과 SHA256 보존 통과.
--smoke 2000/min8/slot rotation의 기존 렌더 완료 조건을 낮추지 않았다.

최종 결과: Build/Obj/P19Transition/result.json.
Debug 증거: PhysicsGeometryFailure/run-c55b1e07263049e0a3b7d6b62bc1f1da.
Release 증거: PhysicsGeometryFailure/run-8e4bf32f43e84956ac99fdfc553ea407.
Shipping 증거: PhysicsGeometryFailure/run-12ad106d33e34e84a1560d06d64aeb53.
경로는 Build/Obj 아래이며 normal/missing/corrupt/revision의 Player 출력·오류·변형
로그와 result.json을 포함한다. 패키징/게이트는 P19Transition/D2/R2/S2 결과로 묶었다.

초기 병렬 Release 패키징의 렌더 대기 timeout과 초기 verifier의 stdout 순서 오판은
성공에서 제외했다. C# Console 출력이 buffered C++ 시작 로그보다 앞설 수 있으므로
출발 물리 성공이 목적지 로드보다 앞서는 조건으로 수정하고 순차 재실행했다.
Debug/Shipping의 이미 성공한 패키지는 보존·재사용하고 Release는 새 패키지로 재검증했다.
이는 SceneManager의 목적지 물리 시작 거부가 Player 종료 정책으로 전달되는 제품
증거다. DDOL 객체를 동반한 실패 전환·Editor 실패 복원·SDK include 전체 감사와
동적 convex/제품 경사·계단/실제 콘텐츠 이전/전체 회귀/profiler·성능 수용은 잔여다.
B1/C0/C1/M0/M2는 progress 유지이며 Phase19 전체 완료가 아니다.


### 2026-10-02 실제 단검 동적 convex — Debug/Release/Shipping Player

Weapon_Dagger_G3_005_Separate.glb의 정적 메시를 사용했다. 원본 SHA256은
50b2a38725557a7ce96e72c3c727349756a329c3194bad106821f9801298a0b7이며 변경하지 않았다.
2832 POSITION / 중복 제거 2119점, identity node·skin/animation 없음.
GltfImporter와 같은 Z 반사만 적용했으며 authored metre 좌표/scale1을 보존했다.
HTTP CLI로 실제 메시와 같은 Entity에 mass1kg 동적 PhysicsBodyComponent,
단일 convex ShapeInstance, 정적 box 바닥과 카메라·C# probe를 저작했다.
레이어는 기존 Default/CollisionMatrix를 사용한다. 단일 hull이며 convex 분해 증거는 아니다.

초기 native cook은 SDK eZERO_AREA_TEST_FAILED로 거부됐다. SDK 기본 면적 epsilon이
얇은 실물 형상에 너무 커서, 최대 AABB extent 제곱에 비례한 1e-6 면적 기준을
기본값 이하로 적용했다. 직접 cook과 geometry blob cook에 같은 정책을 적용했다.
eCHECK_ZERO_AREA_TRIANGLES와 GPU cooking을 유지하고, 부피 없는 점군은 SDK 호출 전
상대 크기의 double 연산으로 거부한다. 작은 tetra 1cm/10cm/1m, collinear 거부와
실제 단검 direct/blob/import를 추가한 네이티브 회귀는 Debug/Release/Shipping/ASan
각1824개 검사 및 실제 GPU 경로를 통과했다(cooked-all-final.log).

PostPhysics는 고정 스텝 콜백이 아니라 게임 프레임 콜백이므로 누적 delta로 검사한다.
각 실제 Player에서14/0: 동적 종류·mass·단일 convex, 0.5초 중력 낙하,
4초 바닥 관통 방지/정지, 선속도·각속도 명령과 실제 회전 자세 변화,
2 N·s X 임펄스의 속도 증가, 10초 재착지/정지 및 Entity Transform 위치 일치.
X 속도는 약0.98→2.97m/s, 최종 높이 약0.0336m·속도0이다.
이는 이 형상의 단일 바디/정적 바닥 충돌과 운동 증거이며 제품 전체 물리 수용이 아니다.

최종 별도 실행은 테스트 소유 Player 창960×540, smoke12000/min8 및 기존 slot rotation
조건을 사용했다. 실제 completed Game display, 정상 종료0, text-parser0,
cooked .cepg1개/저작 .cegeometry0개, 패키지 파일 집합·SHA256 불변을 확인했다.
Shipping은 compiled=no/enabled=no·endpoint 없음과 최종 DLL 소켓/서비스 격리도 통과했다.

- Debug: 14/0, frames29653, promotions8, run-033e851de1c74005b6bddfaa058ee62e.
- Release: 14/0, frames130201, promotions9, run-307e418bbad14586b5153a2dc39e89d9.
- Release Shipping: 14/0, frames103664, promotions8, run-84b7facc7a97454aa3b60da7d6be52c6.

최종 묶음: Build/Obj/P19Dagger/result.json. 개별 패키지/게이트: P19Dagger/D3/R3/S4.
증거 디렉터리는 Build/Obj/Phase19DaggerPlayer 아래다. 재현 도구는
prepare-physics-static-glb-convex.py, build-physics-dagger-player-fixture.ps1,
verify-physics-dagger-player.ps1와 GameScripts/DaggerConvexPlayerProbe.cs.
원본 GLB는 fixture builder의 SourceGlb 인자로 제공하며 fingerprint를 대조한다.

초기 프레임 수 기반 낙하 단정 실패와 고해상도 Scene lookup candidate+previous의
GPU 메모리 예산 초과(종료4)는 성공에서 제외했다. 후자는 물리 수정으로 해결했다고
보지 않으며 고해상도 렌더 잔여다. Release Editor의 MSVC LNK1000 내부 오류는
WholeProgramOptimization=false 호출 옵션으로 재빌드해 통과했다. 프로젝트 기본 설정은
바꾸지 않았고 Release/Shipping 제품 증거는 이 옵션의 바이너리이며 성능 수용이 아니다.
Shipping 초기 인자 전달 실패도 배열 수정 후 새 S4 패키지로 대체했다.

B1/M0의 실물 단일 동적 convex Player 증거를 추가하며 progress 유지.
제품 경사/계단·다중 콘텐츠 이전·전체 회귀·SDK include 전체 감사와 profiler/성능 수용,
DDOL 실패 전환·Editor 실패 복원 등 기존 잔여를 완료로 올리지 않는다.

#### B2 차등 Transform 동기화·렌더 보간 (2026-10-02)

바디의 권위 상태는 완료된 고정 tick의 pose다. 렌더 소비자는 별도의 이전/현재 pose와
accumulator 비율로 위치와 회전을 보간한다. 게임 Transform·저장·스크립트 ReadState에는
보간 값을 쓰지 않는다. MeshRenderer의 bounds와 렌더 proxy는 같은 렌더 행렬을 소비한다.
활성 바디와 정지 직후 한 tick의 수렴 항목만 렌더 dirty 대상에 넣으며 zero-tick 프레임에서도
보간을 갱신한다. Stop·비활성화·삭제·teleport는 이력을 초기화한다.

Transform dirty EntityHandle을 중복 제거한 뒤 고정 tick 전에 반영한다. static/dynamic의
명시적 Transform 변경은 pose 재배치, kinematic은 target 요청이다. 물리 결과를 Transform에
반영할 때 관측 행렬도 갱신해 출력이 다시 teleport 요청으로 들어가지 않게 한다.
부모 이동·회전은 world pose로 환산하고 scale 변경은 기존 형상 교체 경로를 사용한다.
C++23 ranges/zip과 Mathematics rows view로 행렬 비교·round-trip 검증을 수행한다.
부모 비균등 scale과 자식 회전이 만드는 shear는 SDK 변경 전에 거부한다.

Physics.TransformCommit과 Physics.RenderInterpolation 마커를 추가했다. 재사용하는 순회
scratch는 Scene의 비공개 렌더 registry 상태가 소유한다. 전용 물리 스레드의 T0/T1/T2,
전체 제품 프로파일 capture와 성능 수용 M3은 별도 잔여다.

네이티브 검증: verify-physics-b2.ps1 -Configuration All -RequireGpu.
Debug/Release/ASan 각각 472, Shipping 470 checks 및 실제 GPU 경로 통과.
zero-tick 보간, catch-up 마지막 두 pose, 정지 후 목록 제거, teleport/static/kinematic,
수명 초기화, 부모 변환·shear 거부를 포함한다. 증거:
Build/Obj/P19Dagger/b2-all-final.log, Build/Obj/Phase19B2/<configuration>.

에디터 HTTP 검증은 두 정상 플레이 각각 27/0, 두 Stop의 6개 객체 변환 복원,
세 번째 플레이의 shear 실패 자동 Stop 및 6개 변환 복원을 통과했다.
실패 사유는 Physics Transform contains unsupported shear, failureCount=1이다.
검증 도구는 Tools/regression/verify-physics-b2-http.ps1이며 기본 입력은 HTTP로 저작한
Dynamic_CPP/Assets/Scenes/PhysicsB2Player.creator다. 모델/geometry가 import된 프로젝트와
현재 GameScripts 빌드가 필요하다. 증거:
Build/Obj/P19Dagger/author-c723fb4b6dd34c768f11853ca8785c8f/result.json.

Player 최종 구성별 검증 결과와 B2 상태는 아래 수용 기록에서 판정한다.
초기 빈 stdout 처리 오류, scene.load를 활성화로 오해한 실행, 두 HTTP 변경 사이 자동
Stop이 발생한 부정 테스트는 최종 수용 증거에서 제외했다.

B2 최종 수용 기록 (2026-10-02): **B2 완료**, 공수 4일 유지.

- 실제 Player Debug/Release/Shipping 각각 27/0 assertions 및 정상 exit 0.
  완료 렌더 frames=26035/97457/102172, display promotions=8/8/9.
- 각 구성의 지원 불가 shear 부정 테스트는 실패 사유를 기록하고 예상 exit 3으로 종료했다.
- 추가한 저장소 HTTP 검증 도구를 재실행해 정상 두 플레이·Stop·실패 Stop의 변환 복원을 확인했다.
  최종 Editor 증거: Build/Obj/Phase19B2Editor/run-1c12f2860eae4aaca95de194d44250ff/result.json.
- Player staged 파일·해시 불변, runtime text parser calls=0, cooked Scene 신원,
  Shipping 계측/서비스 격리 검증도 통과했다. 입력 GLB 원본 SHA256은 기존 기록과 일치한다.

통합 증거: Build/Obj/P19B2/result.json. 개별 Player는 D1/R1/S1 및 각각 -shear 디렉터리,
재현 도구는 build-physics-b2-player-fixture.ps1과 verify-physics-b2-player.ps1이다.
네이티브와 제품 렌더 검증은 구분한다. GPU에 게시된 proxy 행렬의 수치 capture,
전체 제품 profiler 계층 capture·성능 수용은 M3의 잔여이며 이번 완료에 포함하지 않는다.

제품 렌더 검증은 960×540 조건이다. 기존 고해상도 Scene lookup GPU budget 초과 문제는
별도 잔여다. Release·Shipping은 기존 MSVC LNK1000 우회를 위해 빌드 호출에
WholeProgramOptimization=false를 사용했으며 프로젝트 기본값은 바꾸지 않았다.
이 결과로 성능 수용을 주장하지 않는다. B1/C0/C1/M0의 전체 저작·캐릭터 회귀와
T0/T1/T2/M3 등 다른 게이트 상태는 유지한다.

#### T0 요청·스냅샷 수명과 스레딩 계약 (2026-10-02)

PhysicsSceneChannel은 작업자가 복사하는 요청/완료 스냅샷 통로다. SDK Scene/body/controller를 소유하거나
Scene/Component 포인터를 보관하지 않는다. Scene 소유 스레드가 통로를 발급하고,
작업자는 값 명령을 제출하거나 shared_ptr<const tick_snapshot>을 읽는다.
SDK 직접 변경과 통로 발급은 소유 스레드만 허용한다.

요청 큐·시퀀스 이력·발행 저장소를 SDK implementation 수명에서 분리했다. owner가 큐
mutex 아래 입력을 닫고 in-flight fetch를 완료한 뒤 미래 명령 payload를 폐기하고 SDK
dispatcher를 drain한다. 통로를 보관한 작업자와 Scene 종료가 경합해도 SDK/Scene에
접근하지 않으며 이후 유효 요청은 wrong_phase다. 마지막 완료 스냅샷과 이전에 읽은
스냅샷은 Scene 파괴 뒤에도 유효하다. 재Play 통로의 identity는 바뀌며 이전 세션 신원은
거부한다. is_closed는 입력 차단이고 SDK drain 완료 fence가 아니다.

기존 P3의 (tick, producer, sequence) 병합, 마감/중복/용량 진단, force 비병합 및 solver
콜백 값 복사·retired identity 수명을 유지한다. 요청 순서의 결정성을 검증하며 CPU/GPU
solver 결과의 bitwise 결정성을 주장하지 않는다. Physics.RequestClose를 scene/tick과
함께 계층 계측했다. 전용 물리 스레드와 simulate/fetch overlap은 이번에 추가하지 않는다.

검증 도구: Tools/regression/verify-physics-t0.ps1 -Configuration All -RequireGpu.
Debug/Release/ASan 각각 173 checks, Shipping 170 checks 및 실제 GPU 경로 통과.
8 producer × 8 요청의 역순 도착·논리적 순서 적용, 다른 스레드 SDK 변경/발급 거부,
종료 경합·종료 후 1000회 거부·snapshot 유지·새 세션 신원 격리를 CPU/GPU 모두 확인했다.
RequestClose 계층과 complete/unacked=0 캡처도 확인했다.

기존 P3 All/RequireGpu는 Debug/Release/ASan 각 3971, Shipping 987 checks;
B2 All/RequireGpu는 Debug/Release/ASan 각 472, Shipping 470 checks로 모두 재통과했다.
이는 SDK·커맨드·콜백·수명·보간 회귀이며 제품 실회귀와 구분한다.
증거: Build/Obj/P19B2/t0-native-final.log 및 Build/Obj/Phase19T0/<configuration>.
제품 빌드·에디터/Player 회귀를 마친 수용 기록은 아래에서 최종 판정한다.

T0 최종 수용 기록 (2026-10-02): **T0 완료**, 추정 공수 3인일 유지.

Debug/Release Editor·Player·AssetCooker 및 Shipping Player·AssetCooker 전체 빌드 통과.
현재 SDK 변경으로 제품 에디터 두 Play/Stop·shear 오류 Stop의 6객체 변환 복원을 다시 확인했다.
증거: Build/Obj/Phase19B2Editor/run-a00dc3ca9fcc45d2bfa52d8827759cd1/result.json.
실제 Player Debug/Release/Shipping 각각 27/0 및 completed display·정상 exit 0,
Debug shear 오류의 사유·exit 3도 통과했다. stage 파일/해시 불변·cooked Scene 신원·parser0,
Shipping 소켓/서비스 격리도 유지한다. 이번 T0 제품 오류 종료 재실행은 Debug 범위이며
Release/Shipping 오류 종료의 이전 B2 증거와 구분한다.

통합 증거: Build/Obj/Phase19T0/result.json. native/P3/B2 구성별 결과·현재 소스 SHA256과
제품 Editor/Player 결과를 연결했다. 초기 probe의 API 이름·friend 접근 컴파일 오류는 수정했으며
최종 All 검증과 전체 제품 빌드 결과만 수용했다. Release/Shipping no-WPO 호출 옵션과
960×540 제품 렌더 검증 조건은 B2 수용 기록과 같다. 고해상도 렌더러 문제는 별도 잔여다.
전체 제품 profiler 캡처·성능 수용은 M3, 안전 overlap/워커 실행 최적화는 T1,
읽기 창·쿼리 배치는 T2에 남는다. 전용 물리 스레드는 이번에 도입하지 않았다.


## T1 실행 배치 후보 및 실측 기록 (2026-10-02, 후보 평가 이력)

**T1 진행 중 — 안전성 검증 완료, 성능 수용 미통과.** 추정 공수 3인일 유지.

ScenePhysicsSimulation은 완료된 owner-side render history만 읽는 준비 작업을
simulate/fetch 사이에 배치했다. 256개 미만에서는 동일 작업을 fetch 이후 수행한다.
Register에서 scratch 용량을 예약하고 동기 step 동안 안정된 entry 주소를 사용한다.
SDK·Transform·Component·스크립트 변경이나 외부 callback은 이 창에서 실행하지 않는다.
성공한 fetch 이후에만 active pose와 render history를 병합·게시한다. 실패한 fetch는
staged scratch를 폐기하고 이전 게시 값을 유지한다. 작은 집합의 after-fetch 배치는
별도 legacy 경로가 아니라 같은 작업의 실행 시점 정책이다.

Physics.SimulateSubmit / Physics.InFlightRenderPrepare 또는 Physics.RenderPrepare /
Physics.FetchWait를 scene/tick으로 연결했다. 실제 PhysXTask의 다른 worker thread와
in-flight 준비 작업이 겹치는 것을 캡처로 검증했다. owner 대기와 worker 실행 시간은
중복 합산하지 않는다. 전용 SDK dispatcher를 유지하며 공용 enkiTS dispatcher와 별도
physics owner thread는 도입하지 않았다.

최종 native Debug/Release/ASan 각 5078 checks, Shipping 3530 checks 및 실제 GPU 통과.
캡처 complete/unacked=0, CPU/GPU 64·256·1024 active history 병합 및 fetch 실패의
게시 불변성을 확인했다. 제품 Debug/Release/Shipping 빌드와 Player 각 27/0,
completed display·exit0, Debug shear exit3, Shipping 격리를 통과했다.
Editor 두 정상 Play/Stop와 오류 Stop에서 6객체 authored Transform을 복원했다.
제품 조건은 960×540, Release/Shipping no-WPO 기존 빌드 우회다.

동일 코드에서 준비 작업만 after-fetch로 강제한 **검증용 생성 소스**를 비교 기준으로
사용했다. 제품에 serial 선택 스위치나 이전 PhysX 배선을 추가하지 않았다.
Release /O2, CPU/GPU × active16/256/1024 × profiler off/on, ABBA 두 회전,
총 96회 실행, 각 side/cell 4표본·표본당 240 ticks의 평균/p99 중앙값을 기록했다.
각 지표 CV 10% 초과는 해당 지표의 수용 판단에서 제외한다.

CPU256 평균은 off +2.74%, on +3.84%; CPU1024 계측 on p99는 +12.71%로 악화했다.
GPU1024 계측 off 평균 -2.34%, p99 -10.21% 개선도 관측했지만 일반적인 개선이나
256 임계값의 최적성을 입증하지 못했다. 측정 당시 배치는 검증된 후보였으며 **성능 수용은
미통과**다. CPU 경합 및 GPU 계측 비용의 분리·배치 정책 재검증이 T1 잔여다.
전체 제품 workload 계측/성능 수용 M3, 읽기 창·query batch T2도 남는다.

통합 증거: Build/Obj/Phase19T1/result.json. 최종 source SHA256, 구성별 native,
Editor/Player 결과, serial 생성 소스 해시와 ABBA summary를 연결했다.
벤치마크 재현: Tools/regression/verify-physics-t1-benchmark.ps1.
회귀 재현: Tools/regression/verify-physics-t1.ps1 -Configuration All -RequireGpu.


### T1 성능 회귀 후보의 제품 철회 (2026-10-02)

위 ABBA 측정에서 모든 backend/계측 조합의 개선을 입증하지 못했으므로 제품의
256개 in-flight 준비 정책을 철회했다. 현재 제품은 **성공한 finish_step 이후**에
Physics.RenderPrepare를 실행한다. 임계값·backend 조건·runtime overlap 선택은 없다.
완료 history 병합, 등록 시 scratch 예약, 성공 후 게시 및 fetch 실패의 게시 불변성은
유지한다. 계층은 SimulateSubmit → FetchWait → RenderPrepare이고 worker PhysXTask의
scene/tick 계측도 유지한다. 전용 SDK dispatcher와 기존 owner 정책은 바뀌지 않는다.

검증 도구의 candidate/serial은 각각 제품 소스에서 생성하는 비제품 비교 대상이다.
serial은 현재 제품의 after-fetch 배치이고 candidate만 과거 256개 overlap 배치를
생성한다. Tools/regression/verify-physics-t1-benchmark.ps1은 ABBA 순서로 이 둘을 비교한다.
이 후보는 제품에서 사용하거나 런타임 fallback으로 선택하지 않는다.
과거 96회 증거의 current 명칭은 당시 후보를 뜻하며 현 제품 바이너리를 뜻하지 않는다.
안전 배치와 계측 작업은 구현했으나 성능 개선 수용은 T1 잔여로 유지한다.


T1 후보 철회 후 최종 회귀 기록:
현재 after-fetch 제품 native Debug/Release/ASan 각 **5079**, Shipping **3530** checks,
실제 GPU 및 capture complete/unacked=0 통과. Release 계층 768 ticks에서 in-flight
준비/worker 겹침은 0건이며 SDK worker 계층은 유지했다. Debug/Release/Shipping 제품
빌드·Player 각각 27/0·completed display·exit0, Debug shear exit3, Shipping 격리 통과.
Editor 정상 두 Play/Stop와 오류 Stop의 6객체 authored Transform 복원도 재통과했다.
최종 Editor 증거: Build/Obj/Phase19B2Editor/run-e105202d5ba348ed8125e166eda7654c/result.json.
최종 Player 증거: Build/Obj/P19B2/{DT1Serial,RT1Serial,ST1Serial,DT1Serial-shear}/result.json.

격리된 candidate/serial 재비교도 ABBA96회 완료했다. CPU256 후보 평균은 profiler off
+2.22%, on +3.42%로 다시 악화했다. GPU1024 off 평균 +2.02% 악화, on 평균 -0.70%
개선으로 backend만으로 정책을 선택할 근거도 부족했다. p99 CV10% 초과 셀은 수용 판단에서
제외했다. 후보의 보편적 개선은 입증되지 않았으며 제품 철회와 T1 진행 중 상태를 유지한다.
재현 결과: Build/Obj/Phase19T1Bench/run-c4c83d532d164736bad062da15a6c4cc/summary.json.
통합 Build/Obj/Phase19T1/result.json은 현재 제품 source SHA256·최종 회귀·후보 생성 소스
해시·최신 비교를 연결한다. 앞 절의 current 표본은 역사적 후보 측정으로 구분한다.
벤치마크 wrapper의 인코딩 오류로 손상된 param 선언은 복구했고, 해당 실행은 중단·제외했다.
최종 구문·param AST 확인과 새 96회 측정만 수용한다. 전체 제품 성능 M3는 여전히 잔여다.


### T1 active-first 병합 후보 평가 (2026-10-03)

제품 after-fetch 정책을 유지한 채 active pose를 먼저 한 번만 append하고, 직전 history에서
이번 tick에 active가 아니었던 바디만 수렴 처리하는 독립 merge 후보를 평가했다.
prepare 단계의 중복 render slot 쓰기·prepared index·pointer scratch 기록을 줄이는 방식이다.
제품 소스를 변경하지 않고 generator의 merge side에서만 이 변형을 생성한다.

후보 native Debug/Release/ASan 각각 5079, Shipping 3530 checks 및 실제 GPU 통과.
fetch 실패의 게시 불변성, sleep convergence, zero-tick interpolation, transform 계층,
active history 병합 및 SDK worker/profile 캡처 계약을 유지했다. 후보 Editor/Player 제품
검증은 수행하지 않았으며 이전 제품 회귀 결과로 후보 수용을 대신하지 않는다.

Release ABBA96회, CPU/GPU active16/256/1024, profiler off/on, 각 side/cell 4표본·
표본당 240 ticks. 후보 CPU256 평균 off -1.24%, on -0.79% 개선에 그쳤고 CPU1024는
+1.68/+1.70%, GPU256 on은 +11.77% 악화했다. 모든 CPU p99와 여러 GPU p99는
CV10% 초과라 수용 판단에서 제외했다. GPU1024 on 평균도 변동 기준을 넘었다.
보편적인 비용 개선을 입증하지 못해 **후보를 제품에 적용하지 않는다**. T1은 진행 중이다.

통합 증거 Build/Obj/Phase19T1Merge/result.json은 생성 소스 해시·도구 및 제품 소스 해시·
구성별 후보 native 결과·고유 benchmark root/summary를 연결한다. native 누적 prepare/
fetch 시간은 컴파일과 병행한 correctness 캡처이며 통제된 전후 성능 비교로 사용하지 않는다.
최신 비교: Build/Obj/Phase19T1Bench/run-122df9bab7814ea0a9330bc91c2f8def/summary.json.
재현: verify-physics-t1.ps1 -Configuration All -RequireGpu -MergeCandidate;
verify-physics-t1-benchmark.ps1 -CandidateSide merge. 기본 native 검증은 현재 제품 경로다.
다음 T1 작업은 solver/dispatcher와 snapshot/merge 비용 분리이며, 측정 근거 없이 공용
작업 스케줄러나 별도 physics owner thread를 도입하지 않는다. T2/M3는 별도 잔여다.


### T1 solver/dispatcher·snapshot/merge 비용 분리 (2026-10-03)

**비용 분리 구현·검증 완료, T1 전체는 진행 중.** 제품 실행 순서와 SDK 전용 dispatcher를
유지했다. FetchWait 하위에 FetchResults와 DispatcherDrain을 노출하고 SnapshotStatistics와
RenderMerge를 추가했다. CPU SDK task span 및 TaskSubmit을 scene/tick/task로 연결한다.
SDK task span은 run+release의 elapsed이며 CPU 사용 시간이나 순수 solver/GPU kernel 시간이
아니다. inline/실제 worker task 수를 구분한다. owner 부모/자식 및 worker span 합을 중복
합산하지 않는다. 자세한 의미는 PhysicsAPIContract의 T1 실행 비용 계층 계약을 따른다.

최종 native Debug/Release/ASan 각각 8161, Shipping 3530 checks 및 실제 GPU 통과.
제품 Debug/Release/Shipping 전체 빌드·Player 각27/0·completed display/정상 exit0,
Debug shear exit3, Editor 두 정상 Play/Stop·오류 Stop의 6객체 복원, Shipping 격리 재통과.
제품 해상도960×540, Release/Shipping no-WPO 우회 조건은 유지한다.
최종 Editor 증거: Build/Obj/Phase19B2Editor/run-ac2bc9fdcb024e32a08ae084a4e5ff90/result.json.
최종 Player 증거: Build/Obj/P19B2/{DT1Costs,RT1Costs,ST1Costs,DT1Costs-shear}/result.json.

빌드/제품 GPU 실행 종료 후 독립 비용 sweep48회: CPU/GPU × active16/256/1024 ×
profiler off/on, off/on/on/off 두 회전, 상태별4표본·240tick, 총1024 바디·SDK worker2개.
24개의 실제 캡처 모두 complete/unacked0·event/counter drop0, owner 하위 depth/범위/스레드
정합성과 SDK task 제출→실행 연결 누락0을 검증했다. 모든 owner 비용 span은240tick이다.
길이0 span도 관측값으로 포함하고 instant와 구분했다. 처음 길이0 span을 제외한 집계는
폐기하고 집계기 수정·새48회 실행 결과만 최종 기록에 사용했다.

계측 on에서의 per-tick inclusive 평균 중앙값 (µs, 모듈 실험):

| backend·active | fetchResults | dispatcher drain | active pose 수집 | snapshot 통계 | render 준비 | render 병합 |
|---|---:|---:|---:|---:|---:|---:|
| CPU16 | 88.38 | 1.76 | 1.45 | 0.19 | 0.50 | 1.03 |
| CPU256 | 129.86 | 0.29 | 15.13 | 0.93 | 4.92 | 13.15 |
| CPU1024 | 236.19 | 0.21 | 58.90 | 4.62 | 20.67 | 56.99 |
| GPU16 | 1159.83 | 0.51 | 1.89 | 0.23 | 0.68 | 1.34 |
| GPU256 | 1223.67 | 0.28 | 15.53 | 0.98 | 5.51 | 13.28 |
| GPU1024 | 1276.21 | 0.33 | 61.13 | 4.69 | 22.08 | 57.60 |

CPU Advance 평균 off→on: 16개67.57→108.35µs(+60.36%), 256개150.30→174.07µs
(+15.82%), 1024개354.97→389.61µs(+9.76%). 이는 현재 전체 계측/counter 켜짐의 wall
차이이며 새 marker만의 추가 비용을 분리한 비교가 아니다. GPU에서는 on/off wall 차이가
음수(-7.20/-2.78/-4.37%)였으나 이를 음의 계측 비용이나 성능 개선으로 주장하지 않는다.
SDK task 제출→실행 지연의 평균 중앙값은 CPU 약1.01/1.07/2.01µs, GPU 약2.61/2.99/3.42µs.
모든 평균 wall cell은 CV10% 이하이며 CPU256 wall p99는 변동 기준을 넘어서 수용에서 제외.
개별 phase·task 지연의 CV도 summary에 제공하며 변동 기준 초과 지표는 성능 수용에 쓰지 않는다.

현재 자료에서 drain 자체보다 SDK 완료 지연과 active pose/render 후처리 비용이 크다.
다음 T1 대상은 **SDK worker 예산(현재 제품 기본값은 hardware-derived)과 작은 씬의
계측 비용**이다. worker2개 모듈 표만으로 제품 기본값을 교체하거나 공유 dispatcher/새
physics owner thread를 도입하지 않는다. 전체 제품 workload/성능 수용 M3은 별도 잔여다.

재현: Tools/regression/verify-physics-t1-costs.ps1. 최종 sweep:
Build/Obj/Phase19T1Costs/run-f088211044324a7297711a6158a74883/result.json 및 summary.json.
통합 증거: Build/Obj/Phase19T1Costs/result.json (현재 Phase19T1/result.json도 동일 최신 기록).
source SHA256·native/제품 결과·비용 표·캡처를 연결했으며 과거 후보 비교와 구분한다.



### T1 SDK worker 예산 비교 (2026-10-03)

worker 예산 실험을 완료했다. **제품 코드와 기본값은 변경하지 않았다.** 현재 workers=0은
min(256,max(1,hardware threads-4))이며 이 머신의12 logical threads에서는8 SDK workers다.
benchmark CLI에 선택적 여섯째 workers 인수를 추가했고 기존 다섯 인수는2 workers를 유지한다.
검증 도구 verify-physics-t1-workers.ps1은 요청값·실제 worker 수·backend·활성 수·정상 exit를
확인한다. CPU/GPU active16/256/1024, budget auto/1/2/4, profiler off/on, 대칭 순서
0/1/2/4/4/2/1/0 두 회전, 각 budget/state4표본·60warm+240측정 ticks, **최종192회** 실행했다.
빌드나 다른 제품/GPU gate 없이 순차 측정했다. on96캡처는 complete/unacked0/drop0,
SDK task 제출→실행과 owner 하위 계층 오류0 및 **모든 owner marker240 tick 보존**을 확인했다.

계측 off Advance 평균 중앙값 (µs, 총1024 자유 이동 primitive boxes·접촉 없음):

| backend·active | 자동8 workers | 1 worker | 2 workers | 4 workers |
|---|---:|---:|---:|---:|
| CPU16 | 438.75 | 57.97 | 67.45 | 120.48 |
| CPU256 | 474.75 | 130.14 | 150.66 | 215.67 |
| CPU1024 | 645.46 | 336.45 | 358.94 | 414.86 |
| GPU16 | 1486.62 | 1187.81 | 1284.99 | 1200.78 |
| GPU256 | 1483.33 | 1305.85 | 1282.85 | 1254.69 |
| GPU1024 | 1673.58 | 1392.72 | 1431.38 | 1380.10 |

평균은 최종 모든 budget/cell에서 CV10% 이하. p99는 여러 셀이 변동 기준을 넘었으며
summary의 p99Stable=false 표본은 수용에서 제외한다. on CPU16 평균은 auto8=534.14µs,
1=70.39,2=113.57,4=258.27로 동일한 증가 경향을 보였다. GPU의 최저 budget은 활성 수와
계측 상태에 따라 달라졌다. 자유 이동1024바디 모듈 표로 접촉이 많은 대규모 씬이나 전체
제품 기본 예산을 수용하지 않는다. 이번에 제품 회귀를 재실행했다고 주장하지 않으며,
기존 제품 소스 SHA256이 Phase19T1Costs 기록과 동일함을 확인했다.

현재 dispatcher는 submitTask마다 mutex 안에서 notify_one을 호출한다. SDK worker가
현재 task의 release에서 successor를 발행한 경우에도 다른 잠든 worker를 깨우는 경로다.
CPU16 계측 on 첫 대칭 구간의 제출→실행 지연 평균은1/2/4/8 workers에서 약0.58/0.99/3.20/
7.85µs였다. SDK task 수도64/66/68/72 per tick으로 달라지므로 wake 비용만이 원인이라고
단정하지 않는다. **다음 T1 후보는 worker successor의 wake/handoff 정책**이며 dependency
fan-out·bounded queue saturation·inline fallback·종료 drain/수명 계약을 함께 검증해야 한다.
기본값을 임의의1/2/4로 제한하거나 shared dispatcher·별도 physics owner thread를 도입하지
않는다. 접촉 workload와 큰 활성 집합, 전체 제품 성능 M3 수용도 남는다. T1은 진행 중이다.

처음128MiB 보존 예산의 일부8-worker 캡처는 early frame이 trim되어 owner237~239 또는
219~228 tick만 보존했다. complete/unacked0/drop0는 측정 창 전체 보존의 보장이 아니다.
처음 두 sweep은 최종 수용에서 제외했다. **벤치마크만**512MiB 예산으로 재빌드하고 전체
192회를 새로 측정했다. 최종 최대 캡처 메모리는약134.32MiB다. 제품 profiler 예산은 그대로다.

통합 증거 Build/Obj/Phase19T1Workers/result.json: source SHA256, 두 고유 sweep root,
구성/예산별 평균·p99·CV·auto 대비 관측 차이. 최종 off root run-625e2600ba074f26a0a8006c82c14bba,
on root run-7b0d44571bcb409ca9686b82a2c89098. 재현: verify-physics-t1-workers.ps1;
계측 on 재현: 동일 도구 -EnableProfiler. 제품 설정을 바꾸는 도구가 아니다.

### T1 SDK successor wake 후보 비교 (2026-10-03)

제품 적용 전 독립 후보에서 실제 worker의 `release()`가 제출한 첫 successor를
현재 worker가 이어서 처리하도록 했다. 게임 스레드 제출과 `run()` 중 중첩 제출은
기존 wake를 유지하며, 대기 queue가 두 개 이상이면 다른 worker를 깨운다.
inline 재귀 실행에는 release 문맥을 저장·복원한다. 제품 dispatcher는 변경하지 않았다.

기존/후보 ABBA 두 반복, CPU/GPU × active16/256/1024 × 계측 off/on의96회 비교를
수행했다. 양쪽 모두 현재 auto8 workers,1024 자유 이동 box,60 warm+240 측정 tick이다.
계측48회는 owner240 tick 전체, 하위 계층 및 task 연결 정합을 요구했다.

| backend / active | off 평균 변화 | on 평균 변화 |
|---|---:|---:|
| CPU16 | -41.43% | -45.75% |
| CPU256 | -32.40% | -35.12% |
| CPU1024 | -26.01% | -26.33% |
| GPU16 | -7.76% | -13.45% |
| GPU256 | -5.71% | -7.22% |
| GPU1024 | -4.41% | -4.58% |

표는 후보/기존 median wall mean 관측 변화이며 모든 평균의 CV는10% 이하다.
GPU16 off p99는+4.99%로 증가했고, CPU16/256 off 및 GPU1024 off p99는
CV10% 초과로 수용 판정에서 제외한다. 접촉 없는 모듈 workload 결과를 제품 전체
성능 개선으로 일반화하지 않는다. 접촉·큰 활성 집합·제품 M3 수용 전까지 후보는
독립 검증에만 남기고 T1은 진행 중으로 유지한다.

재현: `verify-physics-t1-benchmark.ps1 -CandidateSide wake -Workers 0`.
통합 증거: `Build/Obj/Phase19T1Wake/result.json`; 원본96회:
`Build/Obj/Phase19T1Bench/run-1f22d17923ad467cb2900933eeadbf74/result.json`.

후보 native Debug/Release/ASan 각8162,Shipping3531 및 실제GPU 검증 통과.
SDK 시뮬레이션 검사에 더해 단일 release successor256개 체인(1/2/8 workers,
queue1/4),64 leaf fan-out·queue1 포화·inline fallback, `run()` 중 자식 제출 후
대기(2 workers)를 독립 합성 task로 검증했다. 이 스트레스 검사는 실제SDK workload와
구분하며 모든 task의 run/release가 각각 한 번 수행되었는지 확인한다.
재현: `verify-physics-t1.ps1 -Configuration All -RequireGpu -WakeCandidate`.

### T1 지속 접촉·4096 바디 후보 비교 (2026-10-03)

wake 후보와 기존 dispatcher를 auto8 workers에서 CPU/GPU × free4096,
contact1024/contact4096 × 계측 off/on으로96회 ABBA 비교했다. 각 상태·후보별4실행,
60 warm+240 측정 tick이다. contact는 마찰0 바닥 위에서 vx1m/s로 미끄러지는
독립 dynamic box이며 바닥 static body 하나는 표의 dynamic 개수에 포함하지 않는다.
복잡한 적층이나 dynamic-dynamic 접촉 장면을 대표하지 않는다.

측정240 tick 모두 활성 바디 수 일치, 접촉 장면은 매 tick contacts_stored≥dynamic 개수,
이벤트/접촉 누락0·미해결 식별자0을 요구했다. 계측48회 모두 owner240 tick 보존,
task 연결·하위 계층 정합을 통과했다. SDK 오류 출력은 없으며 profiler 종료는
abandoned/retained/foreign0이다. 프로파일러 확인 비용은 Advance wall 측정 밖에 둔다.

| backend / workload | off 평균 변화 | on 평균 변화 | off p99 변화 |
|---|---:|---:|---:|
| CPU free4096 | -9.56% | -7.30% | CV>10%, 제외 |
| CPU contact1024 | -9.41% | -9.39% | -9.23% |
| CPU contact4096 | -0.63% | -0.84% | +4.33% |
| GPU free4096 | +0.62% | -3.65% | CV>10%, 제외 |
| GPU contact1024 | -1.37% | -11.43% | -2.96% |
| GPU contact4096 | -7.92% | -3.17% | -5.83% |

변화는 후보/기존 median의 관측치다. 평균 CV는 전부10% 이하이나 작은 차이가
유의한 개선이라는 의미는 아니다. CPU contact4096의 on p99도+2.67%다.
무접촉/독립 바닥 접촉에서의 평균 이득만으로 제품 최적화를 수용하지 않는다.
제품 dispatcher·worker 기본값은 유지하며 다음은 dynamic-dynamic 적층·접촉 장면,
worker 예산별 fan-out 거동 및 제품 M3 성능 수용이다. T1은 진행 중이다.

재현: `verify-physics-t1-contact.ps1` (기존/후보 빌드 포함).
완료 root 집계: `summarize-physics-t1-contact.py <root>`.
통합 증거 `Build/Obj/Phase19T1Contact/result.json`; 원본96회
`Build/Obj/Phase19T1Contact/run-8dc2d47d72fa4ce58cfd19473d8fd6e9/result.json`.

### T1 dynamic-dynamic 적층·worker 예산 검증 (2026-10-03)

**release-successor의 무조건적 handoff 후보는 현재 제품에 채택하지 않는다.**
worker 수와 workload에 따라 평균/p99 이득이 달라지고, 두 worker의 GPU 평균 증가가
추가 실행에서도 관측됐다. 크기는 일정하지 않아 통계적 유의성을 주장하지 않는다.
전용 SDK dispatcher의 기존 wake 및 auto worker 예산을 유지한다. T1은 진행 중이다.

최종 부하는 4개 높이의 dynamic box 적층으로, 회전 전체와 Z 이동을 잠그고 X/Y는
허용한다. 마찰0·vx1m/s·중력을 사용하며 static 바닥 하나를 별도로 둔다. 이 조건은
일정한 solver 접촉 부하를 비교하기 위한 제약 적층이며 비제약 적층 안정성이나
전체 제품 장면 성능을 대표하지 않는다. Mathematics/SDK 제품 배선은 변경하지 않았다.

CPU/GPU × dynamic1024/4096 × worker1/2/4/auto8 × 계측 off/on을 ABBA 두 번씩
256회 실행했다. 각 상태/후보4표본,60 warm+240 측정 tick이다. 매 tick 활성 바디 수,
이벤트/접촉 누락0·미해결 식별자0, 유한한 위치 및 높이0.2~5m를 확인했다.
실행 중 최소 평균 높이1.5m와 접촉 부하를 요구하고, 최종 집계는 매 tick dynamic
접촉 쌍≥바디 수/2를 재검증했다. 초기 프로그램의 접촉 하한은N/4였으나 최종 verifier와
집계는N/2다. 모든 유효 실행의 실제 최솟값은N×0.75, 평균 높이 최솟값은1.99636m였다.

| 계측 off 평균 관측 변화 | worker1 | worker2 | worker4 | auto8 |
|---|---:|---:|---:|---:|
| CPU1024 | -0.36% | -1.31% | -1.61% | -10.04% |
| CPU4096 | +0.11% | -1.12% | -2.18% | -2.49% |
| GPU1024 | +0.45% | +6.89% | -0.81% | -3.77% |
| GPU4096 | +0.52% | +0.24% | -1.73% | -4.17% |

auto8 CPU1024 off p99는-11.77%,CPU4096 off-1.93%,GPU4096 off-5.25%였다.
GPU1024 auto8 off p99는+0.26%다. on 평균/p99와 각 CV는 통합 JSON에 기록했다.
32개 상태의 평균 CV는 모두10% 이하지만 p99 8개 상태는10%를 초과하여 수용에서 제외했다.
작은 평균 차이의 방향이나 CV 통과만으로 유의한 성능 개선을 주장하지 않는다.

GPU1024/worker2만16회를 추가 실행했다. 추가 실행의 off/on 평균 변화는 각각
+0.67/+3.42%였다. 처음과 추가 실행의 후보별8표본을 합친 median은 off+4.97%,
on+2.42%다. 두 실행 시점의 분포가 달라 합산 수치는 관측 집계이며 유의성 판정이 아니다.
최종 유효272회 중 계측136회 모두 complete/unacked0/drop0,owner240 tick 전체,
하위 계층·task 연결 정합 및 profiler 종료 abandoned/retained/foreign0을 확인했다.
SDK 오류 출력은 없었다. 제품 Player/Editor M3 성능 수용은 별도다.

처음 비제약 적층 sweep은192회 후 GPU4096/worker1 기준선이 접촉 하한N/2를
239/240 tick만 만족해 중단됐다(최소1938쌍). 추가 진단에서 평균 높이1.48632m도
관측되어 일정한 적층 부하로 수용하지 않았다. 그 부분 sweep은 제외했고, 제약 적층의
전체256회를 새로 측정했다. 비제약 장면은 `stack-unconstrained` 진단 인자로 남긴다.
이는 wake 후보에 의한 제품 회귀라는 판정이 아니며, 비제약 GPU 적층/solver 조건의
안정성 비교는 M3에서 별도로 다룬다.

재현: `verify-physics-t1-stack.ps1`; 표적 재측정은 같은 도구에
`-Backends gpu -ActiveCounts 1024 -WorkerBudgets 2`를 지정한다.
집계: `summarize-physics-t1-stack.py <root> [output-json]`.
통합 증거 `Build/Obj/Phase19T1Stack/result.json`: source/binary SHA256,
32개 상태·추가 비교·제외 조건·제품 미채택 결정을 기록했다.
전체 root run-0a705619343d4ad594eaf4e048d4ecd9, 추가 root
run-22fa61964b70445488673b3723edfd72. 제품 source SHA는 이전 접촉 검증과 동일하다.

다음 T1 작업은 작은 씬의 SDK task 계측 비용 축소다. profiler 계층과 scene/tick/task
연결을 보존해야 하며, worker 수를 벤치마크만으로 고정하거나 무조건적 handoff를
제품에 넣지 않는다. 제품 M3 회귀·성능 수용도 남는다.

### T1 SDK 계측 게시 비용 축소 — 제품 적용 (2026-10-03)

worker의 `publish_thread()`가 SDK 작업 queue mutex를 잡고 실행되는 경로를 줄였다.
**계측 recording 중이고 SDK outstanding 작업이 남아 있을 때만** mutex를 놓고 게시한다.
게시가 끝나 mutex를 다시 얻은 뒤에 active batch를 감소시키므로 drain은 게시 완료까지
기다린다. off/pause 및 마지막 SDK 작업 뒤의 게시와 Shipping은 기존 직렬 경로를 유지한다.
SDK wake 정책·worker 기본값·after-fetch 렌더 준비·물리 소유권은 바꾸지 않았다.
마커를 삭제하거나 task 계측을 샘플링하지 않았다.

무조건적 mutex 해제 후보160회는 CPU 계측 on 이득을 보였지만 GPU256 on p99가
+7.88%여서 제품에 채택하지 않았다. 최종 제한 정책은1024 전체 바디의 active16/256/1024
CPU/GPU × off/on96회와 실제16바디 씬의 worker2/auto8 CPU/GPU × off/on64회로 검증했다.
각 후보/상태4표본,60 warm+240 측정 tick이다. auto8은 현재12 logical threads 기준이다.

| CPU 계측 on 평균 관측 변화 | 전체 바디 | 활성 바디 | worker | 변화 |
|---|---:|---:|---:|---:|
| 작은 활성집합 | 1024 | 16 | auto8 | -16.11% |
| 중간 활성집합 | 1024 | 256 | auto8 | -12.84% |
| 전체 활성 | 1024 | 1024 | auto8 | -9.18% |
| 실제 작은 씬 | 16 | 16 | auto8 | -15.32% |
| 실제 작은 씬 | 16 | 16 | 2 | -6.35% |

GPU on 평균은1024 전체 바디의 active16/256/1024에서-4.33/-6.08/-3.03%,
실제16바디에서는 auto8 -6.05%,worker2 -0.01%였다. GPU off active16의 p99가
처음+8.66%여서8회를 추가했다. 재측정은-4.76%로 방향이 반전되었으며 지속적인 증가나
유의한 개선을 판정하지 않는다. CV10% 초과 지표는 각각 제외한다. 표는 모듈 owner wall
관측이며 GPU 커널 시간·CPU 이용률·전체 제품 FPS 개선을 뜻하지 않는다.

최종 정책168회 중 계측80회는 owner240 tick 전체·complete/unacked0/drop0,
하위 계층과 Submit→PhysXTask(run+release)→Complete 연결을 확인했다.
기존 제출 연결 검사에 completion 누락·역전 및 SDK span 수와 completion 수 일치를
추가했다. 제품 profiler 예산은 그대로이며 벤치마크만512MiB를 사용한다.

Editor Debug/Release와 Player·Cooker Debug/Release/Shipping 빌드 통과(no-WPO는 기존 환경 우회).
동일 정책 native T1 D/R/ASan8161·Shipping3530 및 실제GPU 통과.
제품 코드의 T0 수명·close/drain은 D/R/ASan173·Shipping170 및 실제GPU 통과.
Player D/R/S는각27/0·완료된 game display·정상 종료0·불변 파일·cooked-only를 확인했다.
Debug shear는예상 종료3,Editor 정상 Play/Stop 두 번과 오류 중단 한 번에서6객체 복원,
Shipping 격리 통과. 제품은960×540 기준이며 높은 해상도 renderer backlog는 그대로다.

통합 증거 `Build/Obj/Phase19T1Profile/result.json`; 초기 후보는
`unconditional-result.json`으로 보존했다. 최종 large root
run-8fa7b1259d5b4cab942e1895971375c5,small root run-7516f4c3a4694bbea42103bc7bce7a00,
GPU off 재측정 root run-81c22448e80b4a879ad201e7e239bad5.
Editor root run-c5c30dd690a6477b873446a9dd931a66; Player P19B2/*T1Profile/result.json.

재현: `verify-physics-t1-benchmark.ps1 -CandidateSide profile -Workers 0` 및
`verify-physics-t1-profile-small.ps1`. 비교할 때만 serial 빌드의 `-LegacyPublication`으로
mutex 내부 게시 baseline을 생성한다. 현재 제품과 candidate 생성은 동일한 제한 정책이다.
이전 정책은 테스트 생성 소스에만 있으며 제품에 이중 배선하지 않는다.
통합 집계: `summarize-physics-t1-profile.py <large-root> <small-root> <confirm-root> [editor-root]`.

계측 게시 최적화 한 항목을 제품에 반영했으며 T1은 진행 중이다. SDK 전체 worker 예산과
다른 workload의 성능 수용,제품 M3 평균/p99·메모리·계측 on/off 수용은 남는다.
이번 기능 회귀 통과를 전체 제품 성능 수용으로 바꾸지 않는다.


### T2 owner query batch 첫 구현 (2026-10-03)

혼합 raycast/sweep/overlap 입력을 variant로 받고 결과는 요청별 expected 슬롯으로 돌려준다.
요청·hit·결과 span은 반환까지 빌리며, 비동기 작업이나 결과 lifetime을 숨기지 않는다.
owner idle 진입 검사와 슬롯 수 검사가 실패하면 출력은 변경하지 않는다. 개별 입력 오류는
다음 요청을 취소하지 않는다. overflow는 정확한 required_capacity와 truncated로 보고한다.

쓰기 없는 query_read 창에서 SDK query 구조를 한 번 flush한 뒤 owner가 순차 실행한다.
Physics.QueryBatch 아래 Physics.QueryStructureUpdate와 기존 쿼리별 scope를 유지한다.
조회 후 idle 복원과 변경된 바디의 재조회, 외부 스레드/진행 중 simulate 거부를 검증한다.
재현: Tools/regression/verify-physics-t2.ps1 -Configuration All -RequireGpu.
초기 캡처 검사에서 Scene context 누락을 발견했다. 첫 gate는 marker 존재를 확인했고,
후속 Scene 소비자 연결에서 context를 보완해 nonzero Scene ID를 다시 검증했다.
추가 tick으로 기존 command tick을 바꾼 fixture 오류는 수정 후 결과만 수용한다.
제품 소비자 연결·전체 배치 계측/성능 수용은 후속 작업이며 T2는 progress다.

최종 네이티브 결과: Debug/Release/ASan 각201 checks, Shipping198 checks, 모두 GPU 검증 통과.
비Shipping capture 완료·unacked0·QueryStructureUpdate 계층 존재를 확인했다.
증거: Build/Obj/Phase19T2/result.json 및 구성별 result.jsonl/baseline.ceprof.
제품 Editor/Player 빌드·배치 호출 게이트는 이번 네이티브 결과에 포함하지 않는다.


### T2 Scene 세션·소비자 연결 (2026-10-03)

Scene::QueryPhysicsBatch → ScenePhysicsSimulation::QueryBatch → PhysicsScene::query_batch로
owner/active runtime 검사를 통일했다. Editor 상태와 Stop 후에는 SDK runtime을 만들지 않고
wrong_phase를 반환한다. C++ Scene 소비자는 혼합 batch를 사용할 수 있다. 기존 CLR API의
raycast/overlap은 Scene의 단일 요청 batch 경계로 연결하며 즉시 반환 계약과 ABI는 유지한다.
관리형 다중 요청 ABI는 아직 제공하지 않는다. 단일 요청 경계 통합은 성능 개선 주장과 구분한다.

QueryBatch/QueryStructureUpdate 및 하위 query scope에 Scene ID·현재 fixed tick을 명시한다.
Tick 0은 simulate 이전/새 세션의 유효한 초기 조회이며, idle이라는 이유로 Scene 신원을
비워 두지 않는다. 외부 스레드·진행 중 simulate는 batch 진입에서 거부한다.
재현 도구: Tools/regression/verify-physics-t2-session.ps1.
제품 및 최종 구성별 수용 결과는 통합 result.json의 실행 증거로 판정한다.
T2 progress 유지: query workload 계측/성능 수용과 관리형 다중 요청 소비자 범위는 별도다.


최종 세션 네이티브 결과: Debug/Release/ASan 각496, Shipping492 checks; 모든 구성 GPU 실행 확인.
비Shipping capture complete/unacked0 및 QueryBatch·QueryStructureUpdate의 nonzero Scene ID 확인.
Debug Editor의 두 정상 Play 각27/0, 두 Stop의 6개 객체 변환 복원과 shear 실패 자동 Stop 복원 통과.
Editor 증거: Build/Obj/Phase19B2Editor/run-08fcbe31dd5645e9a4800e78aae18cb7/result.json.
Debug Player 정상27/0·completedGameDisplay·exit0 및 shear 예상 exit3·패키지 불변 통과.
초기 Windows PowerShell 5의 Environment 인자 미지원, 재링크 후 forest recipe 덮어쓰기의
시작 거부는 실행 환경 실패로 제외했다. PowerShell 7 및 기존 검증 forest 리소스 배치 후 재실행했다.

최종 제품 수용: Editor Debug/Release, Player/Cooker Debug/Release/Shipping 빌드 통과.
Player Debug/Release/Shipping 각27/0, completedGameDisplay=true·exit0·원본 및 stage 불변 확인.
Debug shear 예상 exit3 및 Shipping 소켓/CommandService 격리도 통과했다.
통합 증거: Build/Obj/Phase19T2Session/result.json. Debug 최종 context 재링크 로그를 포함한다.
재현 제품 게이트는 verify-physics-b2-http.ps1, build-physics-b2-player-fixture.ps1,
verify-physics-b2-player.ps1 및 verify-player-shipping-isolation.ps1을 재사용한다.
현재 제품 호출은 관리형 개별 overlap의 단일 요청 batch이며 혼합 다중 요청 제품 gate와
query workload 평균/p99·계측 on/off 수용을 완료한 것으로 계산하지 않는다. T2는 progress다.


### T2 C# 다중 요청 배치·제품 검증 (2026-10-03)

Physics.QueryBatch의 요청/hit/요청별 결과 span을 하나의 Scene 읽기 창에 연결했다.
raycast/overlap 혼합, 개별 오류 이후 계속 실행, zero-capacity 정확한 필요 용량 및 overflow,
출력 구간 겹침/버퍼 alias/음수·과대 offset 거부, 출력 보존과 최대64개 요청을 검증한다.
요청64/공용 hit4096/요청별 hit256 상한이다. 입력 구간은 순서와 무관하게 겹침을 거부한다.
소유 scratch에서 native/managed hit를 변환한 뒤 성공한 결과만 커밋하며 외부 포인터를 보관하지 않는다.
Native/managed 함수 테이블168슬롯과 ABI32를 함께 적용했다. 구 ABI 호환 배선은 남기지 않는다.

ABI Debug/Release 각33 checks 통과. Editor 두 정상 Play 각 기존27+배치48 checks,
두 Stop과 세 번째 shear 실패 Stop에서6개 객체 변환 복원을 확인했다.
Player Debug/Release/Shipping 각 기존27+배치48=75 checks, 완료 display·exit0·패키지 불변 통과.
Shipping 소켓/CommandService 격리도 통과했다. Editor D/R, Player/Cooker D/R/S 및
배포에 포함되는 AssetPacker D/R을 ABI32로 빌드했다. 초기 AssetPacker ABI31의 배포 거부와
검증 래퍼의 지원하지 않는 Shear 인자는 환경/도구 실패로 제외하고 최종 재실행만 수용했다.

통합 증거: Build/Obj/Phase19T2Managed/result.json.
재현: verify-physics-script-abi.ps1, verify-physics-query-batch-http.ps1,
verify-physics-query-batch-player.ps1 (Tools/regression). 제품 다중 요청 검증은 완료했으나
query workload 계층 capture/평균·p99·계측 on/off 성능 수용은 남는다. T2 progress 유지.

## T2 네이티브 혼합 쿼리 계측 (2026-10-03)

제품 코드는 변경하지 않았다. `physics_t2_benchmark.cpp`는 static body 1024개 씬에서
ray/sphere sweep/sphere overlap을 요청1/16/64개로 반복하고 scalar와 batch의 body/shape/거리/개수를 비교한다.
CPU 및 실제 GPU backend, Release /O2, profiler off/on, 각 조합 ABBA 두 회(4표본/측),
60회 warmup 후600회 측정으로 총96개 프로세스를 실행했다. 프로세스별 원시 mean/p99를 보존한다.
GPU backend 표기는 PhysX 씬 설정이며 이 수치는 CPU owner query wall time이다. GPU kernel 시간이나 FPS가 아니다.

48개 capture 모두 complete·unacked0·dropped0. `Physics.QueryBatch` 아래
`Physics.QueryStructureUpdate`와 개별 query의 thread/depth/시간 포함 관계 및 Scene identity/tick1/task0를 검증했다.
배치 capture당 구조 갱신600회, 쿼리 요청수×600회. 전체 결과 일치·계층 위반0.
벽시계 구간에 쿼리 실행만 포함하며 frame publication·초기화·결과 비교는 제외했다.
profiler 수집 스레드의 경합은 on 비용에 포함된다. worker 시간을 더하지 않는다.

| backend | 요청 | scalar 평균 μs | batch 평균 μs | batch on 평균 μs | 평균 비교 안정 | on/off 안정 |
|---|---:|---:|---:|---:|---|---|
| cpu | 1 | 0.374 | 0.385 | 3.903 | 통과 | 미수용 |
| cpu | 16 | 5.973 | 6.012 | 7.096 | 통과 | 통과 |
| cpu | 64 | 25.730 | 25.344 | 28.300 | 통과 | 통과 |
| gpu | 1 | 0.338 | 0.381 | 3.787 | 통과 | 미수용 |
| gpu | 16 | 6.000 | 6.039 | 7.298 | 통과 | 통과 |
| gpu | 64 | 26.290 | 25.180 | 27.683 | 통과 | 통과 |

48개 mean/p99 지표 중 CV≤10%는 24개다. 나머지는 원시 값을 보존하되 수용하지 않는다.
변경 없는 씬의 native batch에서 일관된 속도 개선을 입증하지 못했다. 소규모 단일 요청은 추가 read-window/flush/scope 비용을 가진다.
배치의 CLR 왕복 감소와 구조 갱신 단일화 효과는 이 native baseline으로 결론 내리지 않는다.
T2 progress 유지. 다음은 실제 managed 반복 부하 및 moving-scene 갱신 부하의 동일 parity/capture 측정,
불안정 p99 재측정과 성능 수용 기준 판정이다. 제품 전체 M3 수용과 별도다.

증거: `Build/Obj/Phase19T2QueryBench/Release/result.json` 및 개별 `.ceprof`.
재현: `pwsh -NoProfile -File Tools/regression/build-physics-t2-benchmark.ps1 -Configuration Release`,
`pwsh -NoProfile -File Tools/regression/verify-physics-t2-benchmark.ps1`. 후자는 요약을 함께 생성한다.

## T2 실제 managed moving-query 부하 (2026-10-03)

`PhysicsB2PlayerProbe`에 `CE_PHYSICS_QUERY_BENCH=1`인 경우만 실행하는 반복 부하를 추가했다.
기존 기능9 checks/role 및 batch16 checks/role 이후 dynamic convex 바디를 velocity(1,0,0)로 움직인다.
각 PostPhysics block은 현재 body 위치를 기준으로 ray/overlap 요청16/64개를 구성한다.
블록 사이의 physics 이동 및 현재 위치에서 자기 convex 충돌을 검증한다. 블록 내부는 synchronous query만
반복하므로 SDK simulation과 동시 실행하지 않는다. 결과 개수·required capacity·truncation 및
Entity/Component/Shape/거리 일치를 순서에 의존하지 않고 비교한다.

Release 실제 Player 960×540, profiler off, 두 독립 프로세스에서 각각 ABBA 두 회,
각 block60 warmup+600 timed samples. 입력/저장소 준비와 parity 검사는 wall-time 구간 밖이다.
CLR 호출, native scratch/변환 및 bridge 검증 비용은 포함한다. 각 측·요청 크기별8개 block 결과의 median과 CV를 계산했다.
초기 다른 Player와 겹친 실행은 성능 근거에서 제외했다.

| 요청 | scalar 평균 median μs | batch 평균 median μs | 감소 관측 | scalar/batch CV | 수용 |
|---:|---:|---:|---:|---:|---|
| 16 | 23.932 | 14.563 | 39.1% | 17.4% / 30.0% | 미수용 |
| 64 | 94.791 | 46.774 | 50.7% | 8.9% / 14.1% | 미수용 |

평균상 batch가 더 짧았지만 CV10% 기준에서 scalar64 평균만 통과했다. 나머지 평균 및 모든 p99는
변동폭이 커 성능 수용하지 않는다. 관측 감소율을 제품 성능 보장이나 FPS 개선으로 사용하지 않는다.
두 프로세스의32개 block 모두 parity/moving collision 통과. 기존75 checks, 완료 Game display8회 이상,
2000 smoke frames·exit0·패키지 불변·cooked-only geometry gate도 통과했다.
기본 벤치 옵션을 끈 새 패키지의 기존75 checks 및12000 smoke frames도 별도 통과했다.

통합 증거 `Build/Obj/Phase19T2QueryBench/Managed/result.json`. 원시 로그/블록 결과/제품 영수증/소스 hash 포함.
새 패키지 `Build/Obj/P19B2/RT2QueryPerf1/result.json`.
재현: `pwsh -NoProfile -File Tools/regression/verify-physics-managed-query-benchmark.ps1 -Stage <packaged-stage>`,
`python Tools/regression/summarize-physics-managed-query-benchmark.py`. package build recipe는 기존 B2 fixture를 사용한다.

T2 progress 유지. managed profiler on/off 및 Physics.ScriptQueryBatch 아래 제품 계층 capture,
변동 원인 분석·안정적인 mean/p99 재측정과 대표 workload/M3 성능 수용이 남는다.
이번 단계는 Release off 측정이며 Debug/Shipping 성능이나 제품 전체 성능을 대신하지 않는다.

## T2 managed profiler on/off·제품 계층 capture (2026-10-03)

Development Player에 기존 인증된 command service의 profile.record/pause/save를 연결했다.
동일 descriptor의 host role을 Both로 확장하고 Shipping의 handler/등록은 제외했다.
gate file을 설정한 probe는 profiler 명령 응답을 확인한 후에만 반복 부하를 시작한다.
입력/저장소 준비·결과 비교·HTTP 제어·capture 저장은 timed query 구간 밖이다.

첫 capture에서 Player의 profiler frame publication 누락을 확인했다. 이벤트가1프레임으로
합쳐지고 counter1785개가 누락됐다. Player EndOfFrame 뒤 Time frame count를 발행하도록 수정했다.
이전 capture는 수용 근거에서 제외하며 제품 profiler 메모리 예산은 변경하지 않았다.

Release 실제 Player 독립4프로세스 off/on/on/off, 각 요청16/64 scalar/batch ABBA 두 회,
60 warmup+600 samples/block, 총64블록을 측정했다. on capture 두 개는각600프레임,
complete/unacked0/event drop0/counter drop0, Scene identity/양수 fixed tick/owner task0 및 계층 위반0.
각 capture ScriptQueryBatch5305·QueryBatch217145·QueryStructureUpdate217145·ray/overlap423884개.
유효 bridge batch와 scalar-as-batch를 함께 검증한다. warmup/parity/기존 기능 쿼리도 capture에 포함된다.
capture 메모리는약67.8MiB이며 제품 전체 메모리 수용이나 GPU kernel 시간/FPS 측정이 아니다.

| 요청 | scalar off 평균 median μs | batch off 평균 median μs | batch on 평균 median μs | on/off 평균 증가 관측 | 수용 |
|---:|---:|---:|---:|---:|---|
| 16 | 24.166 | 11.072 | 11.871 | 7.2% | 미수용 |
| 64 | 95.220 | 42.194 | 48.783 | 15.6% | 미수용 |

개별 지표 CV≤10% 기준은 그대로 유지한다. batch off 평균 CV16개20.4%/64개11.7%,
batch on 평균 CV16개18.7%/64개6.9%다. 비교 쌍 중 한쪽 이상이 불안정하므로
평균 감소율이나 on/off 비용을 성능 수용으로 올리지 않는다. p99 비교도 모두 미수용이다.
네 실행 모두 moving convex parity·기존 body27 assertions·완료 Game display·2000 smoke frames·
exit0·immutable package·cooked-only geometry gate 통과. 기존 batch16/role 출력도 보존되어 있다.
Release/Shipping Player 빌드와 Shipping service/socket 격리를 확인했다. Release no-WPO는 기존 빌드 우회다.

증거 `Build/Obj/Phase19T2QueryBench/ManagedProfile/result.json`, 개별 capture와 profile-commands JSONL.
실패/제외 이력: 최초 CommandRoles enum의 operator| 사용 compile 오류(Both로 수정),
capture probe 초기 ThreadStream link 누락(수정), Player 프레임 발행 전 counter 누락.
최종 빌드·구문·capture probe·제품 실행 결과만 수용한다.

재현: `build-physics-managed-query-capture-probe.ps1`,
`verify-physics-managed-query-profile.ps1 -Stage <fresh-stage>`,
`summarize-physics-managed-query-profile.py` (Tools/regression).

T2 managed on/off·제품 계층 capture 게이트는 검증했다. T2 progress 유지.
다음은 tiered JIT/렌더 경합/초기화 등 변동 원인을 분리하여 안정적인 평균/p99를 다시 측정하고,
대표 query 부하 성능 수용을 판정하는 작업이다. 전체 simulation/worker/메모리 M3는 별도 잔여다.

## T2 변동 진단 — tiered JIT 비활성 조건 (2026-10-03)

동일한 Release 패키지와 renderer/physics/query 부하를 유지하고 테스트 프로세스에만
`DOTNET_TieredCompilation=0`, `COMPlus_TieredCompilation=0`을 전달했다.
기본 실행에서는 해당 변수를 추가하지 않는다. 엔진/패키지/runtimeconfig의 기본 JIT 정책은 변경하지 않았다.
별도 RunName을 사용하여 기존 ManagedProfile 증거를 덮어쓰지 않는다.

off/on/on/off 독립4프로세스, 각16/64 요청 scalar/batch ABBA 두 회, 총64블록을 재측정했다.
모든 parity/이동 충돌/기존 body 검사/2000 smoke frames/완료 display/exit0/패키지 불변 통과.
on capture 두 개도 complete·unacked0·event/counter drop0·계층 위반0을 확인했다.

| 요청 | batch off 평균 median μs | batch on 평균 median μs | off/on CV | 평균 비교 수용 |
|---:|---:|---:|---:|---|
| 16 | 11.483 | 13.113 | 21.8% / 18.5% | 미수용 |
| 64 | 40.753 | 48.843 | 14.7% / 6.1% | 미수용 |

평균 및 p99의 모든 비교 쌍은 기존 CV10% 기준에서 미수용이다.
이 조건만으로 변동이 해소되지 않았다. JIT가 영향이 없다는 결론이나 렌더 경합이 원인이라는 확정으로 확대하지 않는다.
기존 기본 JIT 측정과 시간상 분리된 비교이며 CPU/GPU 부하 및 OS 스케줄링의 차이도 있을 수 있다.
다음은 완료 display 기준을 유지하며 렌더/OS 스케줄링 간섭을 통제하거나 관측하는 실험이다.
제품 기본 정책 변경과 성능 수용은 하지 않았다. T2 progress 유지.

증거 `Build/Obj/Phase19T2QueryBench/ManagedProfileNoTier/result.json`.
재현: `verify-physics-managed-query-profile.ps1 -Stage <same-stage> -DisableTieredCompilation -RunName ManagedProfileNoTier`,
`summarize-physics-managed-query-profile.py ManagedProfileNoTier` (Tools/regression).

## T2 CPU 배치 통제 진단 (2026-10-03)

동일 Release 패키지와 tiered JIT off를 유지하고 query ready gate 전에 Player CPU 배치를 통제했다.
Windows GetLogicalProcessorInformation의 physical-core mask를 읽어 LP0/1 코어(mask3)를 예약했다.
CoreWindow::Then의 message-loop owner는 HWND owner thread와 같은 스레드이므로 해당 owner를 LP0(mask1)에 고정했다.
ready 경계에 존재하는62개 Player 스레드 중 나머지는 LP2~11(mask4092)에 배치했다.
부팅 당시 process allowed mask4095 및 스레드별 이전/적용 mask를 cpu-placement.json에 보존했다.
프로세스 밖 OS 작업·kernel/GPU 작업·이후 새로 생긴 스레드까지 격리한 조건은 아니다.

off/on/on/off4프로세스·64블록, 기존60 warmup/600 timed samples/ABBA 두 회다.
모든 moving convex parity/body27 assertions/완료display/2000 smoke frames/exit0/패키지 불변 통과.
on capture각228/215frames, complete·unacked0·event/counter drop0·계층 위반0 통과.
각 ScriptQueryBatch5305/QueryBatch217145/QueryStructureUpdate217145/query423884를 확인했다.

| 요청 | scalar off 평균 median μs | batch off 평균 median μs | scalar/batch CV | 평균 비교 |
|---:|---:|---:|---:|---|
| 16 | 24.898 | 11.114 | 6.6% / 23.7% | 미수용 |
| 64 | 93.328 | 44.371 | 6.3% / 9.0% | 통제 조건에서 통과 |

64개 off 평균 비교에서 batch가52.5% 짧았고 양측 CV≤10%를 확인했다.
이 결과는 해당 CPU 배치·JIT·fixture 범위의 비교 수용이다. 제품 기본 정책에서의 성능 보장이나
렌더 경합이 변동의 단독 원인이라는 확정으로 확대하지 않는다. 요청16, 모든 p99 비교와 on/off 비용은 미수용이다.
CPU 고정을 후속 렌더 smoke까지 유지하면 처리량이 크게 떨어졌다. 측정16블록 완료 이후
느린 한 실행은 기존 affinity로 수동 복원했고, 하네스에 측정 후 살아 있는 기존 스레드의 원래 mask 복원을 추가했다.
복원은 timed query 구간 밖이며 모든 실행의 query 타이밍 구간은 같은 배치를 사용했다.
이 고정 정책을 제품 기본값으로 적용하지 않는다. 초기 실행은 shutdown까지 고정을 유지한 기록도 포함된다.
전체 렌더 처리량 수치는 별도로 집계하지 않았으므로 그 감소율은 주장하지 않는다.

증거 `Build/Obj/Phase19T2QueryBench/ManagedProfileCpuSplit/result.json` 및 cpu-placement.json/개별 capture.
재현: `verify-physics-managed-query-profile.ps1 -Stage <same-stage> -DisableTieredCompilation -IsolateGameThread -RunName ManagedProfileCpuSplit`,
`summarize-physics-managed-query-profile.py ManagedProfileCpuSplit` (Tools/regression).

T2 progress 유지. 다음은 owner CPU 소비와 wall time을 구분하는 계측, p99의 원시 표본/스케줄링 지연 분석,
실제 기본 스케줄링 조건에서 평균/p99·계측 비용 수용이다. 대표 workload·제품 전체 M3도 남는다.

## T2 owner CPU 소비·원시 지연 계측 (2026-10-03)

fixture probe에 Windows GetThreadTimes(kernel+user), QueryThreadCycleTime, OS thread ID를 추가했다.
60 warmup 뒤600개 timed query loop의 경계에서만 읽으며 호출별 P/Invoke 계측은 추가하지 않는다.
cycle은 시간/GHz로 환산하지 않는다. CPU block은 loop/timer와 accounting 경계 비용을 포함하므로
개별 query scope 합과 다른 구간이다. rawUs는 정렬 전 실행 순서의600개 값을 보존한다.

새 Release 패키지, tiered JIT off, 기본 CPU 배치, off/on/on/off4프로세스·64블록이다.
동일 owner thread·원시 표본 수/유한 값·CPU counter 단조성을 검사했다.
기존 body27 assertions·moving convex parity·완료display·2000 smoke frames·exit0·패키지 불변 통과.
on capture 두 개 모두 complete/unacked0/event-counter drop0/계층 위반0 통과했다.

| 상태 | 요청 | 호출 | block wall CV | cycle CV | wall/cycle 상관 |
|---|---:|---|---:|---:|---:|
| off | 16 | scalar | 22.4% | 22.6% | 1.000 |
| off | 16 | batch | 17.1% | 16.7% | 0.997 |
| off | 64 | scalar | 6.0% | 6.0% | 1.000 |
| off | 64 | batch | 10.6% | 10.4% | 1.000 |
| on | 16 | scalar | 8.1% | 8.0% | 0.999 |
| on | 16 | batch | 14.9% | 14.9% | 1.000 |
| on | 64 | scalar | 8.9% | 8.8% | 0.999 |
| on | 64 | batch | 6.6% | 6.8% | 0.997 |

8블록/조합에서 wall/cycle 상관0.997~1.000을 관측했다. wall 변동만 있고 cycle 소비는 일정한 패턴이 아니다.
owner descheduling만을 전체 변동의 설명으로 삼을 근거는 부족하다. CPU/cache/allocator/실행 경로 등의
어느 항목이 원인인지는 이 계측으로 확정하지 않는다. 과거 실행과 패키지/계측 경계가 달라 직접 성능 회귀 판정도 하지 않는다.
GetThreadTimes의 표기는100ns지만 이번 CPU 차분 값은0/15625/31250/46875/62500/78125μs였다.
64블록 중30개에서 CPU 차분이 wall보다 크다. wall-CPU를 정확한 wait 시간으로 해석하거나 음수를0으로 자르지 않는다.
38,400개 원시 표본에서 block median의3배 초과37개를 관측했다. 위치/최대/처음-마지막 사분위 median도 보존한다.
표본 제거·임계값 완화는 하지 않았다. 평균·p99 비교와 on/off 비용은 이번 실행에서 모두 CV 기준 미수용이다.

증거 `Build/Obj/Phase19T2QueryBench/ManagedProfileCpuAccounting/result.json` 및 원시 Player 로그.
재현: fresh B2 fixture를 package한 뒤 `verify-physics-managed-query-profile.ps1 -Stage <new-stage> -DisableTieredCompilation -RequireCpuAccounting -RunName ManagedProfileCpuAccounting`,
`summarize-physics-managed-query-profile.py ManagedProfileCpuAccounting`,
`analyze-physics-query-cpu.py ManagedProfileCpuAccounting` (Tools/regression).

T2 progress 유지. 다음은 managed/native 경계에서 검증·scratch 할당/변환·SDK query 실행 비용을 분리하여
cycle 변동과 연결하고 원시 tail을 분석하는 작업이다. 기본 제품 정책 및 전체 M3 성능 수용은 변경하지 않는다.

## T2 managed/native bridge 비용 분해 (2026-10-03)

`Physics.ScriptQueryBatch`를 ABI 진입부터 계측하고 직계 자식으로 `ScriptQueryValidate`,
`ScriptQueryPrepare`, 기존 `QueryBatch`, `ScriptQueryTranslate`, `ScriptQueryCommit`을 배치했다.
검증은 phase/포인터/alias/anchor/출력 구간을 포함한다. 준비는 scratch/results/answers 할당,
reserve 및 입력 변환을 포함한다. 변환은 translated 버퍼 할당, body/shape resolve, registry 등록을 포함한다.
출력 반영은 성공한 요청의 hit 복사와 summary 게시다. 기존 batch 오류 시 출력 보존 순서는 유지한다.
SDK 구간은 query 구조 갱신·ray/overlap 실행과 그 profiler 비용을 포함하는 inclusive `QueryBatch`다.
부모에서 직계 자식 합을 뺀 잔여에는 해제·제어 흐름·scope 비용 등이 포함되며 순수 allocator 비용으로 해석하지 않는다.

Release/Shipping 제품 빌드, Shipping 소켓·서비스 격리 통과. 새 dagger convex 패키지에서
JIT off·기본 CPU 배치·off/on/on/off 4프로세스·64블록을 실행했다. 처음 빌드와 겹친 실행은 별도 보존하고
성능 분석에서 제외했다. 최종 clean 실행은 빌드·Shipping 격리 종료 후 시작했다.
각 Player body27 assertions, batch3역할 각16/0 로그, moving convex parity/운동, 완료 display,
2000 이상 smoke frames, exit0·패키지 불변 통과. 원시 query-loop 38,400개를 보존했다.
두 on capture 각600프레임, complete/unacked0/event-counter drop0/Scene·tick·계층 오류0 통과.
기본128MiB 예산을 유지하며 각 capture 메모리는 약72.6MB였다.

아래는 profiler-on 두 capture의 완전한 batch 호출을 pooled 집계한 평균이다.
16/64개 실제 실행 요청을 기준으로 분류하며 warmup·timed·parity 호출을 포함한다.
따라서 timed-only 평균, profiler-off SDK 시간, 성능 개선율과 구분한다.

| 구간 | 16요청 평균 μs | 비중 | 64요청 평균 μs | 비중 |
|---|---:|---:|---:|---:|
| 검증 | 0.192 | 1.49% | 2.168 | 4.26% |
| 준비·할당 | 1.393 | 10.80% | 4.465 | 8.77% |
| SDK inclusive | 9.088 | 70.47% | 34.189 | 67.19% |
| 변환·할당 | 1.463 | 11.35% | 7.884 | 15.49% |
| 출력 반영 | 0.125 | 0.97% | 0.482 | 0.95% |
| 잔여 | 0.636 | 4.93% | 1.699 | 3.34% |
| 전체 | 12.896 | 100% | 50.886 | 100% |

표본은16요청5296개,64요청5302개다. 실제 SDK 구간이 가장 큰 비중이지만 구간 비중만으로
CPU-cycle 변동의 원인이나 최적화 효과를 확정하지 않는다. clean 실행의 scalar/batch 평균·p99 비교와
on/off overhead 비교는 모두 CV≤10% 조건 미수용이다. 표본 제외·임계값 완화 없음.

증거 `Build/Obj/Phase19T2QueryBench/ManagedProfileBridgeCostsClean/result.json`.
재현: fresh fixture 뒤 `verify-physics-managed-query-profile.ps1 -Stage <stage> -DisableTieredCompilation
-RequireCpuAccounting -RequireBridgeCosts -RunName ManagedProfileBridgeCostsClean`,
`summarize-physics-managed-query-profile.py`, `summarize-physics-query-bridge-costs.py`,
`analyze-physics-query-cpu.py`에 동일 RunName을 전달한다.
T2 progress 유지. 다음은 기존 capture의 SDK 구조 갱신/개별 query 비용과 tail을 분해하고,
기본 조건의 평균·p99 안정성 및 전체 M3 성능 수용을 마무리하는 작업이다.

## T2 SDK 내부·tail 분해 (2026-10-03)

제품 코드/패키지 재실행 없이 직전 clean Player capture 두 개를 새 분석기로 재검증했다.
원본 ceprof SHA256·분석기 소스/실행파일 SHA256을 별도 receipt에 기록한다.
complete/unacked0/event-counter drop0/Scene·tick·계층0 조건을 다시 확인했다.
호출마다 구조 갱신1회와 ray/overlap 반반 구성, 부모 시간의 자식 합·잔여 분해를 검사한다.
16/64요청 각5296개의 완전한 호출을 CSV로 보존했다. 기존64요청 pooled5302개 중
ray/overlap 반반이 아닌 검사6개는 이번 구성 비교에서 제외했으며 원본 capture는 보존한다.
warmup·timed·parity 구간이 섞인 profiler-on 집계다. managed rawUs와 호출 ID로 연결하지 않았으므로
이번 native tail이 기존 managed rawUs tail과 동일 호출이라는 주장은 하지 않는다.

| 요청 | 전체 평균 μs | 구조 갱신 μs | ray 합 μs | overlap 합 μs | SDK 잔여 μs | median / p99 / max μs |
|---|---:|---:|---:|---:|---:|---:|
| 16 | 12.896 | 0.046 | 3.973 | 3.566 | 1.503 | 10.8 / 31.1 / 244.9 |
| 64 | 50.906 | 0.059 | 15.260 | 13.409 | 5.475 | 43.6 / 103.5 / 226.0 |

SDK 잔여는 dispatch/제어·자식 scope 사이 gap·계측 비용 등을 포함한다. 순수 SDK CPU 실행 시간이나
대기로 단정하지 않는다. 구조 갱신 평균은 전체의0.36%/0.12% 미만이며 이번 fixture에서 지배 비용이 아니다.
ray/overlap scope는 SDK 실행과 scope 내부의 profiler 비용·스케줄링 영향을 포함한다.

nearest-rank p99 이상 호출은 각53개다. 16요청 tail 평균46.902μs에서 SDK31.525,
준비5.543, 변환3.396, 부모 잔여5.847μs였다. 64요청 tail 평균113.409μs에서는 SDK66.640,
준비9.902, 변환26.783, 부모 잔여5.443μs였다. 64요청에서 일반 평균 대비 tail 증가62.503μs 중
SDK 증가32.437μs(약52%), 변환 증가18.891μs(약30%)를 관측했다. 모든 tail의 원인이 동일하다고
단정하지 않으며 allocator/cache/descheduling 원인으로 확정하지 않는다.
median3배 초과는16요청42개,64요청4개이며 제거하지 않았다. 가장 큰10호출의 Scene/tick과
모든 분해 시간을 receipt에 보존했다. 제품 성능 수용은 여전히 false다.

증거 `Build/Obj/Phase19T2QueryBench/ManagedProfileBridgeCostsClean/SdkTail/result.json` 및 capture별 CSV.
재현 `build-physics-managed-query-capture-probe.ps1` 뒤
`analyze-physics-query-sdk-tail.py ManagedProfileBridgeCostsClean` (Tools/regression).
T2 progress 유지. 다음은 결과 변환의 body/shape 조회·registry 등록·버퍼 초기화 비용을 더 분리하고,
계측 off 기본 조건에서 의미 있는 최적화와 평균/p99 안정성을 검증하는 작업이다.

## T2 결과 변환 내부 비용 분해 (2026-10-03)

`ScriptQueryTranslate`의 자식으로 `ScriptHitInitialize`(버퍼 allocation/value initialization),
`ScriptHitLookup`(hit별 body/shape 조회·layer 선택), `ScriptHitEncode`(hit별 registry 등록·hit 구성)를 추가했다.
출력 반영 순서와 batch/요청별 오류 정책을 유지했다. Release/Shipping 제품 빌드와 Shipping 격리 통과.
새 dagger convex 패키지에서 빌드 종료 후 JIT off·기본 CPU 배치·off/on/on/off4프로세스64블록 실행.
각 body27 assertions·batch3역할 각16/0·moving parity·완료 display·2000 이상 frames·exit0·패키지 불변 통과.
두 capture complete/unacked0/event-counter drop0/Scene·tick·계층0, 보존266/358프레임 및99.12/99.31MB.
기본128MiB 예산을 유지했다. retention에 따라 프레임 수는 이전600과 다르며 완전한 benchmark 호출 수를 별도 검사했다.
16/64요청 각5296개, 호출당 초기화1회·lookup/encode 각16/64회·부모 시간 분해 합을 검증하고 raw CSV를 보존했다.

첫 capture의 빈 출력 버퍼 초기화 마커는0길이였고 다음 형제 commit과 같은 타임스탬프 경계에 있었다.
기존 timestamp/depth 정렬이 다음 sibling을 부모로 골라 계층 오류1을 냈다. 분석기는0길이 ScriptHit 마커에
대해 같은 부모 depth에서 해당 시간을 포함하는 유일한 Translate 구간을 확인한다. 이벤트 제외나 오류 임계값
완화가 아니다. 기존 capture 재검증0 및 수정 분석기로 새4프로세스 실행 통과. 초기 중단 실행은 최종 성능 비교에서 제외했다.

| 요청 | 변환 평균 μs | 초기화 μs | body/shape lookup 합 μs | registry/hit encode 합 μs | 변환 잔여 μs |
|---|---:|---:|---:|---:|---:|
| 16 | 4.253 | 0.304 | 1.263 | 0.838 | 1.849 |
| 64 | 23.289 | 8.656 | 4.665 | 2.888 | 7.081 |

64요청 초기화는 변환 평균의37.17%,lookup20.03%,encode12.40%였다.
전체 호출 p99 이상 tail53개의 변환 평균52.343μs에서 초기화22.119μs(42.26%),
lookup12.760μs,encode5.794μs,변환 잔여11.670μs를 관측했다. 16요청은 p99 경계 동률을 포함해54개였다.
Profiler-on warmup/timed/parity pooled 값이며 per-hit marker가 계측 비용을 추가한다.
이전 capture와 직접 성능 회귀/개선 비교를 하지 않는다. 변환 잔여에는 반복·summary·scope 사이 gap 등이
포함된다. CPU/cache/allocator/descheduling 원인으로 확정하지 않는다. 평균/p99·on/off 비교는 모두CV 기준 미수용.

현재 변환 버퍼는 실제 hit 합이 아니라 공유 output capacity 전체를 resize한다.
이번 fixture에서64요청 capacity512에 실제 hit64였으므로 사용하지 않는 슬롯까지 초기화한다.
다음 최적화는 실제 written 합 크기의 조밀한 변환 버퍼와 요청별 시작 offset을 사용하여
공용 output offset으로 마지막에 복사하는 방식이다. batch 실패/요청 오류의 출력 보존·truncation·alias·
zero-capacity·원래 요청 순서를 유지하고 profiler-off fresh Player 평균/p99 및 memory를 검증한다.
장기 registry cache나 전역 ownership 변경은 이번 측정으로 정당화하지 않는다. 아직 최적화 구현/효과 수용은 아니다.

증거 `Build/Obj/Phase19T2QueryBench/ManagedProfileHitCostsClean/result.json` 및
`SdkTail/result.json`, `SdkTail/hit-result.json`, capture별 CSV.
재현 fresh fixture 뒤 `verify-physics-managed-query-profile.ps1 -Stage <stage> -DisableTieredCompilation
-RequireCpuAccounting -RequireBridgeCosts -RunName ManagedProfileHitCostsClean`,
`summarize-physics-managed-query-profile.py`, `analyze-physics-query-sdk-tail.py`,
`analyze-physics-query-hit-costs.py`에 동일 RunName을 전달한다. T2 progress 유지.

## T2 actual-written 변환 버퍼 최적화 (2026-10-03)

ABI batch 결과에서 성공 요청의 written 합을 계산하고 그 수만큼만 translated vector를 resize한다.
64개 고정 size_t 배열에 원래 요청 인덱스별 시작 위치를 기록한다. hit 입력은 기존 공유 scratch offset에서
읽고, 변환 결과는 조밀한 prefix에 기록하며, 마지막 commit에서 원래 output offset으로 복사한다.
오류 요청·빈 결과·zero-capacity는 output hit을 건드리지 않는다. 개별 변환 오류가 나면 해당 요청은 게시하지 않는다.
전체 batch 실패/예외는 commit 이전에 반환한다. 요청 순서·required/truncated 및 alias 계약을 유지한다.
전역 cache나 Scene/Editor 생명주기·소유권 변경은 없다. 기존 C++23 views 경로를 사용한다.

직전 계측 분해에서 layer ID를 담던 임시 변수가uint32_t로 좁아진 문제도 발견해uint64_t로 복구했다.
shape override, Entity layer ID, physics_hit ABI가 모두64비트다. 이 비교 fixture는 default layer ID 범위이므로
큰 stable ID의 제품 런타임 동작을 이번 성능 측정으로 확인했다고 주장하지 않는다.

새 Player probe에 별도 dense-batch6 checks/역할을 추가했다. 공용4096-hit 버퍼를0x5a로 채우고
output offset4000→0 역순·큰 gap, 중간 invalid request, zero-capacity discovery, no-hit를 섞는다.
성공 hit identity·원래 요청별 summary, 모든 gap/실패 요청/unwritten tail 바이트 보존을 검사한다.
Release/Shipping 빌드 및 Shipping 소켓·서비스 격리 통과. 새 패키지의 초기93 checks 통과.
최종 변경 패키지4실행 각 기존body27+batch48+dense18=93/0, moving convex parity·완료 display·
2000 이상 smoke frames·exit0·패키지 불변 통과. 이전 패키지2실행도 기존75/0 통과.

빌드 종료 후 fresh before/after/after/before profiler-off4프로세스, 이어after on/on2프로세스다.
JIT off·기본 CPU 배치·각 프로세스 inner scalar/batch ABBA×2. 총96블록57,600 raw loop samples.
이전 native binary를 포함한 패키지와 새 패키지를 모두 새 프로세스로 실행했다. binary SHA256 및 stage 불변을 기록한다.
새 probe의 추가 저작/회귀 검사는 timed loop 밖이지만 heap/cache 상태에 영향을 줄 수 있으므로
이 비교를 코드 변경만의 인과 효과로 단정하지 않는다. 이전 패키지는 이전64→32 layer 임시값도 포함하지만 default ID fixture다.

64요청 batch block 평균 median은47.892→44.848μs(6.36% 단축 관측), CV6.40%/9.00%로 해당 평균 비교만 통과.
scalar 평균 median87.935→90.153μs, CV3.55%/10.09%로 비교군 안정성 미수용이다.
64요청p99와16요청평균/p99 비교는 CV기준 미수용. 임계값 완화·표본 제거 없이 모든 값을 보존했다.
전체 성능 수용은false이며6.36%를 보편적/인과적 speedup으로 발표하지 않는다.

두 변경 capture complete/unacked0/event-counter drop0/Scene·tick·계층0, 보존353/327프레임,
99.30/99.22MB. 기본128MiB 예산 유지. 각16/64요청5296개의 완전 호출을 분해했다.
64요청 profiler-on 초기화 평균0.285μs, tail0.585μs, 변환 평균14.270μs를 관측했다.
과거 다른 capture의8.656μs와 직접 회귀/개선율을 산출하지 않는다.
ABI physics_hit64B와 실제 hit수16/64를 확인했다. fixture의 요청 capacity는8배이므로 변환 hit 저장량은
16요청8KiB→1KiB,64요청32KiB→4KiB(87.5% 감소)다. 새 offset 배열512B는 별도 stack 저장량이다.
allocator metadata/capacity 또는 전체 query scratch·managed hit 버퍼의 감소를 뜻하지 않는다.

증거 `Build/Obj/Phase19T2QueryBench/ManagedDenseComparison/result.json`,
`After/result.json`, `After/SdkTail/result.json`, `After/SdkTail/hit-result.json` 및 원시 Player/CSV.
재현 `verify-physics-query-dense-comparison.ps1 -BeforeStage <previous-stage> -AfterStage <new-stage>`,
`analyze-physics-query-dense-comparison.py ManagedDenseComparison`, 이후 기존 profile/SDK/hit 분석기에
`ManagedDenseComparison/After`를 전달한다. actual-written 최적화 구현/기능 검증 완료, T2 progress 유지.
다음은 같은 managed probe를 사용한 before/after 패키지와 기본 tiered JIT 조건에서 대표 부하·p99 안정성을
검증하고 전체 M3 성능/메모리 수용을 연결하는 작업이다.

## T2 동일 probe·기본 JIT 재검증 (2026-10-03)

이전 native distribution을 현재 PhysicsB2PlayerProbe로 새로 package하여 추가 dense 회귀 검사를 양쪽에 맞췄다.
Before/After probe 소스 SHA256 동일, ScriptCore.dll SHA256 동일을 검사하고 receipt에 남겼다.
DOTNET_TieredCompilation/COMPlus_TieredCompilation 환경 override는없고 ScriptCore runtimeconfig에
System.Runtime.TieredCompilation 강제 설정이 없다. 제품 기본 JIT 정책을 사용하며 실제 각 메서드의 tier 승격
시점까지 확인한 것은 아니다. 이전 JIT-off 비교는 별도 진단으로 보존한다.

Fresh before/after/after/before profiler-off4프로세스, 기본 CPU 배치, inner scalar/batch ABBA×2.
각 기존body27+batch48+dense18=93/0, 이동·충돌 parity·완료 display·2000 이상 frames·exit0·패키지 불변 통과.
64블록38,400 raw query-loop samples와 CPU accounting을 보존했다. CPU pinning·JIT off·표본 제외 없음.
이번 실행은 off-only이므로 새 on capture/overhead 검증을 주장하지 않는다. 기존 capture 증거와 구분한다.

| 요청 | batch 평균 median before/after μs | 평균 CV before/after | p99 median before/after μs | p99 CV before/after |
|---|---:|---:|---:|---:|
| 16 | 14.378 / 14.668 | 25.53% / 24.47% | 27.4 / 27.6 | 56.35% / 15.52% |
| 64 | 52.607 / 50.846 | 33.92% / 8.73% | 110.7 / 90.65 | 43.50% / 11.06% |

16/64 batch 평균·p99 before/after 비교와 scalar control 비교는 모두CV≤10% 수용 조건 미달이다.
64 평균 -3.35%,p99 -18.11%의 관측값은 speedup으로 수용하지 않는다. 이전JIT-off에서 관측한
6.36%도 기본조건 개선율로 확정할 수 없다. 기본JIT가 모든 변동의 원인이라는 추론도 하지 않는다.
actual-written 변환 버퍼의 구현/기능 검증과 요청 hit payload 감소는 유지하며 제품 성능 수용은false다.

부하 범위는 실제 dagger convex를 포함한3개 body 역할, 이동하는 Scene의16/64 ray/overlap 혼합이다.
주로 요청당1hit이며 다수 body·다중 hit/overflow가 많은 Scene까지 대표한다고 보지 않는다.
다음은 HTTP CLI로 저작한 높은 body 밀도·다중 hit 부하에서 correctness/overflow/메모리와
기본JIT 평균·p99를 검증하는 작업이다. 동일 소규모 fixture 반복만으로 T2/M3 완료 처리하지 않는다.

증거 `Build/Obj/Phase19T2QueryBench/MatchedDefaultJit/result.json` 및 원시 Player 로그.
Before fixture `Build/Obj/P19B2/MatchedDefaultBefore/fixture.json`, After는기존RT2Dense fixture다.
재현 `verify-physics-query-dense-comparison.ps1 -BeforeStage <matched-before> -AfterStage <after>
-DefaultJit -OffOnly -BeforeProbeSource <before-source> -AfterProbeSource <after-source> -RunName MatchedDefaultJit`,
`analyze-physics-query-dense-comparison.py MatchedDefaultJit`. T2 progress 유지.

## T2 HTTP 저작 고밀도·다중 hit Player 검증 (2026-10-03)

기존 B2 씬을 HTTP CLI로 열고 remote cluster에 static sphere body128개를 추가했다.
object.create/component.add/object.property/physics.shapes/object.transform 및 scene.save의
저작·검증643명령(quit 별도)을 journal로 보존했다. 씬 문서를 손으로 편집하지 않았다.
저장된 PhysicsQueryStress.creator 및 meta를 양쪽 Project에 그대로 복사하여 source SHA256 동일을 확인했다.
같은 managed probe source, ScriptCore.dll, 기본 JIT(강제 override 없음), 기본 CPU 배치다.
기존 공용 layer catalog를 그대로 사용하며 새 물리 layer 체계나 registry cache를 만들지 않는다.

실제 Player에서 remote sphere overlap이 서로 다른128 body identity를 반환함을 검사했다.
full128 hits,8-hit overflow(required128/truncated),zero-capacity(required128),중간 invalid request,
그 뒤 no-hit 요청,역순/희소 output 위치 및 모든 미사용 바이트0x5a 보존을7 checks로 검증했다.
양쪽 패키지의 초기93 checks 및 최종4실행 각기존93+stress7=100/0 통과.
기존 dagger 이동·충돌과16/64 ray/overlap scalar/batch parity도 매 블록 검증했다.
모든 overlap benchmark 요청은 written8/required128/truncated를 확인한다.
cooked Scene GUID ebe286e4-199c-4405-967b-151ee5158a71,CEPG1/source geometry0,완료 display,
2000 이상 frames·exit0·패키지 불변 통과. 새 Player source 코드 재링크는 없고 managed fixture를 새로 컴파일했다.

Fresh before/after/after/before off4프로세스·64블록·38,400 raw loop samples다.

| 요청 | batch 평균 median before/after μs | 평균 CV before/after | p99 median before/after μs | p99 CV before/after |
|---|---:|---:|---:|---:|
| 16 | 71.058 / 65.907 | 12.65% / 9.52% | 139.8 / 135.45 | 25.51% / 23.14% |
| 64 | 229.590 / 225.955 | 3.65% / 4.64% | 371.15 / 370.8 | 12.57% / 16.49% |

64 batch 평균 비교만CV≤10% 통과(-1.58% 관측),scalar 평균 비교도 안정적이나 -9.18%로 더 크게 변했다.
따라서batch 차이를 최적화의 인과 효과로 확정하지 않는다. p99와16요청 비교는 미수용이며 표본 제외 없음.
제품 전체 성능 수용은false다. 이번은off-only이며 높은 hit 밀도에서 새 on capture의 drop/계층 검증을
수행했다고 주장하지 않는다. 기존 낮은 hit 밀도 capture 증거는 별도로 유지한다.

100ms 단위 owned-process memory sampling을 추가해 working set lifetime peak 관측값과 sampled private
최댓값을 남겼다. before peak WS624.78/632.81MiB,after625.66/623.29MiB였다.
private sampled max는before637.33/671.05MiB,after645.93/649.43MiB다.
렌더러·CLR·물리가 포함된 전체 Player 수치이며 물리 전용 또는 private lifetime peak/누수 검증이 아니다.
희소5요청 correctness case의 실제 written합136을 확인했다. 변환 hit 요청 저장량은
4096×64B=256KiB에서136×64B=8.5KiB로 줄며 offset array0.5KiB는 별도다.
전체 query scratch·managed 버퍼·allocator capacity/metadata 감소로 해석하지 않는다.

증거 `Build/Obj/Phase19T2QueryBench/Dense128DefaultJit/result.json`,HTTP author journal 및 Player memory-samples.json.
저작 씬 `Dynamic_CPP/Assets/Scenes/PhysicsQueryStress.creator`;fixture receipts `Build/Obj/P19B2/StressBefore`
및 `StressAfter/fixture.json`. 재현 fixture builder의 -SceneSource에 HTTP 저장 씬을 전달하고,
`verify-physics-query-dense-comparison.ps1`에 -DefaultJit -OffOnly -QueryStress -ExpectedSceneGuid 및
matched Before/AfterProbeSource를 지정한 뒤 `analyze-physics-query-dense-comparison.py Dense128DefaultJit` 실행.
T2 progress 유지. 다음은 공유4096-hit 출력을 실제로 채우는 최대 경계, 더 높은 hit 수/다양한 동적 부하,
범위를 제한한 고밀도 profiler capture 및 p99/M3 수용이다.

### T2 공유 출력 4096-hit 최대 경계 검증 (2026-10-03)

HTTP 저작 static sphere128 씬에서 실제 Release Player의 64개 overlap 요청에 각각64-hit 출력을
할당해 공유4096-hit 버퍼를 모두 채웠다. 출력 offset은 요청 순서와 역순이다.
모든 요청 written64/required128/truncated=true, 모든4096 hit의 소유권과 요청별64개 identity
중복 없음, 출력 앞뒤 각각64바이트 보호값0x5a 보존을4 checks로 검증했다.
기존93+stress7+최대경계4=104/0 통과. 완료 display8,frames90806,exit0,텍스트 parser0,
CEPG1/source geometry0 및 패키지 불변 통과. 새 managed probe 컴파일·package smoke도 통과했다.
기본 JIT·기본 CPU 배치이며 native 재링크 없이 기존 dense 배포판을 사용했다.
이 검증은 Release 기능 경계이며 Shipping 실행·ASan·고밀도 profiler capture·성능 수용 증거가 아니다.

재현: 새 stress fixture에 verify-physics-b2-player.ps1 -QueryStress -RequireMaximumBatch
-ExpectedSceneGuid ebe286e4-199c-4405-967b-151ee5158a71 -SmokeFrames 2000.
이전 stress 패키지 비교에는 RequireMaximumBatch를 지정하지 않아 기존7-check 재현을 유지한다.
증거 Build/Obj/Phase19T2QueryBench/Max4096/result.json 및 연결된 Player query-stress.json.
T2 progress 유지. 다음은 더 높은 hit 수/다양한 동적 부하, 범위를 제한한 고밀도 profiler capture,
p99 및 M3 제품 수용이다.

### T2 고밀도 제한 구간 profiler 캡처 (2026-10-03)

고밀도128-body 씬의 실제 Release Player에서 독립 on/on 두 프로세스를 실행했다.
기본 JIT·기본 CPU 배치이며 제품 캡처 예산을 변경하지 않았다. 별도 BoundedDenseCapture 모드로
16블록(16/64 요청,scalar/batch ABBA ABBA)을 유지하고 블록당warmup2·samples20으로 제한한다.
일반 성능 모드는 기존warmup60·samples600을 유지한다. 이번320 samples/프로세스는 성능/p99 수용용이 아니다.

각 실행 기존104/0·scalar/batch identity/count/truncation parity·moving dagger 충돌,
owner CPU/raw identity·cooked Scene·CEPG1·완료display8·exit0·패키지 불변 통과.
캡처563/600frames,10,933,824/10,927,752bytes,complete=true/unacked0,
event drop0/counter drop0/계층 위반0. 각캡처QueryBatch7903 및구조갱신7903,쿼리15661,Scene1.
검증/준비/SDK/변환/commit 5단계와Scene/tick/task 소유권 계층 검증을 유지했다.
16/64 요청의 혼합 배치 각각96회가 전부 보존됐으며,각batch당lookup/encode96/384개와
initialize1회도 일치했다. 이 씬의 ray는수직4개 구를 반환하고 overlap은8개를기록(required128/truncated)한다.

최초 verifier는ray1-hit 및시작전기능검사미포함으로 가정해coverage를거부했다.
실제 씬의ray4-hit에맞추고 혼합half-ray/half-overlap요청을명시적으로식별하도록수정했다.
원캡처 재검증 통과 후 두fresh 실행을완료했다. 이벤트제외나drop/계층기준완화는없다.
기존600-loop 저밀도캡처도기본모드재검증통과,고밀도모드에서는exit6으로거부하는control을확인했다.

재현: build-physics-managed-query-capture-probe.ps1 후 HTTP stress 씬으로새fixture를만들고
verify-physics-dense-bounded-capture.ps1 -Stage <새stage> 실행.
증거 Build/Obj/Phase19T2QueryBench/DenseBounded-016a6d228abf4a00b9c88a7e28078a6c/result.json,
연결된query.ceprof/capture-result.json/bridge-costs.csv 및 DenseCaptureVerifierControl.
T2 progress 및performanceAccepted=false 유지. 남은작업은더높은hit/다양한동적부하와p99/M3 제품수용이다.

### T2 동적128-body 고밀도 쿼리 검증 (2026-10-03)

HTTP CLI로 기존128개 DenseQuery 구를dynamic·중력 비활성으로 변경한 별도 씬을저작했다.
258개저작/검증명령과quit를포함한259 journal entries,Scene GUID
16037efe-0cd6-4802-a24a-aafd56644346이다. 원래stress 씬을수정저장하지않고새경로로저장했다.
재현용동일바이트fixture는 Tools/regression/fixtures/PhysicsQueryDynamic.creator에보존한다.

직전원격통합이포함된Release Editor/Player와새로빌드한AssetCooker로새배포본을검증·생성하고,
관리probe를컴파일한새패키지로실제Player 두독립프로세스를실행했다. 기본JIT·기본CPU배치다.
128개바디에+20m/s를적용해12m이상이동시킨후-20m/s로복귀하고0속도로정지한다.
모든owner읽기구간에서128개바디의종류·상대병진·Y/Z보존과128-hit scalar 신원을검사했다.
원래쿼리영역의hit128→0→128 및이동목적지영역복귀후0을검사해query구조갱신을확인했다.

출발/이동/복귀각지점에서32개overlap×128hit=4096전체출력을역순offset으로기록하고,
각요청written128/required128/truncated=false·128개고유body identity와앞뒤128B보호값을검증했다.
동일각지점의16개혼합ray/overlap도scalar/batch 수·신원·Entity/shape/layer/거리·location 및중복없음을검사했다.
ray4-hit/overlap128-hit이며중간physics step 없이동일owner읽기구간에서비교한다.
기존stress/최대64-request/4096-hit truncation 및B2각역할의104개검사도각실행통과했다.

| 최종 실행 | owner read windows | 관측된 pose 변경 | 동적 누적 assertions | 완료 GT frames | display promotions |
|---|---:|---:|---:|---:|---:|
| 1 | 1498 | 65 | 3060/0 | 169256 | 8 |
| 2 | 1605 | 68 | 3274/0 | 160246 | 8 |

PostPhysics호출과실제fixed tick은다르므로readWindows 및observedPoseChanges로구분한다.
pose변경관측횟수도실제tick수나모든중간tick검사증거가아니다. 누적assertion수는읽기횟수에영향을받는다.
초기실행의ticks명칭은정정했고해당실행을최종증거에서제외한후새패키지두번으로재검증했다.
모두exit0/parser0/cookedScene·CEPG1/source geometry0/패키지불변통과했다.

증거 Build/Obj/Phase19T2QueryBench/DynamicQueries-0c1f6c496f124489b4d44ab9340620cc/result.json,
연결된query-dynamic.json/query-stress.json/Player result와HTTP author journal.
재현:새배포본에build-physics-b2-player-fixture.ps1 -SceneSource <동적fixture>로패키징후
verify-physics-query-dynamic-player.ps1 -Stage <새stage> 실행.
중력비활성집단병진운동범위이며충돌적층·회전/kinematic혼합·GPU solver·Shipping 실행이나
동적profiler capture·성능/p99 수용을주장하지않는다. T2 progress 및performanceAccepted=false 유지.
대표부하확장과p99/M3 수용은남는다. 다음우선작업은M2 DDOL 동반전환실패와Editor 복원게이트다.

### 2026-10-03 M2: DDOL 동반 Editor 활성화 실패와 반복 복원

실제 Release Editor의 HTTP CLI로 정상 DDOL 전환/Stop/Replay에 이어,
비균일 부모 스케일과 자식 회전으로 unsupported shear를 가진 목적지를 저작했다.
실행 중인 Character와 부모·분기·비활성 자식까지 persistent 5개가 목적지로
이송된 시점에서 물리 시작이 거부되는 것을 확인했다. 실패 자동 정지 후 원래
6개 객체의 Transform·활성·레이어·컴포넌트 ID·부모 관계가 복원되며,
목적지 객체와 기존 CLI/C# 핸들은 접근이 거부된다. runtime tick/force/velocity는
초기화되고 다시 Play한 뒤 두 번째 Stop도 저작 Transform으로 복귀한다.
출발·목적지 저장 파일 SHA256 불변, hierarchycheck 불일치/고아/도달 실패 0.

재실행 후 Stop에서 발견한 실제 회귀를 수정했다. 슬롯 기반 엔티티 저장소의
빈 슬롯이 null로 직렬화되는데 엄격한 복원 루프가 이를 알 수 없는 타입으로
취급했다. null 슬롯만 건너뛰며 실제 알 수 없는 타입에 대한 거부는 유지한다.
목적지 실패는 지연된 OnAddedToScene 이전이므로 콜백은 added +1 / removed +2;
정상 전환과 같은 added +2를 요구하지 않는다. 소유권은 실패 시 native 진단으로
별도 확인한다. Stop 요청 반환 이후 복원 완료 상태까지 기다려 검증한다.

Release Editor 빌드 통과. 최종 PHYSICS_CHARACTER_HTTP_OK: HTTP 163명령,
C# 18/0. evidence Build/Obj/Phase19C0/http-c7001c82e646406cbc5d52e856c15fec/result.json
및 results.jsonl/editor.out. 이전 실패 실행은 최종 근거에서 제외한다.
재현: verify-physics-character-http.ps1 -EditorExe <Release Editor> -TransitionFailureProbe.
M2는 progress 유지. Player DDOL 동반 missing/corrupt/revision 전환 실패,
전체 SDK include 경계 감사와 제품 전체 회귀는 남는다. 이번 근거는 Editor
unsupported-shear 목적지 활성화 실패이며 Player 오류 종류 전체를 대체하지 않는다.

### 2026-10-03 M2: Release Player DDOL 동반 목적지 geometry 실패

최신 Release Player와 새 배포본/패키지에서 verify-physics-geometry-failure-player.ps1
-Ddol을 실행했다. 정상 primitive 출발 씬의 이동·접지 12/0 이후 CharacterGateActor를
DDOL로 지정하여 cooked geometry 목적지로 이송한다. 정상 대조군은 관리 생명주기·운동
8/0과 네이티브 핸들 이송 8/0, 목적지 completed display 및 종료 0을 확인했다.
missing/corrupt/revision 세 사본은 모두 출발 운동 12/0 이후 목적지의 살아 있는
DDOL 소유 객체 1개를 확인한 시점에서 물리 시작이 거부되고 제품 fatal 정책으로
종료 3이 됐다. missing/corrupt는 Player cooked geometry unavailable,
revision은 Invalid or incompatible cooked geometry artifact이며 성공 DDOL simulation
및 목적지 성공 display가 없다. 전 사례 runtime text parser 0, 원본·사본 SHA 불변.

실제 표시 창 960×540, smoke 2000/minimum promotions 8 및 slot rotation 조건 유지.
새 Release Player 빌드·배포 검증·패키징 통과. 이전 stale 환경맵 cook으로 인한
패키징 실패와 구 CEMF 변이 도구 실행은 제외하고 현재 소스로 변이 도구를 재빌드했다.
최종 evidence Build/Obj/P19DdolFailure/result.json 및 연결된
PhysicsGeometryFailure/run-9f5cca199110439a8798c40a8482ac24/result.json.
게이트는 정상 핸들/운동, 실패 시점 소유권·정확한 오류·종료, 파일 불변을 검사한다.
M2 progress 유지. Debug/Shipping의 같은 DDOL 오류 조합, 복합 DDOL 계층 오류 조합,
전체 SDK include 경계 감사는 남는다. 이번 실행은 Release 단일 Character DDOL 범위다.

### 2026-10-03 M2: Debug/Shipping DDOL 목적지 오류 검증 확장

Release와 같은 단일 Character DDOL 정상/missing/corrupt/revision 조합을 최신
Debug와 Release-Shipping Player에서 각각 네 번 실행해 전부 통과했다. 정상은
출발 운동 12/0, 관리 이송 생명주기·운동 8/0, 네이티브 핸들 이송 8/0,
목적지 completed display와 종료 0이다. 각 오류는 출발 운동 12/0 이후 목적지
persistent 1개 소유권을 확인한 시점에서 시작을 거부하고 제품 fatal 종료 3으로
끝난다. missing/corrupt는 cooked geometry unavailable, revision은 incompatible
artifact이며 성공 DDOL simulation/목적지 성공 display가 없다.
전 사례 parser 0, 원본·사본 패키지 SHA 불변. 960×540 실제 표시와
2000/minimum promotions 8·slot rotation 조건을 유지했다.

Debug/Shipping Player 빌드, 새 배포본 검증·패키징 통과. Debug 최초 패키징의
구 AssetCooker artifact 버전 불일치 실행은 제외하고 AssetCooker도 다시 빌드한
새 배포본으로 재검증했다. 단계별 staged runtime SHA는 현재 빌드와 동일하다.
Shipping 서비스 격리: WS2_32 import 없음, WSAStartup/endpoint.json/CommandService
표식 없음; Development 대조군은 존재한다. EngineShipping=true의 별도 출력 사용.

최종 evidence Build/Obj/P19DdolFailureDS/result.json.
Debug: PhysicsGeometryFailure/run-822da353914c45fdb17057c3576bd0bb/result.json.
Shipping: PhysicsGeometryFailure/run-c7bd4b5ade23453ebf7cd60647672301/result.json.
기존 Release 근거와 합쳐 총 12사례. 단일 Character DDOL 오류 조합 D/R/Shipping은
확인됐으며 M2 progress 유지. 복합 DDOL 계층 오류 조합과 전체 SDK include 경계
감사는 남는다. 이번 작업은 실행 검증이며 추가 물리 런타임 정책 변경은 없다.

### 2026-10-03 M2: 복합 DDOL 계층 목적지 geometry 실패 D/R/Shipping

부모·분기·활성 Character·비활성 Character/Body 5개를 가진 기존 저작 fixture를
정상 primitive 출발 씬으로 사용하고, geometry 목적지 정상/missing/corrupt/revision을
Debug/Release/Shipping에서 각각 네 번 실행했다. 총 12사례 통과.
정상은 출발 운동 12/0, 관리 이송 생명주기·운동 8/0, native 계층 56/0과
목적지 completed display·종료 0을 확인한다. 계층 검사는 부모/자식 remap,
원래 Scene 핸들 무효화·목적지 핸들 연결·레이어/활성/포즈 보존,
비활성 자식 SDK 핸들 부재·재활성화 운동/force·재비활성화 정지를 포함한다.

오류 세 종류는 출발 운동 12/0 이후 목적지 소유의 살아 있는 DDOL 객체 5개를
실패 진단으로 확인한다. 물리 시작 거부 후 제품 fatal 종료 3,
missing/corrupt는 Player cooked geometry unavailable, revision은 incompatible
artifact이며 성공 DDOL simulation/목적지 성공 display가 없다.
전 사례 parser 0·원본/사본 SHA 불변, 960×540·smoke2000/minimum promotions8·slot
rotation 조건 유지. staged runtime SHA는 각 구성의 현재 빌드와 같다.
기존 최신 배포본에서 새 managed script·계층 시작 씬으로 재패키징했다.

초기 정상 계층 검증은 기존 단독 캐릭터의 고정 Y 범위 때문에 실패했다.
진단 상태는 tick30에서 Y4.905/footY2.805/fall -1.471로 실제 하강 중이었다.
스케일을 가진 계층과 다른 캐릭터가 있는 목적지에는 단독 궤적의 고정 높이
기대값을 적용하지 않는다. 명시적인 계층 진단 환경에서 이송 높이5에서 하강,
발의 바닥 위 위치 및 음의 수직 속도를 요구하며 기존 단독 경로의 조건은 유지한다.
진단은 한 번 상태를 출력한다. 초기 기대값 실패·진단 실행은 최종 근거에서 제외했다.
이번 변경은 fixture/managed probe/verifier이며 native 물리 정책 변경은 없다.

최종 evidence Build/Obj/P19DdolFailureDS/Hierarchy/result.json.
Debug: PhysicsGeometryFailure/run-371eeb94955142e68b20cd502efbb3b4/result.json.
Release: PhysicsGeometryFailure/run-f39a764ddbd0492c933b4b2e50246447/result.json.
Shipping: PhysicsGeometryFailure/run-acdb830edc484f0db68fe008ac95e509/result.json.
재현: 새 배포본으로 build-physics-character-player-fixture.ps1 -Transition -Hierarchy,
이후 verify-physics-geometry-failure-player.ps1 -Stage <stage> -Mutator <최신 도구> -Hierarchy.
M2 progress 유지. 전체 SDK include 경계 감사와 제품 전체 해제/잔류 카운터 감사는
남는다. 실패 경로의 모든 개별 stale SDK/C# 핸들을 추가 관측한 근거로 확장하지 않는다.

### 2026-10-03 M2: SDK include 경계 전체 감사와 기존 검사 복구

verify-physics-sdk-boundary.ps1로 git source inventory의 native 소스 1,252개,
프로젝트/props/targets 35개를 검사했다. SDK include/타입/namespace 노출과
구 PhysicsMathAdapter/PhysicsSystem/Collider/RigidBody/CharacterController/Ragdoll
헤더 include 및 프로젝트 재편입을 거부한다. 헤더 include·SDK 타입·forward
선언의 negative control 3개와 SDK-free facade 대조군을 통과했다.
허용은 Engine/Physics/PhysicsScene.cpp backend 구현과 역사적 P0 직접 SDK
기준선 Tools/regression/physics_p0_cpu_probe.cpp 두 파일뿐이다. 제품 소비자와
public physics header에서 SDK 노출 0. SDK include directory는 Directory.Build.targets의
Physics 프로젝트 조건 하나, backend project reference는 EngineDiagnostics 하나다.
소스별 SHA256과 예외/위반 목록을 결과에 기록한다. 이 예외는 runtime facade 밖의
제품 SDK 사용을 허용하는 정책이 아니다.

기존 수학 게이트는 삭제된 PhysicsMathAdapter.h와 Px 타입을 요구해 깨져 있었다.
이를 SDK-free PhysicsGeometry.h/pose와 Mathematics의 offset·scale/transform 계약으로
정리했고 PhysX include directory/헤더 탐색 요구를 제거했다. 구 SDK conversion과
backend dirty 비교 자체를 일반 Mathematics 공개 계약으로 계속 요구하지 않는다.
수학 스캐너가 image fixture의 두 PackedVector half 픽셀 변환 함수를 vector math로
오인하던 조건도 해당 파일의 완전 수식된 두 함수만 예외 처리했다. 다른 DirectXMath
타입/함수/include 금지는 유지한다. full Mathematics Debug/Release compile/run 통과.
에디터 카탈로그 회귀의 구 물리 타입 이름도 PhysicsBodyComponent와
CharacterMovementComponent로 바꿨고 분류/검색 검사 29개 통과.

최종 evidence Build/Obj/PhysicsSdkBoundary/Final-20261003/result.json,
Build/Obj/P19DdolFailureDS/math-boundary-final.log 및 catalog-boundary.log.
PowerShell parse·git diff --check 통과. 추가 제품 runtime 변경은 없다.
SDK 경계 전체 감사 항목은 확인 완료. M2 전체 progress는 유지하며 제품 전체
해제·잔류 카운터 감사와 실패 종료에서 모든 개별 SDK/C# 핸들의 추가 관측은 남는다.
최종 빅뱅 제거 감사 M4와 성능/메모리 수용 M3를 완료로 올리지 않는다.

### 2026-10-03 M2: 종료 소유권 ledger

`CE_PHYSICS_RESOURCE_PROBE=1`일 때만 backend 소유권 생성/해제 수를 누적한다.
SDK 외부 RAII owner, scene, body, character, geometry, dispatcher, worker,
SDK task의 8종류를 SDK-free read-only API로 관측한다. 기본 실행에는 전역 atomic
누적을 하지 않는다. 설정은 첫 사용에서 고정하며 실행 중 변경은 지원하지 않는다.
소유자 token은 첫 멤버로 선언해 나머지 멤버 해제 뒤 회수되고, SDK task는
run/release 완료 뒤, worker는 루프/프로파일러 등록 해제 뒤 회수한다.
최종 판정은 생산자 join 뒤에만 가능하다. 동시 실행 중 snapshot은 일관된
전체 시점 snapshot을 보장하지 않으며 balanced를 종료 판정으로 사용하지 않는다.

P2 회귀는 지역 객체 소멸 후 ledger 균형을 검사한다. Debug/Release/ASan은
8종류 전부의 생성 노출을 요구하며 캐릭터 명시 삭제와 씬 종료 자동 해제를
함께 실행한다. Shipping 기존 body/query 전용 probe는 자기 노출 범위의
잔류를 검사한다. 퇴화 convex는 사전 검증 invalid_argument 계약으로 정합성을
복구했고 실패 진단은 호출 위치를 남긴다. D/R/ASan 1068, Shipping 366 검사 및
실제 GPU 경로 통과. 생성/해제 배열과 executable SHA receipt는
Build/Obj/Phase19P2/resources-result.json, 개별 stderr.log에 기록한다.

Player는 계측 활성 또는 smoke 실행에서 enabled 상태를 함께 기록한다.
비활성 smoke의 0 카운터는 수용 근거가 아니다. Player는 서비스 종료, 관리 runtime 종료, presentation/render join, 씬/DDOL
삭제, renderer/표시 host 종료 이후 `[physics.player.resources]` JSON을 출력한다.
로더가 runtime DLL을 유지하므로 출력은 명시적으로 flush한다. geometry 실패
Player verifier의 -ResourceProbe는 정상/실패 exit 판정을 유지하며 별도로
ledger 균형과 실제 자원 노출을 요구한다. 기본 기존 검증에는 강제하지 않는다.

이 ledger는 SDK 외부 소유권/작업 해제 근거이며 SDK 내부 전체 할당, allocator
byte peak, RSS/p99 수용을 대체하지 않는다. M2 progress 유지; Debug/Shipping
제품 종료 ledger 조합, 실패 개별 C# stale wrapper 추가 관측 및 M3 메모리/성능
수용은 별도 잔여다.

Release 실제 복합 DDOL 계층 종료 ledger 4사례 통과:
Build/Obj/PhysicsGeometryFailure/run-631f8db7d2fc425891b687fb1c16cad8/result.json.
정상은 출발12/0, managed DDOL8/0, native hierarchy56/0, 목적지 완료 display,
exit0, 8종류 생성/해제 일치. owner41/scene5/body5/character6/geometry3/
dispatcher5/worker19/task296509 전부 회수. missing/corrupt/revision은
출발12/0, 목적지 persistent5, 정확한 오류 이유와 exit3, 성공 DDOL 없음,
owner9/scene1/body1/character1/geometry0/dispatcher1/worker8 및 제출 작업
전부 회수. geometry0은 host 로드 거부로 SDK 자산 생성 이전에 실패한 결과다.
네 사례 모두 enabled=true, balanced=true, 원본/사본 패키지 SHA 불변,
저작 parser0. 수정된 현재 Release runtime DLL SHA를 포함한 추가 receipt는
Build/Obj/Phase19P2/product-resources-result.json. 이전 marker 누락 시도 및
offscreen 시간 제한 시도는 수용 근거에서 제외한다.

Windows에서는 CRT 환경 사본 대신 프로세스 환경을 직접 읽는다. SDK include
감사 ResourcesFinal-20261003, PowerShell parse, git diff --check 및 대시보드
439항목/전체 parse 검사 통과. SDK 내부 allocator 메모리, 실패 개별 C# wrapper,
제품 Debug/Shipping ledger 추가 검증은 미완료이며 M2 progress 유지.

### 2026-10-03 M2: Debug/Shipping 제품 종료 ledger 수용

현재 소스로 Debug/Shipping Player를 빌드하고 새 배포본/복합 DDOL 계층 패키지를
생성했다. 각 구성 정상/missing/corrupt/revision 4사례, 합계8 통과.
기존 Release4의 runtime DLL SHA도 현재 출력과 다시 대조하여 D/R/Shipping
총12사례를 통합했다. 전체 enabled=true, balanced=true, 생성/해제 배열 일치.
정상은 출발12/0 + managed DDOL8/0 + native hierarchy56/0·5노드,
목적지 completed display 및 exit0. SDK owner41/scene5/body5/character6/
geometry3/dispatcher5/worker19와 제출 작업 전부 회수.
오류는 출발12/0, 목적지 transferredPersistent5, 정확한 missing/corrupt 또는
revision 사유·exit3, 성공 DDOL/display 없음. SDK owner9/scene1/body1/
character1/geometry0/dispatcher1/worker8 및 제출 작업 전부 회수.
각 작업 수는 실행 중 tick/표시 대기 시간이 달라 구성 간 성능 비교 근거로 사용하지 않는다.
parser0, 원본/사본 패키지 SHA 불변, 960×540·2000/min8·slot rotation 유지.
Shipping native WS2_32 import와 서비스/endpoint marker 부재도 재검증 통과.

통합 evidence:
Build/Obj/P19DdolFailureDS/ResourcesDS-212528726db849ab9ad290159007c8d0/all-configurations.json.
Debug: PhysicsGeometryFailure/run-a4d75339063047758180acc498d28071/result.json.
Shipping: PhysicsGeometryFailure/run-370881dd9e29473c9e2e0050974e0568/result.json.
각 DLL SHA/현재 빌드 일치, 개별 JSON ledger, exit와 패키지 불변을 저장했다.

긴 Shipping 배포 경로에서 도구 프로세스 시작 실패가 발생하여 짧은 SD/SF 경로에
새 배포본/패키지를 생성했다. 다음 초기 smoke는 오래된 forest recipe로 exit161.
실제 shader closure16개의 현재 recipe dba493920d5395791e93ae8e5a9df649d88e09e6a85400eeea757c36ded4a917과
일치하는 cooked artifact를 배포 입력에 복구했다. EnginePublisher는 Shipping
Player의 별도 출력과 함께 공통 Resources는 Bin/x64-Release에서 읽으므로,
그 경로를 복구한 후 다시 새 배포본을 생성했다. 기존 배포본을 덮어쓰지 않았다.
첫 두 실패는 제품 수용에서 제외하고 SF-8dbcf236의 실패 로그와
ResourcesDS 결과 폴더 environment-repair.json에 원인을 남겼다.
제품 runtime 코드 변경은 없다; 빌드/검증 및 생성된 리소스 복구 작업이다.

제품 Debug/Shipping 종료 ledger 잔여는 닫았다. M2는 progress 유지:
실패 시 개별 SDK 핸들/C# wrapper의 stale 접근 진단을 추가 관측해야 한다.
외부 SDK 소유권 ledger는 SDK 내부 allocator/peak/RSS 검사나 제품 profiler
전체 계층·평균/p99 수용을 대체하지 않는다. 해당 M3/M4와 M1은 미완료다.

### 2026-10-03 M2: native 개별 핸들 수명 게이트 보강

`physics_handle_lifetime_probe.h`의 공용 검사를 진단/Shipping P2 실행 파일에
연결했다. 캐릭터 삭제 직후 읽기·이동·텔레포트·필터 변경은 stale_handle,
슬롯 재사용 후에는 이전 핸들의 삭제도 stale_handle로 거부한다.
슬롯 재사용 전 중복 삭제는 기존 idempotent 계약대로 성공해야 한다.
이전 핸들의 거부된 명령이 새 컨트롤러의 위치를 바꾸지 않음을 확인한다.
잘못된 radius의 생성 요청은 invalid_argument이고 기존 컨트롤러는 유효하다.
소유 씬을 실제 파괴한 다음 새 씬에서 보관된 캐릭터의 5개 API와 바디의
읽기·삭제를 wrong_scene으로 거부하고, 새 캐릭터 생성/읽기를 확인한다.
파괴된 씬이나 컴포넌트 포인터를 역참조하지 않는다.

이 게이트는 native 값 핸들의 세대/씬 식별 계약이다. 제품 목적지 활성화
실패 후 개별 C# wrapper 접근의 수용 근거로 확대하지 않는다. 삭제된 출발
객체 wrapper는 StaleHandle을 검증해야 한다. DDOL로 생존한 객체 wrapper는
객체 수명이 유지되므로 읽기가 성공할 수 있고, Simulating=false 및 실행
명령 WrongPhase를 검증해야 한다. 이 제품 C# 검사는 후속 잔여이며 M2 progress 유지.

검증: fresh Debug/Release/ASan 각1098, Shipping395 checks, 실제 GPU verified=true.
4구성 모두 종료 resource ledger 생성=해제. D/R/ASan profiler abandoned/retained/foreign0.
근거 Build/Obj/Phase19P2/handle-lifetime-result.json (소스/실행 파일/결과/로그 SHA 포함).
SDK 경계 감사 native1253·위반0: PhysicsSdkBoundary/HandleLifetime-20261003.
### 2026-10-03 M2: Release Player 실패 후 C# wrapper 수명 검증

CharacterPlayerProbe가 출발 씬의 실제 완료 물리 틱에서 캐릭터·바디 wrapper,
Entity 값 핸들과 shape ID를 보관한다. 새 WrapperProbe 옵션은 정상 대조에서는
실패 콜백을 보내지 않고, Player가 목적지 활성화 실패를 기록한 뒤 기존
QueueScriptMessage/FlushScriptMessages 경로로 검사 콜백 하나를 전달한다.
새 고정 틱, 시뮬레이션 재시작, admin InvokeCallable/UserCodeScope 우회는 없다.
일반 제품 실행에서는 환경 flag와 smoke가 없으므로 이 경로를 실행하지 않는다.

DDOL은 이름 조회로 새 목적지 객체를 선택하지 않도록 출발 스크립트 인스턴스
ID를 보관한다. 일반 전환은 출발 스크립트가 삭제되므로 목적지 수신 인스턴스만
EnsureInstance로 만든다. 이는 생명주기/OnBeginSimulation 훅 전달과 분리된 기존
API이며 실패한 물리 시뮬레이션을 실행하지 않는다. 파괴된 native 포인터를
보관하거나 역참조하지 않는다. 일반 전환은 두 정본 healthy source GUID만 허용한다.

fresh Release 빌드·새 배포본/계층 패키지로 DDOL 계층 유지와 전체 출발 객체 삭제
각 정상/missing/corrupt/revision 4사례, 총8 통과. 오류6사례 각16 checks=96:
- 삭제된 출발 바디의 읽기·속도·힘·shape count/read/flags는 StaleHandle.
- 일반 전환의 삭제된 캐릭터는 읽기·속도·점프·강제 이동·취소·텔레포트 StaleHandle.
- DDOL 캐릭터는 Entity 수명과 읽기를 유지하되 Simulating=false, 실행 명령 WrongPhase.
- 실패 출력 기본값과 거부된 캐릭터 명령 후 위치/입력/timer/tick/flags 보존을 확인.

정상2는 completed display·exit0이며 실패 콜백0회. 오류6은 콜백1회·exit3,
정확한 missing/corrupt/revision 사유와 목적지 성공 표시 부재를 확인했다.
DDOL 오류는 transferredPersistent5; 정상 managed8/native hierarchy56도 유지.
8실행 모두 외부 SDK resource ledger 생성=해제, parser0, 원본/사본 패키지 불변.
창960×540, smoke2000/min8 조건을 유지했다. 자원 수/시간을 성능 근거로 해석하지 않는다.

통합 receipt: Build/Obj/P19DdolFailureDS/WR-22b4186a/result.json.
DDOL: PhysicsGeometryFailure/run-ee7e061bfba3420b8c229c0ecc405bb1/result.json.
삭제: PhysicsGeometryFailure/run-8f184621568748718b042a8d4ff2089d/result.json.
현재 Release runtime DLL SHA BDE482721C02EE379B5655BB58B73B9B8676B69F33A78CE4EA6C95DF7A466E69와
패키지 DLL 일치, 패키징된 managed 소스/현재 소스 일치 및 개별 로그 SHA를 기록했다.
SDK 경계 감사1253·위반0: PhysicsSdkBoundary/ManagedFailure-20261003.

첫 nullable Entity 사용의 managed compile 실패, 새 목적지 인스턴스 미생성/동명
조회로 callback 수신이 실패한 run-3efd37ff..., PowerShell 주석 오류로 검증이
중단된 run-3d1e8c3e...는 제외했다. 원본 로그/실패 후보는 보존했다.
.NET Console과 native CRT stdout 버퍼의 출력 순서에 의존하지 않는다.
실패 분기에서의 전달과 source Entity 수명/Simulating/명령 거부 상태로 검증한다.

M2 progress 유지. 새 wrapper 검사의 Debug/Shipping 실제 제품 실행은 잔여다.
기존 D/R/Shipping 종료 ledger12와 native 핸들4구성 수용은 유지한다.
이 검사로 SDK 내부 allocator, managed heap/peak/RSS 또는 M3/M4 성능 수용을 주장하지 않는다.
### 2026-10-03 M2 완료: Debug/Shipping wrapper 검증과 수용 근거 대조

현재 소스로 Debug와 Shipping Player/AssetCooker를 빌드하고, 새 배포본과
계층 씬 패키지를 생성했다. 각 구성 DDOL 유지/전체 출발 객체 삭제 각각
정상/missing/corrupt/revision 4사례, 총16 통과. 기존 Release8의 현재 DLL,
managed 소스와 로그 SHA도 다시 대조하여 D/R/Shipping 총24사례를 통합했다.
오류18사례 각16 checks, 합계288. 삭제된 바디·캐릭터 wrapper는 StaleHandle,
살아 있는 DDOL 캐릭터 wrapper는 읽기를 유지하고 Simulating=false 및 실행
명령 WrongPhase를 확인했다. 거부된 명령 이후 상태 보존과 실패 출력 기본값도 확인.

정상6은 completed display·exit0·실패 콜백0회. 오류18은 정확한 사유·exit3·
실패 콜백1회·목적지 성공 표시 부재를 확인했다. DDOL 오류의 persistent5,
정상 managed DDOL8/native hierarchy56도 유지한다. 24실행 전부 외부 SDK
소유권 ledger 생성=해제, parser0, 원본/사본 패키지 불변. 960×540,
smoke2000/min8 조건 유지. Shipping WS2_32 import와 WSAStartup/endpoint.json/
CommandService marker 부재도 통과. 시간·작업 수 차이를 성능 근거로 쓰지 않는다.

통합 evidence: Build/Obj/P19DdolFailureDS/WD-46a3b33f/result.json.
Debug DDOL: PhysicsGeometryFailure/run-928726fe0faf4816a0f38015158ff4f6/result.json.
Debug 삭제: PhysicsGeometryFailure/run-4aad3eb1b79f42b6a38ccf3905e9e746/result.json.
Shipping DDOL: PhysicsGeometryFailure/run-086fdac65eeb45f98d2555189ad12344/result.json.
Shipping 삭제: PhysicsGeometryFailure/run-01e1b82c2d334ab69a79a28fd678f1c9/result.json.
현재 D/R/Shipping runtime DLL SHA와 각각의 패키지 DLL 일치, packaged managed
소스 일치, 소스 snapshot/개별 로그 SHA를 저장했다. 새 runtime 수정은 없다.
첫 matrix 러너의 단일 옵션이 문자로 분해된 호출은 Player 시작 전 실패했으며
수용에서 제외했다. 인자 배열을 수정한 뒤 기존 Debug 패키지로 다시 검사했다.
긴 경로/forest recipe 문제는 짧은 출력 경로와 현재 cooked 배포 입력으로 예방했다.

M2의 기존 필수 근거를 함께 대조했다:
- 최신 SDK 경계 감사: native1253/project35, negative controls3, 위반0.
- Mathematics 경계 Debug/Release, 새 컴포넌트 catalog29 통과.
- 실제 Release Editor HTTP163/C#18: DDOL 목적지 실패 후 6객체·Transform·활성·
  레이어·컴포넌트 ID·부모 복원, stale 접근 거부, Replay/두 번째 Stop, 파일 불변.
- native 핸들 수명: D/R/ASan1098·Shipping395, 실제 GPU, 세대/씬 수명과 자원 회수.
- 실제 Player D/R/Shipping 24사례: 정상·실패·DDOL/삭제 wrapper 및 종료 ledger.

수용 목록은 WD-46a3b33f/m2-acceptance.json. 위 근거로 M2를 done으로 변경한다.
기존 M2 progress/잔여 기록은 당시의 판정이며 이 항목이 현재 판정이다.
M1 스키마/corpus, M3 제품 성능·SDK 내부 allocator/managed heap/peak/RSS,
M4 최종 빅뱅 재유입 감사는 완료로 올리지 않는다. Phase19 전체 완료가 아니다.

## 2026-10-03 R0/B0/M0 수용 정리와 L0 검증기 수정

이 절이 해당 항목의 현재 판정이다. 앞의 소비자 미이전/제품 미빌드 기록은
철거 당시 이력이며 현재 잔여 구현 목록으로 해석하지 않는다.

- R0 완료: fresh `audit-physics-cutover.ps1 -Gate Complete`에서 삭제55,
  남은 파일/프로젝트 참조/구 소비자0. 새 API 제품 빌드·실행은 M2 수용 목록과 연결한다.
  lexical 감사가 AST/링크 감사는 아니므로 최종 재유입·링크 확인은 M4에 남긴다.
- B0 완료: fresh `verify-physics-b0.ps1 -Configuration All -RequireGpu`,
  Debug/Release/ASan 각4978, Shipping4971 checks, 네 구성 실제 GPU 통과.
  완결된 바디 정의·축 잠금·제어·등록/해제·실패 회수·필터 트랜잭션을 검증했다.
  M2의 실제 Editor HTTP163/C#18과 Player D/R/Shipping24사례/288 checks를
  제품 생명주기 수용 근거로 연결한다. Editor 실패 복원·Stop/Replay·DDOL/삭제와
  Player의 저작 snapshot 제외를 포함하며 성능/내부 메모리는 M3이다.
- M0 소비자 이관 범위 완료: 구 소비자0 및 fresh ABI32/168슬롯 순서·초기화,
  Debug/Release 각33 checks. M2 wrapper/수명주기 및 기존 Body/Shape/Query/CCT
  제품 검증을 연결한다. 구 데이터 변환은 M1, 모델 자동 충돌의 전체 저작 통합
  회귀는 M3, 최종 소스/링크 재유입 감사는 M4로 구분한다.
- L0 progress 유지: fresh catalog 네 구성 각7602, SDK 필터 D/R/ASan568·
  Shipping564(실제 GPU), import/현재 자산 네 구성 각1715, migration1409,
  package6, 저작 소유권 SourceOnly 통과. M2의 Editor 복원·Player cooked/DDOL
  레이어 보존도 연결한다. 실제 Editor 정책 변경→Body/CCT refilter→Undo→
  Stop/재시작을 한 경로로 확인하는 통합 게이트는 아직 확보하지 않았다.

레이어 import 검사기의 첫 재실행은 현재 tags-only TagManager.asset에 구 layers
구역이 없어 실패했다. 제품 회귀와 구 입력 검사기 불일치를 구분한다. 검사기는
`fixtures/LegacyProjectLayers/`의 고정 변환 전 입력을 읽고, 별도 인자로 현재
Dynamic_CPP/Assets를 검증하도록 수정했다. 런타임 구 스키마 fallback은 추가하지 않았다.
수정 후 네 구성 모두 통과했으며 최초 실패를 수용 결과로 포함하지 않는다.

Fresh 로그: Build/Obj/Phase19B0/closure-all.log,
Phase19L0Sdk/closure-all.log, Phase19L0/closure-catalog.log,
Phase19L0Import/closure-import.log, Phase19M0Script/closure-abi.log.
철거 결과: Build/Obj/Phase19R0/cutover-audit.json.
제품 수용 연결: Build/Obj/P19DdolFailureDS/WD-46a3b33f/m2-acceptance.json.
제품을 이번 문서 정리에서 다시 구동한 것은 아니며 위 M2 보존 실행을 재사용한다.
계획 공수는 유지한다. Phase19는 10/19 완료, progress6/todo3이다.
다음은 L0 Editor 정책 변경 통합 게이트이며 M1/M3/M4 완료를 주장하지 않는다.

## 2026-10-03 L0 완료: 실제 Editor 정책 변경/Undo/Stop 통합

Fresh Release CreatorEditor를 현재 소스로 빌드한 뒤
`Tools/regression/verify-physics-layer-http.ps1`을 실행했다.
HTTP143명령/86 checks, 독립 동적 box와 CCT가 같은 공통 레이어 정책을 사용한다.

실행 중 발견한 결함: LayerSettingsCommand가 게임 Undo 스택을 사용하면서도
Layers.celayers에 실행 중 정책을 저장했다. Stop의 메모리 snapshot 복원만으로
디스크 변경은 회수되지 않았다. 명령 생성 시 편집/Play publication 정책을
고정했다. Play 변경과 Undo/Redo는 메모리 쌍/revision만 발행하며, 편집 모드는
기존 Editor 저작 트랜잭션으로 파일을 저장한다. Inspector와 HTTP는 같은 경로다.
엔진 런타임/Player에 저작 저장 경로를 추가하지 않았다.

실제 제품 수용:
- 편집 정책 차단의 파일 변경, Undo의 정확한 파일 복원, Redo 재게시와 재Undo.
- Play의 이름 변경/Undo 후 layer ID·slot·Entity 소속 보존.
- 허용 상태: body 중심 y0.499999, CCT foot y약0으로 접지.
- 두 쌍 차단 후 기존 Body/CCT를 초기 위치로 재배치: body y-2.024974,
  CCT 중심 y-2.39285로 실제 바닥 통과. 게임 Undo 깊이+2, 편집 스택 불변.
- Undo 두 번 후 새 revision: Body/CCT 재접지. Body component 신원 유지.
- Redo 두 번 후 다시 통과: body y-2.965099.
- 차단 상태에서 Stop: 세 객체 pose/rotation/scale/layer/활성/component ID 복원,
  캐릭터 simulating=false/tick0, 공통 정의 복원과 monotonic revision 확인.
- Replay: 원래 허용 정책으로 다시 접지(body y0.4999991), 두 번째 Stop 복원.
- Play/Undo/Redo/Stop 동안 저장 Scene·Layers.celayers·tags-only TagManager.asset
  SHA 불변. owned Editor 종료 후 호출자 원본 Layers.celayers도 정확히 보존.

최종 evidence: Build/Obj/Phase19L0Editor/http-93576c5699e0408c890e3e501fac9b49/result.json.
receipt의 runtime DLL SHA는 현재 바이너리와 일치하며 probe 소스 SHA도 기록한다.
빌드: Build/Obj/Phase19L0/editor-layer-host-fix-build.log 및 layer-probe-build.log.
실행: Phase19L0/layer-http-final.log, 개별 results.jsonl.
SourceOnly 저작 소유권 게이트도 통과했다.

최초 loader exit161은 생성된 배포의 오래된 forest recipe였으며 현재 검증된
cook 입력을 Bin 배포에 적용하고 재실행했다. 원본 Resources는 변경하지 않았다.
수정 전 Play 파일 변형을 검출한 실행은 실패 증거다. 첫 수정 후 러너의 중복
초기화로 누적 검사 수가4로 기록된 receipt도 제외했다. 중복 초기화를 제거하고
다시 실행한 최종86 checks만 수용한다.

앞 절의 native catalog/SDK/import/migration/package와 M2의 DDOL·cooked 제품
검증에 이번 실제 Editor 게이트를 결합하여 L0를 done으로 변경한다.
Phase19는 11/19 완료, progress5/todo3이다. 계획 공수는 유지한다.
M1 데이터 스키마 변환, M3 대표 회귀/프로파일러 비용/내부 메모리, M4 최종
소스/링크 재유입 감사는 남는다. 다음 구현은 M1 일회성 물리 스키마 변환이다.

## 2026-10-03 M1 착수: primitive 일회성 변환/차단/원본 회수

`Tools/regression/migrate-physics-schema.py` 및 사용 계약 PhysicsSchemaMigration.md를 추가했다.
구 UUID와 P0 Scene/Prefab 저장값을 기준으로 Box/Sphere, compound, Rigidbody의
static/dynamic/kinematic enum을 새 PhysicsBodyComponent/Shape schema1로 변환한다.
Rigidbody instance ID는 유지하고 collider instance ID를 ShapeId로 사용한다.
콜라이더만 있는 경우 명시적 static body를 만들며 같은 ID를 body에도 사용한다.
파일/prefab UUID·meta는 유지한다. type UUID는 새 Body UUID로 바꾸고 원래 UUID를
매핑 보고서에 보존한다. 비물리 값은 유지하되 변환 파일의 YAML 표기는 재작성된다.
변경 없는 문서는 원본 바이트 그대로이며 반복 실행도 변경0이다.

Rigidbody mass를 새 compound 질량/관성 계약에 적용한다. 구 density는 provenance에
기록한다. 직렬화되지 않았던 locks/초기 속도는 복원 가능한 과거 값으로 주장하지
않으며 초기화 정책을 보고한다. 구 속도/충격량 상한은 새 저작 필드가 없으므로
기본 차단한다. explicit --reset-legacy-limits도 정확한 구 default 값에만 허용한다.
custom 상한은 계속 차단한다. 실제 프로젝트에 이 정책으로 apply하지 않았다.

안전 경계: duplicate YAML key/alias, 잘못된 값/신원, 독립 비활성 형상,
동적 sensor-only, 충돌-disabled body, 기존 새 Body와 혼합 소유권, prefab override,
asset-linked collider, 제거 collider의 외부 참조를 거부한다. 전체 프로젝트의
하나라도 진단이 있으면 apply도 게시0이다. ZIP은 원본 bytes/CLYR/SHA256/매핑을
보존하고 partial write failure는 게시된 파일을 역순 원복한다. symlink/범위 외
저작 경로와 변경된 layer catalog/source도 거부한다.

Fresh `verify_physics_schema_migration.py`: 37 checks. 실제 P0Drop prefab 기반,
ID/shape/material/mass/Transform/UUID/layer, compound Box/Sphere, static 소유권,
미지원 거부, dry-run 게시0, 프로젝트 전체 사전 검증, 둘째 파일 쓰기 실패 원복,
ZIP/manifest/meta 보존과 반복 변경0을 확인했다.
변환 Shape를 엔진 native ParsePhysicsShapeDocument에 전달하여 Debug/Release/ASan
각46 checks 통과. Python 변환값만 확인한 것이 아니며 전체 제품 로드 증거도 아니다.
증거 Build/Obj/Phase19M1/{migration-tests.json,native-shapes.log,
primitive-converted.prefab,primitive-shapes.yaml,primitive-mappings.json}.

현재 Dynamic_CPP dry-run은 P0 prefab/Scene의 구 solver limit 때문에 exit2.
explicit default reset dry-run도 P0 Scene의 구 CCT 때문에 exit2이며 전체 게시0.
보고서 project-dry-run.json/project-explicit-dry-run.json. 원본 P0 fixture와 실제
프로젝트 파일을 변경하지 않았다. 새 runtime 구 스키마 fallback은 없다.

M1은 progress다. Capsule/CCT(C1 단위·소유권 정책), mesh/terrain/ragdoll,
reference/override remapping, 실제 대표 corpus의 native load/cook/package/제품
회귀는 남는다. cooked 파일을 직접 고치지 않고 새 schema로 재쿠킹해야 한다.
Phase19 11/19 완료, progress6/todo2. 공수는 유지한다.

## 2026-10-03 M1 Capsule 축/높이 이전 및 CCT 진단 연결

구 Physx.cpp의 static capsule은 PxCapsuleGeometry(radius,height)를 X축으로
생성했다. dynamic/kinematic은 height/2와 local +90° Z 회전으로 Y축을 사용했다.
새 API는 항상 Y축이므로 static 변환은 local -90° Z로 새 축 보정을 상쇄하고
halfHeight=구 height를 사용한다. dynamic/kinematic은 halfHeight=구 height/2다.
단순히 모든 저장 height를 절반으로 바꾸면 static의 길이와 축이 달라진다.

migrate-physics-schema.py에 Capsule 저작 변환을 연결했다. 원래 shape ID와
재질/반지름을 보존하고 매핑 manifest에 Capsule 정책을 기록한다. 구 dynamic의
pre-scale 경로와 새 scale 1회 적용을 혼동하지 않도록, 이번 slice는 자산 내
모든 Transform unit scale 및 Capsule offset0/rotation identity에 제한한다.
비단위 scale·offset 합성은 명시적 진단이며 임의 근사하지 않는다.

Python migration 51 checks: static/dynamic 축과 height, 정책 기록, 미지원 scale/
offset 거부, 기존 primitive/rollback/ID/UUID 검증 및 CCT 보고 연결 통과.
실제 변환 Capsule YAML을 native parser와 static/dynamic ValidatePhysicsShapes에
전달하여 Debug/Release/ASan 각52 checks 통과. 실제 SDK 충돌/전체 native Scene
로드·재쿠킹·제품 회귀를 이번 단독 parser/preflight 증거로 주장하지 않는다.
증거 Build/Obj/Phase19M1/{capsule-migration-tests.json,capsule-shapes.yaml,
capsule-native-final.log}. 초기 typed BuildPhysicsShapes 탐침은 독립 parser 실행파일의
CollisionGeometry::kind 링크 의존 때문에 실패했으며 제외했다. 의도한 SDK-free
저작 preflight를 검증하는 ValidatePhysicsShapes로 바꾼 최종 결과만 수용한다.

CCT는 변환을 계속 차단한다. --baseline-seconds로 명시적 구 tick을 지정하면
기존 C1 단위 도구를 연결하여 원본 SHA와 unit proposal/reviewRequired를 보고한다.
이 옵션은 CCT 게시를 허용하지 않는다. 1/60 fixture 기준 P0 저장 maxSpeed는
61.5m/s, late-update baseSpeed*multiplier는1.5m/s이며 이 차이를 모두 기록한다.
gravity -12m/s², jump3m/s, acceleration60m/s² 제안과 감쇠 단위도 연결한다.
새 movement API의 외부 desired-velocity 입력 정책, dynamic lerp/자동 회전,
offset/scale, 동반 Rigidbody 소유권과 비직렬화 controller 설정은 미해결이다.
C1 단위 회귀22 checks도 통과했다.

현재 프로젝트 explicit reset + baseline dry-run은 구 CCT를 진단하며 게시0이다.
보고서 Phase19M1/capsule-cct-dry-run.json에 source hash/characterUnitProposals를
보존한다. 실제 P0 자산과 저작 프로젝트에는 apply하지 않았다.
M1은 progress 유지. 다음은 CCT 입력/소유권 정책 확정과 참조/override remapping,
복잡 지오메트리 및 전체 제품/corpus 이관 검증이다.


### 2026-10-03 M1 CCT reviewed migration and reference closure

원본 CCT+collider 없는 companion Rigidbody SHA에 연결된 명시 정책으로
CharacterMovementComponent 변환과 velocity carrier 제거를 구현했다.
입력 speed 선택·static decay·dynamic damping 폐기·외부 회전·fall limit을 기록하며
게임 입력/회전 코드를 생성하지 않는다. 제한된 typed prefab override는
Rigidbody mass/linear damping/gravity, CCT radius/height의 effective 원본 값에
일치할 때만 변환한다. 다른 자산의 retired component 참조는 전체 게시를 차단하고,
쓰기 직전 전체 inspected corpus 변경도 검사한다.

Python 73 checks 통과. Release Editor에서 변환된 P0 fixture 이동·접지·점프와
Play/Stop 두 회차 원상복귀, 씬/레이어 파일 불변을 확인했다.
근거: Build/Obj/Phase19M1Editor/http-351781bf414343fc916efe2568d83ce2/result.json.
점프는 completed-ground 전제 거부를 기록하고 조건을 만족한 요청의 수용을 검증했다.
M1 progress 유지: 실제 게임 입력/회전, 복잡 geometry/override/typed reference remap,
전체 corpus 및 cook/Player 검증은 잔여다. 실제 저작 자산에는 apply하지 않았다.


### 2026-10-03 M1 external type-reference closure

다른 Scene/Prefab의 ID 없는 구 물리 타입명·UUID 참조도 전체 사전 검증에
포함했다. type key, override YAML 내 타입명, 대문자 UUID를 거부하며 새 타입
참조는 허용한다. 자동 문자열 치환은 하지 않고 명시 typed remap을 요구한다.
Python 78 checks 통과, 현재 Dynamic_CPP dry-run 2files/3bodies/1character,
진단0·게시0. 실제 게임 입력/회전 소비 코드는 현재 저작 프로젝트에 없어
연결 완료로 판정하지 않는다. M1 progress 유지.
근거: Build/Obj/Phase19M1/type-closure-project-dry-run.json.


### 2026-10-03 M1 geometry source recovery preflight

삭제 전 revision 12f970c7ed3a4408d268479f5d5acb5f80372b8c의 MeshCollider.h,
TerrainCollider.h, PhysicsManager.cpp를 대조했다. MeshCollider m_Info는
직렬화 대상이 아니며 convex 정점을 채우는 배선도 없었다. Terrain 높이 데이터는
TerrainComponent에서 공급했다. 콜라이더 YAML만으로 cooked 형상 복원은 불가능하다.

변환 실패 보고서에 geometryRecoveryRequirements를 추가했다. 파일/컴포넌트 SHA,
원본 값, 동일 Entity의 MeshRenderer/TerrainComponent 공급 후보와 필요한
model/submesh·높이 순서/양자화·축 scale·재질/cook 정책·geometry UUID/revision을
기록한다. Ragdoll은 별도 body/joint 소유권을 요구한다. 적용 허용 정책은 아니며
누락 데이터가 있는 프로젝트는 전체 게시를 차단한다. Python84 checks 통과:
공급 후보·원본 hash·Terrain/Ragdoll 누락·apply 무변경 검증 포함.
M1 progress 유지. 실제 geometry 이전 및 native/cook/Player 수용은 잔여다.


### 2026-10-03 M1 explicit geometry identity bindings

geometry 이전 정책 schema1 검증기를 추가했다. authoring/component/supplier 및
원본 파일·대상 geometry·meta SHA를 고정하고 UUID/revision/kind·CECG v1
체크섬을 검증한다. Mesh는 convex, Terrain은 heightfield만 인정한다.
프로젝트 밖 경로·중복 component·변경 hash/identity·손상 체크섬을 거부한다.
실제 단검 geometry 파일의 신원 연결을 회귀에 사용했다. 원본 모델 fixture는
합성 identity-only 데이터이므로 모델→형상 동등성 검증 근거가 아니다.
Python97 checks. read-only identity_bound_native_validation_pending이며
native payload decode/cook, 재질/pose/scale 정책, shape 변환/Player 수용은 잔여다.
M1 progress 유지; 실제 자산 apply0.


### 2026-10-03 M1 native geometry policy acceptance

engine CollisionGeometryCodec decode→PhysX cook_geometry_blob→load_geometry_blob
게이트를 추가했다. Release 실물 단검 convex(6030 cooked bytes), heightfield(102),
triangle mesh(372) 수용. checksum을 재계산한 잘못된 point count payload는
native decode 단계에서 거부된다. 파일/실행파일 SHA receipt를 정책 검증기에
연결하고 hash/kind/revision/result 불일치를 거부한다. 실제 단검 receipt를 넣은
Python102 checks 통과. evidence Phase19M1NativeGeometry/Release/.
SDK cook/import 수용이며 바디 충돌이나 이전 source 동등성 증거는 아니다.
재질/pose/scale/cook 정책·shape 변환·Player 수용이 남아 M1 progress 유지.


### 2026-10-03 M1 reviewed geometry shape conversion

geometry-policy/native-receipt를 오프라인 변환기에 연결했다. sourceSelection,
재질/pose/geometryScale 명시 정책을 요구하며 실제 원본 pose 일치와 전체
owner/ancestor unit scale을 확인한다. Mesh→convex shape3, Terrain→heightfield
shape5; Terrain은 standalone static solid/identity 회전만 허용한다.
unknown 필드·비활성 독립 shape·asset link·Ragdoll/geometry override는 차단한다.
geometry UUID/revision·body/shape ID 보존, 파일 쓰기 전 의존 hash 재검증.
임시 fixture apply·ZIP 원본 복구·재실행 no-op 포함 Python112 checks.
실제 단검 native receipt 사용, 생성 convex YAML Release native parser56 checks.
Terrain 검사는 정의 변환 fixture이며 실제 높이 원본→대상 동등성/cook 제품
수용을 의미하지 않는다. 실제 저작 geometry apply 및 cooked Player는 잔여.
M1 progress 유지.


### 2026-10-03 M1 real dagger source equivalence and migrated package

Weapon_Dagger_G3_005_Separate.glb SHA 50b2a38725557a7ce96e72c3c727349756a329c3194bad106821f9801298a0b7:
static identity node/all primitives의 Z reflection·metre float32 고유점2119개가
CECG convex 입력과 byte-record multiset으로 일치했다. point 변경 후 checksum을
재계산한 geometry는 불일치로 거부된다. cooked hull topology 비교는 아니다.

기존 단검 제품 fixture에서 구 Rigidbody/MeshCollider 구조를 재현해 명시 정책으로
변환했다. 역사적 게임 저장 자산이 아닌 재현 fixture다. Shape ID 외에는 기존
바디 정의와 동일하며 원본 ZIP과 변환 SHA를 보존했다. fresh Release distribution
패키지의 Scene 및 Player.runtime.dll hash가 로컬 결과와 일치한다.
패키지 smoke와 별도 Player 물리 각각14/0(낙하·접촉·회전·impulse·재착지).
별도 completed display/정상종료 게이트는 600초 내 완료 결과가 없어 실패했다.
소유 Player를 종료했고 제품 전체 수용으로 판정하지 않는다. 렌더/RHI 원인은
확정하지 않았다. evidence Phase19M1Dagger/acceptance.json.


### 2026-10-04 M1 Player timeout phase isolation

Player smoke에10초 간격 GT/render/published/consumed/completed/promotion/slot
진행 로그를 추가했다. fresh Release Player 빌드 통과. 기존 패키지 사본의 runtime을
진단 빌드로 교체한 실행이며 정식 재패키지 제품 수용 근거가 아니다.
resize/no-resize 두 실행 모두 물리14/0, GT frame 증가, snapshot은
published1/consumed1/rendered0/inFlight0/completed0/promotion0/readyfalse.
정상 shutdown 진입 전 display 전제 대기임을 확인했다. resize 단독 원인으로
설명할 수 없으며 최초 frame 내부/GPU 단계 원인은 아직 미확정이다.
30초 무진행 watchdog으로 두 실행을 실패 기록·소유 process 종료했다.
verifier 기본은60초 무진행, 총 제한600초 유지. 완료 기준8promotion/slot rotation은
유지한다. initial shader/cook warmup 가능성을 원인 분석에서 배제하지 않는다.
근거 Phase19M1Dagger/progress-investigation.json, failure.json 두 건.
M1 progress 유지; 다음은 frame1 내부 phase와 RHI 제출/완료 경계 진단이다.


### 2026-10-04 M1 render startup versus steady sealing failure

명시 CE_RENDER_PROGRESS_TRACE=1일 때 첫3 frame consume/tuning/proxy/collect,
pipeline device/pass/IBL/display, pass 이름과 Forward shader/PSO 변형 진행을
추적하도록 진단 로그 추가. 최종 Release Player 빌드 통과. 진단 runtime 사본
실행이라 정식 제품 package 수용 근거는 아니다.

Forward 초기화는 고정 정지가 아니라 실제 진행했다. 초기30초 watchdog이
render count만 보아 예열을 무진행으로 오판한 한계를 확인하고 초기화 phase
진행도 signature에 반영했다. 후속 실행에서 pipeline/IBL/display 구축·sealing과
frame1/2 완료(display2/slotMask3/readytrue)를 확인했다. 이후 GT publish/consume은
계속 증가하나 display2에서 멈추고 idle 증가, enabledtrue에 아래 오류가 반복된다:
LX Scene input sealing failed: Scene graph geometry 7990584863953042883:
Scene mesh reuse needs a valid current view and matching pose layout.

직접 실패 경계는 MaterialGraphMeshSurface::BuildSceneZeroLod 재사용 preflight다.
GPU hang이나 shutdown lock을 원인으로 확정하지 않는다. 다음은 current view,
world affine, bone layout 중 실제 거부 조건 분리와 수정, fresh package 재검증.
근거 Phase19M1Dagger/render-boundary-investigation.json 및 render-error-trace.log.
M1 progress 유지, physics14/0과 최종display 실패를 분리한다.

### 2026-10-04 M1 physics render affine transport fix

재사용 preflight 오류를 world/view/palette/layout별로 분리한 실제 Player 실행에서
world affine 거부를 확인했다. 정상 범위의 포즈 값이지만 정확한 homogeneous
0/1 규약에 실패한다. Scene::PhysicsRenderMatrix의 world * inverse(bodyWorld) *
interpolated 경로를 native 128포즈로 재현했으며, 기존 경로 62포즈에서 m[3][3]이
정확한 1을 벗어났다. 진단 runtime 실행은 반복 중 통과/실패가 섞여, 단일 통과를
해결 근거로 삼지 않는다.

바디 자신은 compose한 interpolated 행렬을 직접 반환한다. 자식은 일반 inverse의
homogeneous 열을 affine 상수로 복원한 뒤 상대 변환을 적용하며, 특이행렬이면
현재 world를 보존한다. Render mesh 검사의 허용오차는 완화하지 않았다.
Release B2 729 checks / GPU verified 통과: 128포즈에서 정확한 affine 성분과
자식 local offset/scale, singular fallback을 검증했다. Release Player 빌드 통과.

초기 fresh package는 forest cook recipe stale(exit161)로 차단됐다. 환경 recook
(roundtrip exact/validation0) 및 Bin 배포 파일 갱신 후 새 distribution과 package를
다시 만들었다. 변환 단검 fresh package smoke 통과; 별도 Player 물리14/0,
displayPromotions8, frames175279, exit0, package immutable/cooked-only/parser0 통과.
월드 affine sealing 거부 없이 완료했다. runtime SHA256
1102167851643D4C40252D28561B809614F06277CEBAFD5E19C706FC3EDE89BA.
근거 Build/Obj/Phase19M1Dagger/affine-fix.json, affine-current-package.log,
Build/Obj/Phase19DaggerPlayer/run-80011cff680043a6886824f9474f6a30/result.json.
M1 전체 corpus/복잡 override/reference/실제 Terrain 복구 및 입력·회전 검증은
별도 잔여로 progress 유지. 새 Editor 재빌드/Play-Stop 검증은 이번 변경에서 미실행.
### 2026-10-04 affine fix Editor lifecycle verification

수정 Scene.cpp/PhysicsTransformPolicy.h를 포함한 Release CreatorEditor 재빌드 통과.
B2 HTTP 게이트에서 dynamic/static/kinematic 각9 assertions를 두 실행에서 통과했다.
두 Play/Stop 이후6 엔티티 position/rotation/scale을 최초 값과 비교해 원복을 확인했고,
세 번째 shear 실패 실행은 idle 복귀 및 같은6 엔티티 원복을 확인했다.
저작 Scene SHA 불변 검사와 affine 정책 header freshness 검사를 게이트에 추가했다.
근거 Build/Obj/Phase19B2Editor/run-cdf8416241b047aca4e6b40ce04aebc9/result.json.

새 Editor로 변환 CCT HTTP 게이트도 통과했다. 명시 CLI 이동/접지/점프,
Play/Stop2회 변환·runtime tick0 원복, Scene/Layer 파일 불변 확인.
근거 Build/Obj/Phase19M1Editor/http-99fdef9816834d1eb2facb0842526fa7/result.json.
캐릭터 게이트의 freshness에 Scene.cpp/PhysicsTransformPolicy.h를 추가했다.
이 검증은 lifecycle/저작 원복 근거이며 Editor Game display fence 수용이나
실제 gameplay 입력 배선 검증을 대체하지 않는다. B2 종료 로그의 profiler
RHIThread abandoned1/retained1 경고는 별도 계측 종료 진단으로 남긴다.
M1 전체 corpus·복잡 참조·실제 Terrain 복구는 잔여로 progress 유지.
### 2026-10-04 Editor completed display gate

B2 HTTP gate에 Play 중 game 및 Stop 이후 scene 대상 완료 검사를 추가했다.
render.live.fence는 RT completedFrame > 요청시 publishedFrame을 검증하고,
dx12.live의 해당 target active/ready/completedFrame > 같은 요청 프레임을
별도로 검증한다. HTTP sync는 고정5초 제한으로 긴 초기화 도중 timeout되므로
fence만 async 요청/operation 완료 polling으로 실행하며, renderer 완료 조건은 유지한다.

새 Release Editor 실행에서4경계 통과:
game after639/RT14703/display14876, scene14889/14890/14906,
game15364/15365/15438, scene15462/15466/15522. 모든 target ready true.
B2 물리 각9 assertions 두 실행, Stop2회 및 shear 실패의6 엔티티 원복,
저작 Scene 파일 SHA 불변도 함께 통과했다. shear 실패 후 표시 프레임은
이번4경계 검사의 범위에 포함하지 않는다. 표시 슬롯 완료 근거이며 사용자
모니터의 실제 scanout 또는 이미지 픽셀 비교를 주장하지 않는다.
근거 Build/Obj/Phase19B2Editor/run-c29413cbda3e484f87abfee70ab9bd1d/result.json.
M1 전체 corpus/복잡 참조/실제 Terrain 복구·게임 입력 배선은 잔여로 progress 유지.

### 2026-10-04 M1 current corpus preflight

Dynamic_CPP Scene/Prefab85개 조사. 기본 dry-run은 P0Drop prefab/P0Baseline scene의
legacy default solver-limit 명시 동의 부재로2파일 차단. 보고서에 inspectedFiles 및
각파일 path/SHA256/outcome 목록 추가. 임시 사본에서 reset-defaults만 허용하면
프리팹1개 준비, P0Baseline 구CCT source-hashed policy 부재로 전체 apply 차단.
부분 쓰기/backup0, 원본85개+Layers byte 불변 확인. 실제 authoring apply0 유지.
새 verify-physics-migration-corpus.py는 임시 corpus preflight/apply, 차단시 무쓰기,
성공시 변환 멱등성/ZIP 원본/after SHA/정확복구 및 원본불변을 검증한다.
P0Drop 단독 positive corpus는1파일 변환·멱등·ZIP byte복구 통과. Python112 통과.
근거 Build/Obj/Phase19M1Corpus/{dry-run,result,positive}.json.
전체 corpus 완료 아님. 실제 P0 CCT 정책의 원본 일치 확인이 다음 선결 작업.


### 2026-10-04 M1 current corpus reviewed CCT policy

기존 character-mappings.json의 검증했던2169397090 정책을 복원해 현재 corpus에
대조했다. CCT+동반 Rigidbody canonical source SHA
0af2ffd862e1c6cad4692882d70c247b5862fa8bd886e6a4d647e40a45367894
일치, baseline1/60·steady1.5m/s·external input/rotation 및 carrier 제거 정책 유지.
corpus verifier에 --character-policy와 정책파일 SHA/실행중 불변 검사를 추가했다.
현재85 Scene/Prefab 임시 사본:2파일/3body/1character 변환, 전체 재실행 변경0,
ZIP 원본/after SHA 확인·정확 byte복구·원본85개+Layers 불변 통과.
source SHA를0으로 바꾼 부정정책은 전체 apply 차단·부분쓰기0 확인. Python112 통과.
근거 Phase19M1Corpus/with-policy.json 및 mismatched-policy-result.json.
실제 authoring apply0, corpus cook/Editor/Player acceptance 및 meta identity 검증은
이 임시 변환 검사와 별개로 남는다. M1 progress 유지.


### 2026-10-04 converted corpus product handoff

Dynamic_CPP Assets/ProjectSetting 전체를 Phase19M1CorpusProduct/Project로 복사하고
검증된 CCT 정책으로2파일/3body/1CCT 변환·ZIP backup을 보존했다. 저작 원본 apply0.
현재 프로젝트에 Assets/Script 폴더가 없어 사본에 현재 GameScripts 소스를 제공했다.
전체 package managed compile 통과. 모델 cook은 Cha_Mon_5.fbx 메타가 요구하는
926b7b0d-1c12-865a-b8b1-841cc4d8c1af generation7 저장소 부재에서 실패했다.
원본 Library에도 해당 generation 없음. generation-audit.json에 총 모델 의존 누락
목록을 기록했다. 첫 모델의 staging generation 재생성 로그와 authoring meta를
구분했다. meta-audit.json은282개 meta 원본/사본 해시 동일(변경0)을 확인했다.
전체 cook/Player 통과 주장하지 않는다.
변환한 사본의 PhysicsP0Baseline.creator를 새 Release Editor로 직접 읽어
CLI 이동·접지·점프 및 Play/Stop2회 원복/Scene-Layer 불변 통과.
근거 Phase19M1Editor/http-a73e856629da4227b90674bab51149aa/result.json.
전체 product gate 실패 근거 Phase19M1CorpusProduct/package.log, generation-audit.json,
meta-audit.json 및 migration.json. 다음은 사본의 source-hashed model generation
재생성·신원 검증 후 corpus cook 재시도. M1 progress 유지.


### 2026-10-04 M1 corpus model generation recovery

새 recover-corpus-model-generations.py는 독립 사본의 모델 source bytes를 원본과
대조하고 native authoring 후 assetId/authoringKey/identity profile/epoch/sourceFingerprint
및 subasset(kind,stableKey,assetId)을 비교한다. 현행 모델15개 generation 재생성·
신원 보존 통과. generation 번호는 재생성으로 변경되며 구 generation7을
그대로 복원했다는 의미가 아니다. 원본 meta는 변경하지 않았다.
DX12ValidationPrimitives/Phase17_Infinian/Phase17_Sponza의 구 GUID 메타3개는
현행 신원으로 바뀌므로 복구 수용에서 제외·사본 구meta 유지·명시 참조 이전 잔여.
이 목록 때문에 receipt result는 CORPUS_MODEL_GENERATIONS_BLOCKED다.
전체 package 재시도는 generation 부재를 넘어 Foliage_WeedPlant02의 embedded
texture export에서 실패: generation 실제textures UUID.jpg, 요구 경로 UUID.png.
임의 확장자 파일 복제 없이 export 경로 생성 오류로 남긴다. 신원 검사 통과가
전체 generation payload cook 수용을 뜻하지 않는다. 전체 Player는 미실행.
근거 Phase19M1CorpusProduct/generation-recovery.json 및 package-recovered.log.
다음은 embedded texture export 경로와 실제 generation payload 경로 일치 수정.
M1 progress 유지; 실제 authoring apply0.


### 2026-10-04 generation texture export path correction

ModelGenerationExportProducer가 texture UUID에 .png를 고정 부가하여 JPG generation
payload를 찾지 못했다. 검증된 generation의 export 파일 목록에서 textures 디렉터리와
UUID stem이 맞는 유일한 실제 파일 경로를 사용한다. 누락/동일UUID 복수파일은 거부.
확장자 변경이나 bytes 재인코딩 없음. Release AssetCooker 빌드 통과.
새 verify-model-texture-export.py 네이티브 JPG/PNG6payload 정확byte/확장자 유지,
동일UUID JPG+PNG 모호성 거부 통과. Foliage 단일 native cook도 통과.
근거 Phase19M1CorpusProduct/TextureExportRegression/result.json 및
texture-export-positive.log. fresh distribution 전체 재시도는 texture export를 넘어
manifest closure에서 exit4 실패했다. entry c86fb443-1481-493c-9f37-6ae76cdadf54
Scene의 구 모델 GUID76cda096-f43a-4eb1-a98b-38fe7d25db52가 manifest에 없다.
앞서 DX12ValidationPrimitives 구GUID 정책 잔여로 확인한 참조 이전 문제다.
근거 package-texture-fixed.log. 전체 Player 미실행, M1 progress 유지.


### 2026-10-04 explicit legacy model Scene reference migration

사본의 legacy 모델3개를 native authoring으로 현행 신원/generation으로 발행했다.
새 migrate-corpus-model-references.py는 source byte SHA·현행 sidecar fingerprint·
UUIDv8 검증, 구/신 sidecar SHA 및 model GUID mapping을 receipt에 남긴다.
재질 subasset은 binding+name 유일 대응만 허용, 미해결 legacy subasset 참조는 거부.
MeshRenderer의 구model GUID+유일 mesh name으로 새 m_modelGuid/m_meshAssetId를
명시 설정한다. 독립 사본만 허용하며 변경 전 입력 재검증·ZIP backup·실패 rollback.
Scene/Prefab scope preflight/apply: DX12Validation1파일/6typed mesh binding 통과.
재실행 변경0. 원본 저작 프로젝트는 미적용. backup/receipt는
Phase19M1CorpusProduct/model-reference-{preflight,applied,idempotent}.json 및
model-reference-backup.zip. 전체 material/참조 이전 완료를 주장하지 않는다.
Phase17_Infinian.asset/Phase17_Sponza.asset는 CEMA v2 binary에 구model GUID가
남는다. 전체 scope는 명시 native codec 필요 오류로 거부; YAML/byte 치환 없음.
다음은 CEMA decode→typed GUID remap→encode/hash 검증 후 전체 cook 재시도.
M1 progress 유지, 전체 Player 미실행.


### 2026-10-04 CEMA classification correction and retirement

앞선 CEMA2파일을 binary material로 분류한 판단을 정정한다. 엔진
AuthoringParsedDocument.cpp는 CEMA를 구 모델 cache로 명시한다. 현재 모델은
source-hashed schema2 generation으로 이미 발행했으므로 구cache codec 재도입 없음.
새 retire-corpus-legacy-model-caches.py: 독립 사본 제한·source byte 고정·CEMA magic·
asset GUID ASCII(대소문자)/raw UUID 양byte-order 참조 전수 검사·정확ZIP 검증 후
2cache+2meta 제외. 실패 rollback, 원본불변 확인. 참조0으로 사본 retirement 통과.
부정 fixture cache 참조를 주면 삭제0/backup0/4파일보존 통과.
전체 text reference preflight 변경0 통과. corpus cook 재시도 중이며 Player 미확인.
근거 Phase19M1CorpusProduct/legacy-cache-retirement.json, legacy-cache-backup.zip,
CacheNegative/result.json 및 model-reference-full-preflight.json. M1 progress 유지.

### 2026-10-04 full corpus cook and model JPG path contract

- CEMA cache retirement 후 전체 native cook: 18 models, 331 artifacts, 36 generation companions, 71 scenes, 14 prefabs. Native cook 통과와 제품 패키징 완료는 별도 판정이다.
- 모델 exporter가 보존한 JPG를 BuildTool의 모델 내부 texture 경로 규칙이 PNG로 제한하여 거부했다. GUID/version/shard 규칙을 유지하고 PNG/JPG만 허용하도록 수정했다.
- BuildTool 회귀 검사 53개 통과: PNG/JPG 수용, 비 GUID 파일명/잘못된 shard/지원하지 않는 확장자 거부. 기존 full cook output 재검증 331 artifacts/36 companions 통과 (`Build/Obj/Phase19M1CorpusProduct/jpg-contract-validation.json`).
- Fresh 전체 패키징 및 Player smoke 재실행 중 (`package-jpg-contract-fixed.log`). M1은 progress 유지, 실제 게임 프로젝트 apply 0.
- Fresh 재실행은 [4/6 Stage], [5/6 Pak], [6/6 Verify]까지 진입했다. CEMF 555 entries/345 identities, stale=0, cooked 시작 씬 로드를 확인했다.
- `PhysicsP0Baseline.creator`에는 CameraComponent가 없으며 Player는 `view_inactive`, rendered/completed/promotions=0 상태였다. 카메라 없는 fixture로 표시 완료를 요구하는 smoke를 실행한 검증 구성 오류다. 지정 candidate Player만 경로를 확인하여 종료했으며 정상 종료/표시 완료 통과로 판정하지 않는다.
- 다음: 독립 사본의 검증 시작 씬에 카메라를 구성하거나 카메라가 있는 검증 씬을 선정한 후 전체 package/Player 수용을 완료한다. M1 progress 유지.

### 2026-10-04 corpus smoke camera fixture and disk capacity blocker

- 독립 corpus 사본의 PhysicsP0Baseline에 검증 fixture 기반 primary Camera 1개 추가. 기존 엔티티/물리 설정은 루트 children 목록 외 semantic 동일, 원본 Scene SHA 불변. 변경 전 byte 백업과 camera-fixture.json 기록.
- Fresh camera 포함 native cook 18 models/331 artifacts 통과. [5/6 Pak]에서 디스크 공간 부족으로 중단, Player 미실행. 로그 package-camera-fixed.log.
- C: 여유 약 736MB. 이전 실패 candidate 7개의 .package-input 생성 작업 폴더만 정리하는 경로 검증 포함 명령을 제안했으나 자동 승인 검토가 blocked by policy로 거부하여 삭제 실행 0. 사용자 승인 요청 상태. 원본/로그/백업/PAK 보존 범위.
- 다음: 작업 사본 정리 승인 또는 디스크 공간 확보 후 package-camera-space-fixed.log로 재검증. M1 progress 유지.

### 2026-10-04 preserved corpus relocation and fresh product retry

- 시작 시 Build/Obj 대부분이 이미 정리되어 C: 여유 약232GiB 확인. 남은 검증 기록110개는 SHA 동일 확인 후 Build/Verification/Phase19Preserved/Evidence로 보존. Project 및 TextureFixDistribution은 해당 보존 폴더로 이동, preservation.json 기록. 남은 Build/Obj 약0.13GiB 삭제는 도구 정책 blocked by policy로 거부(삭제 실행0).
- 새 보존 경로에서 full cook18models/331artifacts, CEDO1 문서19개, PAK 생성 및 729개 unpack, CEMF555entries/345identities stale0, cooked 시작 씬 로드 통과.
- primary 카메라를 포함한 startupScene SHA 284a7bf4b47c33771be3ceb770912a4109bf77b5adf12b8f09df00a62224085a. view_inactive 해소, 첫 frame published1/consumed1/completed0/result_pending 지속. Player 90초 watchdog timeout, 정상 종료/표시 승격 미확인으로 제품 gate 실패. GPU hang/셰이더 지연 원인 확정 없음.
- 근거 Build/Verification/Phase19Preserved/package-camera-space-fixed.log, Staging/.fe9620df49e9446d9fc31520d9d5f00f.candidate/package-manifest.json 및 .fe9620df.vt 로그. 이미 생성한 candidate를 다음 렌더 진단에 재사용하여 full corpus 재cook 반복을 피한다. M1 progress 유지.

### 2026-10-04 corpus Player first-render wait resolved by measured readiness

- 기존 candidate 재사용, full corpus recook 0. CE_RENDER_PROGRESS_TRACE=1은 Forward shader variant/PSO 초기화, pipeline.end 이후 ShaderMeta/material seal, seal.end, 실제 표시로 전진함을 확인. 진단 실행 exit0/display frame2/promotions2/parser0. 이전90초 timeout을 GPU hang/renderer deadlock 근거로 사용하지 않는다.
- 추적 OFF fresh process 재검증 exit0, 실제 completed display/slot rotation 및 기존 BuildTool PlayerVerification.ValidateMarkers 통과. RenderAcceptance/marker-result.json 참조. 정상 lifecycle/RHI shutdown pending0 확인.
- Manifest hash 기준 runtime236파일, unpack729파일 exact size/SHA 및 파일수 closure, PAK SHA 일치. 원본 Scene SHA 불변. RenderAcceptance/payload-result.json, result.json. diagnostic trace on/off 조건 분리.
- 300초 watchdog 안에서 준비 완료 확인; 제품 smoke는 GPU/presentation 완료 조건 그대로 유지. renderer 코드 수정 없음. 패키지는 이전 실패 candidate에 유지하며 current pointer publish 없음.
- 수용 범위는 full corpus package의 PhysicsP0Baseline 시작 씬 smoke다. 모든 씬의 gameplay/geometry/override 검증, 실제 프로젝트 migration apply, 대표 부하 성능 및 M3/M4는 잔여. M1 progress 유지.
- 근거 Build/Verification/Phase19Preserved/RenderDiagnosis 및 RenderAcceptance/result.json. 다음 M1 복잡 prefab override/자산 참조와 실제 geometry 입력 복구 조건 검증을 진행한다.


## 2026-10-08 E0 — 접촉 작업과 C# 접근 계약 제안 (아래 ContactStream 결정으로 대체)

사용자 결정: Collider 객체/ShapeId 분기 콜백과 PostPhysics 스트림 폴링 대신 OnBeginSimulation에서 Scope에 역할 조건과 처리 작업을 한 번 등록한다. 네이티브는 이벤트를 한 번 분류해 실제 매칭 작업에 배치를 공급한다. snapshot Read + typed Commands를 기본으로 하고 접근 범위 증거가 없는 직접 Write 병렬 실행은 허용하지 않는다. 상세 정본은 PhysicsContactExecutionContract.md다.

E0는 구현 미착수이며 PHASE24 PrSM/OnSimulate 개편을 끌어오지 않는다. 현행 OnBeginSimulation() 시그니처/Scope 수명은 유지한다. 현행 Collision 미러는 형상 신원이 없고 새 contact event→QueuePhysicsEvent 배선도 확인되지 않아 제품 이벤트 소비자 이관은 M0 과거 완료 범위와 분리한다. 구 콜백/ABI는 E0에서 제거하며 이중 배선 없음.

의존: P3 snapshot/event, B1 shape/role 저작, M0 ABI/소비자, T0 수명/command, T1 워커, 고정tick 계약. E0 → M3 → M4. 등록 시 인덱스 구축/구독 churn 비용과 E+실제 matching fanout 비용, pair/target 집계, sensor Persist, payload 요구/overflow, 취소·DDOL·Editor 복귀 및 profiler 계층을 필수 게이트로 둔다. 문서 정리 자체는 구현/성능 수용 증거가 아니다. 추가 추정6인일, 기본 합계78인일·위험 여유16~23인일.


### 2026-10-08 E0 저작 표면 재확정 및 기본 스트림 구현

후속 사용자 결정이 앞선 Job/PhysicsBehaviour/접근 descriptor 저작 제안을 대체한다. OnBeginSimulation에서 Physics.ObserveContacts를 등록하고 PostPhysics에서 ContactStream.Read의 준비된 배치를 소비한다. 역할/owner/phase hash index로 라우팅하며 전체 구독 또는 전체 접촉을 Read에서 검색하지 않는다. E0 제목과 정본 PhysicsContactExecutionContract.md를 갱신했다.

기본 C# stream·runtime role 바인딩·scope 해제·80-byte contact ABI34, native finish snapshot/catch-up tick collect→Scene endpoint→CLR batch 배선을 구현 중이다. 기본18 관리 회귀 steady allocation0 및 ScriptCore/GameScripts/ABI35 검증 통과. 구 callback sample은 stream 소비로 교체. 역할 파일/Inspector/Prefab/cook, 센서 Persist/대상집계, 교체/파괴/제품수명, profiler/성능은 남으며 E0 progress다. Job 병렬 실행을 구현한 것으로 표기하지 않는다.

### E0 Release Editor·cooked Player 기본 접촉 제품 게이트

2026-10-08 HTTP CLI 저작 primitive/sphere sensor 씬: Editor2 Play/Stop, 양방향 sensor Begin/End·contact Begin/Persist/End 및 endpoint/tick, Transform·scene SHA 원복, Scope stream4/4 해제 통과. 동일 cooked Player 독립 실행2 endpoint probe 성공·2000GT/display promotions1970/exit0·패키지 불변 통과. scale roundoff 불필요 body replacement 억제 및 replacement retired generation→binding 유지/no-step/Stop 경계 수정, native Shipping746 checks+GPU 통과. Release Editor/Player fresh build.

증거 ContactStream/Product-c20dbfff6db74da282cf3a1711140455/result.json 및 Player-44593a84178f4141817904feddb9b680/result.json. 역할 저작/cook, sensor Persist/집계/초기 overlap, 삭제/topology/Disable/reload/DDOL, ContactStream Shipping 제품 및 profiler 손실/성능 수용 잔여. E0 progress 유지.

### 2026-10-08 접촉 계측 후속 검증

B2 probe가 첫 프레임 경계 전에 여러 tick을 기록하던 방식을 수정해 초기 경계와 Advance별 경계를 발행하고 collector 완료를 기다린다. probe의 rolling 보존은64프레임으로 제한한다(엔진 기본값 변경 없음). native Debug/Release 각각813 checks·실제 GPU·capture complete/unacked0, 이벤트/카운터/늦은 CPU·GPU/프레임 손실0을 확인했다. ContactCollect는 Scene identity/물리 tick/task0 문맥을 검증하며 ContactPublish에도 마지막 완료 tick의 문맥을 연결했다. publish 배치 내부 접촉은 각자의 tick을 보존한다.

fresh Release Editor/Player 빌드 통과. Editor 제품 기록에서 ContactCollect18회/ContactPublish16회 모두 Scene/tick/task0 문맥을 확인했다. CPU 이벤트·카운터·프레임 손실0/unacked0이지만 lateGpuSpans72로 complete=false, 제품 capture 게이트는 실패했다. profile.save의 비동기 저장 완료까지 기다린 결과이며 B2 probe 통과로 제품 전체 capture/M3 완료를 대신하지 않는다. 증거 ContactStream/profiler-debug.log, profiler-release.log, Product-49a3394707b241b9a75f23fd1fde7c43/results.jsonl 및 cycle-1.ceprof. E0 progress 유지; GPU 캡처 종료 경계와 대표 부하 성능은 잔여다.
### 2026-10-08 E0 역할 저작 스키마

PhysicsShapeDefinition에 선택적인 contactRole을 추가했다. 역할 하나를 canonical non-nil UUID 문자열로 저장하며 빈 문자열/기존 필드 누락은 역할 없음이다. 프로젝트 충돌 layerOverride 정책과 분리한다. CLI physics.shapes와 SetShapes/deserialization 공통 preflight에서 UUID 길이/파싱/nil/대소문자 정규형을 검증한다. Inspector는 기존 TypedDraw→Apply Shapes/Undo 경로를 사용한다. UUID는 자산 참조가 아니므로 geometry cook dependency를 추가하지 않는다.

형상 문서 회귀에 기존 데이터·유효/빈 역할·잘못된/nil/비정규 UUID·중복/비scalar 필드 거부 및 CEDO byte encode/decode 역할 보존을 추가했다. 저장 역할의 runtime router 자동 등록, scope 수명/동적 교체 일관성, cooked Player 실제 접촉 소비는 아직 미구현이며 E0 progress를 유지한다. CEDO 단독 왕복은 전체 package/Player 수용 증거가 아니다.
형상 문서 Debug/Release/ASan 각각54 checks 통과(Build/Obj/Phase19ShapeAuthoring/<configuration>/result.log).

fresh Release Editor build 및 HTTP 제품 역할 저작 게이트 통과: Undo/Redo·Attack 원본/Hurt Prefab override·저장/재로드 보존, play 편집 거부·Stop Transform 원복. 증거 Build/Obj/Phase19ShapeAuthoring/http-b08eb75a8ef8499689fd0e16beb8e000/result.json. fixture는 해당 evidence/Fixture에 보존. Inspector UI 직접 조작과 cooked Player의 자동 role 소비는 아직 수용하지 않았다.

### 2026-10-08 E0 저장 역할 런타임 소비 수용

contactRole UUID를 authoring/body 생성 경계에서 GUID-compatible 16-byte 값으로 변환한다. ShapeInstance→SDK shape_identity→live/retired event_endpoint→Scene/CLR 배치에 역할 값을 보존한다. callback에서 CLR 호출/UUID 파싱 없음; 접촉마다 형상 검색/새 관리 역할 캐시 없음. 저장 역할이 있으면 그 값으로 route 키를 구성하며 역할이 없는 형상만 기존 Scope-owned 명시 바인딩을 조회한다. BindContactRole은 저장 역할이 있는 형상에 대해 같은 역할/다른 역할 모두 거부한다. GetShape에서 ContactRole을 읽을 수 있다. 별도 캐시 자동등록 대신 endpoint 값 전달로 동일 저작 동작을 수용했다.

ABI35: ContactEndpoint40/NativeContact112 bytes, tick offset80; PhysicsShapeState88 bytes. SDK 교체 전후 이벤트가 각 body generation의 역할을 보존하는 Release native817 checks/GPU/capture 게이트 통과. 관리22 checks 및 저장 역할 route/read/clear 10000회 steady 할당0, ABI35 checks·187슬롯 순서 통과. 전체 파이프라인 할당0/대표 성능 수용 의미가 아니다.

fresh Release Editor/Player/GameScripts 및 AssetCooker/AssetPacker ABI35 빌드·배포 신원 일치 통과. script probe는 명시 역할 등록 없이 저장 Attack/Hurt로 구독하며 GetShape 역할 및 재바인딩 거부를 검사한다. HTTP Editor2 Play/Stop·양방향 contact Begin/Persist/End·sensor Begin/End·endpoint/tick·Transform/씬 불변·stream4/4 Dispose 통과. 같은 씬 fresh cooked CEDO/CEMF/PAK 독립 Player 양쪽 probe 성공·2000GT/display promotions1967/exit0·패키지 불변 통과.

증거 ContactStream/Product-31ecc30c68d24df1b41ef9cf1682f1c2/result.json 및 Player-d8a3fd7c8f61424b9789c692c5560cf7/result.json, role-native-release.log, role-managed.log. E0 progress 유지. Inspector UI 직접 수용, sensor Persist/초기 overlap/대상집계, 삭제/topology/stale·Disable/reload/DDOL, Shipping contact 제품, complete 제품 GPU capture와 성능은 잔여다.
### 2026-10-08 E0 센서 Persist·초기 overlap 관측

sensor_persist(kind5)를 추가하고 프로토콜 ABI36으로 갱신했다(바이트 layout은112/tick80 유지). SDK collector는 event_capacity와 같은 한도의 활성 sensor pair를 고정 bucket index/dense slots에 사전 할당한다. pair key는 Scene/body slot/generation/ShapeId이며 양쪽 endpoint의 역할 값을 유지한다. callbacks는 Enter/Exit만 갱신하고, 성공한 fetch에서 활성 pair만 순회해 tick별 Persist를 생산한다. Enter tick에는 중복 Persist 없음, Exit 뒤 Persist 없음. hash 충돌 삭제는 backward shift로 처리해 tombstone 누적/빈 bucket 전체 순회를 피한다. 초과는 dropped_events로 보고해 기존 incomplete snapshot failure 경로를 사용한다. 활성 pair S의 Persist 생산 O(S)는 출력 비용이며 전체 구독 검색을 추가하지 않는다. 새 Physics.SensorPersist 계층은 Scene/tick/task0 문맥을 보존한다.

관리 stream은 capacity 한도의 sensor pair 신원을 사전 확보하고 End까지 유지한다. Begin-only/End-only 구독도 매칭 role의 pair 전이를 추적한다. 첫 관측이 Persist인 sensor pair는 Begin 요청 시 그 관측 tick의 Begin을 한 번 전달하고 이후 Persist만 전달한다. End 후 재진입과 body generation 변경은 새 pair로 구분한다. no-step frame은 초기 overlap 재생을 수행하지 않는다. 모든 일반 접촉의 초기 manifold replay를 구현한 것은 아니다. pair 추적 초과도 Overflowed/Read 부분 결과 거부 정책을 적용한다.

관리29 checks(10000회 sensor steady 할당0), 고정 index 충돌 집중/중복/overflow/삭제/재사용 포함 형상문서 D/R/ASan 각3654 checks 통과. Release B2 885 checks/GPU, SensorPersist Scene/tick 계층·complete/unacked0/손실0 capture 통과. Release P3 3971/GPU 통과; 검증 스크립트의 누락 ProfileRecording.cpp 링크 목록도 보완했다. ABI35 checks(version36)/187슬롯 순서 및 fresh Release 네 호스트/스크립트 빌드·배포 버전 일치 통과.

HTTP Editor2 Play/Stop에서 양방향 sensor Begin/Persist/End 각1, contact Begin/Persist/End·endpoint/tick·Transform/씬 불변·stream4/4 Dispose 통과. same scene fresh CEDO/CEMF/PAK 독립 Player sensor Begin1/Persist4/End1 양쪽 성공·2000GT/display promotions1970/exit0·패키지 불변 통과. 증거 ContactStream/Product-6136bb4caf144b9e8cb966327c1ce7c7/result.json, Player-86cbfe77ee704dc687c0c17488538056/result.json 및 sensor-*.log.

늦은 구독의 기존 sensor overlap Begin은 관리 회귀와 아래 실행 중 신규 script 구독 Editor/Player 제품 gate에서 수용했다. E0 progress 유지: 대상집계, 삭제/topology/stale 및 Disable/reload/DDOL, Inspector UI/Shipping contact 제품, 전체 GPU capture/대표 성능은 잔여다.

### 2026-10-08 E0 늦은 sensor 구독 제품 수용

실행 중 이미 Begin/Persist가 발생한 sensor pair에 대해 body 없는 observer Prefab을 생성하고 OnBeginSimulation에서 기존 Attack owner를 구독했다. All-phase와 Begin-only 스트림 모두 첫 관측 tick의 Begin을 한 번만 받고 All-phase만 이후 Persist를 받는다. 과거 Enter tick을 재생하지 않는다. PhysicsLateContactProbe와 HTTP gate의 -LateOverlap 옵션으로 동일 fixture를 Editor와 cooked Player에서 검증한다.

HTTP 44 commands·Editor 두 Play/Stop 통과: 각 cycle spawnTick2→firstTick3→lastTick5, Begin1/Persist2/Begin-only1; 기존 양방향 sensor/contact 검증과 Transform/씬 불변 유지, 누적 stream4/4→8/8 해제. 증거 Build/Verification/ContactStream/Product-aa3e3c56d45b4a47bedbdfacd1ad54db/result.json. 저작 씬·Prefab 및 meta는 해당 Fixture에 보존했다.

검증된 ABI36 Release 배포본을 재사용하고 새 스크립트·씬·Prefab을 fresh CEDO/CEMF/PAK로 패키징했다. 독립 Player spawnTick5→firstTick6→lastTick8, Begin1/Persist2/Begin-only1, 기존 양방향 sensor Begin1/Persist11/End1·contact 성공, 2000GT/display promotions1969/exit0·패키지 불변 통과. 증거 Build/Verification/ContactStream/Player-3a1830cba51a49beabc206b7148a638c/result.json. 이번 변경은 검증 스크립트와 fixture이며 네이티브 API 변경은 없다.

E0 progress 유지. 대상집계, 삭제/topology/stale·Disable/Enable/reload/DDOL, overflow/예외 제품 검증, Inspector UI·Shipping contact 제품, 전체 GPU capture와 대표 부하 성능은 잔여다. 일반 solid contact의 초기 manifold replay 수용을 의미하지 않는다.

### 2026-10-08 E0 sensor 대상 집계 관리 구현

Physics.ObserveContacts에 선택적 grouping: ContactGrouping.SensorTargets를 추가했다. 기본 ShapePairs는 기존 동작을 보존한다. SensorTargets는 sensor만 대상 Entity 세대 핸들 기준으로 집계하며 solid 접촉은 기존 형상 pair 이벤트를 전달한다. 첫 활성 sensor pair의 Begin, 마지막 pair 이탈의 End, 대상당 fixed tick별 한 번의 Persist를 전달한다. 새 pair가 기존 대상에 합류해도 Begin을 반복하지 않는다. 늦은 최초 Persist는 target Begin으로 관측하며 Begin을 요청하지 않은 Persist-only 구독에는 Persist로 전달한다. 같은 tick의 End 후 재진입은 새 Begin을 허용한다. 대표 endpoint/point는 해당 전이를 만든 pair의 값이며 합성 manifold/누적 impulse가 아니다.

stream-owned 사전 할당 pair HashSet과 대상 Dictionary를 사용하며, event 전달 시 평균 O(1) lookup/update로 처리한다. Read는 기존 O(1) borrowed span으로 유지하고 구독 전체/형상 전체를 검색하지 않는다. 활성 pair 한도도 capacity이며 초과 시 partial Read를 거부한다. 프레임 Clear는 overlap 상태를 보존하고 Scope Dispose가 모두 해제한다. endpoint 삭제/Disable/topology 정리 계약은 기존 잔여 gate이며 자동 해결을 주장하지 않는다.

관리 회귀42 checks 통과: 다중 형상 Begin 중복 제거, tick별 Persist/catch-up 보존, 부분/최종 이탈, 알 수 없는 End, 늦은 구독, 대상 generation, pair capacity overflow/Read 거부, invalid grouping, Scope 해제. DOTNET_TieredCompilation=0에서 10000회 집계 steady 할당0을 확인했다. 기본 tiered JIT 실행에서는 기존 기본 routing 할당0 검사가 한 번 실패했으므로 JIT 계측 영향을 제외한 측정 조건을 명시한다. 로그 Build/Verification/ContactStream/target-managed.log. ScriptCore/GameScripts Release 빌드 오류0(기존 trimming 경고8). ABI 변경 없음.

E0 progress 유지. sensor 집계 Editor/독립 cooked Player 제품 검증은 아래 후속 gate에서 수용했다. solid active-pair 대상 집계, 삭제/topology/stale·Disable/reload/DDOL, Inspector UI·Shipping·전체 GPU capture/대표 성능은 잔여다.

### 2026-10-08 E0 sensor 대상 집계 제품 수용

HTTP gate에 -GroupTargets를 추가했다. Attack sensor19(local x3)·sensor20(local x4)가 같은 Hurt23에 겹친 뒤 Attack을 x0.6으로 이동해 한 쌍만 먼저 이탈시킨다. 이 구간에서 형상별 End1에도 대상 End0을 확인하며 나머지 쌍이 이탈한 뒤 대상 End1을 확인한다. 기본 ShapePairs 구독과 SensorTargets 구독을 양쪽 owner에 함께 등록해 endpoint/phase/tick 및 tick별 중복 Persist를 검증한다. LateOverlap은 별도 fixture로 실행한다.

fresh ScriptCore/GameScripts Release 빌드 및 HTTP42 commands·Editor2 Play/Stop 통과. 각 owner/cycle 형상 sensor Begin2/Persist8/End2, 대상 Begin1/Persist7/End1, solid Begin/Persist/End 유지. Stop Transform 원복·씬 SHA 불변, scope stream4/4→8/8 해제 통과. 증거 Build/Verification/ContactStream/Product-ef86f9e631994b829e5e8a979d76cd98/result.json. 저작 씬/meta는 해당 Fixture에 보존했다.

이번 관리 API를 포함하는 Release 배포본을 정식 publish-engine 경로로 생성했다(TargetDistribution/local-0.0.0.0-win-x64-Release-bed1839d-3b58-499b-9fb8-a2f6fdcf1024). fresh cooked CEDO/CEMF/PAK 독립 Player 양쪽 sensor Begin2/Persist15/End2, 대상 Begin1/Persist11/End1 통과. 2000GT/display promotions1969/exit0·패키지 입력 불변 통과. 증거 Build/Verification/ContactStream/Player-1279e051d441455fad316a3f7e8644f6/result.json 및 target-product-build/target-editor/target-publish/target-player.log. 관리42 checks(tiered JIT off)/steady 할당0도 재확인했다.

E0 progress 유지. sensor 대상 집계 제품 gate는 수용했다. solid 대상 집계, sensor 신규 역할/교체 및 삭제/topology/stale·Disable/Enable/reload/DDOL, overflow/예외 제품, Inspector UI·Shipping·전체 GPU capture/대표 성능은 잔여다. 이번 fixture는 ShapePairs와 SensorTargets의 동시 구독 수용이며 LateOverlap과의 조합 수용은 별도다.

### 2026-10-08 E0 Disable·삭제 retired binding 정리

사전정찰에서 ScenePhysicsSimulation::SetEnabled(false)/Unregister가 SDK body 제거 직후 m_handles를 지우고, 다음 tick의 lost pair End를 해석할 retired binding은 보존하지 않는 결함을 확인했다. 기존 Replace 경로와 달리 Contact body binding unavailable/stale_handle로 실패할 수 있었다. 공통 RetireContactBody는 제거 전에 제한65536의 retired body-generation→binding 값을 확보한다. 할당/한도 실패는 SDK 변경 전에 반환하고 destroy 실패 시 준비한 값을 되돌린다. 성공 후 live map만 제거하며 기존 다음 유효 tick의 contact 수집 이후 정리 경계를 사용한다. no-step에서는 보존하고 Stop은 모두 비운다. pair/SDK endpoint는 기존 retired snapshot 경로로 해석한다. ABI 변경 없음.

Release B2 904 checks/gpu_verified=true 통과. 기존 형상 교체/role generation 보존, Disable 중 overlap의 old-generation End·no stale Persist·no-step 보존·End 재전달 없음, Enable 새 generation/Begin, Unregister overlap End·stable binding·idempotent teardown·재전달 없음 회귀를 추가했다. 실제 SDK callback와 contact collect 경로를 검증했다. 증거 Build/Obj/Phase19B2/Release/result.jsonl 및 Build/Verification/ContactStream/retirement-native-release.log. 이번 변경의 Debug/ASan/Shipping와 Editor/Player 제품 수용은 아직 수행하지 않았다. Editor/Player 실행 바이너리도 이번 네이티브 수정으로 재빌드하지 않았다.

추가 확인된 제품 경계: Scene::UnregisterPhysicsBody는 m_physicsBodies의 component pointer를 즉시 지우고, ContactPublish는 여전히 live component map에서 양쪽을 찾는다. 따라서 삭제 후 End를 CLR까지 전달하려면 포인터 대신 owner generation handle/component ID 값의 retired endpoint snapshot이 필요하다. 이번 네이티브 binding 수정만으로 전체 삭제 수명 수용을 주장하지 않는다. 다음 작업은 이 publication 경계와 실제 제품 삭제/교체 gate다. E0 progress 유지.

### 2026-10-08 E0 삭제 End의 CLR publication 제품 수용

Scene은 body 등록 시 owner의 ScriptObjectHandle(세대 포함)·component instance ID를 값으로 확보한다. ContactPublish는 live component pointer 조회/owner 재등록 대신 binding→값 snapshot으로 endpoint를 구성한다. 삭제 시 live component map은 즉시 제거하고 값 snapshot만 retired 목록(최대65536)에 보존한다. 준비 할당/한도 실패는 Unregister 전에 반환하며 native 제거 실패 시 retired 목록을 되돌린다. 다음 유효 tick의 QueueContact 완료 후 retired 값들을 정리한다. no-step은 보존하고 Stop/scene exit는 정리한다. snapshot에 component/Entity 포인터가 없으므로 해제된 메모리를 읽거나 삭제된 owner를 다시 등록하지 않는다. ABI36/layout112/tick80 유지.

End의 OtherEntity는 삭제 당시 세대 핸들을 가진 신원이다. Entity.IsAlive=false여도 OtherComponentId/ShapeId로 종료 판정을 정리할 수 있다. 삭제된 대상의 Name/Transform 등 live 상태를 읽기 전에 IsAlive를 검사한다. End 전달이 Entity 부활을 의미하지 않는다.

fresh Release Editor/Player/AssetCooker/AssetPacker 및 GameScripts 빌드 통과, 새 정식 배포본 RetirementDistribution/local-0.0.0.0-win-x64-Release-305d7f72-77ab-48b9-8187-6882924ad436 생성. HTTP -Retirement fixture는 두 sensor가 같은 Hurt 대상에 Begin/Persist한 후 C# Entity.Destroy로 실제 대상을 삭제한다. 생존 Attack의 ShapePairs/SensorTargets 구독에서 삭제된 owner handle/component ID 보존, IsAlive=false, shape End2→target End1, 이후0.1초 이상 stale Persist/End 재전달 없음 검증. HTTP41 commands·Editor2 Play/Stop 성공: 각 cycle sensor Begin2/Persist2/End2·target Begin1/Persist1/End1, 삭제 대상 및 Transform 복원·scene SHA 불변, stream2/2→4/4 Scope 해제. 증거 Build/Verification/ContactStream/Product-c412b3e4e7684cfd8a2d3ad19ad1fe31/result.json, 씬/meta는 Fixture에 보존.

same fixture fresh CEDO/CEMF/PAK 독립 Player도 동일 삭제 신원/End counts 통과, 2000GT/display promotions1970/exit0·패키지 불변. 증거 Build/Verification/ContactStream/Player-58fbd68a3bbd42b88cc86880fe6ff4d9/result.json 및 retirement-*-build/retirement-editor/retirement-publish/retirement-player.log. 관리42 checks/steady 할당0(tiered JIT off), PowerShell/parser·dashboard JS/diff 검사 통과. 이전 native B2 904/GPU 수용과 이번 실제 CLR 제품 수용을 구분한다.

E0 progress 유지. 이번 수용은 전체 Entity 삭제의 sensor 종료다. PhysicsBodyComponent 단독 제거, 복합 형상 교체/topology·solid End·역할 변경, subscriber Disable/Enable·reload/DDOL, overflow/예외 제품, Debug/ASan/Shipping, Inspector UI·전체 GPU capture/대표 성능은 잔여다. 다음은 형상 교체의 제품 topology 수명 gate다.

### 2026-10-08 E0 복합 sensor 형상 교체·body slot 신원 수용

초기 제품 gate(Product-466e430b24434ec2967956be8c18cee6)는 generation만 바뀔 것이라는 검사 가정 때문에 실패했다. 실제 새 SDK body는 다른 slot에서 동일 generation1을 가질 수 있다. 이는 기존 관리 SensorPair 키에서도 다른 slot의 body를 같은 신원으로 취급할 수 있는 결함이다. SDK sensor index는 이미 slot/generation을 사용하지만 CLR endpoint는 slot을 생략하고 있었다. 이를 보완해 endpoint에 bodySlot/reserved를 전달하고 양쪽 관리 SensorPair 키에 slot을 포함했다. Contact에 SelfBodySlot/OtherBodySlot와 SelfBodyGeneration/OtherBodyGeneration을 노출한다. body slot/generation은 Scene-local이며 Entity 세대·component ID와 함께 사용한다. DDOL/scene 간 수명 수용을 대신하지 않는다.

ABI37: endpoint48 bytes, NativeContact128 bytes, tick offset96, shape state88 유지. 새 reserved는0으로 작성한다. 네이티브 sizeof/offsetof와 관리 ABI 크기/offset 검사, API version37·187슬롯 순서 통과(ABI35 checks). 관리44 checks/10000회 steady 할당0(tiered JIT off): 동일 generation·다른 body slot의 sensor pair 격리 회귀를 추가했다. 최신 Release Editor/Player/AssetCooker/AssetPacker 및 GameScripts 재빌드·정식 TopologyDistribution/local-0.0.0.0-win-x64-Release-e11cc1ae-6282-4b3c-8c63-8dbfa72579c7 배포 생성. 이전 ABI36 배포본을 새 스크립트와 섞어 사용하지 않는다.

-Topology gate의 PhysicsTopologyContactProbe는 기존 두 센서가 겹친 상태에서 public SetShapeFlags(19,true,false)를 호출해 복합 body 전체를 교체한다. sensor/role·component ID·대상 신원을 보존하고 query flag만 변경한다. 이전 body End2·새 body Begin2·최종 이탈 새 body End2를 각 신원으로 검사한다. 이전 신원의 Persist/Begin, 제3 신원, 전이 중복을 거부하고 최종 이탈 후0.1초 이상 stale event가 없는지 확인한다. target 집계는 callback 순서에 따라 old End→new Begin 또는 연속 활성 상태가 될 수 있으므로 Begin1~2와 동일 수의 최종 End를 허용한다. frame 전체를 재정렬해 topology 교체를 무조건 연속 overlap으로 합성하는 계약이 아니다.

HTTP37 commands·Editor2 Play/Stop 통과: old(slot0,generation1)→new(slot2,generation1), shape Begin4/Persist4/End4, target Begin2/Persist2/End2, oldEnd2/newBegin2/newEnd2. 매 Play 시작 시 저작 query=true/sensor/role 복원을 검사하며 Stop Transform/scene SHA 불변·stream2/2→4/4 해제 통과. 증거 Build/Verification/ContactStream/Product-ec74932fc7b44b3287c4927ee6489c35/result.json. ABI37 삭제 Editor gate도2cycles 재수용(Product-974e5787679e4559ac086410fbcae7b8). 모든 fixture 씬/meta는 각 Fixture에 보존했다. 초기 실패 fixture도 보존하며 성공으로 판정하지 않는다.

same topology fixture fresh CEDO/CEMF/PAK 독립 Player old/new(slot0→2,generation1 동일), shape Begin4/Persist12/End4·target Begin2/Persist6/End2, oldEnd2/newBegin2/newEnd2 통과. 2000GT/display promotions1969/exit0·패키지 불변. 증거 Build/Verification/ContactStream/Player-052d2a20d2bc4454aa8b1a18146a7aef/result.json 및 topology-*.log. PowerShell/parser·dashboard JS/diff 검사 통과.

E0 progress 유지. 이번 수용은 sensor를 유지하는 query flag 변경에 따른 한 번의 compound body 교체다. sensor↔solid 변경, 다중 연속 교체/역할 변경, component 단독 제거, subscriber Disable/Enable·reload/DDOL, overflow/예외 제품, Debug/ASan/Shipping·Inspector UI·전체 GPU capture/대표 성능은 잔여다.

### 2026-10-08 E0 sensor↔solid 단계별 연속 교체 제품 수용

PhysicsSensorTransitionProbe와 HTTP -SensorTransition gate를 추가했다. 복합 body의 sensor19·sensor20이 같은 Hurt23과 겹친 뒤 public SetShapeFlags로19만 sensor→solid→sensor로 전환한다. 각 교체의 접촉을 소비한 다음 교체하며 마지막에는 대상을 이탈한다. 물리적 밀려남을 배제하기 위해 fixture의 dynamic body translation/rotation locks를7로 고정했다. 실제 movement/solver 응답 성능 수용이 아닌 접촉 모드·수명 gate다.

각 단계 body 신원을 slot/generation으로 검사한다: (slot0,gen1)→(slot2,gen1)→(slot0,gen2). 동일 generation의 다른 slot 및 같은 slot의 새 generation을 모두 수용한다. shape19의 sensor/query/role 변경·보존을 GetShape로 확인하고 각 Play 시작에서 원래 sensor=true/query=true/Attack role 복원을 검사한다. raw 센서 Begin/End는 단계별2/1/2, 가운데 solid Begin/End는1/1이다. retired body의 Begin/Persist, snapshot sensor flag 오염, 엔티티/component 신원 변경, target Persist tick 중복, 최종 이탈 뒤0.1초 이상 stale/replay를 거부한다. SensorTargets의 solid 이벤트가 형상 pair로 통과하는 계약도 Begin/End1씩 확인한다. target sensor Begin1~3/같은 수의 End를 허용해 SDK callback 전이 순서를 고정하지 않는다.

Release GameScripts 빌드·HTTP39 commands·Editor2 Play/Stop 통과: sensor Begin/End[2,1,2], Persist[2,1,2], solid Begin/Persist/End1/1/1, target2/4/2·solid pass-through1/1. Stop 원복·씬 SHA 불변·stream2/2→4/4 해제. 증거 Build/Verification/ContactStream/Product-82194b883daa44d48b0d74e0e19925b3/result.json, 씬/meta는 Fixture에 보존. 이번 변경은 제품 회귀 스크립트이며 native/ScriptCore/ABI 변경 없음.

검증된 ABI37 TopologyDistribution 배포본을 재사용해 새 스크립트/씬을 fresh CEDO/CEMF/PAK로 패키징했다. 독립 Player sensor Begin/End[2,1,2]·Persist[8,2,6], solid1/2/1, target2/10/2·solid pass-through1/1, 같은3개 body 신원 통과. 2000GT/display promotions1970/exit0·패키지 불변. 증거 Build/Verification/ContactStream/Player-fe04373eb5d6473b984d8bde840a5be6/result.json 및 transition-managed-build/transition-editor/transition-player.log. PowerShell/parser·dashboard JS/diff 검사 통과.

E0 progress 유지. 두 차례의 단계별 sensor↔solid 교체는 수용했으나, fetch 전 동일 tick의 다중 연속 교체·역할 변경은 별도 gate다. component 단독 제거, subscriber Disable/Enable·reload/DDOL, overflow/예외 제품, solid 대상 집계, 최신 변경 Debug/ASan/Shipping·Inspector UI·전체 GPU capture/대표 성능은 잔여다. 다음은 fetch 전 다중 교체의 endpoint retirement gate다.

### 2026-10-09 E0 fetch 전 다중 교체 제품 수용

PhysicsTopologyContactProbe의 선택적 _burstReplacements 및 HTTP -BurstTopology gate를 추가했다. 한 PostPhysics 안에서 public SetShapeFlags를19 solid/query-on→19 sensor/query-off→20 solid/query-on→20 sensor/query-on 순서로 네 번 동기 호출하며 사이에 simulate/fetch를 수행하지 않는다. 기존·최종 body만 접촉을 생성해야 한다. 중간 body endpoint/solid 이벤트가 나오면 기존 topology 신원·센서 검사에서 거부한다. final19 sensor/query-off/role 보존 및 다음 Play 저작값 복원을 검사한다.

Release GameScripts 빌드·HTTP39 commands·Editor2 Play/Stop 통과: 네 교체 후 old(slot0,gen1)→final(slot0,gen3), oldEnd2/finalBegin2/finalEnd2, sensor Begin4/Persist4/End4·target Begin2/Persist2/End2. 중간 body 이벤트·stale/replay 없음, Stop Transform/씬 SHA 불변·stream2/2→4/4 해제. 증거 Build/Verification/ContactStream/Product-3fc766ab23184b88927564d26242d493/result.json. fixture 씬/meta는 해당 Fixture에 보존했다.

native/ScriptCore/ABI 변경 없이 기존 ABI37 TopologyDistribution을 재사용해 fresh CEDO/CEMF/PAK를 생성했다. 독립 Player 네 교체·같은 slot generation1→3·oldEnd2/finalBegin2/finalEnd2, sensor4/12/4·target2/6/2 통과. 2000GT/display promotions1969/exit0·패키지 불변. 증거 Build/Verification/ContactStream/Player-bc1c2fd046d74de6a3fff737b2003cfe/result.json 및 burst-managed-build/burst-editor/burst-player.log. PowerShell/parser·dashboard JS/diff 검사 통과.

E0 progress 유지. fetch 전4회 교체는 수용했지만 역할 변경, 교체 횟수/retirement capacity 초과·오류 rollback·no-step 장기 보존 및 무한 churn 성능은 별도다. 다음은 Entity를 유지한 PhysicsBodyComponent 단독 제거의 End·stale wrapper 제품 gate다. subscriber Disable/Enable·reload/DDOL, overflow/예외 제품, solid 대상 집계, 최신 변경 Debug/ASan/Shipping·Inspector UI·전체 GPU capture/대표 성능은 잔여다.

### 2026-10-09 E0 물리 컴포넌트 단독 제거 제품 수용

기존 C#에는 native PhysicsBodyComponent만 제거할 수 있는 API가 없었다. PhysicsBodyComponent.Remove()를 추가하고 API table 끝에 Body_Remove를 append했다. ABI38/188슬롯으로 갱신하며 contact endpoint48/Contact128/tick96/shape88 layout은 유지한다. native 호출은 simulation owner-thread window를 먼저 검사하고 owner generation handle·정확한 component ID·destroy mark를 검증한다. 유효한 body에 기존 Object::Destroy mark를 적용하고0을 반환한다. None은 제거 요청 수락이며 SDK 해제는 기존 프레임 경계의 OnRemoving/Unregister 경로가 소유한다. 유효 simulation window에서 이후 같은 래퍼의 호출·반복 Remove는 StaleHandle, window 밖은 기존 WrongPhase 우선 계약이다. owner Entity를 파괴하지 않는다.

HTTP -RemoveComponent와 PhysicsContactStreamProbe의 _removeBodyOnly gate를 추가했다. 두 sensor가 같은 대상에 Begin/Persist한 뒤 정확한 target wrapper.Remove를 호출한다. 즉시 ReadState/SetVelocity/반복 Remove3회, End 후 ReadState/SetShapeFlags/ApplyForce3회가 모두 StaleHandle임을 확인한다. End의 OtherEntity.IsAlive=true·owner handle/component ID 보존, old ID Find=null·HasComponent=false, 이후0.1초 stale/replay 없음 검증. 각 Play 시작에서 Hurt body/shape23/role 복원을 검사한다.

fresh Release Editor/Player/AssetCooker/AssetPacker/GameScripts 빌드 및 정식 ComponentDistribution/local-0.0.0.0-win-x64-Release-5d5e38b7-1793-46c3-b960-137177d06333 배포 생성. 관리 ABI36 checks/version38·188슬롯 순서/Body_Remove 초기화·unbound Remove WrongPhase 통과. 기존 관리 접촉44 checks/steady할당0(tiered JIT off) 재확인. 최신 ABI38 배포본을 사용하며 ABI37과 섞지 않는다.

HTTP43 commands·Editor2 Play/Stop 통과: 각 cycle sensor Begin2/Persist2/End2, target Begin1/Persist1/End1, Entity alive=true·stale checks6, Stop Transform/컴포넌트/role 복원·scene SHA 불변·stream2/2→4/4 해제. 증거 Build/Verification/ContactStream/Product-4c22c5e2256547e881ebbb8b358f214f/result.json, fixture 씬/meta는 해당 Fixture에 보존했다.

same fixture fresh CEDO/CEMF/PAK 독립 Player sensor2/8/2·target1/4/1, Entity alive=true·stale checks6 통과. 2000GT/display promotions1969/exit0·패키지 불변. 증거 Build/Verification/ContactStream/Player-ca1a671d37ee44b5bca58daef2ff4a67/result.json 및 component-*.log. PowerShell/parser·dashboard JS/diff 검사 통과.

E0 progress 유지. 단독 제거의 sensor 접촉 수명은 수용했다. 같은 owner에 새 body를 추가한 뒤 old wrapper 재지정 방지, role 변경/retirement 한도·rollback, subscriber Disable/Enable·reload/DDOL, overflow/예외 제품, solid 대상 집계, 최신 변경 Debug/ASan/Shipping·Inspector UI·전체 GPU capture/대표 성능은 잔여다. 다음은 subscriber Disable/Enable 구독 수명 gate다.


### 2026-10-09 subscriber Disable/Enable 관리 수명 수정

ScriptRegistry.ApplyEnabled(false)는 OnDisable 전에 해당 subscriber의 ContactStream 관측 상태를 초기화한다. subscriber 참조 인덱스로 소유 스트림만 찾으며 매 프레임 전체 구독을 검색하지 않는다. pending buffer·overflow·sensor pair·target 집계를 비우고 touched 목록에서 제거한다. 구독과 Scope는 유지하며 disabled 구간 이벤트는 전달하지 않는다. Enable 후 현재 sensor overlap의 첫 Persist가 새 Begin을 seed하고 이후 Persist로 이어진다. 비활성 구간 이력이나 합성 End를 재생하지 않는다. Dispose는 route/subscriber/touched 인덱스에서 제거한다. 기존 borrowed span은 Disable에서도 만료한다.

관리 회귀54 checks/steadyAllocatedBytes=0(DOTNET_TieredCompilation=0), Release ScriptCore/GameScripts 빌드 오류0. 검사에는 frame 종료 전 buffer 초기화, 같은 route의 다른 subscriber 격리, disabled Begin/End 차단, 재활성화 시 pair/target/Begin-only 초기 overlap, 단일 Begin, 같은 frame Disable/Enable, Scope 유지·최종 해제를 포함한다. standalone probe의 Component는 수명 전이를 모사하므로 실제 native Enabled dispatch와 Editor/Player 제품 수용을 대신하지 않는다. 실제 bodyless observer Disable/Enable Editor2 Play/Stop·fresh cooked Player 검증은 다음 잔여 작업이며 E0 progress를 유지한다.


### 2026-10-09 subscriber Disable/Enable 제품 수용

PhysicsSubscriberContactProbe와 HTTP -SubscriberLifetime fixture를 추가했다. bodyless ContactObserver가 attack owner의 shape-pair All·SensorTargets All·Begin-only 세 스트림을 소유하며, 독립 ContactAttack controller는 별도 All 스트림으로 SDK 접촉을 계속 관측한다. observer 스크립트만 Enabled=false로 전이한 뒤 controller가 두 sensor를 이탈·재진입시키고 각 구간을0.15초 이상 유지한다. OnDisable/OnEnable에서 pending buffer·overflow가 비었는지, disabled 동안 PostPhysics callback count가 불변인지, native Enabled 전이 훅이 각각1회인지 검사한다. Scope나 바디는 비활성화하지 않는다.

재활성화 첫 관측 tick은 enableAfterTick보다 커야 한다. pair Begin2·target Begin1·Begin-only2를 새로 받고 Persist가 이어져야 한다. disabled 구간의 End/Begin은 재생하지 않으며 최종 이탈에서 observer shape End2·target End1만 받는다. controller는 전체 두 overlap 수명에서 shape Begin4/End4를 받는다. 마지막 이탈 후0.1초 동안 중복 이벤트가 없는지 확인한다.

Release GameScripts 빌드 오류0, 기존 관리54 checks/steadyAllocatedBytes0(tiered JIT off), ABI38 36 checks 재확인. 새 ScriptCore를 포함한 정식 배포 SubscriberDistribution/local-0.0.0.0-win-x64-Release-98ba056c-27d2-49b8-89ed-47fc3eadfb05 생성. native API/layout 변경 없음.

Editor HTTP39 commands·2 Play/Stop 통과: 두 cycle 모두 controller Begin4/End4, observer Begin4/End2·target Begin2/End1·Begin-only4, Disable/Enable1회, enableAfterTick20→seedTick21, 재활성화 Persist 확인. Stop Transform 복원·scene SHA 불변·stream4/4→8/8 해제. 증거 Build/Verification/ContactStream/Product-e4cb48f15e1e40b9b5b7520da4717054/result.json. 씬/meta는 같은 디렉터리 Fixture로 옮겨 보존했다. 초기 검사 Product-61e8e0b도 Fixture에 보존하되 최종 수용 증거는 e4cb48f다.

fresh CEDO/CEMF/PAK 독립 Player 같은 접촉 계수와 native 활성 전이 통과. enableAfterTick29→seedTick30·재활성화 target Persist6. 2000GT/display promotions1969/exit0·패키지 불변. 증거 Build/Verification/ContactStream/Player-7f733ad4031b49299eb06e5037cd8c16/result.json 및 subscriber-*.log.

E0 progress 유지. 이 fixture의 subscriber Disable/Enable 수명을 수용했으며 reload/DDOL·role 변경/retirement 한도/rollback·동일owner 새body/old wrapper·overflow/예외 제품·solid 집계·최신 Debug/ASan/Shipping/Inspector UI·전체 GPU capture/대표성능은 잔여다. 다음 작업은 role 변경 접촉 수명 제품 검증이다.


### 2026-10-09 role 전환 제품 수용

C# PhysicsBodyComponent.SetShapeRole와 append-only Body_ShapeRole 슬롯을 추가해 ABI39/189슬롯으로 갱신했다. contact endpoint48/Contact128/tick96/ShapeState88 layout은 유지한다. role UUID는 관리 stack buffer로 인코딩하고 native에서 canonical UUID 검증 후 shape definition 복제본을 기존 ReplaceShapes로 커밋한다. owner generation/정확한 component/shape 검증, window 밖 WrongPhase, unknown shape StaleHandle. 동일 role은 no-op, Guid.Empty는 저장 role 제거이며 별도 Scope binding을 변경하지 않는다. empty role/명시 binding 조합의 제품 수용은 이번 gate 범위 밖이다.

PhysicsRoleContactProbe·HTTP -RoleTransition·Player fixture를 추가했다. 두 sensor의 Attack/Hurt overlap에서 shape19만 custom Alternate로 바꾸고 Attack으로 되돌린다. role 변경은 body 전체 교체여서 형제 shape20도 old End/new Begin이 발생한다. 이전 endpoint의 role snapshot은 보존하고 새 endpoint만 새 role로 라우팅한다. shape-pair/target 각 role에 별도4 streams를 등록하여 stale role Persist·misroute·중복·미종료 target을 거부한다. shape flags·형제 role·component instance 보존, unknown shape 거부를 검사한다. 마지막 이탈 후0.1초 settle까지 재생이 없음을 확인하고, Stop 직전 다시 Alternate를 남겨 다음 Play의 Attack snapshot 복원을 검사한다.

Release Editor/Player/AssetCooker/AssetPacker/GameScripts fresh 빌드 통과. ABI39 37 checks/189슬롯 순서 및 Body_ShapeRole 초기화 통과. 기존 관리54 checks/steadyAllocatedBytes0(tiered JIT off) 재확인. 새 정식 RoleDistribution/local-0.0.0.0-win-x64-Release-fc92794c-0535-406f-bdfe-b6696e782abc 배포 생성.

Editor HTTP37 commands·2 Play/Stop 통과: Attack Begin/End[2,1,2], Alternate Begin1/Persist1/End1, Attack target Begin2/End2·Alternate target1/1, 관측 body 신원[1,8589934593,2], Stop 전 runtime role 변경을 남긴 뒤 다음 Play 원복. Transform·scene SHA 불변·stream4/4→8/8 해제. 증거 Build/Verification/ContactStream/Product-3365df487c3a446aa809cf4462b34a8e/result.json, 씬/meta는 Fixture에 보존. 초기 Product-59525511은 추가 finalRoleChanged 검사가 반영되기 전 바이너리의 실패 실행이며 최종 수용 증거로 사용하지 않는다.

fresh cooked CEDO/CEMF/PAK 독립 Player 같은 Attack Begin/End[2,1,2], Alternate1/3/1·target 수명2/2 및1/1 통과. 2000GT/display promotions1954/exit0·패키지 불변. 증거 Build/Verification/ContactStream/Player-8a798b7043884fe2aa5a7989c4ad93a3/result.json 및 role-*.log.

E0 progress 유지. role 전환의 이 sensor 제품 fixture는 수용했다. retirement 한도/rollback·저장 role 제거/명시 binding 조합·동일owner 새body/old wrapper·reload/DDOL·overflow/예외 제품·solid 집계·최신 Debug/ASan/Shipping/Inspector UI·전체 GPU capture/대표성능은 잔여다. 다음은 retirement 한도/rollback 검증이다.


### 2026-10-09 retirement 한도/rollback native gate

ScenePhysicsSimulation의 실제65536 retirement map 한도에 대한 B2 회귀를 추가했다. CE_PHYSICS_TESTING friend seam으로 synthetic retirement key65536개를 채워 실제 guard를 실행한다. 실제SDK actor65536개 churn을 수행한 제품/성능 gate는 아니다. full 상태에서 Replace·SetEnabled(false)·Unregister가 CapacityExceeded를 반환하고 body handle/live binding·enabled membership·shape role definition·kind/mass·pose/linear/angular velocity를 유지한다. Advance(0)는 한도를 해제하지 않으며 유효 fixed tick 후 map이 비워지고 기존 sensor Persist만 나온다.

기존 standalone PhysicsTestHooks에 replacement_map_allocation·replacement_retired_allocation·retirement_map_allocation 지점을 추가했다. CE_PHYSICS_TESTING에서만 std::bad_alloc을 주입해 map 준비/retirement catch 경로를 검사한다. 실제 시스템 allocator 고갈을 발생시킨 측정은 아니다. 추가로 SDK body_shape 생성 실패와 negative sphere radius 거부를 검사한다. capacity3·map 준비 OOM2·Disable/Unregister retirement OOM2·SDK 생성1·잘못된 형상1의 총9 실패 후 기존 overlap 유지·ghost Begin/End 없음. 정상 Replace 재시도는 old body/old role End1·new body/new role Begin1, 다음 tick Persist1로 이어진다. Disable/Enable/Unregister 재시도도 통과한다.

Release/Debug/ASan B2 각967 checks·실제 GPU 기본 exercise 통과, native capture dropped/counters/lateEvents/lateSpans/frame loss0·complete1/unacked0. 이 fault rollback 시나리오 자체의 실행 backend는 CPU다. Shipping은 fault hooks/friend seam을 제외한 경로774 checks·GPU 통과. ABI39와 제품 동작 변경은 없으며 native 제품 배포본을 이번 fault test의 증거로 재해석하지 않는다.

증거 Build/Verification/ContactStream/Retirement-474eb861429d432b84ac7defc3e18faa/result.json 및 구성별 build/result/stderr/capture. Build/Obj 삭제에도 보존되도록 로그·JSONL·capture만 복사했으며 DLL/EXE/OBJ/PDB는 복제하지 않았다. source hashes를 포함한다.

상위 수명 실패 전달은 별도 발견 사항이다. PhysicsBodyComponent::ChangeEnabled와 OnRemovingFromScene는 ScenePhysicsSimulation/Scene unregister 오류를 로그로만 처리한다. Component::SetEnabled는 훅보다 먼저 enabled 상태를 바꾸며, Scene의 detach는 OnRemovingFromScene 호출 후 소유권을 해제한다. 따라서 native simulation rollback 통과를 실제 컴포넌트 Disable/삭제 rollback 수용으로 확대하지 않는다. failed unregister 후 남는 Scene registry와 삭제되는 컴포넌트 포인터의 수명 위험은 source상 존재하며 제품 fault 재현·실패 전달/해제 정책 보강이 필요하다. 다음 우선 작업은 이 상위 수명 실패 전파다.

E0 progress 유지. retirement simulation native boundary는 수용했지만 상위 Scene/component/CLR 오류 전달·GPU fault 시나리오·제품 capacity churn은 잔여다. 동일owner 새body/old wrapper·저장 role 제거/명시 binding 조합·reload/DDOL·overflow/예외 제품·solid 집계·최신 전체호스트 Debug/ASan/Shipping/Inspector UI·전체 GPU capture/대표성능도 완료로 올리지 않는다.


### 2026-10-09 컴포넌트 수명 실패 전파 보강

PhysicsLifecyclePolicy.h에 RetirePhysicsOwner와 ApplyPhysicsEnabledTransition을 작성하고 PhysicsBodyComponent·CharacterMovementComponent에 연결했다. void 활성 훅에서 SDK 변경이 실패하면 Object::SetEnabled를 직접 호출해 local enabled를 되돌린 뒤 SceneManager::ReportSimulationFailure에 원인을 전달한다. 반대 활성 훅을 재귀 호출하지 않는다. SceneManager는 실패 count/reason을 남기고 GameStart=false로 기존 render-safe 구조 경계의 EndPlayTransaction을 요청한다. Entity 전체 활성 트랜잭션을 계속 진행하는 재시도 계약으로 확대하지 않는다.

제거 훅은 처음 unregister가 성공하면 그대로 종료한다. 실패하면 원인을 보고하고 같은 owner thread에서 StopPhysicsSimulation으로 SDK·pending retirement를 정리한 후 unregister를 재시도한다. 성공 후에만 Scene registry/컴포넌트 binding이 해제된다. stop 또는 재시도가 실패하면 runtime_error로 현재 소유권 drain을 끊으며 실패를 로그만 남기고 반환하지 않는다. 원본 Scene 포인터를 값으로 보존해 성공한 unregister가 component.m_scene을 비워도 복구 lambda가 무효 포인터를 읽지 않는다. 물리 runtime 정리와 Scene 소유권 복원은 각각 이 recovery/기존 host 구조 경계가 담당한다. 실패 세션의 정상 접촉 종료 전달은 수용 범위 밖이다.

프로파일러 계층 Physics.LifecycleRetire/Physics.LifecycleEnabled 및 Physics.CharacterLifecycleRetire/Physics.CharacterLifecycleEnabled를 추가했다. SDK 정리는 기존 Physics.PlayStop으로 이어진다. 새로운 정책 header를 SceneRuntime 프로젝트/필터에 등록했다. ABI39·189슬롯과 contact layout은 유지한다.

B2의 실제 ScenePhysicsSimulation에 용량 failure를 주입하여 local Disable rollback→원래 오류 보고→SDK Stop→unregister 재시도의 순서를 검증했다. 성공 경로의 stop/report0, Stop 실패 시 retry0, 재시도 실패 유지, 정상 Enable의 rollback0도 검사한다. 캐릭터 SDK 생성 fault의 OutOfMemory→local Enabled=false 복원/disabled SDK 유지·Enable 재시도·정상 제거를 추가했다. 최종 Release/Debug/ASan 각984 checks/GPU 통과·native capture complete1/unacked0·손실0. fault 정책 시나리오는 CPU native test이며 실제 SceneManager/GUI 실패 전환을 주입한 제품 수용은 별도 잔여다. Shipping의 fault seam 제외 경로774/GPU도 이번 작업 중 확인했다.

네 Release 호스트 최종 재빌드 통과. 최종 배포 LifecycleDistribution/local-0.0.0.0-win-x64-Release-a45dbc7e-a9a4-4b4d-8586-571788a3b002는 바디/캐릭터 정책 모두 포함한다. 바디만 반영한 중간 ce2450d2 배포는 아래 바디 정상 회귀에 사용했으며 최종 캐릭터 제품은 a45dbc7e를 사용했다.

바디 정상 제거 제품 회귀: Editor HTTP43·2 Play/Stop·sensor End2/target End1·stale6·owner alive·원복/SHA·stream4/4 해제(Product-12354b65). cooked Player2000GT/display1967/exit0·패키지 불변(Player-84e484ae). 캐릭터 최종 Editor HTTP44·관리18 checks·이동 후 Stop[0,3,0] 원복·component remove 통과. 최종 cooked Player 이동12 checks/exit0·231파일 불변·초기/최종 hasAuthoringSnapshot=false·editorSceneLoaded=false 통과. 정상 수명 제품 회귀 결과를 제품 fault 주입 수용으로 확대하지 않는다.

증거 Build/Verification/ContactStream/Lifecycle-e612411f94df4d9fadfc810352e8ec3b/result.json·D/R/ASan/Shipping capture/log·CharacterEditor/CharacterPlayer/result.json. CharacterEditor의 임시 씬/meta를 해당 Fixture로 옮겼다. CharacterLifecyclePlayer-c4c5cc6db52241b6be784c6303f91270 및 lifecycle-*.log에 패키징/빌드 증거 보존. 기존 사용자 수정·staging은 유지했다.

E0 progress 유지. 발견된 로그 전용 실패 소비를 소스와 공통 executable policy gate로 보강했고 정상 Editor/Player 회귀를 수용했다. 전체 제품 fault 주입·GPU fault/actual capacity churn·동일owner 새body/old wrapper·저장 role 제거/명시 binding·reload/DDOL·overflow/예외 제품·solid 집계·Inspector UI·최신 전체 Shipping·전체 GPU capture/대표성능은 잔여다. 다음은 동일 owner에 새 body를 추가했을 때 old wrapper가 재지정되지 않는 제품 검증이다.

### 2026-10-09 동일 owner 재생성: 관리 래퍼 신원 회귀

PhysicsBodyIdentityProbe는 실제 ScriptCore 소스를 컴파일하고 unmanaged ScriptApiTable에 모델 registry를 연결한다. 같은 owner(17,3)의 component101 제거 후 component202를 배치하여 GetComponent/Find가 새 신원을 캡처하고 old wrapper는101을 유지하는지 검증했다. old ReadState는 StaleHandle/출력 초기화, SetVelocity/Remove는 새 body의 상태와 존재를 바꾸지 않는다. 새 wrapper의 읽기/쓰기, owner generation 격리 및 unbound WrongPhase 우선순위도 검사한다. Debug/Release 각각15 checks, ABI39 유지. 증거 Build/Verification/ContactStream/Identity-a6c46c8aaa974f1d928c9fcfeeef48bf/result.json.

이 gate의 native registry는 모델이다. 실제 Scene 컴포넌트 추가/SDK 생성/Editor Play snapshot/Player 재생성 수용으로 확대하지 않는다. C#에는 native AddComponent API가 없으며 HTTP component.add는 EditorOperation/Editor 전용이다. 검증 편의를 위한 공개 생성 API는 추가하지 않았다. 다음 실제 제품 gate에는 owner-thread 구조 경계에서 기존 C++ Entity::AddComponent를 호출하는 테스트 경로가 필요하다. E0 progress 유지; 동일 owner 재생성 제품 검증은 잔여다.

### 2026-10-09 동일 owner 재생성 gate의 적용 범위 정정

실제 제품 경로를 재정찰한 결과 EditorObjectOperations::AddComponent는 Play 중 PhysicsBodyComponent/CharacterMovementComponent 추가를 physics.authoring_frozen으로 거부한다. C# runtime native component 생성 API도 없다. 따라서 동일 owner body 제거 후 신규 component 생성은 현재 지원하는 스크립트 계약에 포함되지 않는다. 모델 wrapper 신원 gate15 checks는 유지하되, 이 시나리오를 현재 E0의 즉시 실행 가능한 제품 수용으로 표시했던 순서를 정정한다. runtime 생성 계약을 확장할 때 실제 Scene/SDK gate를 추가한다. 현재 저작 freeze를 시험 때문에 해제하거나 비공개 자동 재생성 훅을 추가하지 않는다. 기존 지원 범위인 component 제거 후 stale6 제품 gate와 shape 교체의 SDK generation gate는 계속 유효하다.

### 2026-10-09 저장 role 제거 + Scope 바인딩 제품 수용

PhysicsRoleContactProbe의 ExplicitBinding fixture를 추가했다. OnBeginSimulation에서 원본 Attack role 복원 확인→shape19 저장 role 비우기→Scope Attack 바인딩→중복 바인딩 거부를 확인한다. 초기 빈 저장 role은 Scope 바인딩으로 전달되고, runtime Alternate 저장 role 지정은 해당 바인딩보다 우선한다. 이후 저장 role을 다시 비우면 Scope Attack으로 복귀한다. 형제 shape20의 Attack/flags와 shape19의 flags는 유지된다. SDK body identity3개에 대한 retired End/new Begin·stale Persist 배제·target 집계 종료를 기존 role transition gate로 검사한다. 마지막 빈 role을 남겨 Stop/다음 Play의 원본 role 복원과 이전 Scope cleanup을 재검증한다.

Editor HTTP39·2 Play/Stop 통과: Attack Begin/End[2,1,2]·Alternate1/1·Attack target2/2·Alternate target1/1, stream4/4→8/8 해제, transform 원복·씬SHA 불변. 증거 Build/Verification/ContactStream/Product-0487301c6a5e4af09132669b2b576e7c/result.json. fixture scene/meta는 해당 Fixture에 옮기고 결과 scene 경로도 갱신했다.

현재 ABI39 LifecycleDistribution/a45dbc7e를 이용해 변경 스크립트를 새로 cook/package한 Player: 동일 역할/retired identity/target 검사 통과·2000GT/display1960/exit0·패키지 입력 불변. 증거 Build/Verification/ContactStream/Player-f98a880b290447608467741c9d7e9a85/result.json. 엔진 native/ScriptCore 구현·ABI 변경 없음; GameScripts Release 빌드 통과(기존 PhysicsTopologyContactProbe CS8602 경고1). 관리 router54 checks/steady allocation0, HTTP/Player PowerShell parser 및 dashboard JS 검사 통과.

저장 role 제거/명시 binding 조합은 이 fixture 범위에서 수용했다. E0 progress 유지. 다음은 ContactStream overflow와 스크립트 예외 발생 시 제품 실패 전파·Play 정리 검증이다. reload/DDOL·solid 집계·Inspector UI·최신 전체 Shipping·전체 GPU capture/대표성능 및 SceneManager/GPU fault 제품 gate는 잔여다.

### 2026-10-09 ContactStream overflow/PostPhysics 예외 제품 격리 수용

소스 정찰에서 ScriptRegistry.Invoke는 콜백 예외를 기록하고 해당 스크립트만 Enabled=false로 격리하며, PostPhysicsTick finally가 프레임 접촉 버퍼를 비운다는 기존 정책을 확인했다. 일반 스크립트 예외를 SceneManager의 전역 Play 실패로 승격하지 않는다. 앞선 다음 작업 표현의 전역 Play 실패 전파 전제는 정정한다. native contact routing 자체 실패의 전역 failure 경로는 이번 대상과 구분한다.

PhysicsContactFaultProbe와 HTTP/Player FaultCase(Overflow/Exception) gate를 추가했다. Attack/Hurt 두 실제 body·복합 sensor 접촉을 사용한다. Overflow는 capacity1의 실제 SDK 접촉2건 이상을 받으며 Read의 정확한 partial-results 거부 예외를 잡아 검증 표시 후 다시 던진다. 일반 Exception은 정상 Read 이후 의도적인 PostPhysics 예외를 던진다. fault 콜백1회·OnDisable1회·비활성 상태 및 정상 Hurt 구독자의 후속 접촉 소비를 확인한다. Scope는 Disable에서 중단 관측되며 세션 종료까지 소유한다. OnEndSimulation 전에 Read가 ObjectDisposedException을 반환하는 두 스트림의 해제를 검사한다.

최종 Editor 각2 Play/Stop: Overflow HTTP42(Product-ff876a19c96d4a0494a9103e549ac9ab), Exception(Product-9b5700e7af9541c8bae4e27de07c3130). 정상 소비 유지·Play failureCount0·stream2/2→4/4·cleanup4·Transform 원복·씬SHA 불변 통과. fixture scene/meta는 각 Fixture에 옮기고 결과 경로 갱신. 실제 runtime 이동을 주입한 원복 gate로 확대하지 않는다.

ABI39 최신 LifecycleDistribution/a45dbc7e를 이용한 fresh cook/package Player 각2000GT 통과. Overflow Player-d56fcb81d25040e8beee91d7421c532c: required4/readRejectedtrue/정상접촉32/display1958. Exception Player-7be72ce7df2740d08a08ea540f16f8f5: 정상접촉30/display1964. 두 경우 exit0·패키지 입력 불변·cleanup2/2. 기존 CE_PHYSICS_RESOURCE_PROBE 계측 활성화로 실제 생성/해제8종 일치/balancedtrue를 검사했다. 결과 result.json·player.out·fault-ownership.json에 보존한다. 두 Player gate는 독립 패키지로 동시에 실행했으므로 display 수치를 성능 비교에 사용하지 않는다.

native/ScriptCore 정책·ABI 변경 없음. GameScripts Release 빌드 통과(기존 Topology CS8602 경고1), 관리 router54 checks/steady allocation0·PowerShell parser·dashboard JS·diff whitespace 검사 통과. 초기 gate는 Read 거부 표시와 shutdown ledger 추가 전 중간 결과이며 위 최종 결과를 수용 증거로 사용한다. E0 progress 유지. 다음은 ContactStream reload/DDOL 수명 제품 gate다. 실제 SceneManager/GPU fault·solid 집계·Inspector UI·최신 전체 Shipping·전체 GPU capture/대표 성능은 잔여다.

### 2026-10-09 ContactStream 씬 이탈 관측 초기화/Editor reload·DDOL 수용

ScriptRegistry의 OnRemovingFromScene 전달 전에 ContactRouter.Suspend를 호출한다. DDOL은 Scope·구독·명시 역할 바인딩을 유지하되 이전 Scene의 pending buffer/overflow/sensor pair/target 관측 상태는 버린다. 새 Scene의 현재 overlap은 첫 Persist에서 Begin으로 시작한다. 씬 이탈에서 synthetic End를 만들어 전달하지 않는다. 최종 파괴/Stop은 기존 Scope 취소로 해제한다.

관리 Debug/Release 각각61 checks/steady allocation0: capacity1 스트림의 이전 pending pair/target 버퍼 제거, 구독·Scope·역할 바인딩 유지, 목적지 Begin seed,97번 씬 경계의 bounded capacity 재사용, 최종 pair/target 스트림 해제. 엔진 native·ABI39 변경 없음. ScriptCore/GameScripts Release 빌드 및 정상 publish-engine 배포 SceneDistribution/local-0.0.0.0-win-x64-Release-d6a48fcd-3f4e-4e89-b84f-5b6191740358 완료.

PhysicsSceneContactProbe·verify-physics-contact-scene-http.ps1 추가. 최종 제품 capacity32는 catch-up tick 배치를 수용하고 관리 capacity1 회귀는 씬 경계 누적만 검사한다. Editor DDOL(Product-c99dc9c898d6404398f4b57864a68b37): runs1/added2·동일 stream1 유지/중간disposed0·이전 target 신원 배제·목적지 Begin2/Persist2·Stop disposed1. Editor Scene reload(Product-3be8c936086e4a2297ddaba92f54836e): runs2/added1·stream2/이전disposed1·새 Begin2/Persist2·Stop disposed2. 두 경우 SceneManager failure0·원본씬SHA 불변, fixture/meta 보존 및 결과 scene 경로 갱신. 목적지에는 새 Hurt body만 둔다.

Player 제품 gate는 아직 미수용이다. 첫 HTTP 방식은 Player에 play.state/script.invoke/scene.switch가 없어 command.unknown으로 실패했다(ScenePlayer-c4ef15c9/a79a82d2). 도구를 기존 --smoke-reload 및 구조화 로그로 교체했다. 첫 smoke(ScenePlayer-e45029fae0fb47abbb68b8e258c89041)는 capacity2가 catch-up 배치를 담지 못해 actual Read overflow/스크립트 Disable로 실패했다; 물리 리소스8종 balancedtrue와 reload activation/exit0만으로 접촉 수용을 선언하지 않는다. 이후 fixture capacity32를 보강하고 Editor 양쪽을 재수용했다. capacity32 Player 재패키징·목적지 물리 Begin/Persist 및 해제까지 기다리는 smoke 경계 검증은 다음 작업이다. Player body DDOL은 현 smoke의 캐릭터 전용 경로 확장이 필요하다. 현재 Player 도구는 DDOL 입력을 명시적으로 거부하며 reload 전용이다. 어셈블리 reload 수용도 이번 Scene reload와 별도 잔여다.

PowerShell 두 gate parser·diff whitespace 통과. 대시보드에 Editor 수용/Player 미수용을 분리 반영. E0 progress 유지. native fault·solid 집계·Inspector UI·최신 전체 Shipping·전체 GPU capture/대표 성능은 기존 잔여다.

### 2026-10-09 Player Scene reload/body DDOL 접촉 수용

PlayerMain의 기존 smoke reload 경계에 명시적 회귀 옵션 CE_PHYSICS_CONTACT_SCENE_PROBE를 추가했다. 목적지 ContactAttack이 해당 Scene에 속하고 body ReadState가 성공해야 하며, PhysicsSceneContactProbe의 직렬화된 Ready 필드를 읽어 목적지 Begin2/Persist2가 확인될 때까지 기다린다. 기존 목적지 표시 완료/제출 경계도 그대로 요구한다. Ready는 fixture 전용 필드이며 명령 등록이나 InvokeCallable 권한을 확대하지 않는다. CE_PHYSICS_CONTACT_DDOL은 기존 ActivateScene owner 경계 직전에 live body owner를 DDOL로 표시한다. 옵션이 없으면 기존 smoke 경로를 사용한다. ABI39 유지.

PhysicsSceneContactProbe는 OnBeginSimulation에서 캡처한 PhysicsBodyComponent wrapper를 보존하고 목적지 PostPhysics에서 ReadState와 접촉 SelfComponentId를 검증한다. State에 component ID를 남겨 DDOL 전후 동일 wrapper/component 신원을 검사한다. runtime 생성 API 추가가 아니며, 같은 owner 새 component 생성 수용과 구분한다.

Release Player native 재빌드 통과(contact-scene-player-build.log), ScriptCore/GameScripts 빌드 통과·관리 Release61 checks/할당0. 새 정상 배포 SceneDistribution/local-0.0.0.0-win-x64-Release-28877b16-851f-4b45-924b-8e6482d449ab. native 변경은 PlayerMain 회귀 경계에 한정; Physics/SceneRuntime native ABI 변경 없음.

최종 cooked Player reload: ScenePlayer-1f6925fc29d04497b3d4ae5554597a65/result.json. 2014GT·목적지 display8·exit0·패키지 불변·실제 물리8종 생성해제 일치. runs1→2/streams1→2/이전disposed1→최종2·새 body component2447813378→402624482·목적지 Begin2/Persist2. source와 destination 모두 ready 로그 확인.

DDOL의 첫 실행 ScenePlayer-9ac43fdfd89b47aeabeb244110789665는 목적지 접촉 readiness에 도달했지만 카메라 없는 fixture의 scene_changed/view_inactive 때문에 표시 완료를 충족하지 못했다. 해당 소유 테스트 프로세스를 종료했고 성공으로 수용하지 않는다. destination primary Camera를 fixture에 추가하고 Editor DDOL을 재수용했다(Product-7f3dcac6452743a3acd6d77a8b491137/result.json;fixture/meta 이동·result.scene 갱신).

최종 cooked Player body DDOL: ScenePlayer-9faa08be7c2d4d628f0b1338965ce5f7/result.json. 2014GT·목적지 display8·exit0·패키지 불변·물리8종 생성해제 일치. runs1/added1→2/stream1 유지·중간disposed0/최종1, component2082428627 동일 wrapper 유지·목적지 Begin2/Persist2·이전 target 배제. 실제 접촉 및 표시 완료를 모두 기다렸으며 대기만으로 성공을 선언하지 않았다.

PowerShell 두 gate parser·diff whitespace·dashboard JS 통과. Scene reload/body DDOL의 이 fixture 제품 범위를 수용한다. 어셈블리 reload는 별도 잔여이며 다중 DDOL 계층·실패 경계 전체를 이 검사로 확대하지 않는다. E0 progress 유지. 다음은 스크립트 어셈블리 reload 시 이전 ContactStream/Scope 폐기와 재구독 제품 gate다. native SceneManager/GPU fault·solid 집계·Inspector UI·최신 전체 Shipping·전체 GPU capture/대표 성능은 잔여다.

### 2026-10-09 Editor 어셈블리 reload ContactStream 수용

Play 중 script.reload 두 번을 실제 실행했다. Product-2d8fb4c083a94ec897a43d39b08e629e/result.json: HTTP46, 각 total1/restored1, 새 인스턴스 Begin2/Persist2, native bodyComponent1040030173 유지, activeScripts1 및 previousContextAlivefalse. 두 이전 Scope와 최종 Stop의 스트림 각각1/1 해제(End marker3개), 씬SHA 불변. fixture scene/meta는 해당 Fixture로 이동하고 result.scene을 갱신했다.

최초 Product-9d55d8b6ae804493a3c4663c1cc09298는 이전 ALC 잔류로 실패했으며 수용하지 않는다. PhysicsSceneContactProbe의 기본 JSON 익명 타입 메타데이터 캐시를 사용하지 않는 primitive invariant JSON 출력으로 변경했다. 직렬화 복원된 Ready가 새 접촉 검사를 건너뛰지 않도록 OnBeginSimulation에서 초기화하고 매 reload의 실제 Begin/Persist를 기다린다. 독립 ContactAssemblyProbe checks2: 기존 default JSON 진단은 ALC retained, 수정 진단은 collected. 이는 현재 SDK와 두 진단 메서드의 비교 증거이며 모든 사용자 캐시의 unload 보장은 아니다. 진단 로그와 소스SHA는 최종 evidence에 보존했다.

GameScripts Release 빌드 오류0(기존 Topology nullable 경고1). native/ScriptCore/ABI39 변경 없음. Player는 runtime script.reload API를 제공하지 않으므로 이 수용은 Editor 범위다. Player Scene 전환 gate가 assemblyReload fixture를 잘못 수용하지 않도록 입력을 거부한다. E0 progress 유지. solid 접촉 소비·집계 계약, 실제 SceneManager/GUI·GPU fault, Inspector UI, 최신 전체 Shipping·전체 GPU capture/대표 성능은 잔여다.

### 2026-10-09 solid 형상 쌍 소비 계약 확정

ContactGrouping.SensorTargets는 센서만 target Entity 단위로 집계한다. solid는 ShapePairs와 동일하게 형상 쌍별 Begin/Persist/End를 전달한다. 같은 target의 두 solid 형상이 같은 tick에 접촉해도 두 사건을 유지하며, 하나만 이탈하면 그 형상의 End와 남은 형상의 Persist를 각각 전달한다. target 전체 End로 해석하지 않는다. solid의 Point와 ContactCount/RequiredContacts는 해당 native 사건의 snapshot이고, 역방향 구독에서도 Point는 동일 좌표로 보존한다. 대표 형상 하나를 고르는 solid target 집계나 접촉점 병합 API를 이번 계약에 추가하지 않는다. 늦은 구독의 solid Persist를 sensor처럼 합성 Begin으로 바꾸지도 않는다.

ContactStreamProbe에 복합 solid 회귀9개 추가: SensorTargets에서도 두 Begin 유지, shape 신원, 역방향 endpoint, point/required metadata, 같은 tick 두 Persist, 부분 이탈, 마지막 이탈, no-step 무재생, Scope 해제. Debug/Release 각각70 checks/기존 steady routing allocation0 통과. 합성 native 사건을 실제 관리 router에 전달하는 회귀이며 실제 PhysX 복합 solid 제품 수용으로 확대하지 않는다. 기존 sensor→solid→sensor Editor/Player gate는 단일 solid 전환만 검증한다. 다음은 두 solid 형상의 실제 Editor/Player 접촉·부분 이탈 제품 gate다. E0 progress 유지; native/ABI 변경 없음.

### 2026-10-09 복합 solid 실제 Editor/Player 접촉 수용

PhysicsSolidContactProbe와 -SolidContacts 제품 gate 추가. gravity off/축 잠금 dynamic body의 두 solid box(shape19/20)가 static shape23과 접촉한다. Z 이동으로 shape20만 이탈한 뒤 shape19의 Persist를 기다리고, 추가 이동으로 마지막 End와 무재생을 검사한다. ShapePairs와 SensorTargets 두 스트림의 사건 수·형상·phase/tick 및 contact count metadata가 동일함을 실제 SDK 사건으로 확인한다. 기존 fixture 모드와 혼용을 거부한다.

Editor Product-eb716fc2e83b4a96a8a537cd03f5fa98/result.json: HTTP39/PlayStop2, 각 Begin[1,1]/Persist[5,2]/End[1,1]/부분 이탈 후 Persist2, stream2/2→4/4 해제, Transform 복원·씬SHA 불변. fixture scene/meta는 Fixture로 보존하고 결과 경로 갱신했다. 최초 도구 실행은 sensor GroupTargets 필드 설정을 solid fixture에도 적용한 verifier 오류로 실패했다. 이를 분리한 최종 실행만 수용한다.

ABI39 기존 정상 SceneDistribution-28877b16을 이용한 fresh cook/package Release Player-abd501f82fe846c2b6b02047eec1b5a8/result.json: Begin[1,1]/Persist[8,4]/End[1,1]/부분Persist3, 2000GT/display1953/exit0/패키지 입력 불변. 접촉 검사 직후 성공을 선언하지 않고 기존 표시 완료와 종료를 기다렸다. GameScripts Release 오류0(기존 Topology nullable 경고1), PowerShell parser 및 dashboard JS·diff whitespace 통과. native/ScriptCore/ABI 변경 없음.

축 잠금·제어된 이동 fixture의 복합 solid 계약 수용이다. 자유 운동 충돌/회전, 양쪽 구독의 실제 point 비교, 전체 Shipping/GPU solver·대표 성능·전체 Editor GPU capture 수용으로 확대하지 않는다. 관리 역방향 payload 검사 D/R70은 별도 근거다. E0 progress 유지. 다음은 최신 ContactStream 변경을 포함한 Shipping 제품 통합 검증이다.

### 2026-10-09 최신 ContactStream Shipping 접촉 통합 수용

현재 소스의 정상 publish-engine -Config Release -Shipping -Build로 Editor/Shipping Player 및 배포 도구를 재빌드했다. ScriptCore Release 빌드 오류0, 정상 배포 ShippingDistribution/local-0.0.0.0-win-x64-Release-Shipping-c20b85d9-6c29-41fc-a9f8-4dd63a1abdc0(ABI39). 기본 프로젝트 최적화 설정 빌드이며 기존 no-WPO 실행과 구분한다. publish 및 SDK 빌드 로그 보존.

verify-physics-contact-stream-player.ps1에 -Shipping 추가: package-game --shipping, package pointer mode 일치, runtime service compiled=no/enabled=no 및 endpoint 부재 검증. 첫 옵션 전달은 PowerShell scalar splat로 Unknown option '-'가 발생해 실패했다(Player-53abf87f). string[] 배열로 고정 후 새 패키징했고 실패 결과를 수용하지 않는다.

fresh Shipping 패키지4개 순차 실행: solid Player-a8d4e4e(두 shape Begin/End[1,1], 부분Persist2), sensorTargets Player-10517a9a(양쪽 sensor2→target1), overflow Player-eb3c0714(capacity1 partial Read 거부), exception Player-0739fec0(PostPhysics 예외). 각2000GT/display 1967,1948,1965,1969, exit0/패키지 입력 불변/Shipping service 격리 통과. 두 fault 사례 콜백1/Disable1/정상 구독 지속/Scope cleanup2 및 실제 물리8종 생성해제 일치. 통합 인덱스 Build/Verification/ContactStream/shipping-contact-matrix.json. display 차이를 성능 비교로 해석하지 않는다.

verify-player-shipping-isolation.ps1 -Config Release -SkipBuild 통과: Development 소켓/서비스 존재를 control로 확인, Shipping runtime의 WS2_32 import 및 서비스 marker 없음. 기본최적화 Shipping 구성에서 위 접촉4범위를 수용한다. Shipping Scene reload/body DDOL·subscriber/role/topology 전체 조합, 전체 GPU capture/대표 성능·M3/M4 완료를 의미하지 않는다. E0 progress 유지. 다음은 Shipping Scene reload/body DDOL 접촉 제품 gate다.

### 2026-10-09 Shipping Scene reload/body DDOL 접촉 수용

verify-physics-contact-scene-player.ps1에 -Shipping 추가: string[] package mode 인자, --shipping fresh package, pointer Shipping 모드 일치, 서비스 compiled=no/enabled=no와 runtime endpoint 부재 검사. 기존 ABI39 기본최적화 정상 ShippingDistribution-c20b85d9 사용; native/관리 SDK 변경 없이 현재 PhysicsSceneContactProbe를 새 패키지에서 컴파일했다.

ScenePlayer-dbe859f9969a4cd3b30c92c8b1784d11/result.json: reload runs1→2, native body2447813378→3646649945, streams1→2/이전disposed1/최종2, 목적지 Begin2/Persist2. ScenePlayer-88e5e16099eb4ae693edb9e46d0b4191/result.json: DDOL runs1/added1→2, body2082428627 동일 captured wrapper/stream1 유지/중간disposed0/최종1, 이전 target 배제와 목적지 Begin2/Persist2.

양쪽2014GT/목적지display8/exit0/패키지 입력 불변/Shipping 서비스 격리/실제 물리8종 생성해제 일치 통과. source 및 destination readiness와 표시 완료까지 기다렸다. 통합 증거 shipping-scene-matrix.json, fixture 원본은 기존 Editor evidence에서 보존한다. PowerShell parser/dashboard JS/diff whitespace 통과. top-level body fixture 범위 수용이며 다중 DDOL 계층·실패 조합 전체를 의미하지 않는다. Player runtime assembly reload는 지원 계약 밖이다. E0 progress 유지. 다음은 Shipping subscriber Disable/Enable·역할 변경·형상 topology 접촉 회귀다. 전체 Editor GPU capture/대표 성능/M3/M4는 잔여다.

### 2026-10-09 Shipping subscriber/role/topology 접촉 전환 수용

기본최적화 정상 ShippingDistribution-c20b85d9/ABI39를 이용해 현재 GameScripts probe로 fresh Shipping 패키지6개를 순차 실행했다. subscriber Player-eae6dc6d: Disable/Enable 각1·Begin-only4·새tick Begin seed 및 정상 Persist. 저장 역할 Player-24202a0a, Scope binding Player-a55bd8a0: Attack Begin/End[2,1,2]·Alternate1/1·역할별 target 종료·retired 역할 snapshot 및 명시 binding 우선순위 전환 수용.

shape flag topology Player-c0aacd00: old End2/new Begin2/new End2·body slot0/gen1→slot2/gen1. fetch 전4회 교체 Player-fb02bb42: replacementCalls4·slot0/gen1→slot0/gen3·중간 body 접촉 없음·최종 Begin2/End2. sensor→solid→sensor Player-80d81305: sensor Begin/End[2,1,2], solid Begin/Persist/End1/2/1·SensorTargets의 solid 사건 유지·서로 다른 body 신원3개 수용.

각2000GT/display 1965,1963,1964,1966,1968,1965/exit0/패키지 입력 불변/Shipping compiled=no enabled=no 및 runtime endpoint 부재 통과. 통합 증거 shipping-contact-transitions.json. display 수치를 성능 비교에 사용하지 않는다. native/SDK/제품 코드 변경 없음; 기존 Editor fixture 원본 그대로 새 패키지에 복사했으며 새 compiler source를 사용했다. 계획/dashboard JS 및 diff whitespace 확인. E0 progress 유지. 다음은 Shipping 늦은 구독 초기 overlap·Entity 삭제·body component 단독 제거 접촉 회귀다. 전체 GPU capture/대표 성능/M3/M4 완료와 구분한다.

### 2026-10-09 원격 master 동기화 및 ABI 통합

origin/master의31커밋을 fast-forward로 반영했다. HEAD=origin/master=329fad205f6560fc4462354b9287d64a68049c8a. 로컬 Phase19 tracked/untracked 작업은 stash로 보존·복원했고 기존 PhysicsContactExecutionContract staged blob도 동일하게 유지했다. 복구 stash는 유지한다. ClrHost.cpp/Native.cs/ScriptApiVersion.h의 AssetDepot/물리 API 충돌3개는 양쪽 슬롯을 보존해 해소했다.

통합 API ABI40/198슬롯. 기존 AssetDepot typed API와 물리 Body_Remove/Body_ShapeRole 모두 유지. Physics ABI checker Release35 및 Contact ABI37/테이블순서198 통과, 관리 router Release70/steady allocation0, GameScripts Release 오류0. AssetDepot managed fixture의 버전 검사는 현재 버전을 기준으로 legacy/future 거부를 검사하도록 수정했으며 해당 fixture 실행은 별도 미검증이다.

동기화 이후 native 호스트 및 전체 제품 실행은 아직 재빌드·재수용하지 않았다. 기존 ABI39 배포·Shipping 결과는 동기화 이전 revision의 유효한 이력이고 ABI40 제품 근거가 아니다. 현재 관리 출력과 기존 native 바이너리는 ABI가 다르므로 다음 제품 실행 전 native/배포 재빌드가 선행되어야 한다. E0 progress 및 M3/M4 잔여 유지.

### 2026-10-09 ABI40 native 재빌드·Shipping lateOverlap/retirement 수용

동기화된 소스에서 BuildTool Release를 재빌드하고 정상 publish-engine Release/Shipping Build 경로를 실행했다. 첫 빌드에서 원격 추가 코드의 reflection/include/소유권 연결/테스트 호출 오류가 드러나 수정했다. FoliageType의 AssetDepot include는 상대 경로로 명시, RenderEngine include 설정은 상속 보존(루트 검색 추가는 MeshOptimizer.h/meshoptimizer.h Windows 이름 충돌을 일으켜 제거). Scene의 AssetBundle/TextureFramePins include를 상대 경로로 명시하고 runtime-only prepared pin은 reflgen::ignore 처리했다.

GBuffer/Forward binding layout alias는 own shader를 직접 std alias owner로 쓸 수 없으므로 이미 shader를 소유하는 std graphics generation을 alias owner로 사용했다. 추가 layout 복사/할당 없이 generation 수명을 유지한다. FBX 외부 경로 u8path deprecation은 u8string 기반 filesystem::path 생성으로 UTF8 의미를 유지했다. MaterialResolver는 TextureAssetRuntime 정의를 직접 include, Editor 렌더 selftest의 BuildRectsFromQueue는 기본 width/height0 뒤 마지막 인자로 texture pin을 전달한다. 원래 staging blob은 유지했다. 최초 실패 로그는 각 abi40-publish-* 이력으로 보존하며 성공 근거로 사용하지 않는다.

최종 abi40-publish-uifix.log 정상 완료. Abi40Distribution/local-0.0.0.0-win-x64-Release-Shipping-e9106c4a-9cf3-4739-8a77-128f58c622c9 manifest의 CreatorEditor/Shipping Player/AssetCooker/AssetPacker 모두 scriptApi40. Development Player 자체는 이번 Shipping publish 빌드 대상 밖이며 별도 재빌드 잔여다. 테이블순서198/Release Physics ABI35 checks/Contact ABI37 checks 재통과.

fresh Shipping Player lateOverlap-7e5c4710: 늦은 구독 Begin1/Begin-only1/Persist 후 이탈. Entity삭제-9db9bb1c: sensor End2/target End1/삭제 Entity 신원 snapshot. body단독제거-95fa9340: Entity alive/End2/target End1/stale wrapper6. 세 경우2000GT/display1936·1942·1944/exit0/패키지 불변/Shipping service 격리 통과. 통합증거 abi40-shipping-retirement.json. display 차이는 성능 비교가 아니다.

ABI40 제품 수용은 위3 fixture 범위다. 이전 ABI39 Editor/Shipping suite는 이력이며 새 revision 전체 수용으로 확대하지 않는다. E0 progress 유지. 다음은 Development Player ABI40 재빌드와 기존 핵심 ContactStream Editor/Player 회귀 재수용이다. 전체 GPU capture/대표 성능/M3/M4는 잔여다.

### 2026-10-09 ABI40 Development 재빌드·핵심 Editor/Player 재수용

정상 publish-engine Release Build 통과(abi40-development-publish.log). Abi40DevelopmentDistribution/local-0.0.0.0-win-x64-Release-2c26c76a-0c56-4d61-a8eb-c9ee0a909d54. Development Player ABI40으로 갱신했고 Editor/cooker/packer도 정상 배포 manifest 검사 통과. native 추가 변경 없이 앞선 빌드 오류 수정 및 통합 ABI40 소스를 사용했다.

새 Editor6범위: solid Product-9138654d(2PlayStop/shape BeginEnd/부분Persist), Scene reload-9050ef70, DDOL-9e53e9f3, assembly reload-aef571b0(2reload/old Scope 해제/새BeginPersist/ALC수거), overflow-30c0e0da, exception-60c4c599(각2PlayStop/정상구독유지·Scope해제). fixture scene/meta를 각 Fixture로 보존하고 result.scene 갱신했다. SceneSHA 불변 및 Stop 원복은 해당 gate 계약대로 검사했다.

fresh Development Player5범위: solid3122979f/overflow bedd5aae/exception a748bbfa 각2000GT, Scene reload fd99f6cd/body DDOL599cb14f 각2014GT. display 1937,1940,1934,8,8·exit0·패키지 불변 통과. 두 fault 사례 실패콜백/Disable1·정상구독지속·cleanup2/물리8종 생성해제 일치, 두 Scene 사례 목적지Begin2/Persist2·표시8·종료해제/물리ledger 일치. DDOL 동일 captured body wrapper/stream 유지, reload 이전stream 해제+새body/stream 확인. Player runtime assemblyreload API는 추가하지 않는다.

관리 router Release70/steady allocation0, 새 dev/ship 바이너리 소켓·서비스 격리 control 감사 통과(abi40-router.log/abi40-isolation.log). 통합 인덱스 abi40-development-core.json. 기본 최적화 구성의 제한 fixture 수용이며 전체 Editor GPU capture·대표 성능·E0/M3/M4 완료가 아니다. 다른 ABI39 contact 전환 suite는 이력이고 ABI40 전체 회귀로 주장하지 않는다. B1 직접 형상 Inspector 저작 경로·C1 입력/회전 정책·전체 콘텐츠/성능 잔여 유지.

### 2026-10-09 C1 입력 프런트엔드와 facing 책임

ScriptCore에 SetPlanarInput/CreatePlanarVelocity를 구현했다. 월드 XZ 입력의 아날로그
크기를 보존하고 대각선 속도를 제한하며 유한값/비음수 m/s 속도를 검증한다.
가속/감속은 기존 fixed-step 정책을 사용하고 legacy dynamic Lerp는 이전하지 않는다.
카메라 변환·데드존·facing·회전 보간은 스크립트가 소유한다. 물리가 중력/넉백에 따라
캐릭터를 암묵적으로 회전시키지 않는다. PhysicsAPIContract의 새 C1 절을 기준으로 한다.
ABI40/198 유지, managed 물리42/접촉44 통과. 실제 Player 입력/facing 검증과 콘텐츠
수치 충돌 해결은 남아 있으므로 C1은 progress를 유지한다.

### 2026-10-09 C1 평면 입력 실제 Development Player 검증

CharacterPlanarInputProbe와 fixture/verifier의 -PlanarInput 옵션을 추가했다.
최신 ScriptCore를 재빌드하고 기존 ABI40 네이티브 제품으로 새 배포본을 생성하여
정상 package-game 경로로 스크립트 컴파일·cook·패키지 smoke를 수행했다.
실제 completed physics tick 241에서 16/0 검사 통과: 아날로그 물리 이동,
대각선 속도 제한/이동, 입력 해제 후 fixed-step braking, 명시적 script facing,
잘못된 입력 거부, force 이동 중 released input/facing 보존과 Cancel.
Player는 authoring snapshot 없이 실행·정상 종료0, 패키지 231개 파일 해시 불변.
증거: Build/Verification/ContactStream/PlanarPlayer/Evidence/result.json 및 player.out.
배포본: PlanarDistribution/local-0.0.0.0-win-x64-Release-1df5067f-47df-41ba-8c4c-0765827c0849.
이번 새 입력 검증은 Development Release/CPU capsule/primitive 씬 범위다.
Shipping·Editor Play/Stop의 새 입력 회귀와 실제 콘텐츠 수치 이전은 잔여이며 C1 progress 유지.

### 2026-10-09 C1 평면 입력 Editor Play/Stop 복원

verify-physics-planar-input-http.ps1을 추가하고 실제 Release Editor/HTTP 저작 씬에서
2회 Play→completed tick241 입력 검사16/0→Stop을 통과했다. 저작 yaw35°에서 시작해
스크립트가 forward/right facing으로 회전하고 물리 이동·감속·force를 수행한 뒤,
Stop은 위치/회전/스케일·enabled/layer를 복원하고 tick/중력/desired velocity/force를
초기화한다. 이전 CharacterMovementComponent 래퍼는 ReadState와 SetPlanarInput 모두
StaleHandle로 거부되며 Replay도 같은16/0을 통과한다.
증거: Build/Verification/ContactStream/PlanarEditor-e0e007f6ebd54d739ace8e81d0774cc5/result.json.

첫 검증 실패는 로그 파일 공유 접근과 검증 메서드 EngineCallable 누락으로 수정했다.
저장 비교에서는 Scene 복원 후 엔티티 배열 순서/인덱스 재배치를 확인했다.
verify_scene_document_equivalence.py는 엔티티 ID로 parent/root/children 인덱스를 해석하여
전체 저작 필드와 순서 있는 계층 관계의 동등성을 검사한다. 명시적 저장 후 두 번 모두
동등성 통과; Play/Stop 자체는 저장 파일을 변경하지 않는다. 저장 후 바이트 불변은 주장하지 않는다.
Shipping 새 입력 회귀와 실제 콘텐츠 단위/저작값 이전은 잔여이므로 C1 progress 유지.

### 2026-10-09 C1 평면 입력 Shipping 수용

최신 ScriptCore를 Release 재빌드하고 기존 ABI40 Shipping 네이티브 제품으로 새
PlanarShippingDistribution/local-0.0.0.0-win-x64-Release-Shipping-455f6f0f-fb90-49a6-a5bb-659ef14e4b62
배포본을 생성했다. 정상 package-game --shipping으로 별도 primitive character 프로젝트를
컴파일·cook·패키징했다. 패키지의 Player.runtime.dll/ScriptCore.dll 해시는 배포본과 일치한다.
새 SDK helper는 native ABI를 바꾸지 않아 이번 실행에서 native 재빌드는 하지 않았다.

실제 Shipping Player 입력16/0·tick244 통과: 아날로그/대각선 물리 이동, 입력 해제 후
fixed-step braking, script facing, 잘못된 입력 거부, force 중 released input/facing 유지.
2000 GT frames/display frame2000/promotions1945, 종료0, 패키지230파일 해시 불변.
cooked Scene/CEMF, runtime text parser0, 서비스 compiled=no/enabled=no, endpoint 부재 확인.
별도 바이너리 격리 게이트도 Development의 WS2_32/서비스 문자열 존재와 Shipping 부재를
확인했다. 증거: Build/Verification/ContactStream/PlanarShippingPlayer/Evidence/result.json,
planar-shipping-summary.json 및 planar-shipping-isolation.log.

새 평면 입력/facing 정책은 Development·Editor 2회 Play/Stop·Shipping에서 검증됐다.
C1 전체 완료는 아니다. 실제 legacy 콘텐츠 단위 충돌/저작값 이전과 그 콘텐츠의
이동 골든 수용은 남아 있으므로 progress를 유지한다. 이번 결과는 CPU capsule/primitive
고정 씬 범위이며 GPU 완료·전체 콘텐츠·성능 수용을 의미하지 않는다.

### 2026-10-09 C1 실제 이전 대상 조사 및 P0 변환 재수용

현재 Dynamic_CPP/Assets의 Scene/Prefab 91개를 읽기 전용으로 조사했다.
legacy CCT는 PhysicsP0Baseline.creator의 P0Character 1개뿐이다. CCT+동반 Rigidbody의
canonical fingerprint는 회귀용 원본과 일치한다(0af2ffd862e1c6cad4692882d70c247b5862fa8bd886e6a4d647e40a45367894).
실제 게임 콘텐츠로 분류할 legacy 캐릭터는 0개다. 파일명만으로 기준선을 제외하지 않도록
Tools/regression/audit-character-migration-corpus.py를 추가했으며 검사 파일 SHA를 재확인한다.
출력은 Assets 외부로 제한하고 원본은 수정하지 않는다. 이것은 현재 프로젝트 범위의 조사이며
외부 프로젝트나 실제 캐릭터 이동 골든의 존재를 대신 검증하지 않는다.

PHYSICS_SCHEMA_MIGRATION_OK 112개 검사 통과. 소스 해시가 지정된 P0 정책으로 생성한
character-converted.creator를 최신 ABI40 Release Editor에서 실행했다. 명시적 1.5m/s 이동,
착지·점프와 2회 Play/Stop pose/component ID 복원·runtime reset·원본/레이어 파일 불변 통과.
입력은 CLI로 지정한 값이다. 게임플레이 입력 연결이나 과거 운동과의 동등성을 주장하지 않는다.
증거: Build/Verification/ContactStream/ContentMigration/character-corpus.json 및
EditorEvidence/result.json. 변환본·정책·mapping도 같은 폴더에 보존했다.
P0 원본은 삭제/변환하지 않았다. 일반 migration dry-run은 P0의 legacy solver-limit 정책으로
게시를 차단했으며 부분 변경도 하지 않았다.

C1 잔여의 '실제 콘텐츠 단위 이전/이동 골든'은 현재 구현 결함이 아니라 외부 콘텐츠와
기준 운동 자료가 없는 수용 항목이다. 임의 속도 선택으로 완료 처리하지 않는다.
C1 progress 유지. 다음 구현 작업은 현재 코드로 진행 가능한 B1 Inspector 저작 검증이다.

### 2026-10-09 B1 PhysicsBody Inspector 검증 경로 수정

PhysicsBody 일반 필드는 Meta::DrawObject 직접 편집을 사용했고 공통 Property는 CCT만
CaptureDefinition 검증을 수행했다. Inspector 일반 바디 필드를 focus-loss 초안 방식으로
바꾸고 EditorObjectOperations::Property를 통해 검증·Undo/Redo·hierarchy lock·Play freeze를
적용했다. 형상은 기존 Apply Shapes 일괄 transaction을 유지한다.
Property는 바디 CaptureDefinition과 kind/mass/damping/axis locks/초기 속도의 유한값·범위를
검증하며 실패하면 이전 문서를 복원하고 Undo를 게시하지 않는다. SDK body 생성 전 검증을
저작 시점에 적용한 변경이며 기존 runtime ABI40은 변경하지 않았다.

최신 Release Editor 재빌드 후 확장 verify-physics-shape-http.ps1 통과.
질량0/음수·감쇠음수·축잠금8·motion3 거부 시 Undo/전체 저장 문서 불변,
정상 mass2 변경의 단일 Undo/Redo, 형상 invalid 거부/Undo/Redo, ShapeRoles 및 Prefab
override 저장/재로드, Play 중 body/shape 저작 거부, 시뮬레이션 이동/Stop 복원 확인.
증거: Build/Verification/ContactStream/BodyInspector/result.json 및 results.jsonl.
이 결과는 실제 HTTP가 Inspector와 공유하는 transaction을 검증한 것이다.
새 바디 InputText의 실제 pointer/keyboard/focus-loss UI 왕복은 별도 잔여이며 B1 progress 유지.
첫 중첩 빌드의 Editor.lib 잠금 실패는 증거에서 제외하고 순차 재빌드로 수용했다.

### 2026-10-09 B1 바디 Motion 실제 Inspector 편집 검증

verify-physics-body-inspector-ui.ps1을 추가했다. 최신 Release Editor에서 Inspector의
실제 InputText를 포인터로 클릭하고 End/Backspace와 UTF-8 text 입력을 전달했다.
직접 object.property로 값을 쓰지 않는다. 실제 위젯 activeId가 살아 있는지 확인한 뒤
motion 2→1 편집의 focus-loss 이전 초안/Undo 불변, focus-loss 이후 단일 Undo 게시,
Undo/Redo, 잘못된 motion3 입력 거부와 문서/Undo 불변, 저장/재로드 의미동등성을 수용했다.
실행46명령 통과, 증거 BodyInspectorUI-4958d76e000142a692b99061031b81e2/result.json 및 commands.jsonl.

표준 InputText도 공통 property field 장부에 좌표/폭/ID를 기록하도록 연결했다.
바디9개·캐릭터12개 입력의 계측 경로가 생겼으며 실제 바디9칸을 확인했다.
editor.nav에 제한된 text 큐(64개·각4096B), End/Backspace와 wheel(-100..100)을 추가했다.
text/wheel은 기존 ImGui 프레임 입력 경로로 전달하며 별도 입력 프레임워크를 만들지 않는다.
검증은 스크롤 후 현재 입력칸 좌표를 다시 읽는다. 검증용 배율과 workspace를 분리하고
EngineSettings.asset 원본 바이트를 finally에서 복원했다. 최초 HTTP wait 사용 및
스크롤 밖 클릭 실패는 수용 증거에서 제외했다.

이 결과는 Motion의 대표 포인터/키보드/focus-loss 왕복이다. 질량·감쇠·축 잠금·벡터·
캐릭터 입력칸 각각의 직접 UI 편집, Shape draft Add/Remove/Apply/Reload와 잠금/Play 상태
UI 검증은 별도 잔여다. HTTP 공유 transaction 결과를 해당 UI 검증으로 확대하지 않는다.
B1 progress 유지.

### 2026-10-09 B1 질량·속도·캐릭터 반경 실제 Inspector 검증

verify-physics-body-inspector-ui.ps1 -ExtendedFields로 최신 Release Editor에서 197명령을 통과했다.
Motion 검증을 재수행하고 질량2.5, 초기 선속도(1, 2, 3), CharacterMovement 반경0.75를
실제 포인터 클릭·Tab 이동·텍스트 입력·focus-loss로 적용했다. 각 입력은 초안 단계에서 Undo를
만들지 않고 확정 시 단일 Undo를 게시하며 Undo/Redo의 저장 문서 복원을 확인했다.
질량-1, 속도(NaN, 2, 3), 반경0은 문서와 Undo 기록을 변경하지 않고 거부했다.
최종 저장/재로드 의미동등성도 통과했다.
증거: Build/Verification/ContactStream/BodyInspectorUI-df252e5eb54e4dae815a3a8cacdc0edc/result.json 및 commands.jsonl.

Tab 이동에 따른 Inspector 자동 스크롤을 처리하도록 매 편집 전에 스크롤을 복원하고 현재
입력칸 좌표를 다시 읽는다. 첫 확장 실행의 스크롤 밖 클릭 실패는 수용 증거에서 제외했다.
검증용 EngineSettings.asset은 finally에서 원본 바이트를 복원한다.
이 결과는 대표 스칼라·벡터·캐릭터 입력의 검증이며 나머지 입력칸 전체, Shape draft
Add/Remove/Apply/Reload, 잠금 및 Play 상태의 직접 UI 검증은 잔여다. B1 progress 유지.

### 2026-10-09 B1 Shape 실제 Inspector 클릭·저작 소유권 수정

최신 Release Editor의 verify-physics-body-inspector-ui.ps1 -ShapeUI가 327명령을 통과했다.
Tab으로 기존 탐색 장부의 버튼 이름을 확인한 뒤 현재 화면 좌표로 직접 클릭했다.
Add 초안은 문서/Undo 불변, Apply는 Shape 1→2개 및 단일 Undo, Undo/Redo는 저장 문서
복원을 확인했다. Reload는 추가 초안을 폐기하고 뒤이은 no-op Apply도 Undo 불변이었다.
펼친 Shape의 Remove 초안과 Apply(2→1개), Undo/Redo도 통과했다.
잠금 및 Play 중 Add/Apply 버튼 클릭은 Undo를 바꾸지 않았으며 잠금 문서 불변,
Stop 전후와 최종 저장/재로드 문서 의미동등성을 확인했다.
증거: Build/Verification/ContactStream/BodyInspectorUI-0394bdccd21f46b8807a40855d8bab98/result.json 및 commands.jsonl.
같은 바이너리의 기존 HTTP 회귀55명령도 통과했다: http-244bbe5949dd450399c52ee770da277e/result.json.
프리팹 override 저장/로드, invalid 거부, Play 시뮬레이션 이동 및 Stop 복원을 재확인했다.

실제 Inspector Apply에서 Physics session owner violation이 발견됐다. SetShapes가 UI 저작
스레드에서 ScenePhysicsSimulation::Define을 직접 호출한 것이 원인이었다. SetShapes는
전체 정의 검증 후 저작 목록만 교체한다. 기존 Scene::StartPhysicsSimulation이 Play 시작 시
최신 목록을 CaptureDefinition/Define하므로 세션 소유 스레드 계약과 Play 변경 금지를 유지한다.
런타임 ReplaceShapes는 변경하지 않았다. UI Undo/Redo도 같은 저작 경계를 따른다.

표준 Shape 버튼/트리를 기존 탐색 장부에 delegated 항목으로 연결하고 현재 Nav 사각형을
스크롤 오프셋을 반영한 화면 좌표로 게시한다. 잠금 전환 후 숨겨진 이전 NavId는 실제
커서/active edit가 없으면 disabled-focus 오류로 집계하지 않는다. 실제 비활성 탐색/편집은 계속 집계한다.
화면 밖 활성화 시도 및 계측 오판 실행은 수용 증거에서 제외했다.
검증용 EngineSettings.asset 원본 복원(imguiScale1.5)을 확인했다.
Release 빌드는 성공했으며 RenderEngine.pdb LNK4020 경고는 별도 디버그 심볼 한계로 남긴다.
B1 progress 유지: 나머지 입력칸 전체 및 바디/캐릭터 입력의 잠금·Play 직접 UI 행렬은 잔여다.

### 2026-10-09 저장소 동기화 후 Release Editor 핵심 재수용

HEAD2965dc487의 GCCE·편집 스레드·렌더 변경과 로컬 Phase19 작업을 통합한 Release Editor를
재빌드했다. PostSync-2965dc487/editor-build.log 성공 및 공유 런타임33파일 배포를 확인했다.
관리 ABI40/198슬롯의 물리42·접촉44 checks, router70 checks/steady allocation0을 재수용했다.
같은 새 Editor에서 기본 ContactStream38명령, subscriber lifetime39명령,
명시 바인딩/role transition39명령을 각각2회 Play/Stop으로 통과했다.
Shape HTTP55명령도 통과해 invalid/Undo/Prefab/Play 시뮬레이션/Stop 복원을 확인했다.
ExtendedFields+ShapeUI 통합470명령은 Motion·질량·선속도·캐릭터 반경의 정상/invalid,
초안/단일Undo/UndoRedo 및 Shape Add/Remove/Apply/Reload, 잠금/Play 버튼 차단,
Stop/최종 저장로드 의미동등성을 통과했다.
통합 증거: Build/Verification/ContactStream/PostSync-2965dc487/result.json 및 각 gate 로그.
Inspector 증거: BodyInspectorUI-bd9fc8b4ad664be6a000a24c61d69896/result.json.

최초 UI 실행은 새 기본 도크의 표시336px보다 요청420px가 커 clipped 검사로 중단됐다.
이 실행은 수용에서 제외했다. verifier에 InspectorScale 인자를 추가하고 user scale0.75,
OS 배율을 포함한 effective1.125에서 요청/표시315px 동일을 확인한 후 통과했다.
기본 도크의 배율별 배치 수용을 이 입력 검증으로 대체하지 않는다. 입력 폭/화면 가시성 검사는
유지하며 검증 후 EngineSettings 원본(imguiScale1.5)을 복원했다.
반사 생성의 비대상 GameInput RG0101 진단1 및 빌드 경고는 기록으로 남긴다.

이번 재수용은 Release Editor 및 관리 핵심 범위다. 동기화 후 Development/Shipping Player
재빌드와 패키지 회귀, 전체 ABI40 전환 행렬/다중 DDOL/실패 조합, 전체 GPU capture와
대표 성능은 별도 잔여다. B1/E0 progress 및 M3/M4 대기 상태를 유지한다.

### 2026-10-09 동기화 후 Development·Shipping Player 핵심 재수용

HEAD2965dc487 및 로컬 Phase19 변경으로 최신 BuildTool을 Release 빌드한 뒤 publish-engine
Release Build와 Release Shipping Build를 순차 수행했다. 두 SDK manifest/host 검사 및 ABI40 배포를 통과했다.
Development: local-0.0.0.0-win-x64-Release-7a05d095-09b6-45ef-8bfa-5f24c0e688fb.
Shipping: local-0.0.0.0-win-x64-Release-Shipping-e152c661-8b90-4d34-9868-6875ddb8b5fc.
두 배포는 Build/Verification/ContactStream/PostSync-2965dc487 아래에 보존한다.

최신 Editor의 기본/SubscriberLifetime/ExplicitBinding fixture를 각 SDK로 새로 패키징했다.
Development와 Shipping 각각3범위 모두2000GT·완료 표시·exit0·패키지 SHA 불변 통과.
Development display promotions1909/1938/1942, Shipping1942/1945/1946이며 성능 비교값이 아니다.
구독 수명과 명시적 역할 재바인딩은 각 probe의 Begin/Persist/End 및 stale 수명 계약을 확인했다.
각 Shipping 실행의 service compiled=no/enabled=no와 endpoint 부재도 확인했다.

SetPlanarInput/CreatePlanarVelocity 새 캐릭터 입력 패키지도 각 SDK에서 새로 생성했다.
Development16/0·tick241·exit0·snapshot 없음·232파일 불변,
Shipping16/0·tick244·2000GT·display1999·promotions1944·parser0·serviceCompiled=false·231파일 불변 통과.
아날로그/대각선 제한·가감속·회전 정책 수용이며 실제 게임 콘텐츠 운동 골든을 의미하지 않는다.

별도 verify-player-shipping-isolation -Config Release -SkipBuild도 통과했다.
Development socket import/WSAStartup/endpoint/CommandService 존재와 Shipping 각 부재를 비교해
빈 서비스 구성의 자명한 성공을 배제했다. Development/Shipping runtime DLL은 별도 경로를 유지한다.
통합 증거: Build/Verification/ContactStream/PostSync-2965dc487/Player/result.json 및 각 publish/package/verify 로그.
모든 gate는 새 바이너리/패키지로 통과했고 기존 스테이징 문서118줄 blob은 그대로 보존했다.

이번 수용은 위3 ContactStream fixture와 primitive character 입력 범위다.
ABI40 전체 전환·topology·다중 DDOL·실패/해제 조합, 실제 콘텐츠 경사/계단·이동 골든,
전체 Editor GPU capture·대표 성능·M3/M4 최종 감사는 잔여다. B1/C1/E0 progress 유지.

### 2026-10-09 B1 바디·캐릭터 전체 입력과 대표 잠금/Play 검증

verify-physics-body-inspector-ui.ps1 -AllFields -InspectorScale0.75로 최신 Release Editor의
바디9개와 캐릭터12개 입력21개를 실제 클릭·Tab·타이핑·focus-loss 경로로 검증했다.
Motion은 기존 공통 검증을 재수행했고 나머지20개는 fieldResults에 개별 결과를 기록한다.
정상값 단일Undo·Undo/Redo 저장 문서 복원, 잘못된 값 거부 시 Undo/문서 불변,
최종 저장/재로드 의미동등성 통과. 전체1185명령.
바디: Motion/질량/중력bool/이동·회전 locks/선·각 damping/초기 선·각 velocity.
캐릭터: 반경/높이/contact offset/step/slope/gravity/minimum distance/
acceleration/braking/jump/max fall/initial velocity.
잘못된 bool 텍스트, 축 잠금8, 음수 감쇠·이동 계수, 0 치수·점프/낙하 속도,
범위 밖 slope 및 NaN 스칼라·벡터를 각 필드의 현재 유효성 계약에 맞춰 거부했다.
증거: BodyInspectorUI-f5b35d0bc86346e397df66f3f4c013ec/result.json 및 commands.jsonl.

-LockedInputs 별도 세션144명령 통과. 두 컴포넌트 각각 대표 첫 입력인 Motion/반경을
잠금 및 Play에서 직접 클릭·텍스트 입력했다. activeId0을 확인해 입력 활성화를 거부하고,
잠금 문서/Undo 불변 및 Play editUndo/gameUndo 불변·Stop 문서 의미동등 복원을 확인했다.
증거: BodyInspectorUI-8645c9010d0d4de89dc1453a9768c342/result.json 및 commands.jsonl.
이는 공통 BeginDisabled 경계의 대표 입력 검증이며21개 입력 각각의 잠금/Play 행렬이라고 주장하지 않는다.
통합 증거: Build/Verification/ContactStream/InspectorAllFields/result.json.

이번 변경은 verifier 확장이다. native 제품 코드/ABI/배포는 변경하지 않았다.
user scale0.75/effective1.125에서 전체 가시 입력 조건을 유지했고 EngineSettings 원본을 복원했다.
B1 직접 입력 정상/invalid 검증의 누락은 해소했다. 기본 도크의 배율별 배치/가시성,
형상 상세 입력/공유 자산의 전체 UI 수용 및 전체 제품 게이트는 별도 범위로 B1 progress 유지.


### 2026-10-09 B1 형상 종류 UI 트랜잭션 검증

- `verify-physics-body-inspector-ui.ps1 -ShapeDetails -InspectorScale 0.75`: 최신 Release Editor 실제 Tab/Space/Down/Enter 입력으로 Box → Sphere 선택. 직접 속성 setter 없이 검증.
- Apply 전 저장 문서/Undo 불변, Apply 한 번의 Undo, Sphere 직렬화(kind=1), Undo/Redo 정확한 문서 복원 통과. 기존 Add/Remove/Reload·잠금/Play 버튼 차단·Stop 복원·재로드 저장 검증 포함 442명령 통과.
- 증거: `Build/Verification/ContactStream/BodyInspectorUI-48e48611f938435ca48f8f343a3cf7cd/result.json`, `commands.jsonl`. 사용자 배율0.75/실효1.125의 제한된 검증이며 전체 배치 통과를 의미하지 않는다.
- B1은 progress 유지. 치수·역할·레이어·공유 geometry UUID/revision 실제 UI 입력 및 기본 도크 배율별 배치 검증은 잔여. 이번 변경은 검증 스크립트와 문서이며 제품 바이너리 변경 없음.


### 2026-10-09 B1 역할·geometry 참조 입력 수정과 검증

- 형상 전용 typed Draw 분기에서 문자열을 초안의 실제 버퍼로 편집하도록 수정. 프레임별 복사본과 별도 InputManagement Enter 판정 제거. 다른 컴포넌트 문자열 입력은 변경하지 않음.
- 기존 편집 분기가 없던 `geometryRevision`을 ImGui U64 입력으로 제공. 전체 형상 Apply가 공통 검증 및 한 번의 Undo를 계속 소유하며, 잠금/Play disabled 상태를 유지. 역할·geometry UUID·revision에 탐색 이름 제공.
- 새 Release Editor 빌드 통과: `Build/Verification/ContactStream/shape-detail-input-build.log`. 공유 runtime hash `858bcd0c57bb22ccd0646b0ec228a99422a8220251e71d630028461706f627a9`.
- `verify-physics-body-inspector-ui.ps1 -ShapeReferences -InspectorScale 0.75`: 실제 Tab/text로 역할 UUID·geometry UUID·revision `9007199254740993` 입력. Apply 전 문서/Undo 불변, Apply 한 번의 Undo, 정확한 문자열/64비트 정수 저장, Undo/Redo 문서 복원 통과. 기존 종류·Add/Remove/Reload·잠금/Play·Stop·씬 재로드 검증 포함 585명령 통과.
- 증거: `Build/Verification/ContactStream/BodyInspectorUI-fa9110a2d6024d6f90cc456313e2ad5d/result.json`, `commands.jsonl`. 최초 실행은 검증 스크립트가 Tab으로 활성화된 입력을 Enter로 종료하여 실패; activeId 사전조건과 중복 Enter 제거 후 재검증 통과. 실패 로그 `shape-reference-ui.log` 유지.
- 범위: Box 형상에서 참조 필드 저작/직렬화만 검증. 실제 cooked geometry 로딩·공유 자산 연결·참조를 유지한 재로드 및 Player 동작은 이 결과로 완료 처리하지 않음. 치수·레이어 상세 입력·invalid 참조 거부·배율별 배치는 잔여이며 B1 progress 유지.


### 2026-10-09 B1 반경·기존 레이어 UI 및 초안 Undo 소유권 수정

- 형상 종류/레이어 Combo와 float 필드에 이름을 제공. 공통 수치 위젯보다 형상 필드 이름을 먼저 등록하여 실제 반경 입력을 식별. 기존 프로젝트 layer catalog의 선택기를 그대로 사용하며 별도 물리 레이어를 추가하지 않음.
- 실제 UI 검증에서 형상 수치 편집이 `CommitMemberChange`의 일반 CustomChangeCommand를 통해 Apply 이전 Undo를 만들던 결함 발견. 이동/폐기 가능한 초안 주소를 Undo에 보관하는 경로를 형상 전용으로 제거. 수치/벡터/체크박스 초안 편집은 값만 바꾸고 전체 Apply가 유일한 검증/Undo 소유자.
- 최신 Release Editor 빌드 통과: `shape-dimensions-undo-build.log`, runtime hash `7e1cccb26f79aae137cfe09a676895cbc8bf202711dc6abbe7393a5dd8039d6f`.
- `verify-physics-body-inspector-ui.ps1 -ShapeDimensions -InspectorScale 0.75`: 실제 UI Sphere 선택·반경0.875·기존 layer1 선택, Apply 전 문서/Undo 불변, 단일 Apply, 음수 반경 거부 시 문서/Undo 불변, Reload 후 Undo/Redo 정확한 문서 복원 통과. Editor 로그의 `Invalid sphere radius`로 실제 검증 거부 확인. 기존 Add/Remove/잠금/Play/Stop/씬 재로드 포함751명령 통과.
- 증거: `Build/Verification/ContactStream/BodyInspectorUI-fc29d4ae2fb34260967b7304abaab0f5/result.json`, `commands.jsonl`, `editor.out`. 식별 실패 `shape-dimensions-ui.log`와 Undo 개수 실패 `shape-dimensions-ui-retry.log` 유지.
- 범위: Sphere 반경과 기본 프로젝트 layer1 대표 검증. Box/Capsule 벡터 치수·사용자/retired 레이어 조합·역할 invalid·실제 cooked 공유 geometry 연결/참조 유지 재로드/Player·기본 도크 배율별 배치는 잔여. B1 progress 유지.


### 2026-10-09 B1 Box·Capsule 치수 실제 UI 검증

- `verify-physics-body-inspector-ui.ps1 -PrimitiveDimensions -InspectorScale 0.75` 추가. 최신 Release Editor에서 Tab/text 입력으로 Box 반치수 X=0.75/Y=1.25/Z=2.5, Capsule 반경0.625/반높이1.5 저작. 제품 코드/바이너리 변경 없이 검증 범위 확대.
- 각 형상 Apply 전 문서/Undo 불변, 단일 Apply, 정확한 저장값, invalid Box Y=-1 및 Capsule halfHeight=-1 Apply 거부 시 문서/Undo 불변, Reload 후 Undo/Redo 정확한 문서 복원 통과. 실제 Editor 로그 `Invalid box dimensions`, `Invalid capsule dimensions` 확인.
- 기존 형상 Add/Remove/Reload·잠금/Play 버튼 차단·Stop 복원·최종 씬 재로드 저장을 포함1186명령 통과. 증거 `Build/Verification/ContactStream/BodyInspectorUI-6e88acd0f14147d099a9453d513d7323/result.json`, `commands.jsonl`, `editor.out`; 정상 치수 문서 `Box-dimensions.creator`, `Capsule-dimensions.creator` 보존.
- 제한: 치수 변경 문서는 각 Undo/Redo 왕복만 확인하며 최종 재로드는 원래 형상으로 돌아온 문서를 대상으로 함. 치수 유지 재로드/Player와 실제 cooked 공유 geometry 연결·사용자/retired 레이어·역할 invalid·기본 도크 배율별 배치 등은 별도 잔여. B1 progress 유지.


### 2026-10-09 B1 치수 유지 실제 씬 활성화/PlayStop/재로드

- 새 `verify-physics-primitive-reload-http.ps1 -FixturesDirectory Build/Verification/ContactStream/BodyInspectorUI-6e88acd0f14147d099a9453d513d7323`로 기존 실제 UI에서 저작한 Box/Capsule 치수 문서를 별도 경로에서 검증.
- 현재 CLI `scene.load`는 비동기 준비만 수행하고 활성 씬을 바꾸지 않음. `scene.switch`로 activationRequested 확인, 해당 requestId의 `scene.load.status`가 Ready인 것을 기다린 뒤 저장 비교. Sentinel 씬에서 시작하여 실제 전환을 강제. 기존 Inspector verifier의 마지막 재로드도 이 계약으로 수정(전체 입력 gate 재실행은 별도).
- 로드 시 `m_sceneName`은 파일 stem으로 갱신되는 정상 메타데이터. 예상 문서는 이 항목만 명시적으로 보정하며 물리 치수/Entity/나머지 모든 저작 값을 의미상 비교. 원본 문서는 SHA256 불변 확인.
- Box XYZ=0.75/1.25/2.5, Capsule radius=0.625/halfHeight=1.5를 유지한 첫 활성화 저장·PlayStop 복원·두 번째 활성화 저장, 두 종류 총6회 문서 비교 및28명령 통과. 증거 `Build/Verification/ContactStream/PrimitiveReload-8ff1eeec4aeb4e4cb576a2e9a0e7d92e/result.json`, `commands.jsonl`; Editor runtime SHA256 `FB0BB88CE7E4327061EA9293A0D601C8963ABB5AD5B20C1E992185DCF1B86F69`.
- 이전 B1 Inspector UI1185/144/442/585/751/1186 gate의 입력/Undo/PlayStop 결과는 유지하되, 마지막 `scene.load` 직후 저장만으로 실제 활성 씬 재로드를 증명했다는 주장은 철회. 이번 결과가 Box/Capsule 치수 유지 실제 활성화 증거를 제공하며 다른 입력 조합 재로드를 소급 완료하지 않음.
- 실패 기록: 최초 `primitive-reload-http.log`는 활성화 미요청으로 UIBody 없음; retry는 sceneName 메타데이터 불일치. final 로그만 accepted. 제품 코드/바이너리 변경 없음.
- B1 progress 유지. 실제 충돌·Player 치수 검증 및 cooked 공유 geometry 연결·역할 invalid·사용자/retired 레이어·배율별 배치 잔여.


### 2026-10-09 B1 실제 공유 cooked convex 연결 및 루트 형제 순서 복원

- `verify-physics-shared-geometry-http.ps1` 추가: geometry.create로 tetra convex 자산 UUID/revision1/카탈로그 등록을 저작하고 두 동적 바디에 동일한 키를 physics.shapes로 연결. Sentinel 씬에서 scene.switch 요청 Ready를 기다려 실제 재로드.
- 첫 재로드/저장·Play에서 두 바디 모두 Y=5→4.9019 이동·Stop 두 위치와 전체 저작 문서 복원·두 번째 재로드/저장3회 문서 비교 및33명령 통과. geometry 원본 SHA256 불변. 증거 `Build/Verification/ContactStream/SharedGeometry-03b6915287c24f8e92a09bef0ad0689a/result.json`, `commands.jsonl`; UUID `988173d2-58bf-4eb0-86f1-9d07e330b48f`.
- 두 번째 재로드 검증에서 기존 RemapLoadBatchIndices가 루트 자식을 batch/슬롯 순서로 재구성해 형제 순서를 바꾸던 결함 발견. 부모 관계로 유효한 소속을 확인하면서 직렬화된 루트 children 순서를 먼저 복원하고, 누락 자식은 기존 부모 기준 복구 루프로 추가. 배치 밖 DDOL 항목 보존/중복 방지 경로 유지. 비교에서 형제 순서를 무시하지 않음.
- 최신 Release Editor 빌드 통과: `shared-geometry-order-build-final.log`; runtime SHA256 `335990744824B2EAA16DEA828239B444E195CA1E44A811DA8B738FB881F7FED8`. 빌드에는 RenderEngine.pdb 형식 레코드 LNK4020 경고가 남음(일부 디버거 심볼 제한); 런타임 gate 실패와 구분.
- 동일 새 바이너리로 Box/Capsule 치수 유지 실제 재로드/PlayStop6회 문서비교28명령 재통과: `PrimitiveReload-db0da467485e4f2c94a79338ea5331aa/result.json`.
- 최초 생성 실패는 미생성 대상 폴더, retry 실패는 실제 형제 순서 변경. 두 실패 로그 유지하고 final만 accepted. 검증 자산은 작은 source/meta/revision/cooked 파일로 보존하며 배포 바이너리 복제 없음.
- 범위: HTTP 저작·같은 자산 키 두 바디·실제 Editor cooked 로딩/운동/복원/재로드 검증. Inspector 자산 선택·메모리 공유 할당 수·충돌 형상 정확도/Player·geometry revision 교체 UI·역할 invalid·사용자 레이어/배율별 배치·전체 DDOL/오염 계층 조합은 미검증. B1 progress 유지.


### 2026-10-09 B1 종료 조건 고정

이번 묶음의 필수 종료 조건은 (1) primitive/compound/sensor 및 cooked source/cook 공유 API, (2) 실제 geometry UUID/revision의 Inspector 저작·revision 변경·invalid 역할/참조 거부, (3) 공통 레이어 선택, (4) 단일 Apply/UndoRedo·잠금/Play 차단·저장/실제 활성 재로드/Stop 복원·Prefab 보존, (5) 기본 도크의 대표 배율0.75/1.0/1.25에서 입력과 작업 버튼 가시성이다. 기존 native 다중 구성/GPU·cook/패키지 검증과 최신 제품 증거를 함께 대조한다.

성능/할당 수 전체 측정은 T1/M3, 전체 Shipping/콘텐츠 회귀는 M1/M3, GPU 완료 캡처는 M3, 전체 생명주기/DDOL 감사는 M2/E0/M4에서 소유한다. 이들을 B1에 중복 잔여로 붙이지 않는다. 자산 브라우저 picker UX 확장은 문자열 UUID/revision 편집이 실제 자산 연결을 충족하는 한 후속 개선으로 기록한다. 필수 조건 실패는 B1 완료를 차단한다.


### 2026-10-09 B1 완료 — 필수 종료 조건 묶음 수용

**B1 완료.** 기존 primitive/compound/sensor/shared cook/source/CEMF/pak/native 다중 구성·GPU 기반 증거와 아래 최신 Editor 제품 게이트를 합쳐 고정된 저작 종료 조건을 충족했다. 종료 후 개선을 B1 잔여로 다시 붙이지 않는다.

- 실제 자산 revision1/2를 geometry.create/update로 생성; UUID 유지 및 source update Undo/Redo를 확인. Inspector에서 실제 convex UUID/revision1 연결 → revision2 교체 → Undo/Redo, invalid contactRole/geometryAsset/revision0 거부, 단일 Apply·문서/Undo 불변·잠금/Play·Stop·최종 scene.switch/Ready/저장 통과.1181명령: `BodyInspectorUI-9b4650fd56ee4c84937da7e405350bc1/result.json`.
- UI 저작 revision2 문서를 새 Editor에서 실제 활성 재로드·PlayStop·두 번째 재로드3회 비교/15명령 통과: `GeometryUIReload-fcfff197ac5341f6bf137028c965835e/result.json`.
- 기본 도크 안의 요청280 논리 폭 Inspector 영역을 가용 폭으로 제한하고 Add/Apply/Reload 버튼을 줄바꿈. 배율0.75/1.0/1.25 각각 overflow0·contentWidth≤visibleWidth·입력폭≥80px/12명령 통과: `BodyInspectorUI-994a3ed11bf04100836aef934a4cd453`, `BodyInspectorUI-9535815f38d444cc89b9ef98cf839bf0`, `BodyInspectorUI-8ec75ea27ba34d9aae1f35c720e81ae6`. 전체 실제 자산 UI 자극은 배율1.0에서 실행했으며 모든 배율의 모든 입력 조합이라는 주장은 아님.
- Prefab 형상/역할 override 저장·실제 scene.switch/Ready 활성 재로드·Play 거부/운동/Stop56명령: `Build/Obj/Phase19ShapeAuthoring/http-37a2ee6c53bc459cb9fad6c52743b37e/result.json`. 이전 verifier의 준비 전용 scene.load는 실제 활성화 대기로 교정.
- 동일 새 바이너리에서 같은 cooked convex 키 두 바디 재로드·양쪽 운동·Stop 저작복원·두 번째 재로드 재통과: `SharedGeometry-c009086300b245c8a2aea4018556edf0/result.json`.
- 실제 UI 자산 Apply 실패를 통해 SetShapes→CaptureDefinition이 Presentation 스레드에서 owner-only geometry 캐시를 읽던 결함 발견. SetShapes는 transform/layer/shape 저작값만 ValidatePhysicsShapes로 검증하며 캐시/SDK 접근을 하지 않음. StartPhysicsSimulation이 owner 스레드에서 cooked definition을 해석하는 기존 책임 유지. 새 Release Editor 빌드 `b1-closure-owner-build.log` 통과.
- 통합7개 제품 게이트 증거: `Build/Verification/ContactStream/B1Closure/result.json`. 최초 UI 실패 `b1-closure-ui.log`는 스레드 소유권 결함 증거로 보존하고 final gate만 accepted.

후속 책임: 대표 성능/할당은 T1/M3, 전체 콘텐츠/최신 Player 회귀는 M1/M3, GPU 완료 캡처는 M3, 전체 DDOL/생명주기는 M2/E0/M4. 자산 브라우저 picker UX는 Phase21 후속 개선이다. B1 필수 조건의 실패를 이관한 것이 아니라 해당 항목의 원래 제품 전체/UX 확장 범위를 중복 제거한 것이다.

## C0 종료 묶음 — 현재 Editor 수용 (2026-10-09)

C0 필수 종료 범위는 독립 CCT, 이동·점프·force·teleport·활성화, 경사/계단,
C# 소유권·수명, 저장 후 실제 재활성화, Play/Stop 원복, DDOL 정상/실패 복원,
대표 cooked Player 실행이다. 전체 콘텐츠 골든과 성능은 기존 C1/M3/T1 담당이다.

현재 Release Editor 두 제품 게이트 544명령 통과:
- http-721288e4ef034c74b008196ff7abb377: 340명령, CLR 18/0,
  낮은 계단 통과/높은 계단 차단, 점프/force/disable/Stop,
  실제 저작·저장·활성화한 triangle mesh의 20도 경사 통과(x2.95/footY1.1058),
  60도 경사 차단(x-0.309808), slope limit 0 통과(x2.98333/footY5.66729).
  각 경사 150개 이상 completed tick 진행과 Stop 저작 위치 원복 확인.
- http-4c15a7e0bf414c628187ddf63b598ea6: 204명령, CLR 18/0,
  회전/scale 부모·비활성 자식 DDOL, 잘못된 목적지 실패 후 Editor 원복·재Play 검증.

기존 gate의 scene.load는 준비 요청뿐이어서 실제 재로드 증거가 아니었다.
Sentinel 씬으로 전환 후 scene.switch → load.status Ready를 기다리도록 수정했다.
정상 DDOL 전환도 같은 완료 조건을 사용한다. 계단 벽의 순간 비접지 상태가
점프 검사에 섞이지 않도록 평지로 teleport 후 접지를 기다려 독립 검사한다.

보존 인덱스: Build/Verification/ContactStream/C0Closure/result.json.
바이너리 사본 없이 JSONL/로그/작은 입력만 보존했다. 실패한 최초 점프 실행은 수용하지 않는다.
과거 P19Mesh/P19Geo/P19Fatal/P19Transition Player 결과는 문서에 있으나 현재
Build/Obj 원본은 없다. 현재 공유 runtime의 fresh cooked Player 수용이 남아 C0는
아직 progress이며, 다음 작업은 이 마지막 실행 묶음이다. 이번 Editor gate만으로
현재 Player를 재검증했다고 주장하지 않는다.

## C0 완료 — 현재 cooked Player 수용 (2026-10-09)

C0 종료 조건을 완료했다. 현재 소스에서 publish-engine Release -Build로 Editor,
Player 및 cooker/packer를 빌드하고 정상 Development SDK로 새 Geometry 프로젝트를
패키징했다. 바이너리 교체나 과거 패키지 재사용 없이 정상 배포/패키징 경로를 사용했다.

새 cooked Player: run-81eacfec8aa7485c977dd5c4ecc1263e/result.json.
Triangle mesh·convex·heightfield 이동 검사 총36개 통과/실패0개,
DDOL C# 계약8개 통과/실패0개, 세 노드 형상 이송·재생성·핸들·목적지 mesh 접지
검사29개 통과/실패0개. CEPG3개, 저작 cegeometry fallback0개,
목적지 mesh import1/assets1/cook0, 정상 렌더 진행·종료0 및 패키지 파일 SHA 불변.

첫 run-df5e116acbe24d1380204f1996481205는 verifier 기본 목적지 설정 오류로 미수용:
형상3개 출발 씬 재로드에 목적지 단일mesh import1을 요구했다.
PhysicsCharacterMesh.creator를 실제 목적지로 지정해 전체 재검증했다.
GeometryDdol의 기본 목적지도 같은 mesh 씬으로 수정해 재발을 방지했다.

통합 완료 인덱스: Build/Verification/ContactStream/C0Closure/result.json.
현재 Editor544명령 및 현재 Development Player73개 검사 실패0으로 C0 완료.
이번 실행은 Release Development 범위다. Shipping/Debug 현재 소스 전체 행렬과
실제 콘텐츠 골든·성능은 기존 M3/C1/T 담당이며 이 결과로 검증했다고 주장하지 않는다.

## C1 완료 — 현재 저장소 이전 범위 (2026-10-09)

종료 범위: 현재 프로젝트 Scene/Prefab의 legacy 캐릭터 전수 조사, 명시적 기준
fixed tick에 대한 단위/변환 정책, P0 변환 문서의 제품 이동·점프·Play/Stop 회귀.
사용자가 초기 실제 물리 씬이 없다고 명시했고 HTTP 저작을 요청한 범위를 따른다.
외부 프로젝트의 미제공 운동 골든을 현재 구현 잔여로 계속 추가하지 않는다.

현재 corpus137개 조사: legacy CCT1개는 P0 component fingerprint 일치,
실제 콘텐츠 검토 대상0개. 원본 SHA 불변. 단위 변환22개 및 스키마 이전112개
검사 통과. 현재 Release Editor http-42d8859752104dc6bde79ea2e1297ecf:
변환 P0 실제 씬 활성화 Ready 확인 후 Play/Stop2회, 점프·소유권/신원·저작값 원복,
completed fixed tick 기준 1.5m/s 수평 이동을 각60개 이상 tick 동안 검사했다.
예상 거리와 차이3cm 이하, 원본 씬/레이어 SHA 불변. 검사 입력은 명시적 CLI속도다.
C0 완료 묶음의 경사/계단/force 회귀를 재사용하며 새 입력/facing 계약은 기존
PlanarInput 제품 검증으로 별도 기록되어 있다.

통합 결과 Build/Verification/ContactStream/C1Closure/result.json.
C1은 현재 저장소 이전 범위에서 완료다. 외부 게임 프로젝트/게임별 운동 골든은
검증하지 않았으며 이 결과가 그 콘텐츠의 운동 동일성을 보장하지 않는다.
전체 콘텐츠 수용과 현재 구성 행렬/성능은 M3의 기존 범위다.

## T1 완료 — 현재 worker 예산/계측 수용 (2026-10-09)

기존 제품의 after-fetch 및 SDK 전용 dispatcher 정책을 유지한다. 기존 계측 게시
최적화 구현을 대상으로 현재 소스 standalone benchmark를 다시 빌드했다.
소스 생성기의 PhysicsTestHooks 상대 include와 새 ProfileRecording.cpp 링크 누락을
수정했다. profiler record는 비동기 시작 요청이므로 wait_until_idle 후 recording을
확인하고 측정한다. 시작 누락으로238/239tick만 기록된 첫 결과는 미수용이다.

CPU/GPU × 활성16/256/1024 × worker요청0/1/2/4 × 계측on/off,
정방향/역방향을2회 반복해 자유 낙하192회, 접촉1024바디 계측on32회, 총224회.
각240tick 측정. 계측on128회는 필수 비용 마커240tick 및 task submit/run/complete
신원·계층 검사 통과. 현재 auto는12 hardware threads에서 실제8workers다.

4개 run의 median 기준, 계측off CPU16: worker1 평균59.03us/p9977.50us,
worker2 69.93/95.75us, worker4 127.56/187.80us, auto8 442.35/628.75us.
계측on CPU접촉1024: worker4 평균1332.13us/p991677.55us,
worker1 1904.90/2462.85us. workload가 달라 전역1worker 변경은 채택하지 않는다.
작은 자유 낙하 CPU는 명시1worker, 접촉 많은 CPU는 명시4worker를 이 장비의
검증된 후보 예산으로 기록한다. GPU256/on/worker4 CV11.75% 그룹은 비용 비교에서
제외한다. 다른55개 그룹 CV10% 이하. 프로파일 계측on 비용은 제품 지연으로 해석하지 않는다.

현재 Editor/Development cooked Player 정상 실행은 C0Closure 완료 증거를 참조한다.
T1의 실행정책·최적화·계측·worker/workload 선택 근거는 완료로 판정한다.
전체 제품 평균/p99/메모리, 다른 장비와 전체 콘텐츠의 수용은 기존 M3 범위다.
보존 인덱스 T1Closure/result.json, measurements.json 및 원시224개 기록.
바이너리/캡처 사본은 추가하지 않았다. 기존 Build/Obj capture 위치는 원시기록에 유지한다.

## T2 회전 kinematic 쿼리 구조 검증 (2026-10-09)

길쭉한 box kinematic을 이동+45도 회전시켜 batch overlap을 검사했다.
완료 tick 이전에는 미래 위치에 hit 없음, 완료 tick 이후 새 회전 위치의 hit1과
등록 owner 일치, disable 이후 hit0 및 이전 SDK hit의 Binding 거부를 확인한다.
현재 소스 Release510개/Shipping506개 검사 통과, 두 구성 모두 실제GPU 확인.

probe 빌드의 새 ProfileRecording.cpp 링크 누락을 수정했다. 비동기 record 시작을
기다리고, 정착200tick 각각 publish_frame하여 단일프레임 누적을 제거했다.
Release capture complete/unacked0 및 QueryBatch/QueryStructureUpdate Scene 신원 검사 통과.
처음 capture complete 실패 실행은 미수용이며 수정 후 전체 재실행했다.

T2Closure/result.json 및 session-Release/Shipping.json에 작은 결과/로그를 보존한다.
T2는 아직 progress: 실제 Player 충돌/회전/kinematic 혼합 쿼리와 동적capture 검증이
남아 있다. 이번 standalone 결과로 제품 검증이나 전체성능 수용을 대체하지 않는다.

## T2 실제 mixed Player 기능 수용·capture 미수용 (2026-10-09)

새 PhysicsQueryMixed fixture 및 PhysicsB2MixedQueryProbe 추가. 현재 정상 SDK로
cooked Player 패키징: 동적바디/정적벽 충돌정지 x103.99949, kinematic 90도회전과
이동 전후 scalar/batch count·Entity/component/shape 신원 검사32개 통과·실패0.
128 dynamic out-and-back/4096출력 검사136개 통과·실패0, 기존104개 제품 검사,
실제display·exit0·패키지불변 수용(run-d32422a9632748d59961e85f88f7c33e).

profiler record/pause/save 비동기 writer 및 save 완료를 기다리도록 verifier 수정.
기록 시작 gate 뒤에 mixed/dynamic 검사를 실행하며 성능벤치마크의 고정 dense
위치 가정과 동적 이동은 같이 실행하지 않는다. capture 검사 기준은 완전성을 유지한다.

현재 fresh SDK/패키지에서도 capture56frames, unacked0이나 sourceDroppedCounters57,
completefalse. 마지막 프레임 게시만으로 해결된다는 가설은 전체 재검증에서 실패했고
PlayerCommands의 speculative pause 변경은 되돌렸다. 해당 빌드 결과는 source와
pause 구현이 다르므로 후속 게이트 전에 다시 빌드해야 한다. capture는 미수용.
T2 progress 유지: source counter 귀속/유실 경로 수정 및 무손실 동적 capture가 남는다.
결과/성공기능/실패로그를 T2Closure에 작은 파일로 보존, 바이너리 사본 추가 없음.

## T2 쿼리 배치 완료 — 실제 동적 Player 무손실 capture (2026-10-09)

이 절이 이전 T2 progress/capture 미수용 기록의 현행 상태를 대체한다. T2 완료.
정상 전체 재빌드 SDK OwnerSafePointDistribution과 새 cooked 패키지로 재검증했다.
실제 혼합 충돌·90도 회전·kinematic 이동 검사40개, 동적128바디 검사156개,
기존 제품 검사104개 통과·실패0. 동적 query 읽기창46회/pose변경45회,
4096-hit 출력3단계/이탈0hit/복귀128hit/128바디 정지 확인.
2,000프레임, display승격1859회, exit0 및 패키지불변 통과.

render submission ID와 엔진 profiler frame ID를 혼용하던 counter 귀속을 분리했다.
호스트가 EnhancedLiveFramePacket.profilingEngineFrame을 게시하고 counter/VRAM/GPU
계측은 그 신원을 사용한다. render/fence/display 신원은 기존 submission ID를 유지한다.
GPU capture admission은 deferred resource preparation 성공 뒤로 옮겼다.
준비보류로 제출하지 않은 프레임을 GPU실패로 세던 순서 오류를 제거했다.
enkiTS 워커는 new-task/task-completion suspend 진입 전에 자기 스트림을 게시한다.
다른 스레드의 TLS를 수집기가 변이하지 않는다. worker pool10417개 및 barrier변이 검출 통과.

동적검사 gate 이후 record/pause/save를 비동기 완료까지 기다린 실제 capture는
complete=true, unackedCPU0, pendingGPU0, failedGPU0,
writer/source frame/event/counter/lateCPU/lateGPU 손실 모두0.
파일 직접 읽기와 capture 종료 counter로 확인했다. 열린 스코프2개는 Stop cutoff에
잘린 경계이며 미응답/손실이 아니다. 이번 구간 admittedGPU0이므로 GPU 타이밍
수용을 주장하지 않는다. 실제 GPU타이밍/전체성능·p99 수용은 기존 M3에 남는다.

정본 evidence: Build/Verification/ContactStream/T2Closure/result.json.
원본 capture: Build/Obj/Phase19B2Player/run-1a04af951f254d91a3f99c4a97f0d02c/query.ceprof.
small receipt/log만 T2Closure로 보존하고 capture/바이너리는 복제하지 않았다.
SDK bundled DotNet deps 누락 경고와 LNK4020 PDB 경고는 별도 환경 제한이다.
Phase19 상태: 완료16 / 진행2(M1,E0) / 미착수2(M3,M4).

## E0 다중 DDOL 부모·자식 계층 — Development 제품 수용 (2026-10-09)

PhysicsHierarchyContactProbe와 독립 HTTP/Player 게이트 추가. 부모와 자식 각각
PhysicsBodyComponent 및 ContactStream을 가진 실제 HTTP 저작 씬으로 검사했다.
Editor는 부모와 자식 모두 DDOL 등록(중복 계층 이송 경계), 같은 목적지 씬을
새로 로드해 연속3회 전환했다. 두 스크립트 OnBeginSimulation 각각1회 유지,
OnAddedToScene 각각1→4, compound sensor Begin2/Persist≥2가 매번 재개됐다.
이전 씬 target 신원 유입은 예외로 차단하며 native body wrapper 신원도 유지됐다.
부모·자식 링크와 로컬(0,5,0), Stop 후 4엔티티 pose/enabled/component 신원,
계층 및 source/destination 파일 SHA 불변, stream2/2 해제를 단정했다. HTTP85명령.

현재 T2 OwnerSafePoint SDK를 그대로 사용해 새 cooked Development Player 패키지
수용: root DDOL에 자식이 동반 이송, 각각 새 목적지 Begin2/Persist≥2·body wrapper
동일·OnBeginSimulation1/OnAdded2·stream2 유지, 최종 Scope stream2/2 해제.
2014GT, 목적지 표시8회 이후 정상 종료0, 패키지 불변, 실제 물리8종 ledger 생성/해제
일치. Editor 연속3회와 Player1회 범위를 구분하며 Shipping/실패 조합은 확장 주장하지 않는다.

정본 Build/Verification/ContactStream/HierarchyClosure/result.json 및 원본 Editor/Player
receipt 참조. 바이너리/프로젝트 중복 보존 없음. E0 progress 유지하되 다중 DDOL
부모·자식의 Editor/Development 완료 범위는 잔여에서 제거한다. 다음은 같은 계층의
최신 Shipping 수용 및 현행 전환/fault 전체 suite 통합이다. M3 GPU/성능 수용과는 별도다.

## E0 다중 DDOL 계층 Shipping 수용 완료 (2026-10-09)

직전 다중 DDOL의 Shipping 잔여를 닫았다. 최신 소스로 Release-Shipping 전체 정상
빌드·SDK publish 후 기존 계층 프로젝트의 source/destination/script SHA 일치를
확인하여 입력만 재사용하고 새 cooked Shipping 패키지를 생성했다. 패키지 host DLL
SHA가 새 SDK와 일치하며 바이너리 교체나 Development 실행 대체는 없다.

부모·자식 모두 source/destination sensor Begin2/Persist≥2, body wrapper 신원 유지,
OnBeginSimulation1/OnAdded2, stream2 유지/최종2해제, 실제 물리8종 ledger 생성/해제
일치. 2014GT·목적지display8·exit0·패키지 입력 불변 수용. Shipping service disabled,
endpoint 없음 및 binary import 감사 통과(Dev ws2_32 있음/Shipping 없음, WSAStartup·
endpoint.json·CommandService marker Dev 있음/Shipping 없음).

HierarchyClosure/result.json에 Editor/Development/Shipping 정본 연결 및 SDK/host/verifier
SHA를 기록했다. 프로젝트·shader·capture 사본 추가 없이 새 패키지/로그만 생성.
E0 전체 전환/fault 통합 suite는 잔여이며 다중 DDOL 계층 정상제품 범위는 완료다.
전체 GPU타이밍/대표성능은 M3, 빅뱅 감사는 M4의 기존 완료 조건을 유지한다.

## E0 현행 Editor 전체 전환・失敗 suite 수용 (2026-10-09)

verify-physics-contact-editor-suite.ps1로 고정18항목을 현재 Release Editor에서
한 번의 suite로 실행: Basic, LateOverlap, SensorTargets, Solid, EntityRetirement,
ComponentRemoval, Topology, BurstTopology, SensorTransition, SubscriberLifetime,
RoleTransition, ExplicitBinding, Overflow, Exception, SceneReload, Ddol,
AssemblyReload, HierarchyDdol. 통과18/실패0/미실행0, HTTP 771명령.
기본/접촉 변화·fault 항목은 기존 게이트의 2회 Play/Stop/원복/해제 단정을 유지한다.
씬·어셈블리·계층 항목은 각 원본 게이트의 전환 및 Scope/ALC 수명 단정을 유지한다.

각 프로세스 exit와 새 receipt를 확인하고 항목별 원본결과를 index에 연결했다.
Editor host SHA가 suite 전후 같고 contact script/gate SHA를 보존한다.
중도 실패 시 failed/pending 상태를 저장하며 오래된 로그를 성공으로 대체하지 않는다.
현행 matrix 정본: Build/Verification/ContactStream/E0Suite-db9b3627fced46658700c89513797670/result.json.

지원 Player 행렬은17항목×Development/Shipping=34행. 이미 완료한 계층DDOL2행은
각 원래 authored fixture/receipt를 연결해 수용; 나머지32행은 최신 배포본에서 검증할
잔여로 고정한다. Player assembly reload는 현재 제공하지 않는 API여서 두 구성 모두
unsupported이며 잔여로 세지 않는다. Editor18항목 완료, E0 전체 progress 유지.
이전 개별 SDK의 성공은 이 최신 Player 행렬의 완료 주장으로 대체하지 않는다.

## E0 완료 — 현행 ContactStream 제품 행렬 (2026-10-09)

고정한 최신 Player 잔여32행을 verify-physics-contact-player-suite.ps1로 모두 실행했다.
16시나리오×Development/Shipping에서 새 cooked package smoke와 실제2000GT 실행,
표시/정상종료/패키지 불변·각 시나리오 접촉/Scope 계약 전부 통과·실패0.
기존 부모·자식DDOL2행을 포함해 지원 Player17×2=34행 수용. Editor18행 통과와
함께 E0의 현행 지원 ContactStream 계약 완료. Player runtime assembly reload는
제공하지 않는 계약으로 unsupported 유지하며 실행/수용했다고 주장하지 않는다.

Basic/LateOverlap/SensorTargets/Solid/EntityRetirement/ComponentRemoval/Topology/
BurstTopology/SensorTransition/SubscriberLifetime/RoleTransition/ExplicitBinding/
Overflow/Exception/SceneReload/Ddol을 두 구성에서 수용했다. 역할·retired 신원,
부분접촉·Enable Begin seed·실패 스크립트 격리·reload/DDOL wrapper와 최종 해제
단정은 각 원본 게이트에 유지했다. Shipping service/endpoint 격리 단정도 통과했다.
전체34행의 staged Player.runtime.dll SHA를 각 지정 최신 SDK와 직접 대조해 일치.

입력 프로젝트는 같은 시나리오의 Development→Shipping 사이에서 재사용하고
씬 SHA가 Editor fixture와 같은지 확인한다. 실행 stage/log/receipt는 구성별 새로
생성해 이전 결과를 덮어쓰지 않는다. 새 suite는 실패/미실행을 index에 남긴다.
최신 GPU capture 및 전체성능/p99 수용은 기존 M3에서 소유하며 이 결과로 대체하지 않는다.
E0 정본: Build/Verification/ContactStream/E0Suite-db9b3627fced46658700c89513797670/result.json.

Phase19 현행 상태: 완료17 / 진행1(M1) / 미착수2(M3,M4).
E0 완료이며 최신 Player32행 잔여를 제거한다. 다음은 M1 corpus 완료판정 정합성
및 남은 문서/제품 gate를 닫고 M3 제품성능 수용으로 진행한다.

## M1 완료 — 현재 저장소 스키마 이전 범위 (2026-10-09)

현행 corpus158개 전체 재조사 및 임시 사본 이전 수용. 준비/변환2파일,
body3/character1, diagnostics0·멱등·ZIP byte 정확 복구·원본 SHA 불변 통과.
legacy CCT1개는 명시된 P0 component fingerprint이며 실제 콘텐츠 검토대상0개.
기준선 원본을 회귀 입력으로 보존하므로 authoring apply0을 유지한다.
C1 완료에서 확정한 현재 저장소/명시P0 범위와 같은 기준으로 M1을 닫는다.

schema verifier의 character-policy-example.json 출력이 뒤쪽 geometry 변수 재사용으로
잘못 저장되는 오류를 수정했다. reviewed character policy를 따로 보존하고 출력 JSON을
다시 읽어 동일 Scene 변환을 재현하는 회귀를 추가했다. schema113개 통과·실패0.
실제로 그 출력 policy를 소비한 현재158 corpus 이전/복구도 재통과했다.
현재 Release Editor로 새 변환P0 이동·접지·점프·fixed tick 1.5m/s 및 Play/Stop2회,
native runtime 제거·저작값/component 신원/씬·레이어 불변 수용. HTTP 70명령.

이미 B1의 geometry 자산/개정/저작 연결과 C1의 운동단위·입력 계약 수용을 연결했다.
이전 대상 corpus에 존재하지 않는 외부 게임/legacy geometry 복구·미제공 골든을
현재 구현 잔여로 추가하지 않는다. unsupported geometry/override/reference는
스키마 gate에서 전체 publication 차단하는 계약을 유지한다. 전체 콘텐츠의
현행 구성 Player 실행·GPU타이밍·대표성능 수용은 기존 M3의 제품 회귀 범위다.

M1 정본 Build/Verification/ContactStream/M1Closure/result.json.
이 절이 과거 M1 progress의 오래된 게임입력/참조/corpus 잔여 서술을 대체한다.
현재 저장소 M1 완료. Phase19: 완료18 / 진행0 / 미착수2(M3,M4).

### 2026-10-09 M3 — 실제 표시 완료 후 GPU 캡처

- Development Player에 읽기 전용 `render.status` 명령과 Player 역할의 descriptor seed를 추가했다. 완료된 Game 표시의 ready/frame/promotions/slotMask를 조회한다.
- B2 Player 게이트의 `-WaitForRenderedCapture`는 ready, completedFrame > 0, promotions >= 8을 확인한 뒤 기록을 시작한다. 이번 시작점은 frame 157, promotions 10이었다.
- 새 Release SDK를 정상 빌드·배포하고 기존 저작 프로젝트를 다시 패키징했다. Player 2,000 프레임, 표시 승격 1,856회, exit 0, 패키지 불변 검증이 통과했다.
- 실제 `.ceprof` 파일에서 complete=true, admitted GPU 81, pending/failed GPU 0, unacked CPU 0, dropped counters 0, stop drain 5.3804 ms를 확인했다. 이전 GPU admission 0 캡처와 구분한다.
- 근거: `Build/Verification/ContactStream/M3Acceptance/result.json`, `capture-inspection.log`; 원본 캡처는 해당 receipt의 evidence 경로에 있다.
- M3는 진행 중이다. Debug/Release 제품 성능 행렬, 평균/p99 및 계측 on/off 비용 수용 판정은 아직 남는다.

### 2026-10-09 M3 — Release Player 쿼리 계측 on/off 측정

- `verify-physics-player-performance.ps1`로 같은 최신 Release 패키지를 off/on/on/off 순서로 4회 실행했다. 매 실행 2,000프레임·렌더 표시·패키지 불변·정상 종료·쿼리 parity 및 소유 스레드 CPU 계측이 통과했다.
- 16/64 요청, scalar/batch의 각 블록 600표본, 총 38,400표본을 보존했다. 표시 완료 이후 시작하며 tiered compilation을 끄고 GT 물리 코어를 예약한다. 물리 이동은 블록 사이에 발생한다.
- 8개 블록값의 CV <= 10% 기준으로 평균 비교 4개 통과, p99 비교 4개 미수용. 평균 off/on 중앙값(us): 16 scalar 24.53/29.88(+21.82%), 16 batch 14.12/17.19(+21.72%), 64 scalar 93.04/106.20(+14.14%), 64 batch 49.16/62.20(+26.52%). 이는 쿼리 벽시계 비용이며 프레임/solver 총비용으로 확대하지 않는다.
- p99 off/on CV(%): 16 scalar 18.4/22.3, 16 batch 53.9/49.2, 64 scalar 15.8/6.3, 64 batch 39.1/11.0. 원자료를 유지하며 p99 개선·수용을 주장하지 않는다.
- 계측 on 캡처 2개는 실제 파일 인덱스 및 마지막 구간에서 complete=true, admitted GPU 22/25, pending/failed GPU 0, unacked CPU 0, dropped counters 0을 확인했다. 전체 로드의 메모리 한도 초과는 구간 로드로 확인했으며 파일 손상이 아니다.
- 근거: `Build/Verification/ContactStream/M3Acceptance/Performance-5b2beb6928704f0e8de5fd2e7857f8e0/result.json`. 새 요약 도구는 변동 큰 비교를 수용하지 않는다. M3는 계속 진행 중: p99 안정화/수용 기준, 활성·변경 비율별 solver 총비용, Debug 제품 회귀 및 GPU 시간 분포 확인이 남는다.

### 2026-10-09 M3 — p99 조건 민감도 조사

- 첫 측정의 CPU 실행시간은 GetThreadTimes 15,625us 양자화가 나타나 짧은 블록에서 0 또는 벽시간 초과가 발생했다. 이 값으로 표본별 스케줄링 지연을 원인으로 단정할 수 없다. `M3Acceptance/p99-diagnosis.json`에 원자료 기반 조사 결과를 보존했다.
- 새 `-PreferLastCore` 옵션으로 같은 Release Player를 마지막 가용 전체 물리 코어에 배치해 off/on/on/off 4회·38,400표본을 재측정했다. 기존 기본 코어 정책은 유지했다. 소유 스레드/코어 배치와 복원, 패키지 불변, 렌더 표시, 정상 종료 및 쿼리 parity가 모두 통과했다.
- p99 off/on 중앙값(us): 16 scalar 30.05/41.25, 16 batch 19.75/22.45, 64 scalar 138.20/163.45, 64 batch 64.65/107.55. 기존 코어보다 낮지만 p99 비교 4개 모두 CV <= 10%를 충족하지 못했다. 평균 비교도 3개만 안정적이며 16 batch on CV 12.9%로 미수용이다.
- 이는 CPU 배치 조건에 대한 민감도를 보여주며 OS 지연이나 엔진 쿼리 자체의 원인 판정을 대신하지 않는다. 느린 표본 삭제·임계값 완화·측정 결과 선별 없이 원자료를 보존한다. 동일 측정의 맹목적인 반복으로 수용을 주장하지 않는다.
- 계측 on 녹화 2개의 complete 및 GPU/CPU 종료 계측은 실제 파일 구간 로드로 재확인했다. 근거: `M3Acceptance/Performance-181142a401c04e738fc8186c146f30dd/result.json`.
- M3 진행 중. 다음 단계는 solver 총비용과 활성/변경 비율 행렬을 측정하고, 쿼리 p99는 표본별 실행/대기 분리 증거를 보강해 수용 여부를 판단하는 것이다.

### 2026-10-09 M3 — solver 활성·변경 비율 행렬

- 현행 ScenePhysicsSimulation 소스를 사용하는 네이티브 Release 벤치마크를 재빌드했다. 기존 생성 도구의 serial 측은 현행 소스의 include 경로만 바꾼 사본이며 과거 코드와의 성능 비교가 아니다.
- 총 dynamic 바디 1,024개, 활성 16/256/1,024개 × tick별 속도 갱신 0/50/100% × off/on/on/off를 CPU·PhysX GPU 각각 36회 실행했다. 총 72회 통과, 각 240 tick의 총 17,280 raw 표본을 보존했다. 실제 GPU backend 선택·worker 2개·활성 수·변경 카운터·손실 0·계측 task 계층 및 240 tick 측정 창을 검증했다.
- SetVelocity 및 Advance를 함께 벽시계로 잰다. 실제 changed_bodies는 갱신 수와 일치한다. active_bodies와 changed_bodies는 별도 카운터이며 활성 pose 수를 변경 입력 수로 오인하지 않는다. 처음 실행에서 이 의미를 잘못 가정한 verifier는 실패했고 소스의 note_change 계약에 맞춰 수정 후 전체 행렬을 다시 실행했다.
- 평균 off/on 비교 CV <= 10%: CPU 9/9, GPU 8/9. p99 비교: CPU 5/9, GPU 4/9. 각 side 독립 프로세스 2개의 예비 안정성 판정이며 전체 성능 수용이나 통계적 보장을 주장하지 않는다.
- CPU off 평균(us), 갱신 0/50/100%: active16=71.0/75.7/71.9, active256=156.2/164.4/174.8, active1024=360.1/409.6/452.8. GPU off 평균은 같은 축에서 1,237.8~1,758.2us로 이 작은 자유 운동 부하의 전체 물리 벽시계 비용은 CPU보다 높았다. 프로파일 on이 일부 더 빠른 수치도 있어 결과를 계측 최적화나 GPU 이득으로 해석하지 않는다.
- 근거: `M3Acceptance/Solver-ca03bcc30cfb4e4592162dd9cc5fe2ae/result.json`(CPU), `M3Acceptance/Solver-e31b3197d4ad4ef5b00118e7ad47127f/result.json`(GPU). 원본 exe·source 해시와 각 capture/JSON 경로를 보존했다.
- 자유 운동 네이티브 세션 행렬의 측정은 완료했다. Player 전체 프레임·충돌 solver/캐릭터/쿼리 혼합 부하의 수용을 대신하지 않는다. M3의 Debug 제품 회귀, 실제 Player/GPU 시간 분포 및 불안정 p99 판정은 남는다.

### 2026-10-09 M3 — 최신 Debug SDK 실제 Player 회귀

- 현행 소스 전체 Debug 빌드·SDK 생성·새 Player 패키징·시작 장면 smoke를 통과했다. 기존 저작 프로젝트를 재사용해 모델/셰이더 입력의 사본 추가를 피했다. 새 SDK: `M3Acceptance/Debug/Distribution/local-0.0.0.0-win-x64-Debug-ffb2ef08-1f0f-49e0-8472-5026a1ac0f16`.
- 실제 Debug Player 2,000프레임 실행, 표시 승격 1,856회, exit 0, 패키지 불변을 검증했다. 동적/정적/키네마틱 기본 검사 각 9개 통과, 동적 쿼리 204개·혼합 쿼리 74개 통과, 실패 0개. dense128 이동/정지/복귀, 최대 batch 4,096 hit, 70 read windows와 67 pose 변경을 확인했다.
- 표시 완료 이후 기록한 실제 파일 인덱스와 끝 구간을 읽어 85프레임 complete=true, GPU admitted 82, pending/failed GPU 0, unacked CPU 0, dropped counters 0, stop drain 18.9312ms를 확인했다.
- 근거: `Build/Verification/ContactStream/M3Acceptance/Debug/result.json`, `capture-inspection.log`, `publish.log`, `package.log`. 원본 실행과 캡처 해시는 receipt에 보존했다.
- Debug의 동적 convex 및 dense/mixed 쿼리 제품 경로는 검증 완료다. 전체 Debug CCT·ContactStream/Editor 회귀 완료로 확대하지 않는다. 실제 Player CPU/GPU 시간 분포와 불안정 p99 판정은 M3에 남는다.

### 2026-10-09 M3 — Debug Player ContactStream·CCT 완료

- ContactStream Player 게이트 3종에 `-Configuration Debug|Release`를 추가했다. 선택한 SDK의 해당 구성 도구로 패키징하고 pointer.config 일치를 검증한다. Debug 쿠커가 요구하는 작은 Prim_Cube 모델 입력을 보완하며 테스트 씬에는 배치하지 않는다.
- 새 Debug suite는 승인된 Editor 입력과 기존 저작 프로젝트를 재사용해 Player 지원 17개 사례 모두 통과, 실패 0개, pending 0개다: Basic/LateOverlap/SensorTargets/Solid/EntityRetirement/ComponentRemoval/Topology/BurstTopology/SensorTransition/SubscriberLifetime/RoleTransition/ExplicitBinding/Overflow/Exception/SceneReload/Ddol/HierarchyDdol.
- 최초 Basic 실패는 모델 corpus 미충족으로 런타임 이전에 발생했고 입력 보완 후 통과했다. 마지막 HierarchyDdol은 기존 project scene/destination이 승인된 fixture와 달라 identity gate가 거절했다. 승인된 두 fixture를 갱신하고 해당 사례만 재실행해 통과했다. 실패·재실행 이력은 보존했다. AssemblyReload는 Player에서 지원하지 않으므로 Editor에 남는다.
- Debug CCT 실제 geometry/DDOL Player 검사 73개 통과: triangle mesh motion 12, DDOL 8, geometry carry 29, convex motion 12, heightfield motion 12. 목적지 cooked 씬과 geometry 3개, authoring geometry fallback 0, 정상 종료·패키지 불변을 확인했다.
- Debug 평면 입력 계약 16개 검사 통과. CCT 합계 89개 통과, 실패 0개. 프로젝트·shader/model 입력을 기존 프로젝트에서 재사용했다.
- 모든 ContactStream 17개 및 CCT 2개 패키지의 Player.runtime.dll 해시가 최신 Debug SDK와 일치함을 확인했다. Contact pointer도 config=Debug/shipping=false를 전수 확인했다.
- 근거: `M3Acceptance/Debug/ContactSuite-7eb0d00f56fc4ca28d294d137e93ec64/result.json`, `M3Acceptance/Debug/contract-result.json`. Debug Player CCT·ContactStream 검증 완료. Debug Editor Play/Stop·assembly reload 및 실제 Player CPU/GPU 시간 분포·불안정 p99 수용은 남는다.

### 2026-10-09 M3 — Debug Editor 수명·Play/Stop·재로드 완료

- 최신 Debug Editor에서 ContactStream Editor 전체 suite 18개 사례 통과, 실패 0개, pending 0개, 총 771 HTTP 명령을 검증했다. AssemblyReload·SceneReload·DDOL·HierarchyDdol 및 Play/Stop 복원 계약을 포함한다.
- CCT 계단/점프/강제 속도/terrain 시나리오 323 HTTP 명령, 계층 DDOL/씬 전환 실패·복구/재생·Stop 원상 복원 시나리오 166 HTTP 명령이 통과했다. CLR API probe 및 최초 저작 위치 [0,3,0] 복원을 확인했다. 평면 입력 Debug Editor 게이트와 저장 문서 동등성 검사도 통과했다.
- 최초에는 StepProbe와 TransitionFailureProbe를 함께 지정해 실패했다. 계층 fixture의 부모 scale=2로 캡슐 반경이 확대되어 정상 high-step 정지 x=4.51667이 scale=1 step 기준 x>4.7에 미달했다. 엔진 실패로 오인하지 않고 별도 fixture로 분리 실행했다. `verify-physics-character-http.ps1`에 StepProbe/HierarchyProbe 동시 요청을 거부하는 guard를 추가했다.
- 실제 Debug Editor runtime DLL과 최신 SDK DLL 및 suite 시작 시 해시의 일치를 검증했다. 근거: `M3Acceptance/Debug/editor-result.json`, Editor suite `E0Suite-8d723239c19948678ab49595aff4b342/result.json`, CCT/transition/planar receipt 경로와 각 원본 로그.
- Debug Editor 및 Player의 CCT·ContactStream 수명/재로드 제품 회귀는 검증 완료다. M3에서 남은 항목은 실제 Player CPU/GPU 시간 분포·성능 수용과 불안정 p99 실행/대기 증거 및 판정이다. M4 최종 감사는 이후 진행한다.

### 2026-10-09 M3 — 실제 Player CPU/GPU 시간 분포 측정

- `physics_player_capture_summary.cpp`를 추가해 녹화 전체를 한 프레임씩 읽는다. complete/finalized, 프레임 dropped events, 실제 GPU 구간 존재를 확인하고 원본 표본·구간 수·평균·nearest-rank p99를 출력한다. 녹화 경계에서 잘린 scope는 별도 개수로 기록하며 완전한 duration 분포에 섞지 않는다.
- 기존 Release/Debug 단기 캡처를 전수 읽었다. 각각 85프레임과 GPU span 3,240/3,280개. 85표본의 p99가 최대값이므로 장시간 성능 수용 근거로 쓰지 않는다.
- B2 Player 게이트에 `-CaptureTailFrames`를 추가했다. 실제 렌더 표시 상태를 읽어 동적/혼합 쿼리 검증 후 추가 1,000 completed display frame을 기다린다. 새 Release 실행은 2,000프레임·정상 종료·패키지 불변·동적/혼합 검사 통과, 1,088 녹화 프레임·GPU 43,400span을 기록했다.
- 실제 GPU submission 1,085개, pending/failed GPU 0, unacked CPU 0, complete=true. 종료 drain 7.6607ms, 경계 잘림 scope 2개. GPU duration은 submission/view/queue별 timestamp interval의 union과 envelope를 별도 집계해 중첩 패스 합산을 피한다.
- CPU PhysicsTick 1,086표본 평균 0.530ms/p99 1.090ms; FetchWait 평균 0.505ms/p99 1.045ms; RenderThreadFrame 평균 3.297ms/p99 4.903ms. inclusive scope를 서로 더하지 않는다.
- frame boundary 평균 16.651ms/p99 17.121ms/최대 27.462ms는 pacing을 포함하는 프레임 간격이다. 순수 CPU 작업 총합이나 표시 지연으로 해석하지 않는다.
- GPU instrumented interval union 평균 1.028ms/p99 5.101ms, envelope 평균 1.031ms/p99 5.108ms. 이는 계측된 제출 구간이며 미계측 GPU 작업·scanout/presentation latency 및 PhysX GPU solver 단독 시간으로 확대하지 않는다.
- 근거: `M3Acceptance/distribution-result.json`, `long-capture-distribution.json`, 원본 `Phase19B2Player/run-36d55636375e4577afab318ee1ab92f7/query.ceprof`. 단기 Release/Debug 분석도 `release-capture-distribution.json`, `debug-capture-distribution.json`에 보존했다.
- 실제 Player CPU/GPU 분포 추출은 완료했다. 단일 장시간 실행은 독립 실행 안정성 및 성능 수용을 대신하지 않는다. M3 남음: 독립 실행 안정성·명시적 성능 수용, 불안정 쿼리 p99 실행/대기 증거·판정. M4 최종 감사는 이후 진행한다.

### 2026-10-09 M3 — 실제 Player 독립 실행 안정성 판정

- 같은 Release 패키지·동적/혼합 쿼리 조건 및 추가 표시 완료 1,000프레임으로 독립 프로세스 3회 측정했다. 모두 제품 gate·정상 종료·패키지 불변·complete 캡처를 통과했다. 합계 3,264 녹화 프레임·GPU span 130,200개. 각 실행 GPU 제출 1,085개, pending/failed GPU 및 unacked CPU 0.
- `summarize-physics-player-capture-stability.py`는 실행별 평균/p99의 CV <= 10%를 각각 판정한다. 평균 CV: PhysicsTick 2.0%, FetchWait 2.2%, RenderThreadFrame 7.8%, GPU instrumented union 8.0%. 모두 평균 재현성 기준 충족.
- p99 CV: PhysicsTick 25.2%, FetchWait 23.9%, RenderThreadFrame 39.9%, GPU instrumented union 41.5%. 모두 미수용. 중앙값은 각각 1.302/1.223/5.847/5.101ms. pacing 포함 frame boundary p99만 CV4.5%로 안정적이다. 안정적인 프레임 간격으로 내부 물리·렌더 꼬리 지연의 안정성을 대체하지 않는다.
- 평균 중앙값: PhysicsTick 0.531ms, FetchWait 0.506ms, RenderThreadFrame 3.324ms, GPU 계측 interval union 1.028ms. 구간별 inclusive값을 더하지 않으며 GPU union은 미계측 시간·표시 지연을 포함한 총량이 아니다.
- 근거: `M3Acceptance/distribution-stability.json` 및 세 실행 원본 capture 해시, `long-capture-repeat.log`, `long-capture-third.log`. 원자료 제외·임계값 완화 없이 보존했다.
- 독립 실행 측정과 평균 재현성 판정 완료. 전체 성능 수용은 보류한다. M3 잔여는 불안정 p99의 task queue/실행/대기 근거 분석과 성능 수용 판정으로 좁힌다. 동일 조건 재측정을 계속해 우연한 통과를 찾지 않는다. M4 최종 감사는 이후 진행한다.

### 2026-10-10 M3 — p99 물리 tick/task 지연 위치 분석

- `physics_player_task_trace.cpp`로 기존 장시간 녹화 3개를 프레임별로 읽어 session/tick/task/thread/depth 및 submit/start/end/complete timestamps를 내보냈다. `analyze-physics-player-task-tail.py`는 owner FetchWait와 SDK task를 tick 단위로 연결하고 가장 느린 1% tick을 원자료와 함께 보존한다.
- 분석 tick 1,086/1,086/1,087개, 합계 3,259개. task submit/start/complete 불일치 0개. 2번째 캡처의 완전한 FetchWait가 없는 경계 tick 1개는 따로 기록하며 비교 대상에서 제외한다. 녹화 중간의 느린 tick을 제외하지 않는다.
- 빠른 절반 vs 느린 1%의 중앙값(us), 실행 1/2/3: FetchWait 406/415/397 → 1,131/1,830/1,996; task별 최대 큐 대기 28.6/30.3/28.1 → 106/140/178; 최대 task 벽시계 실행 30.6/33.1/30.5 → 131/157/230. 큐 지연과 실행 구간의 증가가 함께 관찰됐다.
- 느린 tick에서 FetchWait 대부분은 Physics.FetchResults에 위치한다. DispatcherDrain은 대다수 약 0.2~5.4us이며 한 사례 55.5us로, 1~4ms 전체 꼬리 지연을 drain만으로 설명하지 못한다.
- task 수는 부하 전환 중 81~97개, 비교 기준의 안정 구간은 대부분 52개다. 가장 느린 tick에는 52개 task인 tick도 다수 있어 task 개수 증가만으로 꼬리 지연을 설명할 수 없다.
- task duration은 벽시계이므로 실제 CPU 실행과 OS 선점/대기를 분리한 값이 아니다. worker duration 합은 병렬 중첩을 포함하므로 owner FetchWait와 더하거나 critical path로 해석하지 않는다. 이 자료로 enkiTS 큐 단독 원인이나 PhysX 실행 자체 단독 원인을 확정하지 않는다.
- 근거: `M3Acceptance/task-tail-analysis.json`, `task-tail-analysis.log`, `task-<captureId>.jsonl`과 기존 capture hashes. 분석 위치는 FetchResults 및 SDK task/queue로 좁혔다. 원인 확정·성능 수용에는 실행/선점 분리 증거가 남으며 전체 p99 수용은 보류한다.

### 2026-10-10 M3 — CPU 실행/선점 분리 ETW 준비·환경 제한

- WPR/xperf/WPA 설치를 확인했다. WPR은 기존 recording 없음. `wpr -start GeneralProfile -filemode`는 `0xc5585011: Failed to enable the policy to profile system performance`로 거부됐다. 후속 상태도 recording 없음으로 확인했다. 현재 토큰은 Medium integrity이며 Administrators 그룹이 deny-only다. ETW 실행/선점 분리 결과는 아직 얻지 못했다.
- `record-physics-player-etw.ps1`을 준비했다. 관리자 PowerShell에서 기존 WPR 세션이 없을 때만 자체 기록을 시작하고 실제 Player gate 및 추가 완료 표시 1000프레임을 실행한다. 자신이 시작한 recording만 finally에서 ETL로 종료하며 기존 세션은 중단하지 않는다. PID/arguments/start UTC·capture receipt·ETW trace statistics·thread CSwitch 보고서를 보존한다.
- B2 verifier가 owned Player launch.json을 남기도록 추가했다. task trace exporter는 profiler slot 외 실제 osThreadId를 내보낸다. 현재 라이브러리로 재컴파일하고 기존 원본 캡처에서 OS TID 매핑을 검증했다. 엔진/SDK 바이너리 변경 없이 수집 도구를 준비했다.
- 근거: `M3Acceptance/etw-start.log`, `etw-environment.json`, `task-os-thread-validation.jsonl`. WPR 정책 실패를 엔진 회귀나 성능 통과로 계산하지 않는다.
- 실행 명령(관리자 PowerShell): `pwsh -NoProfile -File C:/Users/idene/source/repos/CreatorEngine/Tools/regression/record-physics-player-etw.ps1`.
- M3는 계속 진행 중이다. ETW 기록 가능한 관리자 환경에서 자료를 얻어 실제 CPU 실행·ready/선점·blocking 구간을 물리 tick/task와 대조한 뒤 p99 수용을 판정해야 한다. 준비된 스크립트만으로 실행/선점 분리를 완료했다고 주장하지 않는다.
### 2026-10-10 M3 — 실제 Player ETW 수집 및 task 스케줄 상태 대조

- 관리자 실행 ETW-3e43617439c6430db31e3d8c24a235c3 수집 성공. 64.775초 원본 ETL의 Lost Buffers/Events 모두 0이다. Player PID 26240, 2000프레임, display promotions 1849, 종료 코드 0, 패키지 불변 확인. 기본 바디 검증 통과 27개·실패 0개.
- 원본 ETL의 RAW_TIMESTAMP(QPC)/PerfFreq를 사용해 엔진 QPC 마이크로초와 직접 대조했다. PhysXTask 60094개, FetchResults 1089개를 OS TID로 연결했고 최대 미분류 시간 간극은 0us다. scheduled는 DPC/ISR 간섭을 포함할 수 있어 순수 명령 실행 시간으로 주장하지 않는다.
- 각 marker별 가장 느린 1%의 독립 중앙값: PhysXTask 벽시계 194.65us / scheduled 143.5us / ready 17.2us / blocked 22.35us. FetchResults 벽시계 5965.3us / scheduled 102.95us / ready 44.85us / blocked 5853.2us. 각 열의 중앙값은 서로 다른 표본에서 나올 수 있어 합산하지 않는다.
- 느린 FetchResults는 소유 스레드 CPU 점유보다 blocking 대기가 지배적이다. worker에는 실행과 ready 및 blocking이 모두 관찰된다. worker 의존 관계/대기 대상과 반복 p99 수용까지 단일 원인으로 확정하지 않는다. ETW 수집 환경 장애는 해소됐으며 M3는 성능 수용 진행 중이다.
- 근거: 해당 ETW 폴더의 player.etl, trace-statistics.txt, physics-tasks.jsonl, switches.csv, task-cpu-analysis.json. xperf dumper 일부 이벤트 해석 경고와 별개로 원본 ETL을 직접 읽었다. 4GB 전체 임시 CSV는 제거했고 원본/필요 스케줄 증거는 보존했다.
### 2026-10-10 M3 — ETW 독립 집계 검증 및 tick 완료 상관관계

- RAW CSwitch 디코더를 실제 캡처의 version 5 / 28-byte 형식으로 제한했다. 미지원 형식은 실패한다. 물리 owner/worker 9개 OS 스레드의 전체 scheduled 합을 독립 xperf thread-cswitch 집계와 대조: 모두 일치, 최대 반올림 차이 0.5us. capture 구간의 미분류 간극 0us 검증도 유지한다.
- FetchResults 1089개 tick 모두 task submit/run+release/complete 식별자로 연결했다. 가장 느린 tick 1258: task 52개, FetchResults 9423.6us, owner blocked 9238.3us / ready 69.4us / scheduled 115.9us. 최대 submit→start 1174.2us, 마지막 task 완료 후 FetchResults 반환 124us.
- 느린 tick은 52개 task인 경우에도 발생한다. 큐 지연 하나로 전체를 설명할 수 없으며, SDK 작업 완료 대기와 동시 worker 지연이 관찰된다. TaskComplete는 run()+release() 뒤이고 실제 fetch wakeup은 release() 도중 발생할 수 있으므로 완료와 fetch 반환의 음수 간격도 모순으로 계산하지 않는다. 마지막 완료 시각은 SDK critical path를 증명하지 않는다.
- 후속 dispatcher drain을 주 지연으로 분류할 근거는 없다. 남은 원인 세분화는 SDK run/release 및 worker 내부 blocking/의존성에 집중한다. ETW 수집·OS 실행 상태 대조·tick 완료 상관관계는 확보됐으나 반복 p99 성능 수용은 보류한다.
- 근거: 같은 ETW 폴더 task-cpu-analysis.json의 xperfAggregateChecks/tickCorrelation, schema.log 및 원본 ETL. 엔진 런타임 변경 없이 분석 도구를 보강했다.
### 2026-10-10 M3 — worker 대기 사유 및 SDK Run/Release 계측 분리

- 기존 ETW의 CSwitch wait reason을 추가 추출했다. worker PhysXTask 내부 blocking은 코드 37, owner FetchResults는 코드 6으로 기록됐다. 코드만으로 특정 잠금이나 API를 단정하지 않는다. 최악 tick 1258의 가장 긴 task는 wall 1177.2us / scheduled 214.6us / ready 96.1us / blocked 866.5us로, task wall을 순수 연산 비용으로 분류할 수 없다.
- Physics.PhysXTask 아래에 Physics.PhysXTaskRun 및 Physics.PhysXTaskRelease 계층을 추가했다. 기존 task identity와 SDK run→release 순서를 유지한다. Shipping의 marker 등록/계측은 기존 compile-out 계약을 따른다. 분석기는 새 marker가 있는 캡처에서 각각 OS 상태를 대조하며, 과거 캡처에 없는 측정치를 만들지 않는다.
- P1 verifier의 ProfileRecording.cpp 소스 누락을 수정했다. probe의 실제 8회 baseline frame 번호와 이후 프레임 간극, 비동기 Record admission/파일 finalize 대기, 녹화 초기화 후 과거 이벤트를 찾던 검증을 수정했다. baseline 캡처와 destruction 캡처를 각각 확인하며 손실/완료 조건은 완화하지 않았다. 초기 counter loss 3290개 실패를 유지한 채 배선 수정 후 손실 없는 캡처로 통과했다.
- 새 Run/Release marker의 session/tick/task identity 및 task별 Submit/Parent/Run/Release/Complete cardinality를 확인했다. 전체 스크립트 재빌드·실행: Debug 및 Release 각각 53238 checks / 4160 tasks / 실제 GPU backend 확인; Shipping 4 checks / 51 tasks. 실패 0. 큰 check 수에는 baseline serialization의 이벤트 identity 재개방 검사가 포함된다.
- 근거: M3Acceptance/task-run-release-result.json, Phase19P1 각 설정 result.jsonl/stderr.log/baseline.ceprof 및 기존 ETW task-cpu-analysis.json. 제품 SDK/Player는 아직 새 계층을 포함하지 않는다. 다음은 제품 Release SDK 재빌드 및 새 Run/Release 계층 포함 캡처로 worker blocking 위치 대조다. M3 성능 수용은 계속 보류한다.
### 2026-10-10 M3 — Run/Release 계측 제품 SDK 재빌드·Player 검증

- Release 제품 SDK를 --build --no-pointer로 재빌드·배포 검증했다: local-0.0.0.0-win-x64-Release-4f1aed37-cc4d-469a-9e95-e7dd6c22afa4. 기존 MixedProfilePlayer 저작 프로젝트를 재사용했고 probe 3개는 현재 소스와 해시가 일치한다. 새 Player stage는 Game-8854b7d4051c4d88826d8c1905c537dd이며 SDK/Player.runtime.dll 해시 일치를 검증했다.
- 실제 Player 2000프레임, display promotions 1864, exit 0, 패키지 불변. 기본 바디 통과 27개·실패 0개, dynamic query 통과 210개·실패 0개, mixed query 통과 77개·실패 0개. 렌더 준비 후 완료 표시 1000프레임 추가 캡처를 수행했다.
- capture complete, 1092프레임, GPU spans 43560, GPU admitted 1089, CPU unacked/GPU pending/GPU failed 각각 0. 종료 경계 clipped scope 3개는 분포에서 제외했다.
- 제품 task 60182개 모두 Parent 안의 Run→Release 순서·동일 OS thread/session/tick/task·중복 없음 확인. Run mean 4.928us/p99 42.7us, Release mean 3.267us/p99 27.9us. 이는 대기·선점을 포함한 벽시계 분포이며, 순수 실행 또는 반복 p99 수용 증거로 주장하지 않는다.
- record-physics-player-etw.ps1 기본 stage를 새 검증 패키지로 갱신했다. 관리자 재수집 후 ETW 손실 0 검사·task split 순서 검사·OS 상태 대조/독립 xperf 합 검증까지 자동 실행한다. 현재 비관리자 실행 토큰의 kernel recording 권한 제약 때문에 새 Run/Release 포함 ETW 수집은 관리자 shell 실행이 필요하다. 과거 ETW로 새 계층의 대기 위치를 추정하지 않는다.
- 근거: M3Acceptance/task-split-product-result.json, product-task-split-summary.json, product-task-split-capture.json, publish-task-split/package-task-split/player-task-split logs 및 run-049d0c666be74f8897b6c45fa09b12ca. 제품 SDK 재빌드·제품 계층 확인 항목을 완료했으며, 남은 M3는 새 ETW에서 Run/Release blocking 위치 확인과 반복 p99 성능 수용이다.
### 2026-10-10 M3 — 새 Run/Release ETW 대조 완료·계측 경로 대기 발견

- ETW-f98086963e144577a0fe28e4803773b7: Lost Buffers/Events 각각 0, task 59974개 Run→Release 계층·identity 검증 통과. 물리 9개 스레드 scheduled 합은 독립 xperf와 최대 0.5us 차이, OS 시간 간극 0us. 0us timestamp-resolution child scope는 hierarchy 검사에 포함하고 양수 duration OS 분포에서는 제외하므로 Run 59884/Release 58099개와 부모 수가 다르다.
- Player 2000프레임, display promotions 1865, exit 0, 패키지 불변. 캡처 complete/1089프레임/GPU spans 43440, admitted 1086, CPU unacked/GPU pending/GPU failed 각각 0. open scope 1개는 종료 경계이며 duration 표본에서 제외한다.
- Run/Release 양쪽 모두 코드 37 blocking 관찰. 각 marker 독립 가장 느린 1% 중앙값: Run wall 177.05us / scheduled 144.6us / blocked 5.2us; Release wall 125.15us / scheduled 75.75us / blocked 23.65us. 중앙값끼리 합산하지 않는다.
- 부모 task blocking 합 107031.8us 중 Run 34483.6us / Release 30495.7us / 자식 외부 42052.5us. 여러 worker의 중첩 합이므로 tick critical path나 전체 elapsed로 해석하지 않는다.
- 가장 긴 부모(task 47731, tick 826) wall 2684.6us: Run 2.5us / Release 166.8us, 자식 외부 wall 2515.3us·blocked 2044.6us. 해당 Run과 Release 내부 blocking은 각각 0이다. Run 종료 timestamp부터 Release 시작 timestamp 사이에 긴 대기가 있으며 현재 소스상 profiler scope 종료/시작 처리 경로다. end_scope→honor_seal_request→seal_current 및 chunk_pool::seal/acquire의 공유 mutex가 조사 후보다. 정확한 잠금/스택을 확정하지 않는다.
- Run/Release 포함 OS 상태 대조 항목은 완료했다. 부모 task wall을 순수 PhysX 연산 지연으로 계산하지 않는다. 다음 조치는 profiler 공유 pool 게시/확보 경로의 대기 단축 검토와 손실 없는 캡처 재검증, 이어서 ETW를 끈 독립 반복 p99 수용이다. ETW 수집 오버헤드가 있는 단일 run을 제품 p99 수용이나 계측 off/on 비교에 섞지 않는다.
- 근거: 해당 ETW 폴더 assessment.json, task-cpu-analysis.json(splitAttribution 포함), task-split.json, capture-summary.json, trace-statistics.txt 및 원본 ETL. M3 성능 수용은 보류한다.
### 2026-10-10 M3 — profiler chunk pool 잠금 분리·반환 임계구역 단축

- ProfileThreadStream의 chunk_pool 봉인 FIFO/semaphore 수명 보호를 별도 m_sealedLock으로 분리했다. seal/take_sealed는 이제 free-list 확보·페이지 할당·보충·반환의 m_lock을 기다리지 않는다. 초기화/종료는 scoped_lock으로 두 상태를 함께 보호하고 FIFO 게시 순서·소유권 이전·empty→nonempty 알림 계약은 유지한다.
- release가 독점 소유한 반환 목록의 count 초기화와 연결은 잠금 밖에서 처리하고, free-list 연결과 카운트 합산만 잠금 안에서 한 번 수행한다. 기존 반환 후 free-list 순서도 유지한다. 잠금 경합을 새 drop/try-lock 정책으로 숨기지 않는다.
- 코어 Debug/Release 각각 1387 checks, 실패 0. 선택한 sparse-pages-retain-pool 변이는 두 설정 모두 sparse-pages/reuse 검사에서 검출됐다. 전체 72개 변이 suite를 실행했다고 주장하지 않는다.
- collector stress Debug/Release 각각 20초 설정: 실제 22.685/22.241초, 37/319 cycles, attempted=collected 606208/5226496프레임, droppedFrames/malformedPages/foreignTouches 0, accounted/drained/complete true. active shutdown abandoned 5개는 해당 stress가 의도적으로 만들고 별도 검증한 소유자 생존 종료 조건이다.
- 수정된 profiler로 Release 물리 P1 전체 재빌드·실행: 53238 checks 통과·실패 0, SDK task 4160개, 실제 GPU backend 확인. 기존 Run/Release task identity/cardinality와 손실 없는 capture/serialization 계약을 통과했다.
- 근거: M3Acceptance/pool-lock-isolation-result.json, pool-core.log, PoolContentionStress/manifest.json, pool-physics-p1.log. 제품 SDK는 아직 이 변경을 포함하지 않는다. 잠금 간섭 제거는 구현·정확성 확인됐지만 실제 blocking/p99 개선량은 미측정이다. 다음은 제품 SDK 반영 후 캡처·blocking 재측정 및 ETW off 반복 p99 수용이다. M3 성능 수용은 보류한다.
### 2026-10-10 M3 — pool 잠금 분리 제품 SDK 반영·ETW off 독립 3회 측정

- Release 전체 재빌드·배포 검증: local-0.0.0.0-win-x64-Release-31cd935f-23fb-4045-be7a-d7cbb434123c. 기존 저작 프로젝트 재사용, 새 stage Game-432a3fbd77b1462ab930bd6960704597 시작 검증 통과, SDK/Player DLL hash 일치. 소스 hash와 바이너리 hash는 pool-product-build-result.json에 보존했다.
- WPR이 기록 중이 아닌 것을 확인하고 같은 새 제품 패키지를 독립 3회 실행했다. 모두 2000프레임, 정상 종료/패키지 불변. 각 query/dynamic/mixed gate와 추가 완료 표시 1000프레임 캡처 및 task Run→Release 순서/identity 검사를 통과했다.
- complete 캡처 1086/1087/1087프레임, 총 3260프레임·GPU spans 129920. task 59870/59974/59922개(총 179766개) 계층 검증. 모든 실행 CPU unacked/GPU pending/GPU failed 0. 종료 경계 span은 종전 규칙대로 분포에서 제외하고 실행/느린 표본은 제거하지 않았다.
- 물리 tick 독립 평균 값 865.97/980.73/942.63us, p99 3259.0/3612.8/3565.9us. 중앙값 평균 942.63us / p99 3565.9us. CV 평균 6.29%, p99 5.52%로 반복성 기준 통과. FetchWait 평균/p99 CV 5.79/5.10%, GPU instrumented union 평균/p99 CV 7.56/9.17%도 통과.
- RenderThread 평균 CV 3.13% 통과, p99 11778.1/13832.6/15485.2us의 CV 13.56%는 보류. 프레임 경계는 pacing 포함 값이며 CPU 작업 또는 실제 presentation latency로 해석하지 않는다.
- 중요한 한계: 물리 지연 절대값은 이전 기록보다 높다. 이전 단일 Run/Release 제품 캡처의 PhysicsTick mean 549.28us / p99 2048.3us와 이번 값을 단순 비교해 원인이나 변경의 개선/회귀를 확정하지 않는다. 이번 3회로 반복성은 확인했지만 잠금 분리의 실제 지연 개선을 확인했다고 주장하지 않는다. 새로운 제품 ETW에서 profiler 외부/Run/Release blocking 위치 대조와 통제된 성능/계측 비용 수용이 남는다.
- record-physics-player-etw.ps1 기본 stage를 새 검증 제품으로 갱신했다. 관리자 shell에서 재수집하면 기존과 동일한 자동 task hierarchy·OS QPC/독립 xperf·Run/Release 외부 blocking 분석을 수행한다. 오래된 SDK ETW 결과를 새 코드의 개선 근거로 재사용하지 않는다.
- 근거: M3Acceptance/pool-product-result.json, pool-product-build-result.json, PoolIsolationPlayer/runs.json·stability.json 및 원본 capture hashes, publish-pool-isolation/package-pool-isolation logs. 제품 SDK 반영·3회 손실 없는 캡처·물리/GPU p99 반복성 확인을 완료했다. M3 전체 성능 수용은 보류한다.

### 2026-10-10 M3 — pool 잠금 분리 후 제품 ETW 대조 완료

- ETW-005887f196d140c2b03fde3aa2c8c361: Lost Buffers/Events 0, task 60055개 Run→Release identity/순서 검증 통과. OS 시간 간극 0us, 9개 스레드 scheduled 합 독립 xperf 대조 최대 차이 0.4us.
- Run/Release 외부 blocking 합은 이전 ETW의 42052.5us/59974 tasks(0.7012us/task)에서 이번 0us/60055 tasks(0us/task)로 관측됐다. profiler scope 사이 대기 제거 증거이며, 중첩 worker 합을 전체 실행 시간 또는 tick critical path로 해석하지 않는다. 독립 ETW 실행 간 비교이므로 통제된 지연 개선량으로 주장하지 않는다.
- Run 내부 blocking 합 33542.5us, Release 내부 41773.5us는 남는다. 부모 wall 최대 1288.5us의 Run은 1285.9us 전부 scheduled로 확인됐다. 내부 blocking의 정확한 mutex/SDK stack 원인은 이 기록만으로 확정하지 않는다.
- Player 2000프레임/display promotions 1866/exit 0/패키지 불변. complete 캡처 1090프레임/GPU spans 43480, admitted 1087, CPU unacked/GPU pending/GPU failed 0. 종료 경계 open scope 3개는 기존 규칙대로 분포에서 제외한다.
- 새 제품 ETW 대조 항목 완료. 남은 M3는 통제된 계측 비용·성능 수용 및 RenderThread p99 반복성(CV 13.56%) 해소이며 M3 전체 수용/M4는 완료하지 않는다.
- 근거: 해당 폴더 assessment.json, task-cpu-analysis.json, task-split.json, capture-summary.json, trace-statistics.txt와 원본 ETL.

### 2026-10-10 M3 — 최신 pool 분리 SDK 쿼리 계측 ABBA 비용 측정

- 최신 Release stage Game-432a3fbd77b1462ab930bd6960704597로 ETW off 확인 후 off/on/on/off 독립 4회 실행. 기존 소유 CPU 고정·tiered compilation 비활성·워밍업·블록당 600표본 조건 유지. 각각 2000프레임/정상 종료/패키지 불변/쿼리 parity·CPU accounting 검증 통과, 총 38400 raw 표본 보존.
- 기존 eight-block CV <= 10% 기준에서 평균 2/4 비교만 안정적: 16 scalar off/on 중앙값 26.797/28.612us(+6.77%), 64 scalar 104.337/112.832us(+8.14%). 안정적인 평균의 계측 비용 측정을 완료했으며, 비용 허용 예산까지 수용한 의미는 아니다.
- 16 batch 평균 15.934/17.479us(+9.70%), 64 batch 55.987/65.453us(+16.91%)는 불안정하므로 비용 수용값으로 사용하지 않는다. p99 4/4 모두 불안정: off/on CV는 16 scalar 22.30/20.22%, 16 batch 25.96/45.80%, 64 scalar 15.04/12.58%, 64 batch 12.19/32.41%.
- 비교 8개 중 안정성 2개 통과. 낮아진 일부 on p99 중앙값을 개선으로 주장하거나 느린 표본을 삭제하지 않는다. 이전 SDK/다른 실행의 값을 혼합하지 않는다. Query wall 비용만 측정하므로 solver 총비용·렌더/GPU 수용을 대체하지 않는다.
- 최신 SDK의 쿼리 ABBA 재측정 항목 완료. 남은 수용 문제는 batch 평균·query p99 변동 원인과 계측 허용 예산, solver 활성/변경 비율별 미수용 셀 및 RenderThread p99 반복성이다. M3는 progress 유지, M4는 시작 전이다.
- 근거: M3Acceptance/Performance-965fee9afd714227a8bdef8c8609d58e/result.json와 각 원본 run/query-benchmark.json 및 pool-query-abba.log·pool-query-abba-summary.log.
