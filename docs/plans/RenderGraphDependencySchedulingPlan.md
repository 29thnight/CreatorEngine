# RenderGraph 리소스 의존성 스케줄링 계획 (PHASE 4.3 · 트랙 RG)

**2026-10-01 사용자 결정:** 이 페이즈의 실행·픽셀·성능 완료 판정은 DX12 Debug/Release다. RHI 중립 계약과 필요한 backend 구현은 유지한다. Vulkan 실행 비교·동등성·교차 픽셀 수용은 [PHASE 4.9](BackendParityPlan.md)의 RenderDoc 캡처 → 리소스 확인 → 픽셀별 비교가 단독 소유하며 이 페이즈의 선행·잔여·실패 조건으로 사용하지 않는다.


2026-08-28 작성. `EnhancedRenderGraph`를 교체하지 않고, 명시적 리소스 접근과
버전 계보로 실행 순서를 컴파일하는 그래프로 단계적으로 확장하는 구현 계획이다.

상태: **2026-10-03 BASE-0 완료, RG1 완료, RG2 완료, RG3 완료, RG4 완료, RG5 생산자 첫 묶음 완료·전체 이관 진행, RG6~RG9 미착수.** 문서 작성과 정적 검증은 구현·빌드·픽셀 검증 완료를
뜻하지 않는다.

---

## 0. 결정 요약

**2026-10-02 BASE-0 범위 정리:** 현재 DX12 Debug/Release 바이너리·고정 대표 장면의 이미지,
graph 진단·변이, capture 계측·GPU validation·정상 종료와 구성 간 대조가 완료 조건이다.
범용 frame packet·재질/애니메이션 재생은 선택 확장으로 분리하며 RG1 선행이 아니다.
2026-10-03 현행 Debug/Release 기준선과 구성 간 16개 이미지 maxError=0, 현재 해시 및 `phaseComplete=true`를
회수했다. [현재 완료 계약과 증거](../analysis/RenderBase0Baseline.md)를 따른다. RG1 이후 현행 기준선과 변경 전후 증거는 [RG1 검증 기록](../analysis/RenderRg1Scheduling.md)에 남긴다. RG2도 완료했으며 [버전·Modify 검증 기록](../analysis/RenderRg2Versions.md)에 현행 기준선을 남긴다. RG3도 완료했으며 [컬링·수명·배리어 검증 기록](../analysis/RenderRg3LifetimeBarriers.md)에 현행 기준선을 남긴다. RG4도 완료했으며 [wave·병렬 기록 검증 기록](../analysis/RenderRg4RecordingWaves.md)에 현행 기준선을 남긴다. 다음 구현은 RG5다.

2026-10-03 RG1 구현 경계: 같은 `EnhancedRenderGraph` 생성자의 `ExplicitSingleWriter` 모드에서
`RGPassUsage.access`를 반드시 명시한다. 최소 authored index를 먼저 꺼내는 안정적 Kahn 정렬이며
순환 오류에는 실제 패스·자원 경로를 기록한다. imported Read는 writer가 없으면 외부 초기값을 소비하고,
writer가 있으면 그 결과를 소비한다. ReadWrite는 initialized imported 자원의 단독 사용만 허용하며
다른 reader나 writer가 있으면 RG2 version/Modify를 요구한다. 제품 기본 모드는 RG5 이관 전까지
`DeclarationOrder`이며 명시적 접근과 암묵 접근을 혼합하지 않는다. 이는 별도 실행 그래프가 아닌 같은
Compile/Execute 경로의 선언 계약 선택이다. Debug/Release 빌드·24 shuffle native fixture·역순 RAW GPU 픽셀 검사 및 제품 변경 전후/구성 간 16개 이미지 maxError=0을 회수했다. RG1 완료이며 제품 이관은 RG5/6에 남긴다.

2026-10-01 착수 판정: BASE-0은 구현·검증된 MAT-0~MAT-8 기반을 소비하며 MAT-9 최종 수용을 기다리지 않는다. 현재 IBL 1024/4096과 이미지 수용 상한을 유지하고, 기존 SSS·투과 품질 미달 및 이동 카메라 성능 미수용을 baseline의 알려진 공백으로 기록한다. 구조 변경의 회귀/재현 통과와 MAT-9 최종 품질/성능 수용은 별도 판정이다. RG6 뒤 필요한 GPU-driven/IBL 재사용 구현·실측을 거쳐 MAT-9 성능 게이트를 회수한다. 하네스·graph snapshot·capture 제출별 GPU 계측·변이 게이트를 구현했으며 DX12 Debug/Release의 재현·변이·계측 검증으로 판정한다. Vulkan 교차 비교는 PHASE 4.9로 이관하여 BASE-0의 실패/선행 조건에서 제거한다.

1. **실행 그래프는 `EnhancedRenderGraph` 하나만 유지한다.** 별도 FrameGraph나 제품용
   이중 실행 경로를 만들지 않고 현재 Compile/Execute 경계를 확장한다.
2. **C# Pipeline IR의 Pass 목록은 저작 순서다.** 실제 실행 순서는 선언한 리소스
   의존성에서 계산하고, 서로 독립인 Pass만 저작 index를 안정 tie-break로 사용한다.
3. **DAG 정렬을 먼저 연다.** `Read`, `Write`, `ReadWrite`와 `RHIResourceState`를 분리한
   뒤 첫 DAG는 단일 writer 리소스와 명시적 ordering token만 받는다. 다중 writer와
   `ReadWrite` 연쇄는 추측하지 않고 오류로 거부한다. 다음 슬라이스에서 버전 핸들을
   도입해 `Write`/`Modify`의 연쇄를 연다.
