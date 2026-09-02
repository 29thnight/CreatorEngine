# PHASE 4 통합 계획 — 차세대 GPU 렌더링

**신설 2026-09-01 · 범위 개정 2026-09-02 · 활성 40행 267.5일 · 잔여 263.5일**

2026-09-02 개정으로 구 `ModelImportPipelinePlan` I/V experiment 배선을 PHASE 4에서
제거했다. 모델 자산 신원·sidecar·loader·renderer/scene/animation 소비·Assimp 은퇴는
[`ModelAssetBigBangCutoverPlan.md`](ModelAssetBigBangCutoverPlan.md), **PHASE 3.75**의
단방향 cutover가 소유한다.

PHASE 4는 PHASE 3.75 완료 뒤 새 모델 자산 계약을 **읽기 전용 선행 입력**으로 받는다.
기존 GUID, legacy 모델 객체, Assimp fallback, experiment on/off를 해석하거나 복구 경로로
다시 만들지 않는다.

---

## 0. 정본 경계

| 범위 | 정본 |
|---|---|
| PHASE 4 트랙 간 순서·선행·우선순위·공수·완료 기준 | 이 문서 |
| `4-0~4-6`, SRP·PBR-S 내부 설계 | `ScriptableRenderPipelinePlan.md` |
| versioned resource·DAG·barrier·aliasing·async compute | `RenderGraphDependencySchedulingPlan.md` |
| 라이트맵 UV1·BVH·직접/간접광·background bake | `LightmapBakerPlan.md` |
| ShaderMeta·material snapshot·Slang compiler 기반 | `MaterialPipelinePlan.md` |
| 모델 자산·vertex schema·typed model/material/texture handle | `ModelAssetBigBangCutoverPlan.md` |
| 대시보드 PHASE 4 | 이 문서의 파생 표시 |

구 [`ModelImportPipelinePlan.md`](ModelImportPipelinePlan.md)는 과거 구현·측정·게이트 실패
기록으로만 보존한다. 그 문서의 I/V 상태와 공수는 이 문서에 합산하지 않는다.

---

## 1. 2026-09-02 범위 개정

### 1.1 제거한 활성 범위

대시보드에서 다음 구 task를 `archive-model-import`로 이동해 표시·진행률·공수 집계에서
제외했다.

- I 트랙: `I0~I8`, `I-fin`.
- V 트랙: `V0~V6`, `V-fin`.
- 활성 행 기준 제거량: **16행, 77일**.
- stopped였던 `V5`, `V6`도 역사 레코드로 이동했다.

이 개정은 77일을 끝낸 것으로 간주한 것이 아니다. PHASE 3.75의 12개 `MBC` 슬라이스,
60일이 기존 진행률을 승계하지 않고 새 정본으로 시작한다.

### 1.2 남은 활성 범위

| 트랙 | 범위 | 목적 |
|---|---|---|
| `BASE-0` | 공통 밀봉 frame 재생·PNG/HDR·CPU/GPU timing·graph stats | 모든 성능/품질 판정의 한 벌짜리 자 |
| `4-0~4-6` | 지원 행렬·기능 계약·의존 그래프·구현 분해 | 네 GPU 기능의 설계 게이트 |
| `RG0~RG9`, `Q0` | versioned resource, stable DAG, 제품 cutover, aliasing, multi-queue | 실행 그래프와 자원 수명 |
| `L0~L7` | UV1, BVH, 직접/간접광, background bake | 화면 밖 간접광과 베이크 품질 |
| `SRP-0~SRP-6` | Blueprint, authored Pass Stack, Slang, Shader Graph | Asset-first Scriptable Pipeline |
| `PBR-S1~PBR-S8` | native Slang PBR, glTF 의미, IBL, shadow, display, lobe | 공용 재질 평가와 품질 |

### 1.3 PHASE 3.75에서 받는 계약

PHASE 4는 다음을 새로 만들지 않고 소비한다.

- `ModelAssetGeneration` typed handle과 generation lifetime.
- UUIDv8 AssetId와 model/mesh/material/texture typed subasset identity.
- vertex attribute mask에서 유도되는 backend-neutral layout schema.
- immutable material/texture snapshot.
- source/cooked 차이를 숨긴 검증 완료 model generation.

PHASE 4 task가 이 계약을 확장해야 하면 먼저 PHASE 3.75 계약 변경으로 되돌려 반영한다.
PHASE 4 내부 adapter로 legacy 객체나 GUID를 다시 들이지 않는다.

---

## 2. 공통 기반 판정

### 2.1 `BASE-0` — 기준선은 한 벌

구 `4-0`, `SRP-G0`, `RG0`, `PBR-S0`가 요구하던 측정 하네스를 하나로 합친다.

