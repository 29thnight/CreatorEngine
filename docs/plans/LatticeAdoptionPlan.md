# Lattice (LX) 머테리얼 우선 도입 계획

> 2026-10-20 정리 · master `27de3f5a02aadde7456aab680db3184c36ee735e` · 잔여 구현·수용 계획

판정 기준은 위 master의 커밋된 소스다. 작업 중인 로컬 코드 변경은 포함하지 않는다. 완료된 구현 단계·날짜별 실행 일지는 제거했다. 대시보드는 SRP 저작 전환 취소에 해당하는 항목만 제거하고, 나머지 상태·공수·링크를 보존한다.

[전체 문서 대조·충돌 판정](MasterPlanAudit20261020.html) · [정리 전 원문](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/docs/plans/LatticeAdoptionPlan.md)

## 남은 작업과 완료 판정

- LX-1/3/3H: Material 창의 열린 LXDocument 수명·인증 HTTP 편집·revision 충돌·Undo/Redo·저장/재개방을 같은 문서 모델로 수용한다.
- LX-4/5/6: Animator/BT 새 저작 창과 잔은 imgui-node-editor 의존 제거를 별도로 수행한다. Material 전용 METEOR와 횡단 LX 기술의 범위를 구분한다.

## 적용할 구조와 경계

`material document → compiled program → sealed scene inputs → material graph stages → RHI submission`

위 경로는 소스에서 확인한 구현 기반이다. 아래 요구의 수용 결과를 뜻하지 않는다. 구현을 다시 만드는 항목과 제품 검증을 혼합하지 않는다.

## 추적할 잔여 항목

| 추적 ID | 소유 단위 (잔여 작업은 위 목록) | 대시보드 보존 상태 |
|---|---|---|
| `LX-1` · PHASE 4.25 | Lattice 주석·Reroute 편집 계약 마감 | progress |
| `LX-3` · PHASE 4.25 | Material 그룹·Blackboard 제품 편집 마감 | progress |
| `LX-3H` · PHASE 4.25 | 열린 Material 문서 HTTP 수명 게이트 마감 | progress |
| `LX-4` · PHASE lattice-followup | Animator 새 저작 창 LX 적용 | todo |
| `LX-5` · PHASE lattice-followup | Behavior Tree 새 저작 창 LX 적용 | todo |
| `LX-6` · PHASE lattice-followup | imgui-node-editor 의존성 제거 | todo |

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

<span id="2026-09-29-editor-material-창-연결"></span>
<span id="sec-머테리얼-우선-순서"></span>
<span id="sec-엔진-연결-게이트"></span>
<span id="sec-이후-창과-의존성-제거"></span>
<span id="sec-현재-잔여와-재작성-제외"></span>
<span id="머테리얼-우선-순서"></span>
<span id="엔진-연결-게이트"></span>
<span id="이후-창과-의존성-제거"></span>
<span id="현재-잔여와-재작성-제외"></span>

</details>
