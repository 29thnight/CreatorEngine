# RenderGraph 리소스 의존성 스케줄링 계획 (PHASE 4 · 트랙 RG)

2026-08-28 작성. `EnhancedRenderGraph`를 교체하지 않고, 명시적 리소스 접근과
버전 계보로 실행 순서를 컴파일하는 그래프로 단계적으로 확장하는 구현 계획이다.

상태: **계획 확정, 구현 미착수.** 문서 작성과 정적 검증은 구현·빌드·픽셀 검증 완료를
뜻하지 않는다.

---

## 0. 결정 요약

1. **실행 그래프는 `EnhancedRenderGraph` 하나만 유지한다.** 별도 FrameGraph나 제품용
   이중 실행 경로를 만들지 않고 현재 Compile/Execute 경계를 확장한다.
2. **Pipeline Asset의 Pass Stack은 저작·직렬화 순서다.** 실제 실행 순서는
   `read`/`write`/`modify`로 만든 리소스 버전 의존성에서 계산하고, 서로 독립인 Pass만
   저작 순서를 안정 tie-break로 사용한다.
3. **접근 의미와 상태를 분리한다.** `Read`, `Write`, `ReadWrite`가 의존성을 만들고
   `RHIResourceState`는 배리어 계획만 담당한다. `write`와 `modify`는 새 버전 핸들을
   반환하며 소비자는 원하는 버전을 명시한다.
4. **단일 그래픽 큐를 먼저 완결한다.** 버전 모델 → stable DAG → 기존 파이프라인 이관 →
   DX12/Vulkan live 픽셀 게이트를 닫기 전에는 aliasing과 async compute를 열지 않는다.
5. **그 뒤에 메모리와 큐 최적화를 연다.** transient buffer/aliasing을 먼저, 실제 GPU
   겹침 이득을 계측한 뒤 multi-queue/async compute를 도입한다.
6. **빅뱅 전환을 금지한다.** 각 슬라이스는 독립 검사와 A/B 스위치를 가지며, 다음
   슬라이스는 직전 acceptance gate가 통과한 뒤 시작한다.

이 순서는 현재 코드가 위상 정렬을 포기한 이유를 무시하지 않는다. 현재의 unversioned
handle과 state 기반 쓰기 추론만으로는 두 writer의 선후를 알 수 없다. 먼저 접근 모드와
버전 계보를 도입해 그 모호성을 제거한 뒤에만 의존성 스케줄러를 켠다.

---

## 1. 현재 코드 기준선

2026-08-28 워킹트리 정적 감사 기준이다.

| 영역 | 현재 보유 | 자동 의존성 실행까지 남은 공백 |
|---|---|---|
| 그래프 생명주기 | `Compile`/`Execute`, pass culling, transient texture pool, lifetime, Transition/UAV 배리어, 병렬 기록 | compile 결과가 리소스 DAG가 아니라 선언 순서 |
| 실행 순서 | `BuildOrder`가 read-before-write를 검사한 뒤 선언 index를 그대로 `m_executeOrder`에 적재 | edge table, stable topological sort, cycle/모호한 writer 진단 없음 |
| 리소스 핸들 | `RGHandle { index }`, texture/buffer import, transient texture 생성 | 논리 resource ID와 version 분리 없음, transient buffer 생성 없음 |
| Pass 사용 | `RGPassUsage { handle, RHIResourceState }` | `IsWriteState`로 쓰기를 추론하므로 read와 read-modify-write를 구분할 수 없음 |
| 서브리소스 | 텍스처 전체 단위 상태와 수명 | mip/array/range별 접근·배리어·alias 판단 없음 |
| 큐 | DX12 `DIRECT` 큐 하나, Vulkan graphics family/queue 하나 | compute queue, queue ownership transfer, cross-queue fence 없음 |
| 제품 표면 | 기본 live pipeline 15개 노드 + Editor 기여 4개, `AddPass`/`AddSplitPass` 정적 호출 108곳(제품 28, test/fixture 80) | 모든 생산 Pass의 접근 선언 이관과 양 backend cutover 필요 |

근거 위치:

