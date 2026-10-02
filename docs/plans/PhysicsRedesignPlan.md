# 물리 재설계 — C++23 PhysX API 선행 재작성 (PHASE 19)

수립일: 2026-08-18 · 빅뱅 범위 개정: 2026-10-01
관련: [컴포넌트 설계](../design/PhysicsComponentDesign.md) · [잡 시스템 계약](../design/JobSchedulerDesign.md) · [직렬화](SerializationPlan.md) · [고정 Simulation Tick](NetworkFrameworkPlan.md)

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
| M3 | 5 | 제품 기능·CPU/GPU 회귀·평균/p99/메모리·실제 캡처 |
| M4 | 2 | 제거/배선 재유입 감사·필수 게이트 통합 확인 |
| **합계** | **72** | 기본 범위, 별도 위험 여유 제외 |

통합·스키마 이전·GPU 검증 불확실성에 대한 여유는 기본 공수의 20~30%(약 14~22인일)로
별도 관리한다. 여유 포함 계획 범위는 약 86~94인일이며 단계 진행 중 새 증거로 갱신한다.
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
