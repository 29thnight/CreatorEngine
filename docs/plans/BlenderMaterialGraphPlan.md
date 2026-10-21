# Blender형 PBR·Material Graph 계획 (PHASE 4.25)

> 2026-10-20 정리 · master `27de3f5a02aadde7456aab680db3184c36ee735e` · 잔여 구현·수용 계획

판정 기준은 위 master의 커밋된 소스다. 작업 중인 로컬 코드 변경은 포함하지 않는다. 완료된 구현 단계·날짜별 실행 일지는 제거했다. 대시보드는 SRP 저작 전환 취소에 해당하는 항목만 제거하고, 나머지 상태·공수·링크를 보존한다.

[전체 문서 대조·충돌 판정](MasterPlanAudit20261020.html) · [정리 전 원문](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/docs/plans/BlenderMaterialGraphPlan.md)

## 남은 작업과 완료 판정

- LX-3의 Material 저작 제품 연결과 MAT-9 전체 재질/route 픽셀·성능 수용을 닫는다. MAT-0~8과 환경 쿠킹 기반은 다시 만들지 않는다.
- 현재 live 제품의 graph-only 재질 경로와 compiled program·sealed frame을 기준으로 비교한다. 이전 Standard fallback이나 pending 시 옛 material instance를 복원하는 방안은 폐기한다.
- Blender 비교는 동일 pre-tone 입력·BRDF/HDRI 설정으로 수렴/허용 오차를 고정한다. renderer 품질·환경 가중치와 Vulkan 동등성은 각각 소유 계획에 남긴다.

## 적용할 구조와 경계

`material document → compiled program → sealed scene inputs → material graph stages → RHI submission`

위 경로는 소스에서 확인한 구현 기반이다. 아래 요구의 수용 결과를 뜻하지 않는다. 구현을 다시 만드는 항목과 제품 검증을 혼합하지 않는다.

## 추적할 잔여 항목

| 추적 ID | 소유 단위 (잔여 작업은 위 목록) | 대시보드 보존 상태 |
|---|---|---|
| `LX-3` · PHASE 4.25 | Material 그룹·Blackboard 제품 편집 마감 | progress |
| `MAT-9` · PHASE 4.25 | Blender golden·route parity·Standard 성능 gate | progress |

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

<span id="0-현재-판정과-실행-범위-2026-10-02"></span>
<span id="1-고정한-비교-범위"></span>
<span id="2-현재-구조에서-막히는-지점"></span>
<span id="2026-10-01-forest-mirror-source픽셀-중심-보간"></span>
<span id="2026-10-01-hdr-에너지-보존--source-cube-대조"></span>
<span id="2026-10-01-hdri--brdf-mis-연결"></span>
<span id="2026-10-01-special-surface-반경스침각-수정과-기준-수렴"></span>
<span id="2026-10-01-special-대조균질-단일-산란-volume-검증"></span>
<span id="2026-10-01-근접-카메라-이동-성능-조사"></span>
<span id="2026-10-01-전체-hdri-26조건-수렴수용"></span>
<span id="2026-10-01-품질-기준선-유지후속-페이즈-진행-결정"></span>
<span id="2026-10-01-후속-구조-정리와-최종-게이트-회수"></span>
<span id="2026-10-04-살아-있는-화면의-split-sum-전환blender-차이--표기-결정"></span>
<span id="3-최종-구조"></span>
<span id="4-아트-팀용-비용-계약"></span>
<span id="5-실행-순서와-공수"></span>
<span id="6-완료-기준"></span>
<span id="7-phase-475-인계"></span>
<span id="implementation-history"></span>
<span id="mat-0-고정-결과-2026-09-28"></span>
<span id="mat-1-공용-abi-결과-2026-09-28"></span>
<span id="mat-2-문서ir-결과-2026-09-28"></span>
<span id="mat-3-core-평가-결과-2026-09-28"></span>
<span id="mat-4-layered-평가-결과-2026-09-28"></span>
<span id="mat-5-special-평가-결과-2026-09-28"></span>
<span id="mat-6-생성진단-결과-2026-09-28"></span>
<span id="mat-7-scene-generation-비동기-준비교체-2026-09-29"></span>
<span id="mat-7-vulkan-native-pso-비동기-준비-2026-09-29"></span>
<span id="mat-7-공통-forward일반-alpha-blend-다섯-번째-구현-단계--2026-10-02"></span>
<span id="mat-7-공통-graphics-shaderpso-소유권-연결-네-번째-구현-단계--2026-10-02"></span>
<span id="mat-7-구현검증-기록-2026-09-2829"></span>
<span id="mat-7-두-번째-구현-단계--lx-소유의-공통-값바인딩쿠킹-복구-2026-10-02"></span>
<span id="mat-7-보조-compute프레임-조회-분기-정리-여섯-번째-구현-단계--2026-10-02"></span>
<span id="mat-7-세-번째-구현-단계--코드-입력의-lx-instanceframe-snapshot-연결-2026-10-02"></span>
<span id="mat-7-실제-scene-shadow-casterdecal-2026-09-29"></span>
<span id="mat-7-실제-scene-sss-transport-2026-09-29"></span>
<span id="mat-7-실제-scene-transmissionrefraction-2026-09-29"></span>
<span id="mat-7-실제-scene-volume-transport와-렌더-설정-명칭-2026-09-29"></span>
<span id="mat-7-자동-scene-cookpackageplayer-2026-09-29"></span>
<span id="mat-7-제품-통합-종료-결과-2026-09-29"></span>
<span id="mat-7-제품-편집복구쿠킹-회귀-일곱-번째-구현-단계--2026-10-02"></span>
<span id="mat-7-첫-구현-단계--생성-어댑터-2026-10-02"></span>
<span id="mat-7-통합-순서와-완료-게이트"></span>
<span id="mat-7-특수-transport일반-alpha-혼합-여덟-번째-구현-단계--2026-10-02"></span>
<span id="mat-8-artist-상태-표시-완료-2026-09-30"></span>
<span id="mat-8-모델-graph-sotinspector-2026-09-29"></span>
<span id="mat-8-편집-ui캔버스-성능-후속-통합--2026-09-30"></span>
<span id="mat-9-corelayered-동일-입력-이미지-대조--2026-09-30"></span>
<span id="mat-9-hdri-대조와-scene-필터-수정--2026-10-01"></span>
<span id="mat-9-가시광-박막환경-쿠킹-후속--2026-09-30"></span>
<span id="mat-9-공통-ggx와-core-principled-수정--2026-10-01"></span>
<span id="mat-9-모델-배치-성능-회귀-조사--2026-09-30"></span>
<span id="mat-9-박막-확대-수용-판정--2026-10-01"></span>
<span id="sec-0-현재-판정과-실행-범위-2026-10-02"></span>
<span id="sec-1-고정한-비교-범위"></span>
<span id="sec-2-현재-구조에서-막히는-지점"></span>
<span id="sec-3-최종-구조"></span>
<span id="sec-4-아트-팀용-비용-계약"></span>
<span id="sec-5-실행-순서와-공수"></span>
<span id="sec-6-완료-기준"></span>
<span id="sec-7-phase-475-인계"></span>
<span id="제외"></span>
<span id="포함"></span>

</details>
