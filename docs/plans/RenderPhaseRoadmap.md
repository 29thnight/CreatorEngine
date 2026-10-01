# PHASE 4 계열 재배치 — C# 저작·그래프·기능 완료선

**정본 2026-10-01 · MAT-0~MAT-8 완료 · MAT-9 rendered parity·성능 수용 잔여 · RND-ENV 배경 슬라이스 진행 · GPU-1/GPU-3 상세 설계 진행 · 그 외 기존 상태 유지.**
이 문서는 PHASE 4 계열의 현재 소유권·표시 순서·공수 원장이다. 이전 Asset-first SRP
분할의 근거·이력은 [`Phase4UnifiedPlan.md`](Phase4UnifiedPlan.md)에 보존했다.
새 C# 저작/네이티브 실행 목표는
[`RenderPipelineTargetArchitecture.md`](../design/RenderPipelineTargetArchitecture.md)다.

## 1. 페이즈와 회계

| 표시 순서 | 단일 완료선 | 활성 행 | 산정 일 | 완료 | 별도 미산정 |
|---|---|---:|---:|---:|---|
| **4** | 현행 DX12 PBR 제품 배선 | 10 | 18 | 18 | — |
| **4.25** | Material Graph·Principled/artist 계약 | 10 | 34 | 32 (`MAT-0~MAT-8`) | — |
| **4.3** | `BASE-0` → 단일 writer DAG → version/Modify DAG·RHI queue 기반 | 12 | 119 | 0 | `Q0`, `RG-V` |
| **4.5** | 모션/히스토리·업스케일·프레임 생성 | 16 | 86 | 0 | — |
| **4.6** | C# Pipeline IR·PassSchema/Roslyn·native 조립 | 7 | 미산정 | 0 | `CSRP-0~6` 전부 |
| **4.7** | UV1·BVH·백그라운드 라이트맵 | 8 | 35 | 2 | — |
| **4.75** | renderer 품질: probe/AO·shadow·display/post·Environment | 4 | 20 | 0 | `RND-ENV` |
| **4.8** | GPU-driven·확률 조명·DXR 설계 판정 | 4 | 5.5 | 0 | 세 기능의 실제 구현 |
| **4.9** | DX12/Vulkan PBR 교차 판정 복귀 | 0 | 미산정 | 0 | 시간 고정 이후 슬라이스 |
| **현재 합계** | | **71 (산정 62·미산정 9)** | **산정 317.5** | **산정 52** | **잔여 산정 265.5일 + 미산정** |

구 원장의 **69행 352.5일·완료 22일**에서 Asset-first `4-1` 2일과
`SRP-0/1/2/4/5/6` 33일, 총 7행 35일을 **새 C# 계획으로 이월하지 않고**
역사 산정으로 닫았다. 완료 2일도 옛 설계 계약의 증거이지 새 C# 계약의 진척이
아니므로 현재 완료에서 뺐다. 새 `CSRP-0~6` 7행을 활성 슬라이스로 추가했다.
나머지 항목은 ID·공수·상태를 보존한 순수 재배치다.
새 `CSRP-0~6`은 0일 구현이 아니라 미산정이다. 숫자가 작아진 것을 작업량 감소나
전체 기능 완료 예상으로 읽지 않는다.

