# PHASE 4 계열 재배치 — C# 저작·그래프·기능 완료선

**정본 2026-09-28 · MAT-0 Blender 기준선 완료 · 나머지 미착수 항목은 그대로 미착수.**
이 문서는 PHASE 4 계열의 현재 소유권·표시 순서·공수 원장이다. 이전 Asset-first SRP
분할의 근거·이력은 [`Phase4UnifiedPlan.md`](Phase4UnifiedPlan.md)에 보존했다.
새 C# 저작/네이티브 실행 목표는
[`RenderPipelineTargetArchitecture.md`](../design/RenderPipelineTargetArchitecture.md)다.

## 1. 페이즈와 회계

| 표시 순서 | 단일 완료선 | 활성 행 | 산정 일 | 완료 | 별도 미산정 |
|---|---|---:|---:|---:|---|
| **4** | 현행 DX12 PBR 제품 배선 | 10 | 18 | 18 | — |
| **4.25** | Material Graph·Principled/artist 계약 | 10 | 34 | 2 (`MAT-0`) | — |
| **4.3** | `BASE-0` → 단일 writer DAG → version/Modify DAG·RHI queue 기반 | 11 | 119 | 0 | `Q0` |
| **4.5** | 모션/히스토리·업스케일·프레임 생성 | 16 | 86 | 0 | — |
| **4.6** | C# Pipeline IR·PassSchema/Roslyn·native 조립 | 7 | 미산정 | 0 | `CSRP-0~6` 전부 |
| **4.7** | UV1·BVH·백그라운드 라이트맵 | 8 | 35 | 2 | — |
| **4.75** | renderer 품질: probe/AO·shadow·display/post | 3 | 20 | 0 | — |
| **4.8** | GPU-driven·확률 조명·DXR 설계 판정 | 4 | 5.5 | 0 | 세 기능의 실제 구현 |
| **4.9** | DX12/Vulkan PBR 교차 판정 복귀 | 0 | 미산정 | 0 | 시간 고정 이후 슬라이스 |
| **현재 합계** | | **69 (산정 62·미산정 7)** | **산정 317.5** | **산정 22** | **잔여 산정 295.5일 + 미산정** |

구 원장의 **69행 352.5일·완료 22일**에서 Asset-first `4-1` 2일과
`SRP-0/1/2/4/5/6` 33일, 총 7행 35일을 **새 C# 계획으로 이월하지 않고**
역사 산정으로 닫았다. 완료 2일도 옛 설계 계약의 증거이지 새 C# 계약의 진척이
아니므로 현재 완료에서 뺐다. 새 `CSRP-0~6` 7행을 활성 슬라이스로 추가했다.
나머지 항목은 ID·공수·상태를 보존한 순수 재배치다.
새 `CSRP-0~6`은 0일 구현이 아니라 미산정이다. 숫자가 작아진 것을 작업량 감소나
전체 기능 완료 예상으로 읽지 않는다.

## 2. 선행 그래프

```text
PHASE 4 PBR-W9 → PHASE 4.25 MAT-9
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
| renderer probe/AO·shadow·display/post | [`RendererQualityPlan.md`](RendererQualityPlan.md) |
| GPU-driven·확률 조명·DXR 설계 | [`GpuFeaturePlanningPlan.md`](GpuFeaturePlanningPlan.md) |
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
