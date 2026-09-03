# PHASE 4 계열 통합 계획 — PBR 안정화에서 차세대 렌더링까지

**신설 2026-09-01 · 재분할 2026-09-03 · 활성 54행 267.5일 · 완료 4일 + 진행 기성 1.5일 · 잔여 262일**

기존 단일 PHASE 4에는 현재 PBR 배선 수정, Blender형 Material Graph, RenderGraph·라이트맵·
일반 SRP·후처리·차세대 GPU 기능이 한데 섞여 있었다. 이 문서는 같은 총공수 267.5일을
다음 세 완료선으로 분리한다.

| 페이즈 | 단일 책임 | 활성 행 | 일 | 상태 |
|---|---|---:|---:|---|
| **4** | 현 제품 PBR `.slang`·Material·Renderer 배선 안정화 | 10 | 18 | 진행 기성 1.5일 |
| **4.25** | Blender 5.1.1 Principled 기반 Material Graph와 artist workflow | 10 | 34 | 미착수 |
| **4.75** | RenderGraph·라이트맵·일반 SRP·shadow/probe/post·GPU 기능 | 34 | 215.5 | 4일 완료 |
| **합계** |  | **54** | **267.5** | **잔여 262** |

모델 자산 신원·sidecar·loader·renderer/scene/animation 직접 소비·Assimp 은퇴는
[`ModelAssetBigBangCutoverPlan.md`](ModelAssetBigBangCutoverPlan.md), **PHASE 3.75**가
소유한다. PHASE 4 계열은 UUIDv8 typed model/material/texture generation을 읽기 전용 입력으로
받으며 legacy GUID, Assimp fallback, experiment on/off를 다시 만들지 않는다.

---

## 0. 정본 경계

| 범위 | 정본 |
|---|---|
| PHASE 4/4.25/4.75 순서·공수·완료선 | 이 문서 |
| 현재 PBR Slang/Material/Renderer 결함과 수정 순서 | [`PBRWiringStabilizationPlan.md`](PBRWiringStabilizationPlan.md) |
| Blender Principled·Material Graph·성능 tier·artist UX | [`BlenderMaterialGraphPlan.md`](BlenderMaterialGraphPlan.md) |
| Pipeline Asset·일반 Pass Shader Graph·Custom Pass | [`ScriptableRenderPipelinePlan.md`](ScriptableRenderPipelinePlan.md) |
| versioned resource·DAG·barrier·aliasing·async compute | [`RenderGraphDependencySchedulingPlan.md`](RenderGraphDependencySchedulingPlan.md) |
| 라이트맵 UV1·BVH·직접/간접광·background bake | [`LightmapBakerPlan.md`](LightmapBakerPlan.md) |
| ShaderMeta·material snapshot·Slang compiler 기반 완료 기록 | [`MaterialPipelinePlan.md`](MaterialPipelinePlan.md) |
| 모델 자산·vertex schema·typed subasset generation | [`ModelAssetBigBangCutoverPlan.md`](ModelAssetBigBangCutoverPlan.md) |
| 대시보드 표시 | 이 문서의 파생 |

구 단일 PBR 직선 레인과 그 ID는 폐기한다. 해당 구조는 배선 결함, 재질 모델, 렌더러 기능과 후처리를 한
직선에 놓아 범위와 완료 판정을 섞었다. 새 정본은 `PBR-W*`, `MAT-*`, `RND-*`다.

---

## 1. 범위 분리 결정

### 1.1 PHASE 4 — 현재 제품 배선을 먼저 정상화

현재 자산을 정확히 읽고 같은 frame에서 흔들리지 않게 그리는 책임만 갖는다.

- GBuffer/Deferred/Forward native Slang 제품 진입점과 공용 현행 평가.
- backend neutral 기본 texture/binding과 실패 종료 코드.
- alpha mode/cutoff/double-sided, AO, emissive, UV/sampler/mip, normal/tangent transform.
- material/texture/sampler/descriptor/PSO generation 원자 밀봉과 플리커 차단.
- Gunner/primitive DX12/Vulkan 실장면 회귀.

