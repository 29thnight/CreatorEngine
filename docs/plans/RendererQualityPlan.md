# 렌더러 품질 계획 (PHASE 4.75)

**2026-09-23 분리 · 활성 4행, 기존 산정 20일 + RND-ENV 미산정. 기존 RND-1~3 미착수, RND-ENV 배경 토글 슬라이스 진행.** 현재 소유권과 공수 원장은
[`RenderPhaseRoadmap.md`](RenderPhaseRoadmap.md)다. 이 계획은 재질 자체의 Principled
의미가 아니라 렌더러의 반사·그림자·표시 결과를 소유한다.

| ID | 범위 | 산정 | 선행과 완료 게이트 |
|---|---|---:|---|
| `RND-1` | local reflection probe, specular AO | 4일 | `MAT-9` 뒤 probe 자산·캡처·바인딩과 AO의 renderer golden·성능 판정 |
| `RND-2` | 공용 shadow sampling, point/spot atlas, quality tier | 10일 | cascade/point/spot의 각 품질 단계와 masked·normal-offset·far fade 픽셀/성능 판정 |
| `RND-3` | display OETF, AgX, auto exposure, bloom | 6일 | pre-tone material golden 뒤 표시 변환·post 순서와 효과별 golden·성능 판정 |
| `RND-ENV` | Environment 배경과 diffuse/specular IBL의 독립 설정·제품 소비 | 미산정 | `MAT-7` 기반, W9 resolver·RND-1 probe 우선순위 공유. [ENV-A~E](EnvironmentRenderingPlan.md)의 쿠킹/캐시·전체 route·픽셀/성능 gate |

기존 세 행의 게이트는 독립적으로 기록한다. `RND-1 → RND-2`는 현재 구현 선행 근거가
없어 페이즈 전체 의존으로 묶지 않는다. `MAT-9`의 재질 출력, PHASE 4.3의 compiled
graph, 필요한 시간축 입력은 소비하지만 이 계획에서 각각의 계약을 재정의하지 않는다.
C# IR로 각 Pass를 선택하는 제품 저작 전환은
[`CSharpRenderPipelinePlan.md`](CSharpRenderPipelinePlan.md)가 소유한다. native Pass의
품질 게이트를 C# 전환의 완료로 대신하지 않는다.

각 행은 동일한 밀봉 입력에서 effect off/on·DX12/Vulkan의 지원 범위·픽셀과 GPU
비용을 기록한다. 현재 20일은 기존 분류에서 이관한 산정이고, 문서 분리는 구현 또는
런타임 검증의 증거가 아니다.

## RND-ENV — 배경과 IBL 환경 설정

2026-10-01 추가. progress/days:null. 배경 표시·세기·fallback 색과 diffuse/specular IBL의
독립 enable/intensity, default HDRI/profile 참조, Standard/LX·Deferred/Forward·Editor/Player
동일 의미와 캐시/쿠킹·픽셀/비용을 [EnvironmentRenderingPlan.md](EnvironmentRenderingPlan.md)의
ENV-A~E로 완료 판정한다. 현재 중립 회색 fallback·기본 forest의 배경 숨김·명시적 환경 선택 시
자동 표시와 중립 Grid가 적용됐다. 색 편집/profile resolver·IBL weight 전체 지원은 후속 gate다.