4. **단일 그래픽 큐를 먼저 완결한다.** 단일 writer DAG → 버전/Modify DAG → 기존 파이프라인 이관 →
   DX12 live 전후 픽셀 게이트를 닫기 전에는 aliasing과 async compute를 열지 않는다.
5. **그 뒤에 메모리와 큐 최적화를 연다.** transient buffer/aliasing을 먼저, 실제 GPU
   겹침 이득을 계측한 뒤 multi-queue/async compute를 도입한다.
6. **빅뱅 전환을 금지한다.** 각 슬라이스는 독립 검사와 A/B 스위치를 가지며, 다음
   슬라이스는 직전 acceptance gate가 통과한 뒤 시작한다.

이 순서는 현재 코드가 위상 정렬을 포기한 이유를 무시하지 않는다. 현재의 unversioned
handle과 state 기반 쓰기 추론만으로는 두 writer의 선후를 알 수 없다. RG1에서 명시적
접근과 **단일 writer DAG**를 먼저 닫고, 모호한 두 writer는 fail-closed한다. RG2에서
버전 계보를 도입한 뒤에만 다중 writer를 스케줄한다.

---

## 1. 현재 코드 기준선

2026-10-01 워킹트리 정적 감사 기준이다. BASE-0은 현행 계약을 관측하며 DAG 스케줄러 구현은 RG1~RG2에 남긴다.

| 영역 | 현재 보유 | 자동 의존성 실행까지 남은 공백 |
|---|---|---|
| 그래프 생명주기 | `Compile`/`Execute`, pass culling, transient texture pool, lifetime, Transition/UAV 배리어, 병렬 기록 | compile 결과가 리소스 DAG가 아니라 선언 순서 |
| 실행 순서 | `BuildOrder`가 read-before-write를 검사한 뒤 선언 index를 그대로 `m_executeOrder`에 적재 | edge table, stable topological sort, cycle/모호한 writer 진단 없음 |
| 리소스 핸들 | `RGHandle { index }`, texture/buffer import, transient texture 생성 | 논리 resource ID와 version 분리 없음, transient buffer 생성 없음 |
| Pass 사용 | `RGPassUsage { handle, RHIResourceState }` | `IsWriteState`로 쓰기를 추론하므로 read와 read-modify-write를 구분할 수 없음 |
| 서브리소스 | 텍스처 전체 단위 상태와 수명 | mip/array/range별 접근·배리어·alias 판단 없음 |
| 큐 | DX12 `DIRECT` 큐 하나, Vulkan graphics family/queue 하나 | compute queue, queue ownership transfer, cross-queue fence 없음 |
| 제품 표면 | 기본 live pipeline 15개 노드 + Editor 기여 4개, `.cpp`/`.h`의 `.AddPass`/`->AddPass`·`AddSplitPass` 정적 호출 183곳(Engine 52, Editor 제품 4, Editor test 98, Tools 29; BASE-0 capture·film upload fixture 포함) | 모든 생산 Pass의 RHI 중립 접근 선언 이관과 DX12 제품 cutover 필요 |

근거 위치:

- `Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h:40`, `:50`, `:286`, `:296`
- `Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp:12`, `:131`, `:192`, `:497`
- `Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp` (`BuildPipelineDesc`, DX12/Vulkan 제품 캡처 경로)
- `Editor/EngineEntry/EditorSceneOverlayContributor.cpp:82`
- `Engine/RenderEngine/RHI/DX12/DX12DeviceResources.cpp:295`
- `Engine/RenderEngine/RHI/Vulkan/VulkanDeviceResources.cpp:390`, `:451`, `:521`

호출 수는 구현 착수 직전 다시 센다. 이 표의 수치는 일정 산정용 기준선이며 완료 판정
자체가 아니다.

---

## 2. 목표 계약

### 2.1 리소스와 버전

이 절은 **RG2 이후의 최종 계약**이다. RG1의 첫 DAG는 명시적 접근을 요구하되
단일 writer만 허용하며 같은 리소스의 추가 `Write`/`ReadWrite`를 실패시킨다.

```text
RGResourceId = 프레임 안에서 동일한 논리 texture/buffer의 정체성
RGVersion    = 그 논리 리소스에 새 내용이 발행될 때 증가하는 버전
RGHandle     = (resourceId, version, kind)

Read(vN, state)      -> vN의 producer에 의존
Write(vN, state)     -> vN+1을 발행, vN을 읽지는 않음
Modify(vN, state)    -> vN을 읽고 vN+1을 발행
```

- import는 외부 내용이 있는 `v0`에서 시작한다.
- transient는 최초 writer가 `v0`을 발행하기 전에는 읽을 수 없다.
- 같은 입력 버전에서 두 writer가 갈라지는 forked write는 compile 오류다. 호출 순서를
  암묵적 답으로 쓰지 않고 어느 출력이 다음 버전인지 선언을 고치게 한다.
- `RGAccessMode { Read, Write, ReadWrite }`와 요구 `RHIResourceState`를 별도 필드로 둔다.
- texture와 buffer가 같은 버전·접근 API를 사용하되 descriptor와 실제 handle 형식은
  타입 안전하게 유지한다.

### 2.2 의존 edge

- **RAW:** `vN` producer → `vN` reader.
- **WAW:** 같은 논리 리소스의 `vN` producer → `vN+1` writer.
- **WAR:** `vN` readers → 물리 저장소를 덮어쓸 수 있는 `vN+1` writer.
- side effect와 외부 출력은 명시적 root/ordering token으로 모델링한다. 이름이나 우연한
  등록 위치로 순서를 만들지 않는다.