Blender Principled lobe나 Material Graph를 결함 수정에 섞지 않는다. `PBR-W1` normal-map
snapshot 배선은 코드와 Debug x64 빌드까지 진행됐으나 런타임 acceptance 전이라 완료가 아니다.

### 1.2 PHASE 4.25 — Blender형 재질 저작과 결과 계약

Blender 5.1.1 Material Preview/EEVEE의 Principled 재질 의미와 pre-tone linear HDR 응답을
목표로 한다.

- `PrincipledSurface` + `MaterialFeatureMask` 공용 Slang ABI.
- `.shadergraph(domain=material)` typed Graph IR, round-trip, deterministic codegen.
- core/layered/special lobe와 Standard/Layered/Special 자동 route.
- cook-time constant folding, dead-lobe 제거, coarse permutation/specialization.
- artist preview와 green/yellow/red 비용 badge.

좌표계, 그림자, AO, reflection probe, AgX/auto exposure/bloom은 Blender material parity에서
제외한다. 이 항목으로 재질 오차를 덮지 않는다.

### 1.3 PHASE 4.75 — 남은 renderer/SRP/GPU 기능

기존 PHASE 4에서 위 두 범위가 아닌 항목을 모두 이곳으로 이동한다.

- `BASE-0`, GPU-driven/Stochastic Lighting/DXR/DLSS 설계 게이트.
- RenderGraph RG/Q와 라이트맵 L.
- Pipeline Asset, general Custom Pass, `.shadergraph(domain=pass)`, Slang Code mode.
- local reflection probe/specular AO, shadow atlas, display/post.

PHASE 4.75의 generic Pass graph는 PHASE 4.25의 graph editor/typed IR 기반을 재사용하되
Material output/Principled 의미를 소유하지 않는다. 반대로 PHASE 4.25는 Pass topology,
RenderGraph resource lifetime이나 post stack을 소유하지 않는다.

---

## 2. PHASE 3.75에서 받는 계약

- `ModelAssetGeneration` typed handle과 generation lifetime.
- `ce.uuidv8.sha256.v1` AssetId와 model/mesh/material/texture typed identity.
- vertex attribute mask에서 유도되는 backend-neutral layout schema.
- immutable material/texture snapshot.
- source/cooked 차이를 숨긴 검증 완료 model generation.

필요한 입력이 부족하면 먼저 PHASE 3.75 계약을 수정한다. PHASE 4 계열 내부 adapter로 legacy
객체나 GUID를 되살리지 않는다.

---

## 3. 전체 실행 순서

```text
PHASE 3.75 MBC11
    ↓
PHASE 4     PBR-W0 → W2/W3 → W4~W7 → W8 → W9
                 └─ W1 normal-map 배선 진행분은 W9에서 최종 판정
    ↓
PHASE 4.25  MAT-0 → MAT-1 → MAT-2/MAT-3 → MAT-4~MAT-6
                                   → MAT-7 → MAT-8 → MAT-9
    ↓
PHASE 4.75  BASE-0
              ├─ RG1 → RG2 → RG3 → RG4 → RG5 → RG6 → RG7 → Q0 → RG8 → RG9
              ├─ L1 ∥ L2 → L3 → Q0 → L4 → L5/L6 → L7
              ├─ SRP-0 → SRP-1 → SRP-2 → SRP-4 → SRP-5 → SRP-6
              ├─ RND-1 → RND-2/RND-3
              └─ 4-2~4-5 → 4-6
```

PHASE 4.25는 PHASE 4 실장면 gate를 통과한 제품 배선 위에서만 시작한다. PHASE 4.75는
PHASE 4.25의 compiled material generation/feature mask/route를 입력으로 받는다. 다만
PHASE 4.75의 RG·L 내부 사전 연구는 제품 배선 없이 진행할 수 있어도 완료/cutover는 앞선
완료선을 우회할 수 없다.

