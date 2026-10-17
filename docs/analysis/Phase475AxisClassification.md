# PHASE 4.75 축 분류 — 독립 완료선 분리 전 점검

> **분리 전 시점 기록.** 아래 22행·95.5일과 `Phase4UnifiedPlan.md` 정본 표기는
> 작성 당시의 상태다. 실제 분리 이후 페이즈·공수 정본은
> [`RenderPhaseRoadmap.md`](../plans/RenderPhaseRoadmap.md)로 이동했다.

**2026-09-23 · 계획/현재 소스 대조 · 분류 결과.** 순서·공수·상태의 정본은
[`Phase4UnifiedPlan.md`](../plans/Phase4UnifiedPlan.md) §8~§10이다. 이 문서는 페이즈 번호를
새로 배정하거나 구현 완료를 선언하지 않는다. 기존 22개 활성 행 95.5일(완료 4일)을
어떤 완료선으로 나눌 수 있는지 판별한다. `SRP-3`은 0일 중단 이력으로 활성 합계에서 뺀다.

## 1. 분류 원칙

1. 같은 제품 결과와 같은 회귀 게이트로 닫히는 작업만 한 축에 둔다. 공용 인프라는 가장 먼저
   필요한 소유 페이즈에 한 번만 두고 나머지는 계약을 소비한다.
2. **구현 선행**(해당 API·데이터가 없으면 실행 불가), **판정 선행**(기능은 개발할 수 있지만
   acceptance를 고정하려면 필요), **순서 선호**를 구분한다. 이 셋을 모두 페이즈 전체의
   직렬 의존으로 바꾸지 않는다.
3. 설계 공수와 구현 공수를 합치지 않는다. `GPU-1~GPU-3`과 `GPU-9`의 5.5일은 구상·판정
   공수다. GPU 기능 구현은 아직 미산정이다.
4. 공통 `BASE-0`, `RG5/RG6`, `Q0`의 소유권은 PHASE 4.3에 남긴다. PHASE 4.5의 `TR`
   결과와 PHASE 4.25의 `MAT` 결과도 각각 입력으로 받는다.

## 2. 분리 가능한 완료선

| 축 | 기존 항목 | 활성 행 / 일 | 완료 | 독립 산출물과 판정 | 실제 선행 경계 |
|---|---|---:|---:|---|---|
| **SRP 저작·확장** | `4-1`, `SRP-0~SRP-2`, `SRP-4~SRP-6` | 7 / 35 | 2일 | Pipeline Asset과 Pass Stack, pass-domain Graph/Code, Native Pass를 제품 픽셀·schema/실패 fixture로 판정 | `BASE-0`; `SRP-1`은 `RG5`, `SRP-4`는 `MAT-6`; 제품 전환은 `RG6` 뒤 |
| **라이트맵 베이킹** | `L0~L7` | 8 / 35 | 2일 | UV1·BVH·직접/간접광과 백그라운드 베이크를 차트, 수렴, 중단/재개, Editor 응답성으로 판정 | `L1`은 PHASE 3.75 authoring/vertex schema, `L4`는 `Q0`; 제품 전환은 `RG6` 뒤 |
| **반사·차폐 품질** | `RND-1` | 1 / 4 | 0 | local reflection probe와 specular AO의 별도 golden·성능 판정 | `MAT-9`의 pre-tone material 기준선은 판정 선행 |
| **그림자 품질** | `RND-2` | 1 / 10 | 0 | cascade/point/spot atlas와 tier별 shadow golden·성능 판정 | 현 계획의 `RND-1` 선행은 코드/API 선행 근거가 확인되지 않아 재검토 대상 |
| **표시·후처리** | `RND-3` | 1 / 6 | 0 | OETF·AgX·auto exposure·bloom의 순서와 각 독립 golden·성능 판정 | `BASE-0`; `MAT-9` pre-tone 기준은 후처리 acceptance의 판정 선행 |
| **GPU 가시성** | `GPU-1` | 1 / 2 | 0 | GPU-driven 입력·컬링·indirect·CPU fallback 설계 | `BASE-0`과 PHASE 3.75 모델/프록시 계약; 구현 공수 미산정 |
| **확률 조명** | `GPU-2` | 1 / 1.5 | 0 | stochastic sampling·reuse·denoise·누출/노이즈·폴백 설계 | 설계는 `BASE-0` 뒤 가능; temporal reuse 구현은 PHASE 4.5 모션/히스토리 입력 필요 |
| **Ray tracing** | `GPU-3` | 1 / 1.5 | 0 | 첫 DXR 효과·BLAS/TLAS·RHI/graph 계약·raster fallback 설계 | `BASE-0`; Vulkan RT 지원 여부는 지원 행렬에서 별도 판정 |
| **교차 축 설계 게이트** | `GPU-9` | 1 / 0.5 | 0 | 위 세 GPU 후보의 공유 자원·선후·최소 수직 슬라이스·구현 공수 결정 | `GPU-1~GPU-3` 뒤. 라이트맵·SRP·renderer 품질의 완료 게이트는 아님 |
| **합계** |  | **22 / 95.5** | **4일** |  |  |