- `Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h:40`, `:50`, `:286`, `:296`
- `Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp:12`, `:131`, `:192`, `:497`
- `Engine/RenderEngine/Render/Scene/EnhancedSceneRendererLive.cpp:1685`
- `Editor/EngineEntry/EditorSceneOverlayContributor.cpp:82`
- `Engine/RenderEngine/RHI/DX12/DX12DeviceResources.cpp:295`
- `Engine/RenderEngine/RHI/Vulkan/VulkanDeviceResources.cpp:390`, `:451`, `:521`

호출 수는 구현 착수 직전 다시 센다. 이 표의 수치는 일정 산정용 기준선이며 완료 판정
자체가 아니다.

---

## 2. 목표 계약

### 2.1 리소스와 버전

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

Pipeline Asset Inspector는 사용자가 이해하고 diff할 수 있는 Pass Stack을 계속 저장한다.
Compiler가 각 슬롯의 버전 핸들을 연결한 뒤에는 다음처럼 해석한다.

- 의존성이 있는 Pass: 리소스 edge가 실행 선후를 결정한다.
- 독립 Pass: Stack 순서가 deterministic tie-break다.
- 화면 표시와 serialization: Stack 순서를 유지한다.
- 실행 미리보기: compiled order와 원래 authored index를 함께 표시한다.

---

## 3. 구현 순서와 공수

1인 전담 엔지니어 기준 개발일이다. 병렬 인원 투입 시에도 RG1→RG6 임계 경로는 줄지
않으며, backend 안정화와 픽셀 판정 시간은 별도로 필요하다.

| ID | 슬라이스 | 선행 | 공수 | 종료 게이트 |
|---|---|---:|---:|---|
| ~~**RG0**~~ | **`BASE-0`에 흡수 (2026-09-01)** — 4-0·SRP-G0·PBR-S0와 같은 하네스·같은 artifact였다. graph dump와 변이 fixture는 `BASE-0`의 소비 항목으로 남는다 | 없음 | (BASE-0 6일에 포함) | [`Phase4UnifiedPlan.md`](Phase4UnifiedPlan.md) §1.1 C1 |
| **RG1** | 명시적 access mode + versioned texture/buffer handle | RG0 | 8일 | `Read/Write/Modify` 단위 검사, import/transient version dump, forked write와 stale handle fail-closed |
| **RG2** | stable single-queue DAG compiler | RG1 | 10일 | RAW/WAR/WAW, 독립 Pass tie-break, cycle chain, 선언 배열 shuffle fixture가 결정적 compiled order를 생성 |
| **RG3** | DAG 기준 culling·lifetime·barrier 재계산 | RG2 | 8일 | 죽은 producer 제거, 마지막 소비 수명, Transition/UAV 계획이 sorted order 기준으로 일치 |
| **RG4** | dependency wave 기반 병렬 recording·진단 | RG3 | 7일 | sequential/parallel compiled order와 픽셀 동일, wave·critical path·edge 원인 dump 제공 |
| **RG5** | 제품 Pass와 Pipeline Asset compiler 이관 | RG4 | 12일 | 기본 19개 node, 제품 호출 28곳과 test/fixture 80곳의 접근 선언 이관; 임시 adapter 잔여 0 |
| **RG6** | DX12/Vulkan 제품 cutover | RG5, **BASE-0** | 8일 | 같은 밀봉 입력의 별도 프로세스 live frame, PNG/차영상/선형 오차, CPU record·pass GPU·graph stats, validation 0 |
| **RG7** | transient buffer + in-frame aliasing | RG6 | 20일 | alias off/on 픽셀 동일, peak committed/resident byte 감소 실측, poison/overlap/lifetime 변이 통과 |
| **RG8** | queue-neutral multi-queue + async compute | RG7 | 25일 | single-queue fallback, cross-queue fence/ownership, DX12/Vulkan validation, 겹침 GPU 이득 실측 |
| **RG9** | subresource·split barrier·Resource Inspector 성숙 | RG8 | 15일 | mip/array/range 추적, split barrier parity, producer/consumer/version/order/lifetime/alias/queue 시각화 |

