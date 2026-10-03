# PHASE 4 계열 재배치 — C# 저작·그래프·기능 완료선

**정본 2026-10-03 · Material 구현 기반 보존 · MAT-7 공통 재질 통합 완료 · LX 마감/MAT-9 품질·성능 수용 잔여 · RND-ENV/GPU 상태 유지.**
이 문서는 PHASE 4 계열의 현재 소유권·표시 순서·공수 원장이다. 이전 Asset-first SRP
분할의 근거·이력은 [`Phase4UnifiedPlan.md`](Phase4UnifiedPlan.md)에 보존했다.
새 C# 저작/네이티브 실행 목표는
[`RenderPipelineTargetArchitecture.md`](../design/RenderPipelineTargetArchitecture.md)다.

## 1. 페이즈와 회계

| 표시 순서 | 단일 완료선 | 활성 행 | 재산정 인일 | 완료 | 잔여 인일 |
|---|---|---:|---:|---:|---:|
| **4** | 현행 DX12 PBR 제품 배선 | 10 | 18 | 18 | 0 |
| **4.25** | Graph→ShaderMeta/Slang·공통 재질·Blender 수용 | 11 | 40 + 미산정 | 32 | 8 + 미산정 |
| **4.3** | BASE-0 → DAG·버전/Modify·RHI queue·viewer, DX12 수용 | 12 | 100 | 30 | 70 |
| **4.5** | 모션/히스토리·업스케일·프레임 생성, DX12 수용 | 16 | 71 | 0 | 71 |
| **4.6** | C# Pipeline IR·PassSchema/Roslyn·native 조립 | 7 | 32 | 0 | 32 |
| **4.7** | UV1·BVH·DX12 백그라운드 라이트맵 | 8 | 35 | 2 | 33 |
| **4.75** | probe/AO·shadow·display/post·Environment | 4 | 28 | 0 | 28 |
| **4.8** | GPU-driven·확률 조명·DXR 설계·구현 공수 확정 | 4 | 9 | 0 | 9 |
| **4.9** | RenderDoc DX12/Vulkan 캡처 → 리소스 확인 → 픽셀별 비교 | 6 | 22 | 0 | 22 |
| **현재 합계** | | **78** | **355 + 미산정** | **82** | **273 + 미산정** |

**2026-10-01 사용자 지시 반영:** Vulkan 실행 비교·교차 동등성·픽셀 수용은 4.9가 단독 소유한다.
4~4.8은 RHI 중립 구현을 유지하면서 **DX12 Debug/Release의 변경 전후 회귀**로 닫는다.
4.9의 착수/완료나 기존 교차 오차를 앞선 페이즈의 선행·잔여·실패 조건으로 사용하지 않는다.

1인 전담 엔지니어의 계획 추정치이며 완료 기성 52일은 보존했다. 부분 구현 행은 임의로 완료
공수를 늘리지 않고 남은 완료 조건을 산정했다. 기존 317.5일은 미산정 범위가 빠진 부분 합계였다.
현재 355일은 Q0·RG-V·CSRP·RND-ENV·BP의 신규 산정 74일과 기존 범위 재산정 -36.5일을 반영한다.
숫자가 늘어난 것은 Vulkan 비교를 앞 페이즈에 유지해서가 아니다. 상세 ID별 변경·근거·중복 제외는
[RenderPhaseEffortEstimate.md](RenderPhaseEffortEstimate.md)가 공수 정본이다.