---

## 4. PHASE 4 — PBR 배선 안정화, 18일

`◐`는 부분 진행이며 완료 공수는 아니다. 대시보드는 확인 가능한 기성만 별도 반영한다.

| ID | 내용 | 상태 | 선행 | 일 |
|---|---|---|---|---:|
| `PBR-W0` | 감사 정본·Gunner/primitive capture·strict gate | ◐ | PHASE 3.75 | 2 |
| `PBR-W1` | normal-map 저작 유무 snapshot 단일화 | ◐ | — | 1 |
| `PBR-W2` | native Slang 제품 진입점·공용 현행 평가 | · | W0 | 2.5 |
| `PBR-W3` | backend neutral resource/binding/exit code | · | W0 | 1 |
| `PBR-W4` | alpha mode/cutoff·double-sided/cull | · | W2 | 2 |
| `PBR-W5` | AO·texture table·GBuffer packing | · | W2, W3 | 2.5 |
| `PBR-W6` | emissive factor/strength·constant·색공간 | · | W2 | 1.5 |
| `PBR-W7` | UV/sampler/mip·normal/tangent transform | · | W2 | 2 |
| `PBR-W8` | generation 원자 밀봉·플리커 fail-closed | · | W3~W7 | 2 |
| `PBR-W9` | DX12/Vulkan 실장면·장시간·재임포트 cutover | · | W8 | 1.5 |

완료선은 [`PBRWiringStabilizationPlan.md`](PBRWiringStabilizationPlan.md) §4를 따른다.

---

## 5. PHASE 4.25 — Blender형 PBR·Material Graph, 34일

| ID | 내용 | 상태 | 선행 | 일 |
|---|---|---|---|---:|
| `MAT-0` | Blender 5.1.1 reference·pre-tone HDR golden | · | PBR-W9 | 2 |
| `MAT-1` | `PrincipledSurface`·`MaterialFeatureMask` ABI | · | MAT-0 | 4 |
| `MAT-2` | typed Material Graph IR·round-trip | · | MAT-1 | 4 |
| `MAT-3` | core Principled 의미·기본값 | · | MAT-1 | 4 |
| `MAT-4` | layered lobe | · | MAT-3 | 4 |
| `MAT-5` | transmission/subsurface/volume | · | MAT-3 | 4 |
| `MAT-6` | material graph→Slang codegen·diagnostic | · | MAT-2, MAT-3 | 4 |
| `MAT-7` | 자동 route·cook specialization | · | MAT-4~MAT-6 | 3 |
| `MAT-8` | artist preview·cost badge·fallback 설명 | · | MAT-2, MAT-7 | 3 |
| `MAT-9` | Blender golden·route parity·성능 gate | · | MAT-7, MAT-8 | 2 |

구 `SRP-3`의 공용 graph 기반과 구 PBR 레인의 material 몫은 이 페이즈가 대체한다.
완료선은 [`BlenderMaterialGraphPlan.md`](BlenderMaterialGraphPlan.md) §6을 따른다.

---

## 6. PHASE 4.75 — 남은 렌더 파이프라인, 215.5일

### 6.1 공통·GPU 설계 게이트 — 14.5일

| ID | 내용 | 상태 | 선행 | 일 |
|---|---|---|---|---:|
| `BASE-0` | 공통 밀봉 frame/PNG/HDR/timing/graph stats | · | PHASE 4.25 | 6 |
| `4-1` | Scriptable Render Pipeline·Custom Pass 확장 계약 | ✅ | — | 2 |
| `4-2` | GPU-driven rendering 아키텍처 구상 | · | BASE-0 | 2 |
| `4-3` | Stochastic Tile-Based Lighting 구상 | · | BASE-0 | 1.5 |
| `4-4` | DXR 구상 | · | BASE-0 | 1.5 |
| `4-5` | DLSS 구상 | · | BASE-0 | 1 |
| `4-6` | 공통 의존 그래프·수직 슬라이스·구현 페이즈 | · | 4-2~4-5 | 0.5 |

