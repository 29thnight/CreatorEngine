# 셰이더 교차 탐색의 DXR 전환 가설·채택 실험

> 2026-10-20 정리 · master `27de3f5a02aadde7456aab680db3184c36ee735e` · 잔여 구현·수용 계획

판정 기준은 위 master의 커밋된 소스다. 작업 중인 로컬 코드 변경은 포함하지 않는다. 완료된 구현 단계·날짜별 실행 일지는 제거했다. 대시보드는 SRP 저작 전환 취소에 해당하는 항목만 제거하고, 나머지 상태·공수·링크를 보존한다.

[전체 문서 대조·충돌 판정](MasterPlanAudit20261020.html) · [정리 전 원문](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/docs/plans/DxrShaderTraversalExperimentPlan.md)

## 남은 작업과 완료 판정

- EXP-0~4: baseline을 봉인한 뒤 Volume 경계·굴절·SSR/SSGI·fog visibility를 각각 독립 DXR 가설로 실험한다. hit/radiance/transport 의미를 분리한다.
- 공유 AS의 update/rebuild·변형·멀티뷰·in-flight 수명과 전체 GPU/VRAM 비용으로 채택/조건부/보류/기각을 판정한다. 예산은 RTP-0/5/13에 중복 합산하지 않는다.

## 적용할 구조와 경계

`material document → compiled program → sealed scene inputs → material graph stages → RHI submission`

위 경로는 소스에서 확인한 구현 기반이다. 아래 요구의 수용 결과를 뜻하지 않는다. 구현을 다시 만드는 항목과 제품 검증을 혼합하지 않는다.

## 추적할 잔여 항목

| 추적 ID | 소유 단위 (잔여 작업은 위 목록) | 대시보드 보존 상태 |
|---|---|---|
| `GPU-3` · PHASE 4.8 | 공통 RT·PathTracing·Hybrid 설계 | progress |
| `GPU-9` · PHASE 4.8 | GPU 세 축의 의존 그래프·수직 슬라이스·구현 페이즈 확정 | todo |

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

<span id="1-현재-근거와-검증-가능한-가설"></span>
<span id="2-실행-순서선행산출물"></span>
<span id="2026-10-08-제품-페이즈-연결"></span>
<span id="3-비교-조건과-크기-sweep"></span>
<span id="4-정확도-계약"></span>
<span id="5-비용-판정과-설계-반영"></span>
<span id="sec-1-현재-근거와-검증-가능한-가설"></span>
<span id="sec-2-실행-순서선행산출물"></span>
<span id="sec-2026-10-08-제품-페이즈-연결"></span>
<span id="sec-3-비교-조건과-크기-sweep"></span>
<span id="sec-4-정확도-계약"></span>
<span id="sec-5-비용-판정과-설계-반영"></span>
<span id="sec-연결-문서"></span>
<span id="연결-문서"></span>

</details>