`BASE-0` 산출물:

- 밀봉 frame packet·카메라·해상도·tuning.
- DX12/Vulkan 별도 프로세스 재생.
- final PNG, linear-space diff, pre-tone HDR.
- CPU record time, pass별 GPU timing, RenderGraph stats.
- graph dump, resource mutation fixture, validation output.
- 지원 GPU·backend·quality mode·래스터 fallback 행렬.

pass fixture 통과와 전체 live frame 통과는 서로 다른 판정이다. 하나로 다른 하나를 대체하지
않는다. `RG0`은 stopped 포인터로만 남고 0일이다.

### 2.2 `Q0` — queue/fence RHI 계약은 한 벌

`Q0`은 queue-neutral queue/fence, cross-queue resource state transition, completion lifetime을
RHI 계층에 둔다. 소비자는 `RG8` multi-queue scheduling과 `L4` background bake다.

- `RG8`과 `L4` 중 먼저 필요한 쪽의 착수 시점에 세운다.
- 어느 트랙도 별도 queue 계층을 만들지 않는다.
- 현재 0일은 완료가 아니라 **미산정**이다. `RG8` 25일에서 분리할 몫을 착수 정찰에서
  재산정한다.

---

## 3. 실행 순서

```text
선행  PHASE 3.75 MBC0~MBC11 완료
  │
T0  BASE-0
  │
T1  RG1 → RG2 → RG3 → RG4
  │
T2  RG5 → RG6                         제품 graph cutover
  │
T3  RG7 → Q0 → RG8 → RG9             VRAM → multi-queue → inspector

L   L1 ∥ L2 → L3 → Q0 → L4 → L5·L6 → L7
    └──────── Q0 전까지 RG와 병렬 가능 ────────┘

S   SRP-0 → SRP-1 → SRP-2·SRP-3 → SRP-4 → SRP-5 → SRP-6
P   PBR-S1 → S2 → S3 → S4·S5 → S6 → S7 → S8

G   4-2~4-5 설계 → 4-6 의존 그래프·최소 수직 슬라이스·구현 페이즈 확정
```

순서 원칙:

1. 측정 자 `BASE-0`을 구현보다 먼저 둔다.
2. RenderGraph는 single-queue correctness와 제품 cutover를 닫은 뒤에만 aliasing과
   multi-queue를 연다.
3. 라이트맵은 `L1` UV1과 `L2` BVH를 병렬로 열 수 있다. `L4`만 `Q0`을 기다린다.
4. SRP authored pipeline과 PBR 구현은 PHASE 3.75 typed asset 계약과 RG 제품 계약을
   우회하지 않는다.
5. `4-2~4-5`는 구상 task다. 구현 공수는 `4-6`이 분해한 후에만 추가한다.

---

## 4. 활성 슬라이스

`✅` 완료, `·` 미착수, `⊘` 흡수/stopped, `차단` 선행 대기. 공수는 1인 개발일이다.

### 4.1 공통·설계 게이트 — 14일

| ID | 내용 | 상태 | 선행 | 일 |
|---|---|---|---|---:|
| `BASE-0` | 통합 기준선 하네스 | · | PHASE 3.75 | 6 |
| `4-1` | Scriptable Render Pipeline·Custom Pass 확장 계약 | ✅ | — | 2 |
| `4-2` | GPU-driven rendering 아키텍처 구상 | · | BASE-0 | 2 |
| `4-3` | Stochastic Tile-Based Lighting 구상 | · | BASE-0 | 1.5 |
| `4-4` | DXR 구상 | · | BASE-0 | 1.5 |
| `4-5` | DLSS 구상 | · | BASE-0 | 1 |
| `4-6` | 통합 의존 그래프·수직 슬라이스·구현 페이즈 확정 | · | 4-2~4-5 | 0.5 |

### 4.2 RenderGraph·queue — 113일 + `Q0` 미산정

| ID | 내용 | 상태 | 선행 | 일 |
|---|---|---|---|---:|
| `RG0` | `BASE-0`에 흡수된 포인터 | ⊘ | — | 0 |
| `RG1` | 명시적 Read/Write/Modify·versioned resource API | · | BASE-0 | 8 |
| `RG2` | stable single-queue DAG compiler | · | RG1 | 10 |
| `RG3` | DAG 기준 culling·lifetime·barrier 재계산 | · | RG2 | 8 |
| `RG4` | dependency wave 병렬 기록·진단 | · | RG3 | 7 |
| `RG5` | 제품 Pass·Pipeline compiler 이관 | · | RG4 | 12 |
| `RG6` | DX12/Vulkan 제품 cutover | · | RG5, BASE-0 | 8 |
| `RG7` | transient buffer·in-frame aliasing | · | RG6 | 20 |
| `Q0` | queue/fence RHI 계약 | · | RG6 | 미산정 |
| `RG8` | multi-queue·async compute | · | RG7, Q0 | 25 |
| `RG9` | subresource·split barrier·Resource Inspector | · | RG8 | 15 |

