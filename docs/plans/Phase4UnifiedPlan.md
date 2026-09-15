# PHASE 4 계열 통합 계획 — PBR 안정화에서 차세대 렌더링까지

**신설 2026-09-01 · 재분할 2026-09-03 · PHASE 4.5 분리 2026-09-14 · PHASE 4.3 분리 2026-09-15 · PHASE 4.9 신설 2026-09-15(공수 미산정) · 활성 69행 352.5일 · 완료 12.5일 + 진행 기성 3.5일 · 잔여 336.5일**

기존 단일 PHASE 4에는 현재 PBR 배선 수정, Blender형 Material Graph, RenderGraph·라이트맵·
일반 SRP·후처리·차세대 GPU 기능이 한데 섞여 있었다. 이 문서는 그것을 다섯 완료선으로
분리한다.

2026-09-14에 **PHASE 4.5 시간축 재구성 계층**이 신설되며 총공수가 267.5일에서 352.5일로
늘었다. 이것은 재배분이 아니라 **신규 산정**이다 — 기존 `4-5 DLSS 구상` 1일이 모션 벡터가
없는 엔진에 업스케일과 프레임 생성을 동시에 얹는 비용을 재지 않고 있었다.

2026-09-15에 **PHASE 4.3 RenderGraph 리소스 의존성 스케줄링**이 분리됐다. 이것은 **순수
재배치이며 총공수는 352.5일 그대로다** — PHASE 4.75가 소유하던 트랙 `RG`·`Q0` 113일과
PHASE 4.5가 소유하던 `BASE-0` 6일이 새 페이즈로 옮겨갔을 뿐이다. 분리 근거는 §1.3에 적는다.

| 페이즈 | 단일 책임 | 활성 행 | 일 | 상태 |
|---|---|---:|---:|---|
| **4** | 현 제품 PBR `.slang`·Material·Renderer 배선 안정화 | 10 | 18 | W2/W4/W5/W6 8.5일 완료 + 진행 기성 3.5일 |
| **4.25** | Blender 5.1.1 Principled 기반 Material Graph와 artist workflow | 10 | 34 | 미착수 |
| **4.3** | 공통 밀봉 하네스와 RenderGraph 리소스 의존성·queue/fence RHI 계약 | 11 | 119 | 미착수 |
| **4.5** | 모션 벡터·jitter·히스토리와 Temporal Upscaling·Frame Generation | 16 | 86 | 미착수 |
| **4.75** | 라이트맵·일반 SRP·shadow/probe/post·GPU 기능 | 22 | 95.5 | 4일 완료 |
| **합계** |  | **69** | **352.5** | **잔여 336.5** |

`Q0`(queue/fence RHI 계약)은 미산정이며 위 119일에 포함되지 않는다.

모델 자산 신원·sidecar·loader·renderer/scene/animation 직접 소비·Assimp 은퇴는
[`ModelAssetBigBangCutoverPlan.md`](archive/ModelAssetBigBangCutoverPlan.md), **PHASE 3.75**가
소유한다. PHASE 4 계열은 UUIDv8 typed model/material/texture generation을 읽기 전용 입력으로
받으며 legacy GUID, Assimp fallback, experiment on/off를 다시 만들지 않는다.

---

## 0. 정본 경계

| 범위 | 정본 |
|---|---|
| PHASE 4/4.25/4.3/4.5/4.75 순서·공수·완료선 | 이 문서 |
| 현재 PBR Slang/Material/Renderer 결함과 수정 순서 | [`PBRWiringStabilizationPlan.md`](PBRWiringStabilizationPlan.md) |
| Blender Principled·Material Graph·성능 tier·artist UX | [`BlenderMaterialGraphPlan.md`](BlenderMaterialGraphPlan.md) |
| `BASE-0` 공통 밀봉 하네스의 소유 페이즈·공수·선후 | 이 문서 §6.1 |
| `BASE-0` 산출물 계약(밀봉 frame/PNG/HDR/timing/graph stats) | [`TemporalReconstructionPlan.md`](TemporalReconstructionPlan.md) §5.1 |
| 모션 벡터·jitter·업스케일·프레임 생성·present 소유권 | [`TemporalReconstructionPlan.md`](TemporalReconstructionPlan.md) |
| Pipeline Asset·일반 Pass Shader Graph·Custom Pass | [`ScriptableRenderPipelinePlan.md`](ScriptableRenderPipelinePlan.md) |
| versioned resource·DAG·barrier·aliasing·async compute | [`RenderGraphDependencySchedulingPlan.md`](RenderGraphDependencySchedulingPlan.md) |
| 라이트맵 UV1·BVH·직접/간접광·background bake | [`LightmapBakerPlan.md`](LightmapBakerPlan.md) |
| ShaderMeta·material snapshot·Slang compiler 기반 완료 기록 | [`MaterialPipelinePlan.md`](archive/MaterialPipelinePlan.md) |
| 모델 자산·vertex schema·typed subasset generation | [`ModelAssetBigBangCutoverPlan.md`](archive/ModelAssetBigBangCutoverPlan.md) |
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