횡단 Lattice 8행(LX-0~6/LX-3H)은 Material 자체의 완료선이 아니므로 기존처럼 별도 미산정이며
이 77행/355일에 합산하지 않는다. GPU 기능의 실제 구현(GD/RT/HY), 셰이더 탐색 전환 실험,
새 하드웨어/미확인 결함 확대분도 미산정이다. **303일을 미산정 범위까지 포함한 전체 잔여로 읽지 않는다.**
기존 Asset-first 35일의 역사 원장은 재도입하지 않는다.

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
위 MAT-7 기반 설명은 당시 진행 기록이다. 2026-10-02 기존 LX generation/binding/쿠킹 3인일을 MAT-7-BASE 이력으로 보존한 뒤 Graph→ShaderMeta/Slang→LX 공통 재질 소비와 alpha/SSS/transmission/Volume Forward+ 혼합·제품 회귀를 완료했다. [최종 검증](../analysis/MAT7ForwardTransportComposition.md)을 따른다. 미산정 MAT-7의 완료 시간을 역산하지 않아 완료 기반 32인일과 MAT-9 잔여 8인일은 유지한다. LX 마감은 미산정이다. 현재 판정은 BlenderMaterialGraphPlan §0, 이전 노트는 archive/Phase425ImplementationHistory.md를 따른다.
[MaterialGraphSurfaceBatch.md](../design/MaterialGraphSurfaceBatch.md)의 graph GPU 평가 →
point buffer bake → bounded draw에서 UV/LOD·world frame·eye·typed override를 소비한다.
완료 readback acceptance와 recording 성공을 구분하고 invalid 지점에 진단 표식을 남긴다.
이 point bake의 Scene lookup 보간/예산·async 게시와 일반 해상도 route 설치는 후속이다.
[PrincipledLayeredSemantics.md](../design/PrincipledLayeredSemantics.md)를 따른다.
EEVEE가 사용하지 않는 anisotropy/thin film의 Cycles 비교·RGB 근사 수용과 제품 route/성능은 후속이다.

## 2. 선행 그래프

```text
PHASE 4 PBR-W9 → PHASE 4.25 현재 구현·검증 기반 (MAT-7 완료, LX/MAT-9 수용 잔여)
                            ↓
PHASE 4.3 BASE-0 → RG1(단일 writer DAG) → RG2(version/Modify DAG) → RG3~RG6(제품 전환)
                    ├─ PHASE 4.5 TR/TU/FG  (BASE-0 뒤부터 RG 본체와 병렬)
                    ├─ PHASE 4.6 CSRP-0/1  (native IR는 RG1 접근 계약부터 소비)
                    │                → CSRP-2~4 → CSRP-5(RG6 뒤) → CSRP-6
                    ├─ PHASE 4.7 L1/L2→L3→L4(Q0 소비)→L5/L6→L7
                    ├─ PHASE 4.75 RND-1 / RND-2 / RND-3 (각각 독립 품질 gate)
                    └─ PHASE 4.8 GPU-1/2/3 → GPU-9 (구상·공수 확정)
PHASE 4.9 BP-0→BP-1→BP-2→BP-3→BP-4→BP-5
  동일 입력 → RenderDoc 캡처 → 리소스 확인 → 픽셀별 비교 → 수정/재캡처 → 회귀 패키지
  각 기능의 DX12 기준선을 입력으로 받는다. 4~4.8로 돌아가는 선행 edge는 없다.
```

**2026-10-01 선행 조정:** BASE-0과 후속 구조 정리는 구현·검증된 MAT-0~MAT-8의 Material/Scene/cook 계약을 소비한다. MAT-9 최종 완료를 일괄 선행으로 받지 않는다. 현재 1024/4096 IBL 품질 설정과 이미지 수용 상한을 고정하고, 미달 항목은 baseline에 명시한다. BASE-0의 통과는 같은 입력의 재현·계측·회귀 검출 판정이며 MAT-9 품질/성능 수용을 뜻하지 않는다. GPU-driven 실제 구현과 측정 뒤 동일 장면의 이미지 오차·Debug/Release CPU/GPU 시간·근접 이동 끊김을 MAT-9에서 다시 판정한다. SSS·투과 품질 오차는 성능과 별도 완료 조건으로 남긴다.