MAT-3 완료는 공용 core 의미와 독립 GPU 8,280개 대조, 현행 Standard 제품 회귀 판정이다.
[PrincipledCoreSemantics.md](../design/PrincipledCoreSemantics.md)를 따른다.
비기본 IOR를 담는 제품 lookup/packing·graph binding은 MAT-6/MAT-7, Blender 교차 판정은 MAT-9에 남는다.
MAT-4는 Layered 공용 평가와 독립 GPU 216,776개 수치·독립성, energy bound/numeric golden 검증이다.
MAT-5는 Special 공용 평가·명시적 Forward 자원 계약과 독립 GPU 202,049개 수치·독립성,
energy bound/numeric golden 검증이다. [PrincipledSpecialSemantics.md](../design/PrincipledSpecialSemantics.md)의
diffusion·단일 경계 glass·homogeneous volume 근사 범위를 따른다.
MAT-6은 초기 지원 graph의 결정적 Slang/metadata·진단·실패 generation 복구와 독립 texture GPU 대조다.
[MaterialSlangCodegen.md](../design/MaterialSlangCodegen.md)를 따른다. 제품 reflection/pass adapter,
실제 자원/PSO/Scene 배선과 자동 cook route는 MAT-7에 남는다.
MAT-7의 reflection·candidate PSO·texture owner fence와 LXMC/CEMF·AssetCooker·encrypted
PAK 기반을 검증했다. [MaterialGraphProduct.md](../design/MaterialGraphProduct.md)를 따른다.
DataSystem/Material GUID generation·typed instance 저장 왕복/실패 복구를 연결하고
runtime 43개·제품 DataSystem Debug/Release 각 26개 검사로 검증했다.
제품 b2·texture·독립 sampler render binding adapter는 실제 엔진 DX12 오프스크린
draw에서 Debug/Release 각 168개 검사·64개 GPU 성분으로 검증했다. Scene 적용은 후속이다.
Core/Layered evaluated-point IBL bake와 별도 base/coat/sheen 응답 소비 draw도
Debug/Release 각 19,351개 검사·18,240개 GPU 성분을 통과했다.
[PrincipledIblBake.md](../design/PrincipledIblBake.md)의 Scene lookup 배치/보간/재사용,
환경 MIS/수렴·실시간 예산은 후속이다.
[MaterialGraphScenePacket.md](../design/MaterialGraphScenePacket.md)의 graph instance·coverage/queue·PSO·
IBL·recording binding을 함께 준비/게시하는 render owner와 실제 두 in-flight 제출의
교체·실패·abort·완료 해제를 검증했다. 이 render owner 검증 자체를 Scene pass 설치로 세지 않는다.
후속 [MaterialGraphSceneHost.md](../design/MaterialGraphSceneHost.md)는 실제 Scene GBuffer·
공유 depth·legacy/LX Masked 가림·Core/Layered HDR 합성에 bounded host를 연결했다.
현재 4,096픽셀·64 draw 이하의 정확도 기준용 IBL 적분 경로다. full-resolution lookup·
재사용/실시간 비용 수용, Special/투명 transport와 자동 Scene host 쿠킹은 남는다.
Debug/Release 각 830,436개 검사·GPU 659,914성분, 실제 합성 graph 24개·가시 3,582픽셀·
실패 거부 120개·GPU validation 0건을 확인했다. 실제 Editor Live Tick·Vulkan native 실행은 별도다.
위 MAT-7 기반 설명은 당시 진행 기록이다. 2026-09-29 제품 통합 종료 묶음과 2026-09-30 MAT-8 artist preview·비용/실패 안내를 완료했다. 현재 MAT 완료 공수는 32/34일이며 MAT-9 rendered parity·성능 수용은 남는다. 상세 현재 증거는 BlenderMaterialGraphPlan.md와 MaterialNodeEditor.md를 따른다.
[MaterialGraphSurfaceBatch.md](../design/MaterialGraphSurfaceBatch.md)의 graph GPU 평가 →
point buffer bake → bounded draw에서 UV/LOD·world frame·eye·typed override를 소비한다.
완료 readback acceptance와 recording 성공을 구분하고 invalid 지점에 진단 표식을 남긴다.
이 point bake의 Scene lookup 보간/예산·async 게시와 일반 해상도 route 설치는 후속이다.
[PrincipledLayeredSemantics.md](../design/PrincipledLayeredSemantics.md)를 따른다.
EEVEE가 사용하지 않는 anisotropy/thin film의 Cycles 비교·RGB 근사 수용과 제품 route/성능은 후속이다.

## 2. 선행 그래프

```text
PHASE 4 PBR-W9 → PHASE 4.25 MAT-0~MAT-8 구현·검증 기반
                            ↓
PHASE 4.3 BASE-0 → RG1(단일 writer DAG) → RG2(version/Modify DAG) → RG3~RG6(제품 전환)
                    ├─ PHASE 4.5 TR/TU/FG  (BASE-0 뒤부터 RG 본체와 병렬)
                    ├─ PHASE 4.6 CSRP-0/1  (native IR는 RG1 접근 계약부터 소비)
                    │                → CSRP-2~4 → CSRP-5(RG6 뒤) → CSRP-6
                    ├─ PHASE 4.7 L1/L2→L3→L4(Q0 소비)→L5/L6→L7
                    ├─ PHASE 4.75 RND-1 / RND-2 / RND-3 (각각 독립 품질 gate)
                    └─ PHASE 4.8 GPU-1/2/3 → GPU-9 (구상·공수 확정)
PHASE 4.9는 PHASE 4에서 미룬 교차 백엔드 판정. 시각 고정 뒤 별도 진행.
```

**2026-10-01 선행 조정:** BASE-0과 후속 구조 정리는 구현·검증된 MAT-0~MAT-8의 Material/Scene/cook 계약을 소비한다. MAT-9 최종 완료를 일괄 선행으로 받지 않는다. 현재 1024/4096 IBL 품질 설정과 이미지 수용 상한을 고정하고, 미달 항목은 baseline에 명시한다. BASE-0의 통과는 같은 입력의 재현·계측·회귀 검출 판정이며 MAT-9 품질/성능 수용을 뜻하지 않는다. GPU-driven 실제 구현과 측정 뒤 동일 장면의 이미지 오차·Debug/Release CPU/GPU 시간·근접 이동 끊김을 MAT-9에서 다시 판정한다. SSS·투과 품질 오차는 성능과 별도 완료 조건으로 남긴다.

번호는 대시보드의 **표시 순서**다. 모든 페이즈가 앞 번호의 전체 완료를 기다리는
직선은 아니다. 특히 4.5는 `BASE-0`만 받고 RG 본체와 병렬이며, 4.7/4.75/4.8의
native 기능은 4.6 C# 저작 완료를 일괄 선행으로 받지 않는다. 제품 결과를 해당
Pass/graph에 얹을 때의 `RG6`, `Q0`, `MAT-9`, 시간축 입력은 **항목별**로 단다.
`RND-1 → RND-2`의 기존 계획 표기는 구현 의존 근거가 확인되지 않아 두 품질 축의
페이즈 전체 선행에서 제외하고 각 독립 gate로 관리한다.

