# ContactStream 물리 접촉·C# 계약

개정: 2026-10-08 · 상태: 기본 스트림 구현 진행(E0), 제품 통합/확장 게이트 미완료.

## 확정한 저작 표면

별도 Job/PhysicsBehaviour/정적 람다·접근 descriptor 생성 요구를 철회한다. 스크립트는 OnBeginSimulation에서 한 번 등록하고 PostPhysics에서 매칭 결과만 소비한다. 기존 OnBeginSimulation()/Scope 수명은 유지한다.

```csharp
private ContactStream? _attackContacts;

public override void OnBeginSimulation()
{
    // 역할 저작의 현행 구현: 해당 형상을 소유하는 스크립트가 명시적으로 바인딩한다.
    Physics.BindContactRole(attackBody, attackShapeId, ShapeRoles.Attack);
    _attackContacts = Physics.ObserveContacts(
        owner: Entity,
        selfRole: ShapeRoles.Attack,
        otherRole: ShapeRoles.Hurt,
        phases: ContactPhases.Begin);
}

public override void PostPhysics(float dt)
{
    foreach (ref readonly var contact in _attackContacts!.Read())
        ApplyDamage(contact.OtherEntity);
}
```

attackBody/attackShapeId와 ApplyDamage는 게임 저작값/게임 함수 예시다. 상대 Hurt 형상도 소유 스크립트에서 BindContactRole을 등록해야 한다. 바인딩이 없는 형상은 역할 구독에 전달하지 않는다. 임의 GUID를 ShapeRole로 사용 가능하며 Attack/Hurt는 기본 예시 신원이다. role은 기존 collision layer/행렬과 독립이다.

## 현재 구현

- 역할은 ObjectHandle generation + native component instance + body-local ShapeId에 Scope-owned runtime 바인딩한다. 존재하는 body/shape를 등록 시 확인하며 같은 형상의 중복 역할 바인딩은 거부한다. 저장 contactRole은 형상 생성 때 값으로 변환하고 SDK endpoint snapshot에 보존한다. 저장 역할이 있으면 명시 BindContactRole은 거부하며, 역할 없는 형상만 Scope-owned script 바인딩을 사용할 수 있다.
- ObserveContacts는 현재 스크립트 훅의 Scope에 자동 귀속한다. owner/role pair/phase 키의 dictionary posting list에 한 번 등록한다. Read는 사전 확보 배열의 ReadOnlySpan을 O(1)로 반환하며 필터 검색/래퍼 할당을 하지 않는다.
- 네이티브 finish_step 결과를 tick과 양쪽 binding/shape/body generation 값으로 수집한다. 최대4 catch-up tick의 이벤트를 해당 Advance 결과에 누적한다. SDK callback에서 CLR 호출 없음. Scene이 양쪽 Entity/component 신원을 붙여 frame당 한 번 managed batch로 발행한다.
- 소유자 방향별 네이티브 event마다 역할/route hash lookup 후 실제 매칭 스트림만 적재한다. 이벤트 E 및 실제 전달 M에 비례하며 모든 구독을 스캔하지 않는다. EndFrame은 이번 frame에 결과가 들어온 스트림만 초기화한다. PostPhysics 자체의 기존 컴포넌트 순회는 유지한다.
- Contact는 Self/Other Entity·component ID·ShapeId, 원래 물리 Tick, phase/sensor, 대표 접점과 written/required 접점 수를 갖는 값이다. complete manifold나 normal/impulse 배열 API를 제공한 것으로 보지 않는다.
- contact_begin/persist/end 및 sensor_enter/exit를 전달한다. 활성 sensor pair 인덱스에서 물리 tick마다 sensor_persist를 합성한다. Begin이 발생한 tick에는 같은 pair의 Persist를 중복 발행하지 않는다. 첫 관측이 Persist인 sensor 구독은 Begin 요청 시 한 번의 초기 Begin을 전달한다.
- scope Cancel/Dispose에서 route/role 바인딩을 제거한다. Dispose 이후 Read는 실패하고 다른 스레드 Read/Dispose는 거부한다. 빌린 span은 PostPhysics 종료 전까지만 소비한다. span escape를 런타임이 모두 탐지하는 API는 아니다.
- 관리 스트림 기본256/max65536 고정 capacity, 초과는 Overflowed/RequiredCapacity로 보고하며 Read가 부분 결과를 거부한다. 네이티브 frame 수집 max65536, SDK event drop/unresolved identity는 simulation failure다. native/managed contact layout은 112 bytes, tick offset80, ABI36다. ShapeState는 role 포함88 bytes다. 구 Collision/OnCollision*/OnTrigger* 미러/콜백과 sample 소비자를 교체한다.