### 1.3 PHASE 4.3 — 공통 밀봉 하네스와 RenderGraph 의존성 스케줄링

리소스 접근 의미와 버전 계보로 **실행 순서를 컴파일하는 그래프**를 세우고, 그 판정에 필요한
공통 밀봉 하네스를 앞에 둔다. 정본은
[`RenderGraphDependencySchedulingPlan.md`](RenderGraphDependencySchedulingPlan.md)다.

- `BASE-0` 공통 밀봉 하네스 — 구 `4-0` + `SRP-G0` + `RG0`의 통합물.
- 트랙 `RG` — versioned resource API, stable DAG compiler, culling/lifetime/barrier 재계산,
  dependency wave 병렬 기록, 제품 Pass 이관, DX12/Vulkan cutover, aliasing, async compute.
- `Q0` — queue/fence RHI 계약. `RG8`과 PHASE 4.75 `L4`가 공유하는 기반이며 **미산정**이다.

**분리 근거는 세 가지다.**

첫째, **RG는 PHASE 4.75의 나머지 트랙이 소비하는 선행**이었다. `SRP-1`은 `RG5`를, `L4`는
`Q0`을 받는다. 선행과 소비자를 같은 페이즈에 묶으면 페이즈 완료 판정이 자기 내부 순서에
갇혀 어느 완료선도 독립적으로 닫히지 않는다.

둘째, **공수가 페이즈 하나를 통째로 채웠다**. 분리 전 PHASE 4.75는 32행 208.5일이었고 그중
RG·`Q0`가 10행 113일 — 절반을 넘었다. 한 페이즈에 두 개의 완료선이 들어 있었다.

셋째, **`BASE-0`은 RG와 결합이 가장 강하다**. 2026-09-14에 이 하네스를 PHASE 4.5로 이관한
이유는 "4.5가 4.75보다 앞에 서기 때문"이었는데, 그 하네스 자체가 `4-0`·`SRP-G0`·`RG0`의
통합물이고 `RG1`·`RG6`가 직접 소비한다. RG가 4.5보다 앞에 서게 된 이상 하네스도 함께
앞으로 온다. PHASE 4.5 `TR0`와 PHASE 4.75는 이제 이것을 **읽기 전용 입력**으로 받는다.

**번호가 완료 선후를 뜻하지 않는 유일한 칸이 여기다.** PHASE 4.5는 `BASE-0` 뒤부터 `RG`
본체와 **병렬**이며, `RG6`·`Q0`를 선행으로 받지 않는다(§3). 4.3과 4.5의 번호 순서는 착수
순서이지 완료 순서가 아니다.

### 1.4 PHASE 4.5 — 시간축 재구성 계층

모션 벡터·jitter·히스토리라는 시간축 입력을 생산하고, 그 위에 벤더 중립 업스케일러와
프레임 생성을 얹는다. 정본은 [`TemporalReconstructionPlan.md`](TemporalReconstructionPlan.md)다.

- 트랙 `TR` — 모션 벡터, jitter, 히스토리, 렌더 해상도 ≠ 표시 해상도 계약.
- 트랙 `TU` — 벤더 중립 Upscaler 인터페이스와 FSR/DLSS/XeSS 백엔드.
- 트랙 `FG` — present 소유권·pacing·지연 마커와 Frame Generation 백엔드.

이 페이즈가 4.75보다 앞에 서는 이유는 두 가지다. 첫째, `TR0` **계측 무해화 계약**이 여기서
확정돼야 이후 모든 렌더 게이트가 해상도와 프레임 종류를 선언한다. 둘째, 모션 벡터는
RenderGraph 재작성과 독립이며 TAA·모션 블러·SSGI 품질 개선의 공통 입력이라 `RG`를 기다릴
이유가 없다. 이 페이즈는 PHASE 4.3에서 `BASE-0` **하나만** 선행으로 받는다.

**벤더 이름을 기능 이름으로 쓰지 않는다.** 구 `4-5 DLSS 구상`은 이 페이즈로 흡수되며 DLSS는
`TU3`/`FG3` 백엔드 하나의 이름으로만 남는다.

### 1.5 PHASE 4.75 — 남은 renderer/SRP/GPU 기능

기존 PHASE 4에서 위 세 범위가 아닌 항목을 모두 이곳으로 이동한다.