- 독립 Pass는 저작 index가 작은 순서로 꺼내는 stable Kahn sort를 사용한다. 이 규칙으로
  graph dump, 픽셀 fixture와 디버깅 재현성을 유지한다.

### 2.3 Compile 순서

```text
접근·버전 검증
  → producer/consumer edge 구성
  → side-effect/output root에서 역방향 culling
  → 살아남은 DAG의 stable topological sort
  → 정렬된 순서에서 lifetime 계산
  → transient 할당/alias 계획
  → barrier와 record wave 계획
  → immutable compiled graph 실행
```

cycle 오류는 최소한 `Pass → ResourceVersion → Pass` 사슬을 출력한다. missing producer,
forked write, read/write 중복 선언, 범위를 벗어난 handle도 GPU 실행 전에 Pass·리소스·버전을
함께 지목한다.

### 2.4 저작 순서와 실행 순서

C# Pipeline IR은 사용자가 이해하고 diff할 수 있는 authored Pass 목록을 보존한다.
Compiler가 각 슬롯의 버전 핸들을 연결한 뒤에는 다음처럼 해석한다.

- 의존성이 있는 Pass: 리소스 edge가 실행 선후를 결정한다.
- 독립 Pass: Stack 순서가 deterministic tie-break다.
- 화면 표시와 IR serialization: authored 순서를 유지한다.
- 실행 미리보기: compiled order와 원래 authored index를 함께 표시한다.

---

## 3. 구현 순서와 공수

1인 전담 엔지니어 기준 개발일이다. 병렬 인원 투입 시에도 RG1→RG6 임계 경로는 줄지
않으며, backend 안정화와 픽셀 판정 시간은 별도로 필요하다.

| ID | 슬라이스 | 선행 | 공수 | 종료 게이트 |
|---|---|---:|---:|---|
| ~~**RG0**~~ | **`BASE-0`에 흡수** — 4-0·SRP-G0와 같은 하네스·같은 artifact였다. graph dump와 변이 fixture는 `BASE-0`의 소비 항목으로 남는다 | 없음 | (BASE-0 4일에 포함) | [`Phase4UnifiedPlan.md`](Phase4UnifiedPlan.md) §6.1 |
| **RG1** | 명시적 access mode + 단일 writer stable DAG compiler | BASE-0 | 8일 | RAW, 독립 Pass tie-break, cycle chain, 선언 배열 shuffle fixture; 다중 writer와 모호한 ReadWrite 연쇄는 명시적 오류 |
| **RG2** | versioned texture/buffer handle + 다중 writer/Modify DAG | RG1 | 10일 | `Write`/`Modify` 새 version, WAR/WAW, import/transient version dump, forked write·stale handle fail-closed |
| **RG3** | DAG 기준 culling·lifetime·barrier 재계산 | RG2 | 8일 | 죽은 producer 제거, 마지막 소비 수명, Transition/UAV 계획이 sorted order 기준으로 일치 |
| **RG4** | dependency wave 기반 병렬 recording·진단 | RG3 | 6일 | sequential/parallel compiled order와 픽셀 동일, wave·critical path·edge 원인 dump 제공 |
| **RG5** | 제품 Pass 접근 선언 이관 | RG4 | 10일 | 기본 19개 node, 제품 호출과 test/fixture 접근 선언 이관; 임시 adapter 잔여 0. C# IR 조립은 PHASE 4.6 소유 |
| **RG6** | RHI 중립 제품 cutover·DX12 수용 — **이 페이즈의 완료선** | RG5, **BASE-0** | 4일 | 같은 밀봉 입력의 별도 프로세스 live frame, PNG/차영상/선형 오차, CPU record·pass GPU·graph stats, validation 0 |
| **RG7** | transient buffer + in-frame aliasing | RG6 | 14일 | alias off/on 픽셀 동일, peak committed/resident byte 감소 실측, poison/overlap/lifetime 변이 통과 |
| **RG8** | queue-neutral multi-queue + async compute | RG7 | 16일 | single-queue fallback, cross-queue fence/ownership, DX12 validation, 겹침 GPU 이득 실측 |
| **RG9** | subresource·split barrier·Resource Inspector 성숙 | RG8 | 10일 | mip/array/range 추적, split barrier parity, producer/consumer/version/order/lifetime/alias/queue 시각화 |
| **Q0** | 중립 queue/capability·fence 기반 | RG8/L4 소비 전 | 6일 | DX12 큐·실패·수명 검증, 중복 구현 0 |
| **RG-V** | compiled graph 기본 reader/viewer | RG3→RG6 | 4일 | 실제 generation·order·resource 표시, 별도 실행 그래프 0 |

**2026-10-01 재산정:** BASE-0 4 + RG1~6 46 + RG7~9 40 + Q0 6 + RG-V 4 = **100인일**.
RG6 첫 제품 완료선은 BASE-0/RG1~6 50인일이며 기본 viewer 4일을 포함하면 54인일이다.
Q0 공통 기반은 RG8/L4에 중복 산정하지 않는다. 상세는 [공수 원장](RenderPhaseEffortEstimate.md)을 따른다.

RG7 이후는 최적화 트랙이다. RG6을 통과하면 리소스 의존성으로 실행 순서를 결정하는
제품 RenderGraph는 이미 성립하며, 뒤 단계가 늦어져도 declaration-order로 되돌리지 않는다.

---

## 4. 슬라이스별 구현 경계

### RG0 — 기준선을 먼저 잠근다

