# GPU-driven geometry implementation slices

> 2026-10-20 정리 · master `27de3f5a02aadde7456aab680db3184c36ee735e` · 잔여 구현·수용 계획

판정 기준은 위 master의 커밋된 소스다. 작업 중인 로컬 코드 변경은 포함하지 않는다. 완료된 구현 단계·날짜별 실행 일지는 제거했다. 대시보드는 SRP 저작 전환 취소에 해당하는 항목만 제거하고, 나머지 상태·공수·링크를 보존한다.

[전체 문서 대조·충돌 판정](MasterPlanAudit20261020.html) · [정리 전 원문](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/docs/plans/GpuDrivenGeometryImplementationPlan.md)

## 남은 작업과 완료 판정

- GD 소스 구현·첫 장면 증거 이후 CPU 정답 가시 집합, 용량 초과·카메라 cut/resize·material route 동등성과 Release 비용/VRAM을 수용한다.
- 현재 resident scene packets·visibility arenas·REAC primitive cache/instance admission을 기준으로 검증한다. 과거 CPU 전체 재작성 전제를 재도입하지 않는다. Vulkan 제품 비교는 BackendParityPlan에 남긴다.

## 적용할 구조와 경계

`material document → compiled program → sealed scene inputs → material graph stages → RHI submission`

위 경로는 소스에서 확인한 구현 기반이다. 아래 요구의 수용 결과를 뜻하지 않는다. 구현을 다시 만드는 항목과 제품 검증을 혼합하지 않는다.

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

<span id="authoring-and-storage-contract"></span>
<span id="goal-and-boundaries"></span>
<span id="initial-meshlet-authoring-usage"></span>
<span id="material-coverage-extension-current-source-checkpoint"></span>
<span id="merge-and-first-execution-2026-10-06"></span>
<span id="reviewed-incremental-sequence"></span>
<span id="runtime-checkpoint-1-source-integrated-execution-unverified"></span>
<span id="runtime-checkpoint-2-source-integrated-execution-unverified"></span>
<span id="sec-authoring-and-storage-contract"></span>
<span id="sec-goal-and-boundaries"></span>
<span id="sec-initial-meshlet-authoring-usage"></span>
<span id="sec-material-coverage-extension-current-source-checkpoint"></span>
<span id="sec-merge-and-first-execution-2026-10-06"></span>
<span id="sec-reviewed-incremental-sequence"></span>
<span id="sec-runtime-checkpoint-1-source-integrated-execution-unverified"></span>
<span id="sec-runtime-checkpoint-2-source-integrated-execution-unverified"></span>
<span id="sec-verification-status"></span>
<span id="verification-status"></span>

</details>