### 4.3 라이트맵 — 35일

| ID | 내용 | 상태 | 선행 | 일 |
|---|---|---|---|---:|
| `L0` | 삭제된 베이커 실측·정답지 | ✅ | — | 2 |
| `L1` | xatlas UV1 언랩 | · | PHASE 3.75 vertex schema | 5 |
| `L2` | SAH BVH·`nth_element`·leaf off-by-one 수정 | · | 없음 | 4 |
| `L3` | 직접광·DX12 RHI·rect-size dispatch | · | L1, L2 | 6 |
| `L4` | background bake·GPU time slice·snapshot/invalidation | · | L3, Q0 | 7 |
| `L5` | progressive 간접광 | · | L4 | 5 |
| `L6` | dilate·seam·padding 원인 수정 | · | L3 | 3 |
| `L7` | 병렬화 실측 판정 | 차단 | L5, L6 | 3 |

### 4.4 SRP — 43일

| ID | 내용 | 상태 | 선행 | 일 |
|---|---|---|---|---:|
| `SRP-0` | Blueprint schema·순수 검증기 | · | BASE-0 | 4 |
| `SRP-1` | 19 node authored Pass Stack·픽셀 동등 컴파일 | · | RG5, SRP-0 | 8 |
| `SRP-2` | Slang Code 모드·Fullscreen Custom Pass | · | SRP-1 | 7 |
| `SRP-3` | 최소 Visual Shader Graph | · | SRP-1 | 10 |
| `SRP-4` | Compute·RendererList·history | · | SRP-2/3 | 5 |
| `SRP-5` | variant·hot reload·preview·선택적 C# 값 | · | SRP-4, PHASE 3.75 typed CLR handle | 6 |
| `SRP-6` | 소스 Native Pass 연결 | · | SRP-5 | 3 |

### 4.5 PBR-S — 62일

| ID | 내용 | 상태 | 선행 | 일 |
|---|---|---|---|---:|
| `PBR-S1` | native Slang 기반·시각 변화 0 | · | BASE-0 | 4 |
| `PBR-S2` | `MaterialInputs→StandardSurface` 공용 모듈 동등 이관 | · | S1 | 5 |
| `PBR-S3` | glTF 의미 교정 9종 | · | S2, PHASE 3.75 typed assets | 9 |
| `PBR-S4` | 에너지·IBL·local reflection probe | · | S3 | 8 |
| `PBR-S5` | 그림자·point/spot atlas | · | S3 | 10 |
| `PBR-S6` | AgX·auto exposure·bloom | · | S4/S5 | 6 |
| `PBR-S7` | 확장 lobe 6종 | · | S6 | 15 |
| `PBR-S8` | Shader Graph→authored Slang module codegen | · | S7, SRP-3 | 5 |

---

## 5. 공수 합계

대시보드 집계 규칙은 `status=stopped` 행을 활성 합계에서 제외하고, 완료 행의 `days`를
완료 공수로 센다.

| 묶음 | 활성 행 | 총일 | 완료 | 잔여 |
|---|---:|---:|---:|---:|
| 공통·설계 게이트 | 7 | 14.5 | 2 | 12.5 |
| RenderGraph·Q0 | 10 | 113 | 0 | 113 |
| 라이트맵 | 8 | 35 | 2 | 33 |
| SRP | 7 | 43 | 0 | 43 |
| PBR-S | 8 | 62 | 0 | 62 |
| **합계** | **40** | **267.5** | **4** | **263.5** |

`Q0`은 0일로 표시돼 있지만 미산정이다. 또한 `4-2~4-5`의 task 일수는 구상만 포함하고
실제 GPU 기능 구현은 포함하지 않는다. 따라서 현재 말할 수 있는 일정은
**잔여 263.5일 + Q0 재산정 + 4-2~4-5 구현 미지수**다.

2026-09-01의 56행 344.5일 수치는 폐기한다. 차이는 구 I/V 활성 16행·77일이 PHASE 3.75로
분리된 결과이며, 그 작업을 완료 처리하거나 77일을 60일로 단순 축소한 것이 아니다.

---

## 6. 페이즈 완료 기준

### 6.1 구조

- [ ] PHASE 3.75의 UUIDv8/model generation/typed subasset handle만 모델 입력으로 사용한다.
- [ ] PHASE 4에서 legacy GUID, Assimp, legacy `Model`/`Material`, experiment fallback을
      참조하는 제품 호출이 0건이다.