- 현행 `BuildOrder` 계약과 제품 graph dump를 artifact로 남긴다.
- 독립 Pass, 연쇄 RAW, 두 writer, read-modify-write, culled branch, imported history fixture를
  RHI 중립 입력을 사용하는 DX12 테스트로 만든다.
- UI/Grid처럼 같은 target을 읽고 다시 쓰는 Pass를 찾아 state만으로 `modify`가 표현되지
  않는 사례를 고정한다.
- 테스트가 잘못된 구현을 잡는지 producer edge 삭제, version 재사용, 순서 뒤집기 변이로
  확인한다.

### RG1 — 최소 접근 선언으로 단일 writer DAG부터 정렬한다

- 기존 `{handle, state}` aggregate 초기화는 임시 adapter에서만 받고 테스트와 이관 대상은
  `Read`/`Write`/`ReadWrite`를 상태와 별도로 선언한다.
- 한 리소스에 writer가 하나일 때 RAW edge로 stable topological
  sort를 수행한다. ready 집합은 authored index로 tie-break한다.
- writer가 둘 이상이거나 `ReadWrite` 연쇄의 producer를 결정할 수 없으면 컴파일을 실패시킨다.
  선언 순서에서 암묵적인 writer 순서를 추측하지 않는다.
- 독립 Pass 재배열, cycle, 누락 producer, 다중 writer 거부 fixture를 기준선과 대조한다.

### RG2 — 버전으로 다중 writer·Modify DAG를 연다

2026-10-03 완료: ExplicitVersioned·resource ID/version/kind/epoch·Write/Modify·RAW/WAR/WAW를 구현했다. 두 구성 240 shuffle·오류 거부 및 실제 GPU 이전 버전 픽셀 0, 변경 전후/구성 간 16개 이미지 오차 0을 검증했다. 제품 기본 DeclarationOrder와 보수적 culling은 유지하며 RG3/5/6에서 정리·이관한다. transient buffer 생성은 RG7이다.

- `Write`/`Modify`가 새 version handle을 반환하고 후속 consumer가 특정 버전을 읽게 한다.
- texture와 buffer의 import/create, read/write/modify 계약을 대칭으로 닫는다.
- RAW/WAR/WAW와 `Modify` 연쇄를 version 계보로 유도하며 다중 writer를 명시적으로 판정한다.
- 컴파일 결과에 resource ID·version·producer·consumer·access·required state를 기록한다.
  edge/indegree 저장을 compact하게 유지하고 hot-frame heap churn을 계측한다.
- debug에서는 전체 cycle 사슬, release에서는 짧은 오류와 stable ID를 남긴다.

### RG3~RG4 — 기존 기능을 새 순서에 다시 연결한다

2026-10-03 RG3 완료: version producer 역추적과 imported 최종 출력 root, dead writer/reader 제거 후 WAR/WAW 재연결·stable sort를 구현했다. compiled order의 수명/할당/Transition 계획을 검증하고 UAV read→read 배리어를 생략했다. 두 구성 120 shuffle 및 실제 GPU dead writer 미실행·이전 버전 픽셀 0, 제품 변경 전후/구성 간 16개 이미지 오차 0을 회수했다. 제품 기본 DeclarationOrder는 RG5/6 이관까지 유지한다. 2026-10-03 RG4 완료: 살아남은 RAW/WAR/WAW의 dependency wave·pass-count critical path, target append 제약을 포함한 recording wave를 구현했다. compiled GPU 제출 순서와 split/cost fallback을 유지한다. 두 구성 24 shuffle·1/2/4 워커 GPU 픽셀 0 및 현재 HEAD의 변경 전후/구성 간 제품 16개 이미지 오차 0을 회수했다. [RG4 증거](../analysis/RenderRg4RecordingWaves.md)를 따른다.

- culling은 "앞서 쓴 모든 Pass" 검색이 아니라 version producer edge를 역추적한다.
- lifetime과 transient 회수는 declaration index가 아니라 compiled index를 사용한다.
- barrier state machine도 compiled order에서 계산하고 access mode와 state 불일치를 거부한다.
- 병렬 기록은 DAG의 ready wave를 사용하되 GPU submit은 compiled order와 backend 계약을
  보존한다. wave 수보다 record cost가 우선인 기존 split-pass 휴리스틱은 유지한다.

### RG5~RG6 — 제품을 한 번만 전환한다

2026-10-03 RG5-1 완료: 실제 GBuffer 6출력·Shadow 1출력 생산자 선언을 버전 모드의 Write로 연결했다. 세 모드×두 선언 순서의 실제 Declare 계획 검사와 두 구성 제품 회귀를 통과했으며 변경 전후/구성 간 16개 이미지 오차는 0이다. 현재 정적 호출 위치는 제품 59·게이트 202(새 검사 1 포함)이며 Tools/regression의 포인터 호출 18도 게이트로 센다. [생산자 이관 기록](../analysis/RenderRg5ProducerMigration.md)을 따른다. 기본 DeclarationOrder 호환 분기는 아직 남아 있으므로 RG5 전체·legacy 추론 0·제품 versioned GPU 수용을 완료로 판정하지 않는다.

