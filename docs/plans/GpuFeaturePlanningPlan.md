# GPU 기능 설계·구현 공수 확정 계획 (PHASE 4.8)

> 2026-10-20 정리 · master `27de3f5a02aadde7456aab680db3184c36ee735e` · 잔여 구현·수용 계획

판정 기준은 위 master의 커밋된 소스다. 작업 중인 로컬 코드 변경은 포함하지 않는다. 완료된 구현 단계·날짜별 실행 일지는 제거했다. 대시보드는 SRP 저작 전환 취소에 해당하는 항목만 제거하고, 나머지 상태·공수·링크를 보존한다.

[전체 문서 대조·충돌 판정](MasterPlanAudit20261020.html) · [정리 전 원문](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/docs/plans/GpuFeaturePlanningPlan.md)

## 2026-10-22 진척 재평가

[현재 작업 진척·제품 수용 평가](../analysis/Phase4Progress20261022.md)를 반영했다. 이미 있는 구현을 진행으로 표시하고 남은 제품 수용을 유지한다. progress에 자동 50% 완료 공수를 주지 않는다. 아래 추적 표 상태는 이번 평가를 반영한다.

## 남은 작업과 완료 판정

- GPU-1/2/3/9의 상세 설계·실험·지원/비용 예산을 확정한다. GD raster 구현, DXR 설계, 실제 RT/PT 제품 완료를 서로 구분한다.
- RT/PT/Hybrid 제품 구현과 연구는 PathTracingHybridPipelinePlan, 효과별 DXR 채택 실험은 DxrShaderTraversalExperimentPlan으로 단일화한다.

## 적용할 구조와 경계

`material document → compiled program → sealed scene inputs → material graph stages → RHI submission`

위 경로는 소스에서 확인한 구현 기반이다. 아래 요구의 수용 결과를 뜻하지 않는다. 구현을 다시 만드는 항목과 제품 검증을 혼합하지 않는다.

## 추적할 잔여 항목

| 추적 ID | 소유 단위 (잔여 작업은 위 목록) | 대시보드 보존 상태 |
|---|---|---|
| `GPU-1` · PHASE 4.8 | GPU-driven rendering 아키텍처 구상 | progress |
| `GPU-2` · PHASE 4.8 | Stochastic Tile-Based Lighting 구상 | todo |
| `GPU-3` · PHASE 4.8 | 공통 RT·PathTracing·Hybrid 설계 | progress |
| `GPU-9` · PHASE 4.8 | GPU 세 축의 의존 그래프·수직 슬라이스·구현 페이즈 확정 | progress |

대시보드 상태는 인계용 식별 정보다. 위 구현 기반에서 이미 충족한 하위 요구는 재작성하지 않고, 남은 제품/실패/비용 수용을 수행한다.

## master 소스 근거

모든 행의 파일·행 번호·Git blob은 고정 master에서 대조했다. 소스 존재·배선 확인과 실행 성공을 구분한다.

- [Engine/RenderEngine/Experiment/MaterialResolver.cpp:23](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Experiment/MaterialResolver.cpp#L23) — `bool NormalizeMaterialKeywordSelections(const Material& material,`
- [Engine/RenderEngine/LXMaterialPipeline.h:18](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/LXMaterialPipeline.h#L18) — `std::string dependencies, sealedProgramIdentity;`
- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp:90](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp#L90) — `using LiveGraphSnapshot = std::shared_ptr<const EnhancedRenderGraph::DiagnosticSnapshot>;`
- [Engine/RenderEngine/RenderScene.h:59](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/RenderScene.h#L59) — `bool BeginProxyFrame(uint64_t sceneEpoch);`

## 검증 경계

이번 정리는 master 소스·문서 전체의 소유권/충돌/링크/HTML 검증이다. 엔진 빌드·GPU 픽셀·SDK/장치·성능·실제 GUI 수용을 새로 실행한 결과가 아니다. 과거 통과는 해당 소스/바이너리 신원에만 유효하다. 남은 제품 수용은 위 요구를 실제 산출물로 닫아야 한다.

<details><summary>이전 절 링크 호환</summary>

이전 절의 완료 이력·구안은 제거했다. 이 링크로 들어온 경우 위의 남은 작업과 단일 소유 계획을 따른다.

<span id="2026-10-01-material-성능-게이트-회수-계약"></span>
<span id="2026-10-01-상세-배선-설계"></span>
<span id="2026-10-06-gd-구현-병합pr-123"></span>
<span id="2026-10-08-rtpthybrid-구현-페이즈-분리"></span>
<span id="sec-2026-10-01-material-성능-게이트-회수-계약"></span>
<span id="sec-2026-10-01-상세-배선-설계"></span>
<span id="sec-2026-10-08-rtpthybrid-구현-페이즈-분리"></span>
<span id="셰이더-탐색의-dxr-전환-가설-판정-2026-10-01"></span>

</details>