- [ ] `BASE-0` 하네스가 한 벌이고 `4-0`, `RG0`, `SRP-G0`, `PBR-S0` 요구를 소비한다.
- [ ] `Q0` queue/fence 계약이 한 벌이고 `RG8`, `L4`가 소비한다.
- [ ] `RG6` 뒤 declaration-order 제품 경로·임시 adapter 0.
- [ ] `RG6` 전에 aliasing·multi-queue 제품 배선 0.

### 6.2 품질·검증

- [ ] 같은 밀봉 frame의 DX12/Vulkan final PNG·linear diff·validation이 허용 범위를 통과.
- [ ] pass fixture와 전체 live frame을 별도 판정.
- [ ] `RG7` aliasing off/on 픽셀 동일과 peak committed/resident byte 감소 실측.
- [ ] `RG8` single/multi queue 픽셀 동일과 GPU overlap 이득 실측. 이득이 없으면 기각 기록.
- [ ] BVH leaf off-by-one이 양쪽 kernel에서 닫히고 mutation으로 검증.
- [ ] bake 중 scene 편집·저장·취소가 가능하고 편집 시 snapshot 무효화·재시작.
- [ ] 단일 bake dispatch가 TDR 안전 예산 아래.

### 6.3 SRP·PBR

- [ ] Pipeline Asset authored Pass Stack과 versioned resource edge가 불변 native pipeline으로
      컴파일되고 독립 Pass만 authored index를 stable tie-break로 사용.
- [ ] Visual `.shadergraph` round-trip과 authored `.slang` code mode가 공통 `.shadermeta`를
      소비하며 generated Slang은 read-only.
- [ ] PBR native Slang 공용 평가를 Deferred·Forward가 공유하고 각 의미 변경에 독립 golden.
- [ ] C#은 stable handle의 Game-thread 값 제어만 사용하며 render/RHI thread CLR 호출 0.

### 6.4 설계 게이트

- [ ] GPU-driven, Stochastic Tile-Based Lighting, DXR, DLSS의 지원 GPU/backend/quality/fallback
      행렬 확정.
- [ ] RenderScene/View → GPU visibility → tiled lighting → DXR → upscale/present 데이터와
      history ownership 문서화.
- [ ] `4-6`이 네 기능의 최소 수직 슬라이스, 의존 그래프, 구현 페이즈, 공수를 확정.

---

## 7. 범위 원칙

- `4-0~4-6`에서 SDK 연동이나 대규모 feature pass를 먼저 만들지 않는다.
- 새로운 모델 importer/sidecar/GUID migration은 PHASE 4 범위가 아니다.
- PHASE 3.75 계약의 빈 부분을 legacy adapter로 메우지 않는다. 필요한 계약은 3.75 계획에
  반영한 뒤 선행 gate로 닫는다.
- 성능 기능은 측정 자와 correctness gate 없이 켜지 않는다.
- backend A/B는 같은 새 frame contract의 DX12/Vulkan 비교다. 모델 legacy/new live A/B와는
  다르며 후자는 제품에 존재하지 않는다.

---

## 8. 다른 계획과의 관계

| 계획 | 관계 |
|---|---|
| `ModelAssetBigBangCutoverPlan` | **하드 선행 PHASE 3.75.** model generation·vertex schema·typed material/texture handle 제공 |
| `MaterialPipelinePlan` | ShaderMeta·Slang·snapshot 기반 제공. 모델 embedded material identity는 3.75 소유 |
| `RenderGraphDependencySchedulingPlan` | RG1~RG9 내부 설계 정본 |
| `LightmapBakerPlan` | L1~L7 내부 설계 정본. L1은 3.75 vertex schema, L4는 Q0 소비 |
| `ScriptableRenderPipelinePlan` | 4-0~4-6, SRP, PBR-S 내부 설계 정본 |
| `BuildPipelinePlan` | Slang/module·cooked shader·player package 재현성 |
| `SerializationPlan` | 일반 authoring/cooked archive 계약. model identity epoch/writer는 3.75가 소유 |

---

## 9. 변경 이력

| 날짜 | 변경 |
|---|---|
| 2026-09-02 | 구 ModelImport I/V experiment 배선을 PHASE 4에서 제거하고 PHASE 3.75로 분리. 활성 56행 344.5일 → 40행 267.5일, 잔여 263.5일. 실행 순서·완료 기준·교차 계획 의존을 RG/L/SRP/PBR 중심으로 재작성 |
| 2026-09-01 | 최초 통합. `BASE-0`, `Q0`, RG/L/SRP/PBR 공수와 순서 정리 |