> **과거 `SRP-1`과의 병합 판정 (2026-09-01, 현재 계획에는 미적용).** `Phase4UnifiedPlan` 백로그 산정 중
> "RG5와 SRP-1이 같은 500줄을 만지니 병합하자"는 제안이 나왔다가 **전수 실측으로
> 기각**됐다. 두 계획서가 같은 명사("19개 노드")를 쓰지만 대상 심볼이 다르다 —
> RG5는 `AddPass`/`AddSplitPass`(총 120 · **제품 38** · 게이트 82)로 **Pass 구현 파일
> 전역에 흩어져** 있고, SRP-1은 `AddNode`(총 33 · 제품 33)로 **조립부에 모여** 있다.
> 교집합 파일은 `EnhancedSceneRenderer.cpp` 하나뿐이고 그 안에서도 다른 줄이다.
> 축도 다르다(접근 **선언** vs 노드 **조립**) — 합치면 픽셀이 붉을 때 어느 축인지
> 못 가린다. 당시에는 `RG5 → SRP-1` 순서와 12일·8일을 따로 산정했다.
> 판정 전문: [`ScriptableRenderPipelinePlan.md`](ScriptableRenderPipelinePlan.md) §12-E.
> 현재 C# 저작 전환은 [`CSharpRenderPipelinePlan.md`](CSharpRenderPipelinePlan.md)의
> `CSRP-5`가 별도로 소유하며, 옛 `SRP-1` 8일을 이월하지 않는다.
>
> **덤 — 호출 수가 늘었다.** §3 표와 아래 목록의 근거인 "제품 28곳 · test/fixture
> 80곳(총 108)"이 2026-09-01 실측으로 **제품 38 · 게이트 82(총 120)**다. 제품만 **+36%**.
> 이 절이 "호출 수는 착수 직전 다시 센다"고 적어 둔 그대로이며, 현재 RG5는 이관 7 + DX12 회귀 3의 10일 계획 추정으로 갱신했다.

- base 15개 + Editor 4개 node를 작은 묶음으로 이관하고 각 묶음마다 현행/A-B 픽셀을
  비교한다.
- migration adapter와 legacy order switch는 테스트 rollback용으로만 유지하고 RG6 종료 시
  제품 기본 경로에서 제거한다.
- pass fixture와 전체 live frame을 별도 판정한다. 하나의 통과로 다른 하나를 대신하지
  않는다.
- DX12에서 같은 밀봉 입력의 compiled graph stable ID와 dependency hash가 재현되어야 한다.

### RG7 — aliasing은 정확성 완료 뒤 연다

- 먼저 transient buffer를 texture와 같은 lifetime 모델에 넣는다.
- 서로 겹치지 않는 compiled lifetime만 같은 heap 영역을 공유한다.
- alias barrier, alignment, format/usage compatibility, imported/history 제외 규칙을 명시한다.
- `r.RenderGraph.Aliasing=0/1`, poison clear, lifetime 강제 연장 모드로 메모리 절감과 픽셀
  동일성을 동시에 판정한다.

### RG8 — async compute는 계측으로 채택한다

- RHI에 graphics/compute queue capability, encoder pool, signal/wait fence와 queue ownership을
  중립 인터페이스로 추가한다.
- scheduler는 capability와 비용 힌트가 허용한 Pass만 compute queue 후보로 만들고, 미지원
  환경에서는 같은 DAG를 graphics queue 하나로 실행한다.
- queue 이동으로 resource lifetime이 늘어 aliasing 이득을 상쇄할 수 있으므로 peak memory와
  GPU critical path를 함께 비교한다.
- Lightmap 트랙 L4는 이 공통 기반을 소비한다. 별도 compute queue 계층을 중복 구현하지 않는다.

### RG9 — RDG 운용 성숙도를 닫는다

- texture mip/array slice와 buffer byte range를 view/range로 선언한다.
- backend가 지원하는 split barrier를 중립 계획으로 표현하되 단일 barrier fallback을 둔다.
- Resource Inspector는 compiled graph의 읽기 전용 소비자다. 실행 그래프나 GPU resource를
  따로 소유하지 않는다.

---

## 5. 회귀와 실패 판정

| 범주 | 필수 판정 |
|---|---|
| 정적/API | implicit state-write 추론 제품 사용처 0, unversioned migration adapter 0, task/graph stable ID 중복 0 |
| 그래프 | missing producer, forked write, cycle, culled branch, side effect, history import의 양·음성 fixture |
| 결정성 | 동일 입력 100회 compiled order/dependency hash 동일, 독립 Pass 등록 순서 tie-break 명시 |
| 배리어 | DX12 debug layer error 0, 요구/실제 state mismatch 0 |
| 픽셀 | pass fixture와 전체 live frame을 분리해 DX12 변경 전후 PNG·linear RMSE·max error·changed pixel 판정 |
| 병렬 | sequential/parallel pixels 동일, CPU record time과 critical path 기록 |
| 메모리 | RG7 전후 peak committed/resident/transient byte와 alias reuse count, poison mode 오류 0 |
| 큐 | RG8 single/multi queue 픽셀 동일, fence wait와 ownership transfer 누락 0, GPU frame time 이득 실측 |

어느 단계든 새 경로가 실패하면 해당 슬라이스의 A/B 스위치로만 되돌린다. 이미 통과한
버전 핸들/API까지 통째로 철회하거나 별도 제품 그래프를 만드는 롤백은 허용하지 않는다.

---

## 6. 비목표와 금지선

- Asset, Shader Graph, C#에 raw D3D12/Vulkan resource·barrier·fence를 공개하지 않는다.
- RG0~RG6에서 성능 추측만으로 독립 Pass를 재배치하는 cost optimizer를 만들지 않는다.
- RG6 전 aliasing, multi-queue, async compute를 제품 기본 경로에 배선하지 않는다.
- graph compile 도중 Pass callback을 실행해 숨은 의존성을 발견하지 않는다. 의존성은 setup
  선언에서 완결한다.
