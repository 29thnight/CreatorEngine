# GPU 기능 설계·구현 공수 확정 계획 (PHASE 4.8)

## 2026-10-08 RT/PT/Hybrid 구현 페이즈 분리

사용자 목표는 Path Tracing 환경과 기존 래스터에 RT 로직을 결합하는 Hybrid 환경의 구성이다. 실제 구현 정본은 [PHASE 4.85](PathTracingHybridPipelinePlan.md)로 신설하며 ReSTIR PT Enhanced와 AMD tetrahedral cages의 구현·평가·채택을 포함한다. 첫 directional hard shadow는 Hybrid의 초기 슬라이스로 유지한다. GPU-3는 두 환경의 공통 RT·재질·지원/fallback 설계를, GPU-9는 신규 RTP-0~13과 기존 RT0/RT1/HY0·EXP의 중복 없는 공수·지원·실행 순서 확정을 담당한다. GPU-2 확률 tile lighting은 ReSTIR PT와 별도 설계 축이다.

14행 전부 미착수·기성 0이며 중앙 추정 208인일(기본 환경 116 + 효과 확장/연구 92)을 별도 구현 페이즈에 산정했다. 기존 설계 4행 9인일은 유지한다. RTP-0/첫 prototype 뒤 GPU-9에서 추정을 재검토하며 이 공수 산정만으로 GPU-9를 완료하지 않는다. 기본 두 환경 구축은 필수이고 연구의 제품 채택은 oracle·품질·전체 CPU/GPU/VRAM 비용으로 판정한다. 외부 Spartan 코드는 정확한 버전/라이선스·상업 허가를 확인한 경우만 도입한다.

**2026-10-01 사용자 결정:** 이 페이즈의 실행·픽셀·성능 완료 판정은 DX12 Debug/Release다. RHI 중립 계약과 필요한 backend 구현은 유지한다. Vulkan 실행 비교·동등성·교차 픽셀 수용은 [PHASE 4.9](BackendParityPlan.md)의 RenderDoc 캡처 → 리소스 확인 → 픽셀별 비교가 단독 소유하며 이 페이즈의 선행·잔여·실패 조건으로 사용하지 않는다.


**2026-09-23 분리 · 활성 설계 4행 9일 · 실제 기능 구현 미산정.** 이 페이즈의
완료선은 세 GPU 기능의 구현이 아니라 지원/폴백·최소 수직 슬라이스·기능별 구현
공수를 확정하는 것이다. 현재 원장은 [`RenderPhaseRoadmap.md`](RenderPhaseRoadmap.md).

| ID | 독립 설계 축 | 산정 | 판정 산출물 |
|---|---|---:|---|
| `GPU-1` | GPU-driven visibility | 3일 | scene/view proxy → instance/meshlet 배치, Hi-Z, indirect draw, PSO 분류, CPU fallback과 필요한 RG/RHI 계약 |
| `GPU-2` | Stochastic Tile-Based Lighting | 2일 | tile 후보·표본·temporal/spatial reuse·denoise, light leaking 기준, Forward+/Deferred 공존과 fallback |
| `GPU-3` | DXR | 2일 | 첫 적용 효과 선정, BLAS/TLAS 수명, shader table, DX12/Vulkan RT 지원 여부, raster fallback |
| `GPU-9` | 교차 설계 게이트 | 2일 | 세 축의 공유 자원·선후, 지원 행렬, 기능별 최소 구현/회귀 슬라이스와 공수 확정 |

### 셰이더 탐색의 DXR 전환 가설 판정 (2026-10-01)

[DxrShaderTraversalExperimentPlan.md](DxrShaderTraversalExperimentPlan.md)의 EXP-0~4를
GPU-3 효과 선정과 GPU-9 설계 채택 게이트에 연결한다. Volume triangle 경계 탐색,
굴절 background 교차, SSR/SSGI 화면 공간 탐색, fog visibility를 각각 가설로 평가한다.
현재 전환은 미확정이며 실험은 미착수다. 정확도·AS build/update 포함 전체 CPU/GPU 비용·
VRAM·fallback을 대조해 효과별 채택/조건부/보류/기각을 결정한 뒤 상세 계약에 반영한다.
prototype/실측 공수는 `days: null`이고 기존 설계 4행 9일·완료 공수는 변경하지 않는다.