## 3. 정본 경계

| 범위 | 계획/결정 |
|---|---|
| C#은 구성·설정, C++은 Pass 실행, RG는 의존성·동기화 | [`RenderPipelineTargetArchitecture.md`](../design/RenderPipelineTargetArchitecture.md) |
| 단일 writer DAG → version/Modify DAG → 제품 cutover → alias/queue | [`RenderGraphDependencySchedulingPlan.md`](RenderGraphDependencySchedulingPlan.md) |
| typed C# builder → immutable IR → native 조립 → Roslyn/제품 cutover | [`CSharpRenderPipelinePlan.md`](CSharpRenderPipelinePlan.md) |
| 현행 PBR 배선 | [`PBRWiringStabilizationPlan.md`](PBRWiringStabilizationPlan.md) |
| Material Graph | [`BlenderMaterialGraphPlan.md`](BlenderMaterialGraphPlan.md) |
| 공통 node 저작·UI와 기존 graph 창 이관 | [`LatticeNodeSystem.md`](../design/LatticeNodeSystem.md), [`LatticeAdoptionPlan.md`](LatticeAdoptionPlan.md) — 독립 ImGui 예제 빌드·조작 게이트 통과. Editor 제품 통합은 별도 단계. 횡단 범위·공수 미산정이며 4.25의 34일에 BT/Animator 이관을 포함하지 않음 |
| 시간축 | [`TemporalReconstructionPlan.md`](TemporalReconstructionPlan.md) |
| 라이트맵 | [`LightmapBakerPlan.md`](LightmapBakerPlan.md) |
| renderer probe/AO·shadow·display/post·Environment | [`RendererQualityPlan.md`](RendererQualityPlan.md) |
| GPU-driven·확률 조명·DXR 설계 | [`GpuFeaturePlanningPlan.md`](GpuFeaturePlanningPlan.md) |
| Meshlet/Mesh Shader raster + DXR 상세 배선 계약 | [`GpuDrivenMeshletDxrWiring.md`](../design/GpuDrivenMeshletDxrWiring.md) — GPU-1/GPU-3 설계 작성·진행, 구현/실측 미검증 |
| 셰이더 탐색 DXR 전환의 가설·채택 실험 | [`DxrShaderTraversalExperimentPlan.md`](DxrShaderTraversalExperimentPlan.md) — GPU-3/GPU-9 판정 입력, 실험 미착수·공수 미산정 |
| 백엔드 패리티 | [`BackendParityPlan.md`](BackendParityPlan.md) |
| 분리 전 4.75 항목 감사 | [`Phase475AxisClassification.md`](../analysis/Phase475AxisClassification.md) |

## 4. 완료선 분리와 검증

- **4.3:** 잘못된 producer/version/order 변이가 graph compile에서 실패하고, 기본
  제품 frame의 DX12/Vulkan 픽셀·validation이 `RG6`에서 통과한다.
- **4.6:** C# `Build()`가 같은 19 node를 IR로 선언하고 native Pass를 선택한다.
  C++ 고정 조립과 동일한 sealed frame 결과, 관리 호출 스레드 격리, reload 실패 시
  마지막 정상 세대와 fence 수명이 증명된다.
- **4.7:** UV1 차트·BVH 정답·직접/간접광 수렴·Editor 비차단·취소/재개를
  `LightmapBakerPlan`의 독립 게이트로 판정한다.
- **4.75:** `RND-1` probe/AO, `RND-2` shadow, `RND-3` display/post는 각각
  Material parity와 별도 golden·성능 결과로 닫는다.
- **4.8:** `GPU-1~3`은 세 기능의 설계, `GPU-9`는 공통 자원·지원/폴백·최소 수직
  슬라이스·기능별 구현 공수를 확정한다. 아직 기능 구현 완료선은 아니다.

문서 재배치는 빌드·GPU 런타임·픽셀 동등성의 새 증거가 아니다. 새 C# 범위와
PHASE 4.9는 실제 소비 표면을 실측한 뒤 공수를 산정한다.
Lattice 설계 초안과 UI 와이어프레임도 구현·검증 기성에 넣지 않는다.

## 2026-10-01 UI/환경 후속 추적

4.3 RG-V와 4.75 RND-ENV를 각각 미산정 활성 행으로 추가했다. 기존 산정 317.5일·완료
52일은 유지한다. RG-V는 RG3부터 시작하고 RG6 제품 결과에서 닫으며 CSRP-5/6에서 같은
viewer로 C# IR을 표시한다. RND-ENV는 MAT-7 기반에서 배경/IBL/캐시 계약을 닫는다.
W9(Phase21)와 14-LIVE/14-7(Phase14)은 이 4 계열 공수에 섞지 않는다.
정본은 [EditorRenderingSurfacesPlan.md](EditorRenderingSurfacesPlan.md),
[EnvironmentRenderingPlan.md](EnvironmentRenderingPlan.md)다.