- GPU-driven/Stochastic Lighting/DXR 설계 게이트 — 트랙 `GPU`.
- 라이트맵 L.
- Pipeline Asset, general Custom Pass, `.shadergraph(domain=pass)`, Slang Code mode.
- local reflection probe/specular AO, shadow atlas, display/post.

`BASE-0`·`RG5`·`RG6`·`Q0`는 PHASE 4.3이 소유하며 이 페이즈는 입력으로 받는다.

PHASE 4.75의 generic Pass graph는 PHASE 4.25의 graph editor/typed IR 기반을 재사용하되
Material output/Principled 의미를 소유하지 않는다. 반대로 PHASE 4.25는 Pass topology,
RenderGraph resource lifetime이나 post stack을 소유하지 않는다.

### 1.6 PHASE 4.9 — 백엔드 패리티 (DX12/Vulkan 교차 판정)

정본은 [`BackendParityPlan.md`](BackendParityPlan.md)다. **2026-09-15 사용자 결정**으로
PHASE 4의 판정을 DX12 백엔드로만 하기로 하면서, 미룬 것들을 잃지 않으려고 세웠다.

- 교차 백엔드 제품 프레임 캡처 1:1 픽셀 대응과 `render.pbr.compare` 판정 복귀.
- `verify-pbr-wiring-baseline.ps1`의 vulkan 회차·`vk.*` 4종 복구.
- **DX12 전용 deferred 검사 신설** — `vk.*`를 끄며 생긴 구멍이다(아래).
- Vulkan 기동 창 `gCubeMap` 결함(PHASE 4와 무관한 별건).

**★ 미룬 것은 판정이지 배선이 아니다.** RHI 중립 어휘(enum·변환표)와 Vulkan 백엔드
구현은 PHASE 4에서도 계속 **양쪽을 채운다**. 새 값 축을 더할 때 두 백엔드 변환표를
모두 채우는 규약은 그대로다 — 어휘에 구멍을 내면 백엔드 비대칭이 생기고 그것이
4.9에서 갚을 빚이 된다. 다른 세션은 PHASE 4에서 vulkan을 걱정하지 않는다.

**`vk.*`는 이름이 범위를 속인다.** `vk.shadow`/`gbuffer`/`forward`/`deferred` 넷은
vulkan 단독이 아니라 **DX12/Vulkan 대조** 검사다(`RunVulkanGBufferTest` 안에
`dx12Capture`와 `vkCapture`가 나란히 있다). 그래서 끄면 **DX12 팔도 함께 꺼진다.**
gbuffer는 `dx12.gbuffer`, forward는 `dx12.forwardshade`가 덮고 shadow는
`dx12.shadowquality`가 축이 달라 절반만 덮으며, **deferred는 대체가 아예 없다**
(`dx12.deferred`라는 명령이 없다). 이 자리가 결정의 실제 비용이고, 교차 백엔드가
아니라 DX12 단독이라 시각 고정을 기다리지 않고 먼저 갚을 수 있다.

**공수는 미산정이다.** 선행 조건인 시뮬레이션 시각 고정(`time.*` 부재)을 실측하기
전에는 슬라이스를 끊을 수 없다. **총공수 352.5일은 변동 없다** — 지어낸 공수를
정본에 넣지 않았고, 대시보드에도 행을 비워 두었다.

**항목 ID 개명 (2026-09-15).** 구 PHASE 4 시절 잔재인 `4-2`/`4-3`/`4-4`/`4-6`을
`GPU-1`/`GPU-2`/`GPU-3`/`GPU-9`로 바꾼다. 하이픈 ID `4-3`이 새 페이즈 번호 `4.3`과
읽는 자리에서 충돌하기 때문이며, 내용·공수·선후는 바뀌지 않는다. 완료된 `4-1`은
GPU 계열이 아니라 SRP 확장 계약이므로 ID를 유지한다.

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
PHASE 4.3   BASE-0
              └─ RG1 → RG2 → RG3 → RG4 → RG5 → RG6 → RG7 → Q0 → RG8 → RG9
    ↓  (BASE-0 뒤부터 PHASE 4.5와 병렬 — 4.5는 RG 본체를 기다리지 않는다)
PHASE 4.5   PHASE 4.3에서 BASE-0만 입력으로 받는다
              ├─ TR0 (계측 무해화)
              └─ TR1 → TR2 → TR3 → TU0 → TU1 ∥ TU2 → TU3 ∥ TU4 ∥ TU5
                                                          └─ FG0 → FG1 ∥ FG2 → FG3 ∥ FG4
                                                                              → TFG9
    ↓
PHASE 4.9   BackendParityPlan — 시각 고정 → 교차 백엔드 판정 복귀 (슬라이스 미확정)
    ↓