- DX12 전용 해법을 먼저 만들고 Vulkan을 나중에 어댑트하지 않는다.
- 문서 항목의 `done`을 빌드·런타임·픽셀 검증 완료로 해석하지 않는다.

---

## 7. 다른 페이즈·트랙과의 의존성

이 트랙은 **PHASE 4.3**이 소유한다. 2026-09-15 이전에는 PHASE 4.75의 한 트랙이었고, 그때
`BASE-0`은 PHASE 4.5에 있었다. 지금은 `BASE-0`도 이 페이즈가 소유하며 PHASE 4.5·4.6·
4.7·4.75·4.8의 필요한 게이트가 읽기 전용 입력으로 받는다. 현재 관계는
[`RenderPhaseRoadmap.md`](RenderPhaseRoadmap.md)가 정본이다.

| 계획/트랙 | 소속 | 관계 |
|---|---|---|
| `BASE-0` | **같은 페이즈** | `RG0`의 graph dump·변이 fixture를 흡수한 고정 장면 기준선 하네스다. `RG1`이 선행으로 받고 `RG6`가 live 판정에 쓴다. 하네스는 한 벌만 만든다 |
| `CSharpRenderPipelinePlan.md` | PHASE 4.6 | C# `Build()`의 immutable IR이 RG1 단일 writer DAG, RG2 version/Modify 계약을 소비한다. 제품 조립 전환 `CSRP-5`는 RG6 이후 별도 판정한다 |
| SRP-G0 | (`BASE-0`에 흡수) | RG0 기준선과 RG6 전체 live backend artifact를 공유한다. 별도 캡처 체계를 만들지 않는다 |
| `LivePipelineDescPlan.md` | archive | 현재 nodes/reads/writes/modifies를 RG5의 첫 native compiler 입력으로 사용한다 |
| `RhiBoundaryPlan.md` | archive | RG7 heap/alias 계약과 RG8 queue/fence 계약을 backend-neutral RHI에만 추가한다 |
| `ModelAssetBigBangCutoverPlan.md` | PHASE 3.75 | MBC6의 vertex attribute mask→input layout/PSO/VSIn 계약과 model generation handle이 RG5 제품 이관 전에 필요하다 |
| 트랙 L4 | PHASE 4.7 | `RG8`이 아니라 **`Q0`**(queue/fence RHI 계약)의 소비자다. `Q0`은 RHI 계층 공용 기반이며 어느 트랙도 별도 queue 계층을 만들지 않는다 — [`RenderPhaseRoadmap.md`](RenderPhaseRoadmap.md) |
| GPU-driven/DXR/Stochastic Lighting | PHASE 4.8 | RG6 단일 큐 제품 cutover 뒤 새 resource/pass를 추가하고, RG7~RG9 기능을 필요에 따라 소비한다 |
| TU/FG | PHASE 4.5 | **이 트랙을 선행으로 받지 않는다.** 모션 벡터·업스케일은 RG 재작성과 독립이고, 프레임 생성은 SDK가 자기 큐를 소유하므로 `Q0`도 받지 않는다. `BASE-0` 하나만 공유한다 — [`TemporalReconstructionPlan.md`](TemporalReconstructionPlan.md) §3.3 |

권장 임계 경로는 다음으로 고정한다.

```text
BASE-0 → RG1(단일 writer DAG) → RG2(version/Modify DAG) → RG3 → RG4 → RG5 → RG6
                                                                              ↓
                                                                            RG7 → RG8 → RG9
```

**2026-09-02 정정.** 모델/정점 입력 계약은 PHASE 3.75 완료를 하드 선행으로 받고, `SRP-G0`는
`BASE-0`으로 흡수돼 이 트랙보다 앞에 선다. **L4의 async compute는 `RG8`이 아니라 `Q0`을
기다린다** — `Q0`은 `RG8`·`L4` 중 먼저 필요해지는 쪽의 착수 시점에 세운다. 그 전의
UV/BVH/직접광 준비는 독립적으로 진행할 수 있다. 현재 통합 순서는
[`RenderPhaseRoadmap.md`](RenderPhaseRoadmap.md)가 정본이다.

**2026-09-15 페이즈 분리.** 이 트랙과 `BASE-0`이 **PHASE 4.3**으로 나왔다. 공수·선행·종료
게이트는 하나도 바뀌지 않았고 **소속만 바뀌었다** — 이 재배치를 진척으로 읽지 않는다.
분리 근거는 셋이다: ① `SRP-1`(RG5 선행)과 `L4`(Q0 선행)를 같은 페이즈에 묶어 두면 어느
완료선도 독립적으로 닫히지 않았다, ② 구 PHASE 4.75 208.5일 중 이 트랙이 113일로 절반을
넘어 한 페이즈에 완료선이 둘 있었다, ③ `BASE-0`은 `4-0`·`SRP-G0`·`RG0`의 통합물이라 가장
앞선 소비자인 RG를 따라와야 했다.

**이 페이즈의 완료선은 `RG6`이다.** `RG7`~`RG9`는 같은 페이즈 안의 최적화 트랙이며, 늦어져도
`RG6`가 세운 제품 RenderGraph를 declaration-order로 되돌리지 않는다.

## RG-V — compiled graph 읽기 전용 viewer (2026-10-01 추가)

PHASE 4.3의 별도 todo 4인일 행이다. RG9의 subresource/split barrier 구현 완료까지
기본 graph viewer를 미루지 않는다. RG3 compiled DAG/culling/lifetime/barrier 결과를 입력으로
초기 viewer를 만들고 RG6 제품 cutover에서 Scene/Game view별 실제 compiled generation으로 검증한다.
RG4 wave/critical-path와 RG7~9 alias/queue/range 정보는 지원되는 세대에서 같은 reader에 추가한다.