### 6.2 RenderGraph·queue — 113일 + Q0 미산정

| ID | 내용 | 상태 | 선행 | 일 |
|---|---|---|---|---:|
| `RG0` | `BASE-0`에 흡수된 역사 포인터 | ⊘ | — | 0 |
| `RG1` | Read/Write/Modify·versioned resource API | · | BASE-0 | 8 |
| `RG2` | stable single-queue DAG compiler | · | RG1 | 10 |
| `RG3` | DAG 기준 culling/lifetime/barrier | · | RG2 | 8 |
| `RG4` | dependency wave 병렬 기록·진단 | · | RG3 | 7 |
| `RG5` | 제품 Pass·Pipeline compiler 이관 | · | RG4 | 12 |
| `RG6` | DX12/Vulkan 제품 cutover | · | RG5, BASE-0 | 8 |
| `RG7` | transient buffer·in-frame aliasing | · | RG6 | 20 |
| `Q0` | queue/fence RHI 계약 | · | RG6 | 미산정 |
| `RG8` | multi-queue·async compute | · | RG7, Q0 | 25 |
| `RG9` | subresource·split barrier·Resource Inspector | · | RG8 | 15 |

### 6.3 라이트맵 — 35일

| ID | 내용 | 상태 | 선행 | 일 |
|---|---|---|---|---:|
| `L0` | 삭제된 베이커 실측·정답지 | ✅ | — | 2 |
| `L1` | xatlas UV1 언랩 | · | PHASE 3.75 vertex schema | 5 |
| `L2` | SAH BVH·leaf off-by-one 수정 | · | — | 4 |
| `L3` | 직접광·DX12 RHI·rect-size dispatch | · | L1, L2 | 6 |
| `L4` | background bake·time slice·invalidation | · | L3, Q0 | 7 |
| `L5` | progressive 간접광 | · | L4 | 5 |
| `L6` | dilate·seam·padding 원인 수정 | · | L3 | 3 |
| `L7` | 병렬화 실측 판정 | 차단 | L5, L6 | 3 |

### 6.4 일반 SRP — 33일

| ID | 내용 | 상태 | 선행 | 일 |
|---|---|---|---|---:|
| `SRP-0` | Blueprint schema·순수 검증기 | · | BASE-0 | 4 |
| `SRP-1` | 19 node authored Pass Stack | · | RG5, SRP-0 | 8 |
| `SRP-2` | Slang Code mode·Fullscreen Custom Pass | · | SRP-1 | 7 |
| `SRP-3` | 공용 Graph IR 기반 | ⊘ | PHASE 4.25 MAT-2/MAT-6에 흡수 | 0 |
| `SRP-4` | Pass-domain Fullscreen/Compute/RendererList/history | · | SRP-2, MAT-6 | 5 |
| `SRP-5` | variant·hot reload·preview·선택적 C# 값 | · | SRP-4 | 6 |
| `SRP-6` | 소스 Native Pass 연결 | · | SRP-5 | 3 |

`.shadergraph(domain=pass)`의 출력 계약과 ResourceSchema는 이 트랙이 소유한다. graph editor,
typed node/pin, serialization/codegen 기반은 PHASE 4.25를 재사용한다.

### 6.5 renderer 품질·후처리 — 20일

| ID | 내용 | 상태 | 선행 | 일 |
|---|---|---|---|---:|
| `RND-1` | local reflection probe·specular AO | · | MAT-9 | 4 |
| `RND-2` | shadow module·point/spot atlas·quality tier | · | RND-1 | 10 |
| `RND-3` | display OETF·AgX·auto exposure·bloom | · | BASE-0 | 6 |

이 세 항목은 Blender Material Graph의 색/재질 acceptance에 포함하지 않는다. 각각 독립
renderer golden과 성능 gate를 사용한다.

---

## 7. 공수와 진행률 규칙