PHASE 4.75  PHASE 4.3에서 BASE-0·RG5·RG6·Q0를 입력으로 받는다
              ├─ L1 ∥ L2 → L3 → Q0 → L4 → L5/L6 → L7
              ├─ SRP-0 → SRP-1 → SRP-2 → SRP-4 → SRP-5 → SRP-6
              ├─ RND-1 → RND-2/RND-3
              └─ GPU-1~GPU-3 → GPU-9
```

PHASE 4.25는 PHASE 4 실장면 gate를 통과한 제품 배선 위에서만 시작한다. PHASE 4.3과
PHASE 4.75는 PHASE 4.25의 compiled material generation/feature mask/route를 입력으로 받는다.
다만 PHASE 4.3의 RG 내부 사전 연구와 PHASE 4.75의 L 내부 사전 연구는 제품 배선 없이 진행할
수 있어도 완료/cutover는 앞선 완료선을 우회할 수 없다.

**4.3과 4.5는 번호 순서가 완료 순서를 뜻하지 않는 유일한 칸이다.** PHASE 4.5는 PHASE 4.3의
`BASE-0` 하나만 선행으로 받고 `RG6`·`Q0`는 받지 않는다. 자체 보간기를 만들지 않으므로
프레임 생성 SDK가 자기 큐와 present 타이밍을 소유하기 때문이다. 이 전제가 뒤집히면
[`TemporalReconstructionPlan.md`](TemporalReconstructionPlan.md) §3.3과 `FG0` 공수를 다시 산정한다.

PHASE 4.75가 PHASE 4.3에서 받는 것은 `BASE-0`(전 트랙의 판정 하네스), `RG5`(`SRP-1`의 선행),
`RG6`(제품 cutover 뒤에만 4.75의 새 Pass를 얹는다), `Q0`(`L4` 백그라운드 베이크의 큐 계약)다.
4.75의 어느 트랙도 별도 queue 계층이나 별도 캡처 체계를 만들지 않는다.

---

## 4. PHASE 4 — PBR 배선 안정화, 18일

`◐`는 부분 진행이며 완료 공수는 아니다. 대시보드는 확인 가능한 기성만 별도 반영한다.

| ID | 내용 | 상태 | 선행 | 일 |
|---|---|---|---|---:|
| `PBR-W0` | 감사 정본·Gunner/primitive capture·strict gate | ◐ | PHASE 3.75 | 2 |
| `PBR-W1` | normal-map 저작 유무 snapshot 단일화 | ◐ | — | 1 |
| `PBR-W2` | native Slang 제품 진입점·공용 현행 평가 | ✓ | W0 | 2.5 |
| `PBR-W3` | backend neutral resource/binding/exit code | ◐ | W0 | 1 |
| `PBR-W4` | alpha mode/cutoff·double-sided/cull | ✓ | W2 | 2 |
| `PBR-W5` | AO·texture table·GBuffer packing | ✓ | W2, W3 | 2.5 |
| `PBR-W6` | emissive factor/strength·constant·색공간 | ✓ | W2 | 1.5 |
| `PBR-W7` | UV/sampler/mip·normal/tangent transform | ◐ | W2 | 2 |
| `PBR-W8` | generation 원자 밀봉·플리커 fail-closed | ◐ | W3~W7 | 2 |
| `PBR-W9` | DX12/Vulkan 실장면·장시간·재임포트 cutover | ◐ | W8 | 1.5 |

완료선은 [`PBRWiringStabilizationPlan.md`](PBRWiringStabilizationPlan.md) §4를 따른다.
2026-09-06 W0/W3 첫 구현과 실장면 캡처 근거는 같은 문서 §6에 기록했다.
W3 기성 0.5일을 추가했으며, W0/W1/W3 모두 최종 acceptance 전이라 진행 상태다.
W2 native Slang 공용 평가는 같은 문서 §7의 양 backend 36-case HDR 비교와 기존 회귀를
근거로 완료했다. Forward 조명 드리프트와 null LUT 직접광 과보상을 함께 수정했다.
W4 alpha/양면 coverage는 같은 문서 §8의 양 backend 40-case·material 왕복·캐시 갱신 검증으로
완료했다. CEMC6 모델 14개를 재게시했다. W5는 §9의 독립 AO·reflection table과
양 backend 48-case로 완료했고 GBuffer 포맷은 검토 후 유지한다. W6는 §10의
발광 색/강도·상수 발광·색공간과 양 backend 78-case를 완료했다. CEMC7 모델 14개 재게시와
ID 보존을 확인했다. W7은 §11의 normal/tangent 변환·양 backend 128-case와
§12의 UV0/UV1 선택·텍스처별 변환·양 backend 64-case, §13의 mip 생성·소비 30-case를
합계 1.5일 기성으로 반영했다. 축소 MASK를 포함한 coverage 48-case도 통과했다.
CEMC8과 하위 ID 310개를 유지하며 재질별 sampler 전달이 다음 단위다. strict GUID의 기존
`ImmProbe.prefab.meta` 추적 정책 위반 1개와 W9 acceptance는 별도 미해결로 남긴다.

2026-09-14 W8/W9 구현이 착지했다(같은 문서 §14·§15). **기성으로 세지 않는다** — 빌드와
실행을 하지 않았고, 새 검사의 이빨을 변이로 증명하지 않았다. 착수 전 실측이 계획서의
전제 둘을 뒤집었다: ① W8이 말한 "약한 신원으로 재사용될 여지"는 여지가 아니라 실재하는
결함이었다(밀봉 중복 제거 키가 legacy `Material*` 주소라, 같은 재질 자산을 공유하는
렌더러들의 인스턴스 override가 서로 덮였다). ② `verify-pbr-wiring-baseline.ps1`은 W0부터
`run-all.ps1`에 물려 있지 않았고, 캡처의 float32 원본을 읽는 코드도 없었다 — PBR 픽셀 축
전체가 도는 세트 밖이었다. 두 결함과 두 공백이 W8/W9의 실제 작업이 됐다.

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

## 6. PHASE 4.3 — 공통 밀봉 하네스와 RenderGraph 의존성 스케줄링, 119일

정본은 [`RenderGraphDependencySchedulingPlan.md`](RenderGraphDependencySchedulingPlan.md)이며
이 절은 공수와 선후만 싣는다.

### 6.1 공통 기준선 — 6일

| ID | 내용 | 상태 | 선행 | 일 |
|---|---|---|---|---:|
| `BASE-0` | 공통 밀봉 frame/PNG/HDR/timing/graph stats | · | PHASE 4.25 | 6 |

구 `4-0`·`SRP-G0`·`RG0`의 통합물이다. 2026-09-14에 PHASE 4.75에서 PHASE 4.5로 이관했고
2026-09-15에 이 페이즈로 다시 옮겼다 — 소비자 셋(4.3 `RG1`/`RG6`, 4.5 `TR0`, 4.75 전 트랙)
가운데 가장 앞에 서는 것이 RG이기 때문이다. 산출물 계약 서술은
[`TemporalReconstructionPlan.md`](TemporalReconstructionPlan.md) §5.1이 계속 소유하며,
PHASE 4.5와 PHASE 4.75는 결과를 읽기 전용 입력으로 받는다. 하네스는 **한 벌만** 만든다.

### 6.2 트랙 RG·queue — 113일 + Q0 미산정

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

`RG0`은 0일 표기가 완료가 아니라 `BASE-0` 흡수를 가리키는 중복 계상 방지 포인터다. `Q0`도
0일이 아니라 **미산정**이며, `RG8`과 PHASE 4.75 `L4`가 먼저 필요해지는 쪽의 착수 시점에 세운다.

**이 페이즈의 완료선은 `RG6`이다.** `RG7` 이후는 최적화 트랙이라 늦어져도 declaration-order로
되돌리지 않는다. PHASE 4.75가 이 트랙에서 받는 것은 `RG5`(`SRP-1`의 선행), `RG6`(제품
cutover), `Q0`(`L4`의 큐 계약)이며, `SRP-1` 병합 기각 판정은
[`RenderGraphDependencySchedulingPlan.md`](RenderGraphDependencySchedulingPlan.md) §4가 정본이다.

---

## 7. PHASE 4.5 — 시간축 재구성 계층, 86일

정본은 [`TemporalReconstructionPlan.md`](TemporalReconstructionPlan.md)이며 이 절은 공수와
선후만 싣는다. `BASE-0`은 PHASE 4.3 §6.1이 소유하며 이 페이즈는 입력으로 받는다.

### 7.1 트랙 TR — 시간축 기반, 25일

| ID | 내용 | 상태 | 선행 | 일 |
|---|---|---|---|---:|
| `TR0` | 계측 무해화 계약 — 측정 표면의 해상도·프레임 종류 선언 | · | BASE-0 | 4 |
| `TR1` | 모션 벡터 생산 — static/skinned/instanced/decal/알파 | · | BASE-0 | 10 |
| `TR2` | 렌더 해상도 ≠ 표시 해상도 계약·jitter 시퀀스 | · | TR1 | 6 |
| `TR3` | 히스토리·이전 프레임 행렬 공용 계약 | · | TR2 | 5 |

### 7.2 트랙 TU — 업스케일, 24일

| ID | 내용 | 상태 | 선행 | 일 |
|---|---|---|---|---:|
| `TU0` | 벤더 중립 Upscaler 인터페이스·기능 질의·폴백 사슬 | · | TR3 | 4 |
| `TU1` | 무-upscale 기준 경로·A/B 대조군 | · | TU0 | 3 |
| `TU2` | FSR 백엔드 — 벤더 중립 베이스라인 | · | TU0 | 6 |
| `TU3` | DLSS Super Resolution 백엔드 | · | TU2 | 4 |
| `TU4` | XeSS 백엔드 | · | TU2 | 3 |
| `TU5` | PostChain 앞 삽입·슬롯 schema 크기 유도 | · | TU2 | 4 |

### 7.3 트랙 FG — 프레임 생성, 32일

| ID | 내용 | 상태 | 선행 | 일 |
|---|---|---|---|---:|
| `FG0` | present 소유권·proxy swapchain·frame pacing 계약 | · | TU5 | 10 |
| `FG1` | 지연 마커 — Reflex·Anti-Lag 2, 게임 루프 경유 | · | FG0 | 5 |
| `FG2` | FSR 3 Frame Generation 백엔드 | · | FG0 | 8 |
| `FG3` | DLSS Frame Generation 백엔드 | · | FG2 | 5 |
| `FG4` | HUD-less 백버퍼·UI 합성·에디터 금지 강제 | · | FG2 | 4 |

### 7.4 통합 게이트 — 5일

| ID | 내용 | 상태 | 선행 | 일 |
|---|---|---|---|---:|
| `TFG9` | 지원 행렬 전수·폴백 사슬·계측 단정·성능/지연 판정 | · | TU3, TU4, FG3, FG4 | 5 |

---

## 8. PHASE 4.75 — 남은 렌더 파이프라인, 95.5일

`BASE-0`과 트랙 `RG`·`Q0`는 PHASE 4.3(§6)이 소유한다. 이 페이즈는 그 결과를 입력으로 받으며
별도 캡처 체계나 별도 queue 계층을 만들지 않는다.

### 8.1 공통·GPU 설계 게이트 — 7.5일

| ID | 내용 | 상태 | 선행 | 일 |
|---|---|---|---|---:|
| `4-1` | Scriptable Render Pipeline·Custom Pass 확장 계약 | ✅ | — | 2 |
| `GPU-1` | GPU-driven rendering 아키텍처 구상 | · | BASE-0 | 2 |
| `GPU-2` | Stochastic Tile-Based Lighting 구상 | · | BASE-0 | 1.5 |
| `GPU-3` | DXR 구상 | · | BASE-0 | 1.5 |
| `GPU-9` | 공통 의존 그래프·수직 슬라이스·구현 페이즈 | · | GPU-1~GPU-3 | 0.5 |

**2026-09-15 개명.** 구 `4-2`/`4-3`/`4-4`/`4-6`이 `GPU-1`/`GPU-2`/`GPU-3`/`GPU-9`다. 하이픈
ID `4-3`이 새 페이즈 번호 `4.3`과 충돌해 읽는 자리에서 갈리지 않았다. 내용·공수·선후는
그대로이며 `4-1`은 GPU 계열이 아니라 SRP 확장 계약이라 ID를 유지한다.

구 `4-5 DLSS 구상`은 PHASE 4.5로 분리됐다. `GPU-9`의 의존 그래프는 PHASE 4.5가 확정한
시간축 입력·업스케일 계약과 PHASE 4.3이 확정한 compiled graph 계약을 **읽기 전용 입력**으로
받는다.

### 8.2 라이트맵 — 35일

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

### 8.3 일반 SRP — 33일

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

### 8.4 renderer 품질·후처리 — 20일

| ID | 내용 | 상태 | 선행 | 일 |
|---|---|---|---|---:|
| `RND-1` | local reflection probe·specular AO | · | MAT-9 | 4 |
| `RND-2` | shadow module·point/spot atlas·quality tier | · | RND-1 | 10 |
| `RND-3` | display OETF·AgX·auto exposure·bloom | · | BASE-0 | 6 |

이 세 항목은 Blender Material Graph의 색/재질 acceptance에 포함하지 않는다. 각각 독립
renderer golden과 성능 gate를 사용한다.

---

## 9. 공수와 진행률 규칙

| 묶음 | 활성 행 | 총일 | 완료 | 진행 기성 | 잔여 |
|---|---:|---:|---:|---:|---:|
| PHASE 4 PBR 배선 | 10 | 18 | 8.5 | 3.5 | 6 |
| PHASE 4.25 Material Graph | 10 | 34 | 0 | 0 | 34 |
| PHASE 4.3 공통 기준선 | 1 | 6 | 0 | 0 | 6 |
| PHASE 4.3 트랙 RG/Q0 | 10 | 113 | 0 | 0 | 113 |
| PHASE 4.5 트랙 TR | 4 | 25 | 0 | 0 | 25 |
| PHASE 4.5 트랙 TU | 6 | 24 | 0 | 0 | 24 |
| PHASE 4.5 트랙 FG | 5 | 32 | 0 | 0 | 32 |
| PHASE 4.5 통합 게이트 | 1 | 5 | 0 | 0 | 5 |
| PHASE 4.75 공통/GPU 설계 | 5 | 7.5 | 2 | 0 | 5.5 |
| PHASE 4.75 라이트맵 | 8 | 35 | 2 | 0 | 33 |
| PHASE 4.75 일반 SRP | 6 | 33 | 0 | 0 | 33 |
| PHASE 4.75 renderer/post | 3 | 20 | 0 | 0 | 20 |
| **합계** | **69** | **352.5** | **12.5** | **3.5** | **336.5** |

페이즈 소계는 PHASE 4 18일 · 4.25 34일 · **4.3 119일** · **4.5 86일** · **4.75 95.5일**이다.

`Q0`은 0일 표기가 완료가 아니라 미산정이다. `GPU-1~GPU-3`도 구상 공수만 포함하며 실제 GPU
기능 구현 공수는 `GPU-9` 뒤 추가한다. 352.5일은 새 기능 전체 완료를 보증하는 상한이 아니다.

267.5일에서 352.5일로 늘어난 85일은 PHASE 4.5 신설분 92일에서 구 `BASE-0` 6일과 구 `4-5`
1일을 뺀 값이다. **이 증가분의 대부분은 벤더 SDK 통합이 아니라 모션 벡터·해상도 계약·계측
무해화다** — 업스케일과 프레임 생성을 둘 다 취소해도 `TR` 25일은 TAA·모션 블러·SSGI
품질의 공통 선행으로 남는다.

2026-09-15 PHASE 4.3 분리는 **총공수를 바꾸지 않았다.** 이동한 것은 두 묶음뿐이다 —
`BASE-0` 1행 6일이 PHASE 4.5에서, 트랙 `RG`·`Q0` 10행 113일이 PHASE 4.75에서 왔다. 어느
행의 공수·상태·선행도 이 이동으로 재산정하지 않았고, 완료 12.5일과 진행 기성 3.5일도 그대로다.
공수를 재산정한 이동이 아니므로 **이 재배치를 진척으로 읽지 않는다.**

---

## 10. 공통 완료 기준

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

### PHASE 4.3

- `BASE-0` 하네스가 한 벌로 서고 PHASE 4.5·4.75의 게이트가 그것을 그대로 쓴다. 별도 캡처
  체계가 생기지 않는다.
- 리소스 접근이 `Read`/`Write`/`Modify`로 선언되고 implicit state-write 추론 제품 사용처와
  unversioned migration adapter가 0이다.
- Pipeline Asset authored order와 resource dependency compiled order가 분리된다.
- 같은 입력에서 compiled order와 dependency hash가 실행 간 동일하고, 독립 Pass tie-break가
  authored index로 명시된다.
- `RG6` 제품 cutover가 같은 밀봉 입력의 별도 프로세스 live frame으로 판정되고 DX12 debug
  layer·Vulkan validation error 0.
- `RG6` 뒤에만 aliasing과 async compute를 연다. `RG7` 이후가 늦어져도 declaration-order로
  되돌리지 않는다.

### PHASE 4.5

- 모션 벡터가 static·skinned·instanced·decal·알파 경로 전부에서 생산되고 독립 검증된다.
- `renderScale = 1.0`에서 기존 픽셀과 동등하다.
- 지원 행렬의 모든 칸이 성공 경로 또는 명시적 invalid 사유를 갖고, 어떤 하드웨어에서도
  폴백 사슬이 "기능 없음"이 아니라 FSR 또는 무-upscale에 착지한다.
- 벤더 ID 기반 분기가 소스에 0건이다.
- 모든 측정 표면이 해상도와 프레임 종류를 선언하고, 선언 누락 변이가 게이트를 붉게 만든다.
- 프레임 생성이 셸 swapchain에서만 활성화되고 에디터 뷰포트에서 fail-closed한다.

### PHASE 4.75

- generic Pass Graph가 Material Graph의 Principled 의미를 재정의하지 않는다.
- authored Pass Stack이 PHASE 4.3의 compiled DAG 위에서 `SRP-1`부터 픽셀 동등하게 선다.
- 라이트맵이 `Q0` 큐 계약을 소비하되 별도 queue 계층을 만들지 않는다.
- shadow/probe/post는 material parity와 독립 golden/성능 gate를 가진다.
- GPU-driven/Stochastic/DXR의 지원 행렬, fallback, 수직 슬라이스와 공수를 확정한다.

---

## 11. 금지하는 재혼합

- PBR 배선 결함을 새 Principled lobe 추가로 덮지 않는다.
- Blender material parity를 tone map/bloom/shadow 결과로 판정하지 않는다.
- Material Graph가 Pass topology·RenderGraph lifetime·RHI binding 번호를 소유하지 않는다.
- generic Pass Shader Graph가 `PrincipledSurface` 의미나 artist material tier를 재정의하지 않는다.
- PHASE 3.75 모델 identity 빈틈을 legacy adapter로 메우지 않는다.
- HLSL과 Slang 제품 PBR을 장기 병행하지 않는다.
- 벤더 이름을 기능 이름·Pass 이름·설정 키에 쓰지 않는다. DLSS는 백엔드 하나의 이름이다.
- 업스케일·프레임 생성의 성능 이득으로 렌더 성능 회귀를 가리지 않는다.

---

## 12. 변경 이력

| 날짜 | 변경 |
|---|---|
| 2026-09-15 | **PHASE 4.3 분리.** 트랙 `RG`·`Q0` 10행 113일을 PHASE 4.75에서, `BASE-0` 1행 6일을 PHASE 4.5에서 옮겨 11행 119일의 새 페이즈로 세웠다. PHASE 4.5 92 → 86일(16행), PHASE 4.75 208.5 → 95.5일(22행). **총공수 352.5일·완료 12.5일·진행 기성 3.5일·잔여 336.5일은 변동 없다 — 순수 재배치다.** 4.5는 `BASE-0`만 받고 `RG` 본체와 병렬이라 번호가 완료 선후를 뜻하지 않는 유일한 칸이 됐다. 항목 ID `4-2`/`4-3`/`4-4`/`4-6`을 `GPU-1`/`GPU-2`/`GPU-3`/`GPU-9`로 개명(하이픈 `4-3` ↔ 페이즈 `4.3` 충돌). §6~§12 절 번호가 한 칸씩 밀렸다 |
| 2026-09-14 | PHASE 4.5 시간축 재구성 계층 신설. 구 `4-5 DLSS 구상` 1일을 TR/TU/FG 17행 92일로 재산정하고 `BASE-0` 6일을 PHASE 4.75 공통·GPU 설계 게이트에서 이관. PHASE 4.75가 §6에서 §7로 이동, 215.5일 → 208.5일. 총공수 267.5일 → 352.5일, 잔여 336.5일 |
| 2026-09-06 | W7 mip 생성/보존·10개 포맷·양 backend 30-case, 축소 MASK를 포함한 coverage 48-case 통과. CEMC8 유지. W7 기성 1.5일, sampler 미완료. 기존 strict GUID 추적 정책 위반 1개 별도 기록. 완료 12.5일 + 진행 기성 3.5일, 잔여 251.5일 |
| 2026-09-06 | W7 UV0/UV1·텍스처별 변환·양 backend 64-case와 변환된 UV1 MASK/Shadow 검사. CEMC8 모델 14개/하위 ID 310개 보존. W7 기성 1일, sampler/mip 미완료. 완료 12.5일 + 진행 기성 3일, 잔여 252일 |
| 2026-09-06 | W7 normal/tangent 변환·양 backend 128-case 검증을 0.5일 기성 반영. UV/sampler/mip는 미완료. 완료 12.5일 + 진행 기성 2.5일, 잔여 252.5일 |
| 2026-09-06 | W6 발광/색공간·양 backend 78-case 완료. CEMC7 모델 14개와 하위 ID 310개 보존. 완료 12.5일 + 진행 기성 2일, 잔여 253일 |
| 2026-09-06 | W5 독립 AO·reflection texture table과 양 backend 48-case 완료. GBuffer 포맷 유지. 완료 11일 + 진행 기성 2일, 잔여 254.5일 |
| 2026-09-06 | W4 alpha/cutoff/doubleSided·Shadow Slang 구현과 양 backend 40-case 검증 완료. CEMC6 모델 14개 재게시·ID 보존. 완료 8.5일 + 진행 기성 2일, 잔여 257일 |
| 2026-09-06 | W0/W3 제품 캡처·neutral/strict exit 구현을 진행 기성에 반영. W2 native Slang 공용 평가와 양 backend 36-case HDR 검증 완료. 완료 6.5일 + 진행 기성 2일, 잔여 259일 |
| 2026-09-03 | 단일 PHASE 4를 4/4.25/4.75로 분할. 현재 PBR 배선 감사를 PBR-W, Blender Principled Material Graph를 MAT, renderer 품질을 RND로 재편. 구 단일 PBR 직선 레인 폐기. 총공수 267.5일 보존 |
| 2026-09-02 | 구 ModelImport I/V experiment 배선을 제거하고 PHASE 3.75로 분리 |
| 2026-09-01 | 최초 통합. BASE-0, Q0, RG/L/SRP/PBR 공수와 순서 정리 |
