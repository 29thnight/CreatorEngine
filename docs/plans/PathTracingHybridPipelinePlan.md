# PHASE 4.85 — Path Tracing·Hybrid RT 렌더 파이프라인

> 2026-10-20 정리 · master `27de3f5a02aadde7456aab680db3184c36ee735e` · 잔여 구현·수용 계획

판정 기준은 위 master의 커밋된 소스다. 작업 중인 로컬 코드 변경은 포함하지 않는다. 완료된 구현 단계·날짜별 실행 일지는 제거했다. 대시보드는 SRP 저작 전환 취소에 해당하는 항목만 제거하고, 나머지 상태·공수·링크를 보존한다.

[전체 문서 대조·충돌 판정](MasterPlanAudit20261020.html) · [정리 전 원문](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/docs/plans/PathTracingHybridPipelinePlan.md)

## 남은 작업과 완료 판정

- RTP-0~13: 공통 RT geometry/AS·hit material 계약, Hybrid RT 효과와 progressive/interactive PT, 설정·fallback·수명·제품 회귀를 연결한다.
- ReSTIR PT와 tetrahedral cage 연구는 재현·oracle·전체 frame/VRAM 비용 후 채택한다. raster meshlet/GD·REAC의 구현을 RT/PT 제품 완료로 보지 않는다. 중앙 208인일은 과거 추정이며 새 구현 공수로 재확정해야 한다.

## 적용할 구조와 경계

`material document → compiled program → sealed scene inputs → material graph stages → RHI submission`

위 경로는 소스에서 확인한 구현 기반이다. 아래 요구의 수용 결과를 뜻하지 않는다. 구현을 다시 만드는 항목과 제품 검증을 혼합하지 않는다.

## 추적할 잔여 항목

| 추적 ID | 소유 단위 (잔여 작업은 위 목록) | 대시보드 보존 상태 |
|---|---|---|
| `RTP-0` · PHASE 4.85 | 현재 call path·장치 지원·세 모드 계약·baseline/예산·연구/코드 출처 봉인 | todo |
| `RTP-1` · PHASE 4.85 | RT RHI·정적 BLAS/TLAS·table·graph AS 의존/수명 | todo |
| `RTP-2` · PHASE 4.85 | RT hit material·BSDF/light/environment 공통 evaluator | todo |
| `RTP-3` · PHASE 4.85 | RT/PT accumulation·재투영·denoising 입출력·history | todo |
| `RTP-4` · PHASE 4.85 | Hybrid 첫 directional hard shadow | todo |
| `RTP-5` · PHASE 4.85 | Hybrid 효과별 반사/GI·굴절·volume 경계·fog visibility 평가/통합 | todo |
| `RTP-6` · PHASE 4.85 | 기본 offscreen progressive Path Tracer·reference | todo |
| `RTP-7` · PHASE 4.85 | interactive PathTracing 제품 graph·Editor Scene/Game·Player | todo |
| `RTP-8` · PHASE 4.85 | ReSTIR PT 기본 reservoir 계약 및 Enhanced 개선 구현/평가 | todo |
| `RTP-9` · PHASE 4.85 | tetrahedral cage 자산 전처리·cook/LOD·reference | todo |
| `RTP-10` · PHASE 4.85 | cage RT 변형·ray 변환·수명·두 모드 통합 평가 | todo |
| `RTP-11` · PHASE 4.85 | Raster/Hybrid/PathTracing 설정·진단·프로파일 | todo |
| `RTP-12` · PHASE 4.85 | 세 모드 제품 회귀·스트레스·계측 수용 | todo |
| `RTP-13` · PHASE 4.85 | 효과·연구별 채택 판정과 최종 페이즈 감사 | todo |

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

<span id="1-현재-기반과-연결-지점"></span>
<span id="2-세-모드와-공통-소유권"></span>
<span id="3-구현연구-슬라이스"></span>
<span id="4-실행-순서와-기존-id-대응"></span>
<span id="5-restir-pt-enhanced-적용-계약"></span>
<span id="6-amd-tetrahedral-cages-적용-계약"></span>
<span id="7-공통-수용과-공수-확정"></span>
<span id="8-공수-산정--2026-10-08"></span>
<span id="sec-1-현재-기반과-연결-지점"></span>
<span id="sec-2-세-모드와-공통-소유권"></span>
<span id="sec-3-구현연구-슬라이스"></span>
<span id="sec-4-실행-순서와-기존-id-대응"></span>
<span id="sec-5-restir-pt-enhanced-적용-계약"></span>
<span id="sec-6-amd-tetrahedral-cages-적용-계약"></span>
<span id="sec-7-공통-수용과-공수-확정"></span>
<span id="sec-8-공수-산정--2026-10-08"></span>
<span id="sec-연결-정본"></span>
<span id="연결-정본"></span>

</details>