> **2026-09-01 정정** — `RG0` 4일은 `BASE-0`으로, `RG8`의 큐/펜스 RHI 계약 몫은 `Q0`으로 빠져나갔다.
> 아래 합계는 정정 전 수다. 통합 합계는 [`Phase4UnifiedPlan.md`](Phase4UnifiedPlan.md) §5가 정본이다.

- **RG0~RG6: 57일, 약 11.4 엔지니어 주.** Unreal RDG형 단일 큐 리소스 의존성
  스케줄링과 제품 전환의 첫 완료선이다.
- **RG7~RG9: 60일.** 메모리 aliasing, async compute, subresource/관측 성숙도다.
- **전체: 117일, 약 23.4 엔지니어 주.** 안정화·리뷰·플랫폼 편차를 포함한 달력 일정은
  1인 기준 약 6~9개월로 본다.

RG7 이후는 최적화 트랙이다. RG6을 통과하면 리소스 의존성으로 실행 순서를 결정하는
제품 RenderGraph는 이미 성립하며, 뒤 단계가 늦어져도 declaration-order로 되돌리지 않는다.

---

## 4. 슬라이스별 구현 경계

### RG0 — 기준선을 먼저 잠근다

- 현행 `BuildOrder` 계약과 제품 graph dump를 artifact로 남긴다.
- 독립 Pass, 연쇄 RAW, 두 writer, read-modify-write, culled branch, imported history fixture를
  DX12/Vulkan 공용 테스트로 만든다.
- UI/Grid처럼 같은 target을 읽고 다시 쓰는 Pass를 찾아 state만으로 `modify`가 표현되지
  않는 사례를 고정한다.
- 테스트가 잘못된 구현을 잡는지 producer edge 삭제, version 재사용, 순서 뒤집기 변이로
  확인한다.

### RG1 — 모호성을 API에서 제거한다

- 기존 `{handle, state}` aggregate 초기화는 임시 adapter에서만 받고 제품 코드는 명시적
  builder API로 옮긴다.
- `Write`/`Modify`가 반환한 새 handle을 다음 consumer가 받게 해 버전 계보를 호출부에
  보이게 한다.
- texture import/create와 buffer import/create의 대칭을 닫는다.
- 컴파일 결과에 resource ID, version, producer, consumer, access, required state를 기록한다.

### RG2 — 정답이 생긴 뒤 정렬한다

- edge table과 indegree를 compact arena/vector로 만들고 hot frame heap churn을 계측한다.
- ready queue는 authored index 오름차순으로 고정한다.
- side-effect-only Pass는 명시적 ordering token이나 root edge를 사용한다.
- debug에서는 전체 cycle 사슬, release에서는 짧은 오류와 stable ID를 남긴다.

### RG3~RG4 — 기존 기능을 새 순서에 다시 연결한다

- culling은 "앞서 쓴 모든 Pass" 검색이 아니라 version producer edge를 역추적한다.
- lifetime과 transient 회수는 declaration index가 아니라 compiled index를 사용한다.
- barrier state machine도 compiled order에서 계산하고 access mode와 state 불일치를 거부한다.
- 병렬 기록은 DAG의 ready wave를 사용하되 GPU submit은 compiled order와 backend 계약을
  보존한다. wave 수보다 record cost가 우선인 기존 split-pass 휴리스틱은 유지한다.

### RG5~RG6 — 제품을 한 번만 전환한다

> **`SRP-1`과의 병합 — 기각 확정 (2026-09-01).** `Phase4UnifiedPlan` 백로그 산정 중
> "RG5와 SRP-1이 같은 500줄을 만지니 병합하자"는 제안이 나왔다가 **전수 실측으로
> 기각**됐다. 두 계획서가 같은 명사("19개 노드")를 쓰지만 대상 심볼이 다르다 —
> RG5는 `AddPass`/`AddSplitPass`(총 120 · **제품 38** · 게이트 82)로 **Pass 구현 파일
> 전역에 흩어져** 있고, SRP-1은 `AddNode`(총 33 · 제품 33)로 **조립부에 모여** 있다.
> 교집합 파일은 `EnhancedSceneRenderer.cpp` 하나뿐이고 그 안에서도 다른 줄이다.
> 축도 다르다(접근 **선언** vs 노드 **조립**) — 합치면 픽셀이 붉을 때 어느 축인지
> 못 가린다. `RG5 → SRP-1` **순서 의존만 유지**하고 공수는 12일·8일 각각 둔다.
> 판정 전문: [`Phase4UnifiedPlan.md`](Phase4UnifiedPlan.md) §8.4.
>
> **덤 — 호출 수가 늘었다.** §3 표와 아래 목록의 근거인 "제품 28곳 · test/fixture
> 80곳(총 108)"이 2026-09-01 실측으로 **제품 38 · 게이트 82(총 120)**다. 제품만 **+36%**.
> 이 절이 "호출 수는 착수 직전 다시 센다"고 적어 둔 그대로이며, 12일 재산정은
> RG5 착수 시점의 일로 남긴다.