`GPU-1~3`은 설계 조사로 병렬화할 수 있다. `GPU-9`가 세 결과를 하나의 graph·RHI
계약에 대조한다. `GPU-9`는 PHASE 4.6 C# 저작, 4.7 라이트맵, 4.75 렌더러 품질의
완료 선행이 아니다. 제품 구현은 PHASE 4.3 `RG6`의 단일 큐 cutover와 필요한
`RG7~9`/`Q0` 계약을 항목별로 받는다. PHASE 4.5의 모션·히스토리도 필요한 기능만
소비한다.

지원되지 않는 하드웨어에서는 조용히 생략하지 않고 기능별 fallback 또는 명시적인
실행 불가 사유를 남긴다. 각 기능의 실제 구현·성능·DX12 런타임 게이트는
`GPU-9`에서 산정할 후속 슬라이스다. 현재 9일을 기능 구현 예산으로 사용하지 않는다.

## 2026-10-01 Material 성능 게이트 회수 계약

후속 설계/구현은 구현·검증된 MAT-0~MAT-8 기반을 소비하고 MAT-9 최종 완료를 일괄 선행으로 받지 않는다. GPU Scene 상주·변경분 갱신·재질/PSO 배칭·간접 드로우와 IBL 재사용/갱신 배선을 함께 설계한다. IBL은 환경/material/view generation과 invalidation·disocclusion·refinement 조건을 검증해야 하며 가시성 배칭만으로 IBL 적분 비용이 해결됐다고 판정하지 않는다. 1024/4096 품질 기준은 유지한다. 실제 구현 후 동일 장면 이미지 오차·Debug/Release CPU/GPU 시간·근접 이동 끊김을 측정해 MAT-9 성능 게이트에 반환한다. SSS·투과 품질은 별도 잔여 조건이다. 설계 9일과 실제 구현/측정 완료는 구분하며 후속 구현 공수는 미산정으로 유지한다.

## 2026-10-01 상세 배선 설계

상세 계약 정본은 [GpuDrivenMeshletDxrWiring.md](../design/GpuDrivenMeshletDxrWiring.md)다.
EnhancedSceneRenderer의 수명 조율과 기존 Raster Pass를 유지하고 공통 sealed geometry에서
Meshlet/Mesh Shader raster와 BLAS/TLAS/DXR 직접광 그림자를 별도 native Pass로 연결한다.
공통 세대/ABI, cook, 세 가시성 집합, bounded indirect, material route, RG/RHI 확장,
지원 행렬, 실패/abort/fence retirement, GD0~HY1 구현 슬라이스와 회귀 기준을 정의했다.

GPU-1/GPU-3는 상세 설계 작성 후 **진행**이다. 실제 capability probe와 baseline 수용은
미검증이므로 완료 처리하지 않는다. GPU-2는 미착수, GPU-9는 교차 설계·실측 예산·구현
공수 확정이 남는다. 후속 슬라이스의 공수는 `null`이며 현재 4행 9일·완료 0일을 유지한다.

### 2026-10-06 GD 구현 병합(PR #123)

GD0~GD3과 HY1 일부(정적 LOD·색인 스키닝 가시성)의 소스가 PR #123(`5ca4e82a`)으로
master 에 들어갔다. 병합 때 실행한 확인은 DX12 한 장면뿐이다: 메시렛 GBuffer 가 색인
경로와 비트 동일, 거친 LOD 전환, HZB 켬/끔 동일, DX12 검증 오류 0, `experiment.cooked`
466/466. `dx12.decal`·`dx12.rendergraph` 검사는 GPU 가시성 준비가 없어 예외로 끝나는
붉은 상태였으나 2026-10-07 visibility 준비·패스 수 fixture를 수정해 Debug/Release 두 명령 정상 종료·GPU validation 0으로 통과했다. 별도 재질 Decal probe도 각 3정책·342프레임 오차 0으로 통과했다. [검사 범위](../analysis/RenderRg5Closure20261007.md)는 전체 GPU-driven 수용과 구분한다. 슬라이스별 증거·잔여는
[구현 계획](GpuDrivenGeometryImplementationPlan.md#merge-and-first-execution-2026-10-06)이 정본이다.

이 병합은 GPU-1(설계)을 닫지 않는다. 기준선 성능·메모리 실측이 없고 GPU-9의 공수 산정
전에 구현이 먼저 들어왔으므로, GPU-9는 GD 실제 투입분을 사후 기록하고 남은
GD 수용·RT·HY 공수만 산정한다. 4행 9일·완료 0일은 그대로 둔다.

2026-10-01 재산정: GPU-1 3 + GPU-2 2 + GPU-3 2 + GPU-9 2 = 9인일. 기존 5.5일은 역사 산정이며 실제 구현/실험은 여전히 별도 미산정이다. [공수 근거](RenderPhaseEffortEstimate.md).