- Window > RenderPass는 설정/시간 계측이 아닌 이 viewer를 연다. 노드·리소스·edge 원인,
  authored/compiled order, version, culling, lifetime/state/barrier를 조사할 수 있다.
- 실행/저작의 정본을 새로 만들지 않는다. compiled immutable snapshot을 읽고, runtime GPU
  리소스를 UI가 소유하지 않는다. 미지원 range/queue는 빈 정답을 만들어 표시하지 않는다.
- 두 view·Scene/resize/세대 교체에서 원 graph와 node/edge/hash/수명/배리어가 일치하고,
  Pass 조건 변경/culled branch·cycle 진단이 실제 compile 결과와 일치해야 한다.
- 선언 배열 shuffle, missing edge, stale snapshot 변이가 검증을 실패시키고, viewer off/on
  비용·메모리 상한과 Debug/Release UI를 확인한다. 단순 현재 pipeline 문자열 dump는 RG-V 완료가 아니다.
- 4.6 CSRP-5/6의 C# Build → immutable IR → native compile 결과를 동일 viewer에 연결한다.
  Source가 C++/C#인 경우를 표시하되 두 개의 편집 가능한 graph SoT를 만들지 않는다.


2026-10-03 RG5-2 정책 분리 검증 완료: 실행 순서 정책을 별도 RGOrderPolicy로 분리한다. 같은 버전 DAG로 의존성 정렬 또는 선언 순서 유효성을 검사한다. [정책 분리 기록](../analysis/RenderRg5OrderPolicy.md). 소비자 이관·제품 GPU 수용과 RG6 기본 전환은 열려 있다.


2026-10-03 RG5-3: Deferred의 GBuffer·Shadow 읽기와 SkyBox의 Modify/ReadWrite를 이관했다. 두 구성 빌드·native 선언 검사·제품 전체 장면 회귀 통과, 변경 전후/구성 간 16개 이미지 오차 0으로 첫 소비 체인 묶음 완료. [소비 체인 기록](../analysis/RenderRg5ConsumerMigration.md). RG5 전체는 진행 상태다.


2026-10-03 RG5-4 완료: SSAO·SSGI 및 현재 프레임 history 저장의 Read/Write·버전 계보를 이관했다. Debug/Release native 8개 조합·제품 회귀와 변경 전후/구성 간 16개 이미지 오차 0을 확인했다. Forward+는 Code/Graph 혼합 스트림과 재질 Graph 선언을 함께 이관하는 다음 묶음이다. [검증 기록](../analysis/RenderRg5IndirectMigration.md).


2026-10-03 RG5-5 Code 경로 완료: Forward+ 타일 버퍼와 Code 색상 스트림을 이관했으며 두 구성 빌드·native·제품 회귀와 변경 전후/구성 간 16개 이미지 오차 0을 확인했다. Graph 혼합 경로는 Lookup/Refraction/Subsurface/Volume 자원 소유자와 함께 이관해야 하므로 아직 열림이며 명시 모드에서 구체적 오류로 거부한다. [범위 및 검증](../analysis/RenderRg5ForwardCodeMigration.md). RG5 전체 기성 0을 유지한다.


2026-10-03 RG5-6 Lookup 소유자 이관 완료: 입력 캡처 출력 버전 반환·Bake sample/statistic Write·Ready Read를 연결했다. Debug/Release 최종 빌드·native 반복 2회·두 정책 및 제품 회귀 통과. 변경 전후/구성 간 각 16개 이미지 오차 0. Release 최초 120초 종료 제한 실패는 보존하고 240초 대기 재실행의 정상 종료로 수용했다. 나머지 Graph 소유자와 혼합 스트림 guard는 아직 열려 있다. [Lookup 기록](../analysis/RenderRg5LookupMigration.md).

2026-10-03 RG5-7 Refraction·Subsurface·Volume 소유자 이관 완료: 최신 Lookup 입력 소비·캡처/배경/Bake/필터/합성 출력 버전을 연결했다. 두 구성 빌드·native·제품 반복 회귀 통과, 변경 전후/구성 간 각 16개 이미지 오차 0과 현재 해시를 확인했다. [소유자 검증 기록](../analysis/RenderRg5SpecialMigration.md). 제품 59·게이트 213 호출. GraphSurface/Draw·혼합 GPU 수용과 RG5 전체 기성 0은 유지한다.

2026-10-03 RG5-8 GraphSurface·Draw 및 반환 출력 완료: Shadow/GBuffer/Color/Blended 출력 반환·블랙보드/Forward+ 전달, 깊이 Copy→Modify 및 색상 Modify, Mesh 정적 업로드·world 버전과 중복 읽기를 연결했다. 두 구성 빌드·native·제품 반복 회귀 통과, 변경 전후/구성 간 각 16개 이미지 오차 0 및 현재 해시 일치를 확인했다. [배선 및 검증](../analysis/RenderRg5SurfaceMigration.md). 제품 59·게이트 217 호출. 실제 versioned SceneHost/mixed GPU 수용·guard 제거와 RG5 전체 기성 0은 유지한다.


2026-10-03 원격 물리·CSM/RG5 통합 후 현행 기준선 갱신 완료: Debug/Release 빌드 및 구성별 독립 2회 × 캡처 2회 회귀 통과, 통합 전후/구성 간 각 16개 이미지 오차 0, 현행 소스·바이너리 SHA-256 및 phaseComplete=true. Release 최초 포트 충돌 실패는 보존하고 v2 정상 반복 실행으로 수용했다. [통합 후 검증 기록](../analysis/RenderRg5IntegrationBaseline.md). 다음은 실제 versioned SceneHost·Code/Graph 혼합 GPU 수용 및 guard 제거이며 RG5 progress/기성 0과 RG6 미전환은 유지한다.