- base 15개 + Editor 4개 node를 작은 묶음으로 이관하고 각 묶음마다 현행/A-B 픽셀을
  비교한다.
- migration adapter와 legacy order switch는 테스트 rollback용으로만 유지하고 RG6 종료 시
  제품 기본 경로에서 제거한다.
- pass fixture와 전체 live frame을 별도 판정한다. 하나의 통과로 다른 하나를 대신하지
  않는다.
- DX12/Vulkan은 동일 compiled graph stable ID와 dependency hash를 내야 한다.

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
| 배리어 | DX12 debug layer error 0, Vulkan validation error 0, 요구/실제 state mismatch 0 |
| 픽셀 | pass fixture와 전체 live frame을 분리해 DX12/Vulkan PNG·linear RMSE·max error·changed pixel 판정 |
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

## 7. 다른 PHASE 4 트랙과의 의존성

| 계획/트랙 | 관계 |
|---|---|
| `ScriptableRenderPipelinePlan.md` | Pipeline Asset의 `read/write/modify`를 RG1 버전 API로 낮춘다. authored Pass Stack은 정본·tie-break이고 compiled DAG가 실행 순서다 |
| SRP-G0 | RG0 기준선과 RG6 전체 live backend artifact를 공유한다. 별도 캡처 체계를 만들지 않는다 |
| `LivePipelineDescPlan.md` | 현재 nodes/reads/writes/modifies를 RG5의 첫 native compiler 입력으로 사용한다 |
| `RhiBoundaryPlan.md` | RG7 heap/alias 계약과 RG8 queue/fence 계약을 backend-neutral RHI에만 추가한다 |
| 트랙 V4 | Raster Pass의 input layout 유도 계약. RG5의 Asset-first 제품 이관 전에 필요하다 |
| 트랙 L4 | **2026-09-01 정정** — `RG8`이 아니라 **`Q0`**(queue/fence RHI 계약)의 소비자다. "먼저 구현하는 트랙이 소유"는 순서를 정하지 않는 문장이었고, 아래 임계 경로의 "L4는 RG8을 기다린다"는 P0인 L4를 P1 뒤 142일째에 묶었다. 소유를 트랙에서 떼어 RHI 계층(`Q0`)에 두면 협상이 사라진다 — [`Phase4UnifiedPlan.md`](Phase4UnifiedPlan.md) §1.2 C2 |
| GPU-driven/DXR/DLSS/Stochastic Lighting | RG6 단일 큐 제품 cutover 뒤 새 resource/pass를 추가하고, RG7~RG9 기능을 필요에 따라 소비한다 |

권장 임계 경로는 다음으로 고정한다.

```text
RG0 → RG1 → RG2 → RG3 → RG4 → RG5 → RG6
                                      ↓
                                    RG7 → RG8 → RG9
```

**2026-09-01 정정.** `V4`는 `I5-D2`/`I5-D34`로 이행 완료됐고(독립 슬라이스 아님), `SRP-G0`는
`BASE-0`으로 흡수돼 이 트랙보다 앞에 선다. **L4의 async compute는 `RG8`이 아니라 `Q0`을
기다린다** — `Q0`은 `RG8`·`L4` 중 먼저 필요해지는 쪽의 착수 시점에 세운다. 그 전의
UV/BVH/직접광 준비는 독립적으로 진행할 수 있다. 통합 순서는
[`Phase4UnifiedPlan.md`](Phase4UnifiedPlan.md) §3이 정본이다.