| 묶음 | 활성 행 | 총일 | 완료 | 진행 기성 | 잔여 |
|---|---:|---:|---:|---:|---:|
| PHASE 4 PBR 배선 | 10 | 18 | 0 | 1.5 | 16.5 |
| PHASE 4.25 Material Graph | 10 | 34 | 0 | 0 | 34 |
| PHASE 4.75 공통/GPU 설계 | 7 | 14.5 | 2 | 0 | 12.5 |
| PHASE 4.75 RenderGraph/Q0 | 10 | 113 | 0 | 0 | 113 |
| PHASE 4.75 라이트맵 | 8 | 35 | 2 | 0 | 33 |
| PHASE 4.75 일반 SRP | 6 | 33 | 0 | 0 | 33 |
| PHASE 4.75 renderer/post | 3 | 20 | 0 | 0 | 20 |
| **합계** | **54** | **267.5** | **4** | **1.5** | **262** |

`Q0`은 0일 표기가 완료가 아니라 미산정이다. `4-2~4-5`도 구상 공수만 포함하며 실제 GPU
기능 구현 공수는 `4-6` 뒤 추가한다. 267.5일은 기존 계획의 총량을 재배분한 값이지 새
기능 전체 완료를 보증하는 상한이 아니다.

---

## 8. 공통 완료 기준

### PHASE 4

- 제품 PBR GBuffer/Deferred/Forward가 native Slang 공용 평가를 사용한다.
- alpha/AO/emissive/UV/sampler/normal 의미가 material generation에서 draw까지 손실 없다.
- backend neutral 기본값과 gate 종료 코드가 일치한다.
- generation 혼합과 부분 게시를 fail-closed하고 검정/변색 플리커 실장면 0.

### PHASE 4.25

- Blender 5.1.1 Principled 재질과 pre-tone linear HDR material-grid 허용 오차 통과.
- Material Graph round-trip, typed IR, generated Slang, route parity 통과.
- Standard tier 현행 성능 상한 유지, 사용하지 않는 lobe/sample/variant 제거.
- artist가 RHI/register/pass를 만지지 않고 비용과 fallback 이유를 이해할 수 있다.

### PHASE 4.75

- Pipeline Asset authored order와 resource dependency compiled order가 분리된다.
- RG6 단일 제품 cutover 뒤에만 aliasing/async compute를 연다.
- generic Pass Graph가 Material Graph의 Principled 의미를 재정의하지 않는다.
- shadow/probe/post는 material parity와 독립 golden/성능 gate를 가진다.
- GPU-driven/Stochastic/DXR/DLSS의 지원 행렬, fallback, 수직 슬라이스와 공수를 확정한다.

---

## 9. 금지하는 재혼합

- PBR 배선 결함을 새 Principled lobe 추가로 덮지 않는다.
- Blender material parity를 tone map/bloom/shadow 결과로 판정하지 않는다.
- Material Graph가 Pass topology·RenderGraph lifetime·RHI binding 번호를 소유하지 않는다.
- generic Pass Shader Graph가 `PrincipledSurface` 의미나 artist material tier를 재정의하지 않는다.
- PHASE 3.75 모델 identity 빈틈을 legacy adapter로 메우지 않는다.
- HLSL과 Slang 제품 PBR을 장기 병행하지 않는다.

---

## 10. 변경 이력

| 날짜 | 변경 |
|---|---|
| 2026-09-03 | 단일 PHASE 4를 4/4.25/4.75로 분할. 현재 PBR 배선 감사를 PBR-W, Blender Principled Material Graph를 MAT, renderer 품질을 RND로 재편. 구 단일 PBR 직선 레인 폐기. 총공수 267.5일 보존 |
| 2026-09-02 | 구 ModelImport I/V experiment 배선을 제거하고 PHASE 3.75로 분리 |
| 2026-09-01 | 최초 통합. BASE-0, Q0, RG/L/SRP/PBR 공수와 순서 정리 |