Read 결과의 K개 순회 O(K)는 필요한 처리 비용이다. 구독 등록/취소의 낮은 빈도 비용은 별도이며 steady route/read/clear 할당0은 관리 회귀 측정 범위다. 네이티브 수집 vector/registry 및 제품 전체 할당0/성능 수용을 주장하지 않는다.

## 수명과 현재 한계

OnBeginSimulation 밖에서 ObserveContacts/BindContactRole을 직접 호출하면 거부한다. pending stream 데이터는 PostPhysics finally에서 비우고 scene unload 시 native queue를 비운다. no-step frame에 지난 contact를 재전달하지 않는다. Scene binding/세대를 값으로 넘겨 객체 포인터의 callback 수명을 넘기지 않는다.

형상 교체의 topology epoch, endpoint 파괴에 따른 합성 End, 여러 역할 집합, DistinctTargets의 active-pair 집계, 늦은 구독 초기 overlap 제품 gate와 Inspector UI 직접 수용은 후속 게이트다. 저장 역할의 Prefab/cooked Release 제품 왕복 및 sensor Persist는 아래 후속 수용으로 반영했다. 현재는 형상 pair event를 그대로 전달한다. DDOL/reload/Disable-Enable/Play-Stop의 제품 검증 없이 포괄 수명 완료를 판정하지 않는다.

스크립트 반응은 기존 PostPhysics owner thread에서 실행한다. 새 병렬 job/직접 Write 계약/명령 writer는 도입하지 않는다. PHASE24 PrSM/OnSimulate 개편은 미래 범위를 유지한다.

## E0 완료 조건

기본 native→managed→stream 제품 접촉 수용, role 저작/cook/Prefab 왕복, sensor Persist/집계/교체 및 stale 경계, buffer overflow/예외 정책, Play-Stop/DDOL/reload, ABI/Shipping 격리, 실제 Physics.ContactCollect/ContactPublish 계층 및 tick/task 연관, sparse/dense/fanout/churn 평균/p99/메모리/계측 on-off 검증. 문서나 단독 probe 통과를 전체 제품 완료로 올리지 않는다.

## 이번 기본 구현 검증

2026-10-08: 관리 라우터18 checks/steadyAllocatedBytes0, ABI35 checks/version34, GameScripts와 Release Player 빌드 통과. native B2는 센서 진입·이탈/endpoint 신원/catch-up 보존을 포함해739 checks까지 진행했으나 profiler 기록 손실로 capture complete 게이트 실패. 당시 B2 전체 통과나 제품 접촉 소비 완료로 판정하지 않았다. 아래 후속 제품 수용을 별도로 적용한다. 대시보드 JS 렌더는 통과했으나 현 문자열 검사기는 다른 페이즈의 quoted property key를 거부한다.

### 후속 Release 제품 접촉 수용

- HTTP CLI가 primitive body 두 개와 sphere sensor, Camera, 관리 probe를 저작했다. fresh Release Editor 두 Play/Stop에서 양쪽 스트림의 sensor Begin/End와 contact Begin/Persist/End, Self/Other Entity·component·ShapeId·tick 방향을 검증했다. Transform position/rotation/scale 원복, scene SHA 불변, 생성한 스트림2→4개 전체 Dispose를 확인했다.
- 같은 씬의 cooked CEDO1/CEMF/PAK Release Player를 별도 프로세스로 실행했다. 양쪽 접촉 검사 성공, 2000 GT frames/1970 display promotions/exit0, stage 파일 SHA 불변. 모델/게임 콘텐츠 부하 없이 primitive contact API의 제품 배선 수용 범위다.
- 실제 실행에서 발견한 Transform 분해 scale roundoff의 불필요 body replacement를 8 float epsilon 상대 비교로 억제했다. 실제 scale 변경은 계속 반영한다. replacement 직전 SDK body generation→component binding은 다음 유효 Advance 이벤트 소비까지 보존하고 no-step frame에서 유지한다. 보존 map 상한65536, Stop에서 제거. native Shipping746 checks/실제 GPU 통과; 완전한 삭제/topology 합성 End 정책을 수용한 것은 아니다.
- 증거: Build/Verification/ContactStream/Product-c20dbfff6db74da282cf3a1711140455/result.json, Player-44593a84178f4141817904feddb9b680/result.json, product-native-source.log. 재현: verify-physics-contact-stream-http.ps1 및 verify-physics-contact-stream-player.ps1.
- 역할 파일/Inspector/Prefab/cook 왕복, sensor Persist·target 집계·초기 overlap, 삭제/다중 교체/topology 수명, Disable/reload/DDOL, contact API Shipping 제품 수용, profiler loss 수정과 capture/성능은 잔여다. E0 progress 유지.

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
