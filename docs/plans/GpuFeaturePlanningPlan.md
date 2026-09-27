# GPU 기능 설계·구현 공수 확정 계획 (PHASE 4.8)

**2026-09-23 분리 · 활성 설계 4행 5.5일 · 실제 기능 구현 미산정.** 이 페이즈의
완료선은 세 GPU 기능의 구현이 아니라 지원/폴백·최소 수직 슬라이스·기능별 구현
공수를 확정하는 것이다. 현재 원장은 [`RenderPhaseRoadmap.md`](RenderPhaseRoadmap.md).

| ID | 독립 설계 축 | 산정 | 판정 산출물 |
|---|---|---:|---|
| `GPU-1` | GPU-driven visibility | 2일 | scene/view proxy → instance/meshlet 배치, Hi-Z, indirect draw, PSO 분류, CPU fallback과 필요한 RG/RHI 계약 |
| `GPU-2` | Stochastic Tile-Based Lighting | 1.5일 | tile 후보·표본·temporal/spatial reuse·denoise, light leaking 기준, Forward+/Deferred 공존과 fallback |
| `GPU-3` | DXR | 1.5일 | 첫 적용 효과 선정, BLAS/TLAS 수명, shader table, DX12/Vulkan RT 지원 여부, raster fallback |
| `GPU-9` | 교차 설계 게이트 | 0.5일 | 세 축의 공유 자원·선후, 지원 행렬, 기능별 최소 구현/회귀 슬라이스와 공수 확정 |

`GPU-1~3`은 설계 조사로 병렬화할 수 있다. `GPU-9`가 세 결과를 하나의 graph·RHI
계약에 대조한다. `GPU-9`는 PHASE 4.6 C# 저작, 4.7 라이트맵, 4.75 렌더러 품질의
완료 선행이 아니다. 제품 구현은 PHASE 4.3 `RG6`의 단일 큐 cutover와 필요한
`RG7~9`/`Q0` 계약을 항목별로 받는다. PHASE 4.5의 모션·히스토리도 필요한 기능만
소비한다.

지원되지 않는 하드웨어에서는 조용히 생략하지 않고 기능별 fallback 또는 명시적인
실행 불가 사유를 남긴다. 각 기능의 실제 구현·성능·DX12/Vulkan 런타임 게이트는
`GPU-9`에서 산정할 후속 슬라이스다. 현재 5.5일을 기능 구현 예산으로 사용하지 않는다.