`SRP-3`은 PHASE 4.25 `MAT-2/MAT-6`에 흡수된 이력 포인터이므로 SRP 축의 새 공수나
완료 조건으로 계산하지 않는다. `GPU-9`를 별도 축으로 둔 것은 세 GPU 기능을 한 구현
페이즈로 확정했다는 뜻이 아니다. 각 기능의 첫 수직 슬라이스와 공수를 산정한 다음 독립
구현 완료선이 필요한지 결정한다.

## 3. 경계에서 발견한 재검토 항목

| 항목 | 현재 계획의 연결 | 분리 전 판정 |
|---|---|---|
| `RND-1 → RND-2` | 반사 probe 뒤에 그림자를 둠 | 그림자 구현은 `EnhancedShadowPass::Declare`의 별도 graph pass다. 반사 probe의 출력/API를 받는 연결이 계획·현재 소스에서 확인되지 않는다. 순서 선호인지 실제 선행인지 확인하고, 근거가 없으면 그림자 축을 독립시킨다. |
| `RND-3` / `MAT-9` | 표에는 `BASE-0`, 설명에는 pre-tone material golden 뒤 | 표시 기능 개발과 재질 기준선 오염 방지용 판정 순서를 분리해 적어야 한다. `MAT-9`를 renderer 소유권으로 옮기지 않는다. |
| `SRP-6` fixture | 기존 계획은 DXR **또는** 업스케일러 Native Pass 예시 | DXR 구현을 SRP 전체의 필수 선행으로 만들지 않는다. 연결된 SRP 계획은 소스 Native Pass fixture로 registry를 증명하도록 정리했다. 실제 fixture는 구현 착수 전에 고른다. |
| `GPU-9` | SRP 계약과 GPU 세 기능을 한 의존 그래프로 합침 | 공유 resource/feature 입력의 설계 인터페이스만 묶는다. 세 기능의 실장·지원 행렬·fallback·release gate는 각 축에서 독립 판정한다. |
| `L4` / `Q0` | 라이트맵이 별도 COMPUTE 큐를 요구 | `Q0`는 PHASE 4.3의 RHI queue/fence 계약이다. `L4`는 소비자이며 새 queue 계층을 세우지 않는다. `Q0` 0일 표시는 완료가 아니라 미산정이다. |

현재 소스 확인 지점: [`EnhancedSceneRenderer.cpp`](../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp)에는
제품 `AddNode` 조립과 graph compile이 있고,
[`EnhancedRenderGraph.cpp::BuildOrder`](../../Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp)는
현재 순서 계약의 소유자다. 그림자는
[`EnhancedShadowPass.cpp::Declare`](../../Engine/RenderEngine/Render/Passes/Geometry/EnhancedShadowPass.cpp)에서
graph pass로 제출한다. Slang 컴파일 기반은
[`RHIShaderCompiler.cpp`](../../Engine/RenderEngine/RHI/RHIShaderCompiler.cpp)에 있으며,
라이트맵은 현재 [`LightMapping.h`](../../Engine/RenderEngine/Interfaces/LightMapping.h)
저작 데이터가 남은 상태다. GPU-driven·stochastic·DXR 구현을 이번 분류에서 완료로 세지 않았다.

## 4. 다음 재배치에서 지킬 것

- **바로 독립 완료선 후보:** SRP, 라이트맵, 반사·차폐, 그림자, 표시·후처리.
  항목 ID와 기존 공수는 그대로 옮기고 각 축의 회귀 결과를 별도로 닫는다.
- **설계 뒤 구현 완료선 결정:** GPU 가시성, 확률 조명, ray tracing. `GPU-9`에서
  공통 인터페이스와 기능별 수직 슬라이스·공수를 확정한 뒤 구현 페이즈를 배정한다.
- **페이즈 번호보다 의존 그래프 우선:** `BASE-0`/`RG5`/`RG6`/`Q0`와 `MAT`/`TR` 중
  실제 소비 항목만 선행으로 단다. `GPU-9`를 SRP·L·RND 전체의 완료 선행으로 삼지 않는다.
- **재배치 회계:** 현재 22행 95.5일과 완료 4일은 순수 이동 시 보존한다. `Q0`와 GPU
  기능 구현처럼 미산정인 작업은 임의의 0일 구현으로 처리하지 않고 별도 산정한다.

이 분류는 문서/소스 정적 대조다. 새 제품 경로의 빌드·GPU 런타임·픽셀 회귀 증거는 아니다.