2026-10-03 RG5-9 실제 versioned SceneHost·Forward+ Code/Graph 혼합 GPU 수용 완료: 세 정책 × 24개 장면을 두 구성에서 실행하여 각 72 frames·정책 간 maxError=0·validation=0, cold/완료 geometry 재사용 및 최신 반환 writer를 확인했다. ExplicitVersioned Graph guard는 제거하고 SingleWriter 거부는 유지한다. 두 구성 빌드·기본 제품 반복 회귀·전후/구성 간 각 16개 이미지 오차 0·현행 해시 기준선 통과. [검증 범위 및 기록](../analysis/RenderRg5MixedGpuAcceptance.md). 제품 기본 DeclarationOrder, RG5 전체 progress/기성 0과 전체 SceneRenderer versioned GPU 수용/RG6 미전환은 유지한다. 다음은 나머지 소비자/ReadWrite 및 legacy 추론/adapter 제거다.

2026-10-03 RG5-10 Decal 접근·출력 버전 이관 완료: snapshot Write·입력 Read, GBuffer Modify/ReadWrite와 원본 텍스처 Read를 명시하고 반환 버전을 블랙보드·SceneHost 후속 입력에 연결했다. 두 구성 빌드·각 3정책/342 frames·7개 첨부 정책 간 오차 0·validation 0, 기본 제품 반복 회귀·변경 전후/구성 간 각 16개 이미지 오차 0 및 현행 해시 기준선 통과. [검증 기록](../analysis/RenderRg5DecalMigration.md). 제품 기본 DeclarationOrder, RG5 전체 progress/기성 0 및 전체 제품 versioned GPU 수용/RG6 미전환을 유지한다.

2026-10-03 RG5-11 Sprite 접근·출력 버전 이관 완료: 독립 출력 Write·기존 HDR Modify/ReadWrite, 깊이 Read 및 원본 텍스처 중복 import/Read 제거를 연결했다. 두 구성 최종 빌드·각 3정책/18 frames·전체 RGBA 정책 간 오차 0·validation 0, 기본 제품 반복 회귀·변경 전후/구성 간 각 16개 이미지 오차 0 및 현행 해시 기준선 통과. [검증 기록](../analysis/RenderRg5SpriteMigration.md). 정적 cpp/h 호출은 제품 59·게이트 221(신규 검사 헤더 포함)이며 inl 전체 목록과 구분한다. 제품 기본 DeclarationOrder, RG5 progress/기성 0 및 전체 제품 versioned GPU 수용/RG6 미전환을 유지한다. 다음은 화면 SSS/SSR 소비 체인 이관이다.

2026-10-04 RG5-12 화면 SSS/SSR 접근·출력 이관 완료: 두 블러 축과 반사 pass의 입력 Read·새 출력 Write/v0 및 선언 당시 핸들 캡처를 연결했다. 두 구성 빌드·각 3정책/48 frames·네 단계 전체 RGBA 정책 간 오차 0·validation 0, 실제 블러/반사 기여와 비활성/마스크/입력 누락 경로, 기본 제품 반복 회귀·전후/구성 간 각 16개 이미지 오차 0 및 현행 해시 기준선 통과. [검증 기록](../analysis/RenderRg5ScreenMigration.md). 정적 cpp/h 호출은 제품 59·게이트 224(신규 검사 헤더 포함)이며 inl 전체 목록과 구분한다. 제품 기본 DeclarationOrder, RG5 progress/기성 0과 전체 제품 versioned GPU 수용/RG6 미전환을 유지한다. 다음은 VolumetricFog 소비 체인 이관이다.

## RG5 남은 이관 순서 — 2026-10-04

RG5-12 이후 현재 코드에서 남은 제품·Editor pass는 7개이며, 최종 출력·캡처 배선과 test/fixture 정리가 별도로 남아 있다.

| 순서 | 대상 | 남은 접근·버전 배선 |
|---|---|---|
| 1 | VolumetricFog | Scatter→Accumulate→Composite, history 읽기·쓰기 및 출력 버전 전달 |
| 2 | PostChain | 후처리 단계별 입력 Read·출력 Write, 중간·최종 출력 전달 |
| 3 | UI | 색상 합성 Modify/ReadWrite, 원본 텍스처 Read |
| 4 | Editor Grid·WireFrame | 색상·깊이 접근과 수정된 출력 전달 |
| 5 | Editor GizmoIcon·GizmoLine | 아이콘 텍스처 Read, 색상·깊이 접근과 출력 전달 |
| 6 | 최종 출력·캡처 | live_present와 capture/readback의 최신 출력 버전 소비 |
| 7 | test/fixture·임시 adapter | 남은 명시 접근 이관, 의도적인 legacy 검증과 구분하여 임시 추론·adapter 정리 |

위 배선과 전체 SceneRenderer의 versioned GPU 수용을 확인해야 RG5를 닫는다. RG6의 제품 기본 의존성 정렬 전환은 이후 단계다. 다음 구현은 VolumetricFog이며, 현행 제품 기본 DeclarationOrder와 IBL 1024/4096을 유지한다. MAT-9 SSS·투과 품질/성능 게이트는 별도 미완료 조건이다. Vulkan 비교는 PHASE 4.9 RenderDoc 캡처→리소스 확인→픽셀별 비교에만 둔다.