**2026-10-02 BASE-0 완료 조건 정리:** BASE-0은 RenderGraph 변경 전 DX12 기준선이다.
2026-10-03 Lattice 저장·재생은 별도 선택 조건으로 분리한다. 카메라·draw 재생만으로 Lattice 검사를
실행하지 않으며 기본 기준선과 선택 확장은 독립 프로세스·결과로 판정한다. 확장 전용 결과는 BASE-0
최종 완료 증거로 받지 않는다. 실제 Lattice 재생 수용 검증은 선택 확장에 남긴다.
현재 소스/바이너리의 Debug/Release 빌드 신원, 고정 자산·장면·카메라·해상도·tuning·환경,
구성별 독립 프로세스 2개 × 캡처 2개의 이미지 및 구성 간 비교, 현행 graph 진단·변이,
CPU compile/record·GPU pass timing·VRAM 계측과 validation/정상 종료를 완료 조건으로 둔다.
IBL 1024/4096·기존 이미지 상한은 유지한다. 범용 frame packet·모든 재질/애니메이션 파일 재생은
선택적인 검증 도구 확장이며 BASE-0 완료 또는 RG1 착수의 필수 선행에서 제외한다.
기본 하네스의 한 구성 `complete`와 두 구성 최종 `phaseComplete`는 구분한다.
[정확한 완료 계약과 판정 도구](../analysis/RenderBase0Baseline.md#현재-완료-계약--2026-10-02-범위-정리)를 따른다.

2026-10-03 현행 Debug/Release에서 구성별 독립 프로세스 2개 × 캡처 2개를 검증하고 구성 간 16개 이미지
maxError=0, GPU validation/encoder drop/소스 변경 0, 정상 종료와 현재 해시를 확인했다.
`Build/Obj/RenderBase0/current-static-v4-phase-complete.json`의 `phaseComplete=true`로 BASE-0을 done·기성 4인일로
회수한다. 이전 재생 및 실패 기록은 보존한다. RG1도 명시적 단일 writer DAG·두 구성 native/GPU 검사 및 변경 전후 16개 이미지 maxError=0으로 완료했다. 제품 기본 순서는 RG5/6 이관 전까지 유지하며 RG2 version/Modify·RAW/WAR/WAW와 두 구성 240 shuffle/GPU·제품 회귀도 완료했다. RG3도 버전 producer 기준 컬링·정렬 후 수명/배리어와 두 구성 native/GPU·제품 회귀로 완료했다. 다음은 RG4 dependency wave 기반 병렬 기록·진단이다. [RG3 증거](../analysis/RenderRg3LifetimeBarriers.md)를 따른다. [RG2 증거](../analysis/RenderRg2Versions.md)를 따른다. [RG1 증거](../analysis/RenderRg1Scheduling.md)를 따른다.
Vulkan 비교·잔여 교차 오차는 [PHASE 4.9](BackendParityPlan.md), MAT-9 품질/실제 성능 수용은 해당 gate에 유지한다.

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
| 공통 node 저작·UI와 기존 graph 창 이관 | [`LatticeNodeSystem.md`](../design/LatticeNodeSystem.md), [`LatticeAdoptionPlan.md`](LatticeAdoptionPlan.md) — 독립 ImGui 예제 빌드·조작 게이트 통과. Editor 제품 통합은 별도 단계. 횡단 범위·공수 미산정이며 4.25의 40인일에 BT/Animator 이관을 포함하지 않음 |
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
  제품 frame의 DX12 변경 전후 픽셀·validation이 `RG6`에서 통과한다.
- **4.6:** C# `Build()`가 같은 19 node를 IR로 선언하고 native Pass를 선택한다.
  C++ 고정 조립과 동일한 sealed frame 결과, 관리 호출 스레드 격리, reload 실패 시
  마지막 정상 세대와 fence 수명이 증명된다.
- **4.7:** UV1 차트·BVH 정답·직접/간접광 수렴·Editor 비차단·취소/재개를
  `LightmapBakerPlan`의 독립 게이트로 판정한다.
- **4.75:** `RND-1` probe/AO, `RND-2` shadow, `RND-3` display/post는 각각
  Material parity와 별도 golden·성능 결과로 닫는다.
- **4.8:** `GPU-1~3`은 세 기능의 설계, `GPU-9`는 공통 자원·지원/폴백·최소 수직
  슬라이스·기능별 구현 공수를 확정한다. 아직 기능 구현 완료선은 아니다.

문서 재배치와 공수 추정은 빌드·GPU 런타임·픽셀 동등성의 새 증거가 아니다.
C#·4.9도 위의 계획 추정치를 사용하며 착수 시 실제 소비 표면·지원 장치 차이를 반영해 갱신한다.
Lattice 설계 초안과 UI 와이어프레임도 구현·검증 기성에 넣지 않는다.

## 2026-10-01 UI/환경 후속 추적

4.3 RG-V 4일과 4.75 RND-ENV 10일을 현재 원장에 산정했다. RG-V는 RG3부터 시작하고
RG6 제품 결과에서 닫으며 CSRP-5/6에서 같은 viewer로 C# IR을 표시한다. RND-ENV는
MAT-7 기반에서 배경/IBL/캐시 계약을 닫는다. W9(Phase21)와 14-LIVE/14-7(Phase14)은
이 4 계열 공수에 섞지 않는다. 정본은 [EditorRenderingSurfacesPlan.md](EditorRenderingSurfacesPlan.md),
[EnvironmentRenderingPlan.md](EnvironmentRenderingPlan.md)다.
