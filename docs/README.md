# CreatorEngine 문서

<!-- master-doc-review:start -->
## 2026-10-20 master 구현 대조

기준: `27de3f5a02aadde7456aab680db3184c36ee735e`. 대시보드는 변경하지 않는다. 아래 현행 대조와 소스 선언이 이전 시점의 구조·미구현 표기보다 우선한다.

[전체 대조·링크 정리 원장](analysis/MasterDocumentationAudit20261020.html)

### 현행 계약과 이 문서의 적용 범위

- Editor/Player Host와 build/cook·manifest/pak·bootstrap을 분리한다. runtime 소비와 Editor authoring writer를 같은 책임으로 합치지 않는다.
- 로컬 package 생성은 Launcher/MSI·서명 update/rollback·clean VM 제품 수용의 증거가 아니다.
- 구 SRP 및 C# pipeline 저작 전환 제안은 취소됐다. 필요한 Pass는 현재 C++ 계약으로 작성하며, 별도 저작 체계는 실제 요구가 생기면 새 미래 계획으로 검토한다. 아래 원래 제안은 활성 계획이 아니다.

### 해당 구현에서 확인한 선언·소유 경계

- [ScriptCore/Physics.cs:13](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/ScriptCore/Physics.cs#L13) — `public struct PhysicsHit`
- [ScriptCore/Physics.cs:41](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/ScriptCore/Physics.cs#L41) — `public readonly struct PhysicsQueryRequest`
- [ScriptCore/Physics.cs:65](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/ScriptCore/Physics.cs#L65) — `public readonly struct PhysicsBatchResult`
- [ScriptCore/Animator.cs:14](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/ScriptCore/Animator.cs#L14) — `public sealed class Animator : NativeComponent`
- [ScriptCore/Animator.cs:18](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/ScriptCore/Animator.cs#L18) — `public void SetBool(string name, bool value) => Native.AnimatorSetBool(OwnerHandle, name, value);`
- [ScriptCore/Animator.cs:19](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/ScriptCore/Animator.cs#L19) — `public void SetFloat(string name, float value) => Native.AnimatorSetFloat(OwnerHandle, name, value);`
- [Engine/SceneRuntime/Animator.h:81](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/Animator.h#L81) — `own::shared_owner<const assets::ModelAnimationPayload> payload{};`
- [Engine/SceneRuntime/Animator.h:86](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/Animator.h#L86) — `own::shared_owner<const assets::ModelAnimationDescriptor> descriptor{};`
- [Engine/SceneRuntime/Animator.h:87](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/Animator.h#L87) — `own::shared_owner<const assets::ModelSkeletonPayload> skeleton{};`
- [Engine/SceneRuntime/Animator.cpp:26](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/Animator.cpp#L26) — `Animator::Animator() : m_instance(AnimatorSystems->CreateInstance()) {}`
- [Engine/SceneRuntime/Animator.cpp:28](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/Animator.cpp#L28) — `AnimInstance& Animator::GetInstance() noexcept`
- [Engine/SceneRuntime/Animator.cpp:31](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/Animator.cpp#L31) — `::ce::contracts::assert_that(`

### 이전 근거의 유효 범위

기존 세부 계약은 위 현행 구현과 모순되지 않는 범위에서 유지한다. 과거 수치·실행 통과·해시·장치 조건은 당시 기록이다. 로컬 산출물이 사라진 항목은 링크 대신 경로와 미보존 상태를 표시하며, 현재 master의 재실행 통과로 승격하지 않는다.

<!-- master-doc-review:end -->

<details data-master-history><summary>세부 계약·원래 분석과 당시 측정 기록</summary>

엔지니어링 문서 트리. 루트에 흩어져 있던 30개 문서를 2026-08-17에 여기로 옮겼다.

- **[TechnicalGuide.md](TechnicalGuide.md)** — 커밋된 master 기준의 기술설명서.
  엔진 구조·소유권·렌더링·콘텐츠·스크립팅·빌드 흐름과 구현·검증 경계를 설명한다.
- **[CONTRIBUTING.md](../CONTRIBUTING.md)** · **[CODE_OF_CONDUCT.md](../CODE_OF_CONDUCT.md)** — 기여 절차와 참여 기준.

- **[RefactoringPlanDashboard.html](RefactoringPlanDashboard.html)** — 전체 리팩터링 페이즈 색인.
  진행 상태·의존 관계·슬라이스 단위를 한 눈에 본다. **여기서 시작하는 것이 빠르다.**

## 구성

| 디렉터리 | 성격 | 판별 기준 |
|---|---|---|
| [`plans/`](plans) | 활성·미래 계획 | 진행·대기·차단·미래 범위를 갱신한다 |
| [`plans/archive/`](plans/archive/README.md) | 종료·대체 계획 | 완료·선택 중단·승계 근거와 검증 한계를 보존한다 |
| [`design/`](design) | 설계 결정 | 무엇을 왜 그렇게 짓는가. 결정과 기각 근거가 본문이다 |
| [`analysis/`](analysis) | 실측·분석 | 특정 시점의 측정 기록. 사후 갱신하지 않는다 |

`index.html`·`style.css`는 GitHub Pages 소개 페이지, `generate_scriptbinder_docs.py`는
ScriptBinder API 문서 생성기(출력은 `API_DOCS/`)로 위 셋과 무관하다.

## plans/

현재 계획과 이력 문서. 진행 상태는 [대시보드](RefactoringPlanDashboard.html#doc-index)를 함께 본다.

| 문서 | 대상 |
|---|---|
| [PBRWiringStabilizationPlan.md](plans/PBRWiringStabilizationPlan.md) | PHASE 4 · DX12 완료 — W0~W9 제품 배선·실장면 게이트. Vulkan 교차 판정은 PHASE 4.9. |
| [BlenderMaterialGraphPlan.md](plans/BlenderMaterialGraphPlan.md) | PHASE 4.25 · MAT-0 완료 — Blender 5.1.1 재질 기준 장면과 선형 HDR golden 고정. Principled ABI와 Material Graph 제품 작업은 후속. |
| [RenderPhaseRoadmap.md](plans/RenderPhaseRoadmap.md) | PHASE 4 계열 · 정본 — DAG 우선 순서, 분리 완료선, 현재 공수 원장. |
| [RenderGraphDependencySchedulingPlan.md](plans/RenderGraphDependencySchedulingPlan.md) | PHASE 4.3 · RG — 공통 밀봉 하네스와 리소스 의존성 스케줄링, 단계별 제품 전환. |
| [LightmapBakerPlan.md](plans/LightmapBakerPlan.md) | PHASE 4.7 · L — 라이트맵 베이커 재작성과 비동기 베이킹 계약. |
| [RendererQualityPlan.md](plans/RendererQualityPlan.md) | PHASE 4.75 · RND — probe/AO·shadow·display/post의 독립 품질 게이트. |
| [GpuFeaturePlanningPlan.md](plans/GpuFeaturePlanningPlan.md) | PHASE 4.8 · GPU — GPU-driven·확률 조명·DXR 설계 및 구현 공수 확정. |
| [LatticeAdoptionPlan.md](plans/LatticeAdoptionPlan.md) | LX-0~6 · 공수 미산정 — LX-2 독립 게이트 통과, Material Editor·열린 문서 HTTP 제품 연결 진행. MAT-0~8 완료, LX-3/3H의 전체 조작 게이트·새 BT/Animator 창·`imgui-node-editor` 제거는 후속. |
| [BackendParityPlan.md](plans/BackendParityPlan.md) | PHASE 4.9 — DX12/Vulkan 교차 판정 복귀. 공수 미산정. |
| [EditorAutomationCLIPlan.md](plans/EditorAutomationCLIPlan.md) | PHASE 14.5 · LC0~LC9 — 라이브 HTTP/JSON 명령·실행 중 Player 제어와 Commandlet 분리. 2026-09-15 재정의 뒤 소유 게이트 13칸 HEAD 재실행으로 조건 ① 닫음(초록 11·붉음 2는 게이트 결함). 남은 조건은 GUI 수동 하나, MCP 보류. |
| [ScriptSurfacePlan.md](plans/ScriptSurfacePlan.md) | PHASE 9.5 — 현재 네이티브 계약에 맞춘 C# 스크립트 표면 재설계. |
| [EnginePackagingPlan.md](plans/EnginePackagingPlan.md) | PHASE 10·11 등 — EffectSystem·Terrain의 의존 역전과 패키지 경계. 잔여 작업 유지. |
| [ModelGeometryTextureImprovementPlan.md](plans/ModelGeometryTextureImprovementPlan.md) | 비동기 배치 완료 · 나머지 구조 개선은 제안 단계. 기존 PHASE의 완료·공수와 구분 |
| [TexturePipelinePlan.md](plans/TexturePipelinePlan.md) | PHASE 12 · 구현·부분 수용 — 텍스처 import 설정·mip·cook·Player 소비와 편집 자동 처리. 전체 품질·성능 및 잔여 게이트는 계획과 검증 기록 참조. |
| [BuildPipelinePlan.md](plans/BuildPipelinePlan.md) | PHASE 12.5 · 진행 — 게임 빌드·cook·stage·managed·CI 파이프라인. B/L 잔여 유지. |
| [EngineLayerSeparationPlan.md](plans/EngineLayerSeparationPlan.md) | E0~E7 · 잔여 있음 — Runtime Core·Editor·Host 경계. E2 writer와 E7 잔여 유지. |
| [AnimationSchedulerPlan.md](plans/AnimationSchedulerPlan.md) | PHASE 13 — 애니메이션 스케줄러·LOD·CPU 버짓 재설계. |
| [TaskSchedulerUnificationPlan.md](plans/TaskSchedulerUnificationPlan.md) | PHASE 13 S0.5·S6 부속 — 태스크 스케줄러 enkiTS 이관의 실태 조사·측정. fork-join만 옮기고 장기 블로킹 스레드 18곳은 존치. |
| [ProfilingCapturePlan.md](plans/ProfilingCapturePlan.md) | PHASE 14 P0~P6 · 진행 — CPU/GPU/GC 시간축 수집·녹화·구간 분석. |
| [MemoryProfilerPlan.md](plans/MemoryProfilerPlan.md) | PHASE 14 MP0~MP6 · 진행 — Memory 탭 수동 스냅샷·A/B 분석과 객체별 네이티브/관리/GPU 계측 확장. |
| [RenderFrameDebuggerPlan.md](plans/RenderFrameDebuggerPlan.md) | 14-7 독립 트랙 · 미착수 — 원하는 뷰의 다음 완료 제출 하나를 수동 캡처하고 이벤트/상태/리소스·출력을 조사(RF0~RF7). |
| [UtilityFrameworkModernizationPlan.md](plans/UtilityFrameworkModernizationPlan.md) | PHASE 15 · 진행 — 유틸리티의 실제 소비·계약을 기준으로 정리. |
| [MathematicsMigrationPlan.md](plans/MathematicsMigrationPlan.md) | 구조 완료 · 검증 잔여 — 수학 라이브러리 이주. pixel·Physics runtime gate가 남아 있다. |
| [UISystemRedesignPlan.md](plans/UISystemRedesignPlan.md) | PHASE 16 · 진행 — Scene 소유 UI Runtime과 값 타입 렌더 제출. 9-15 개정에서 텍스트 렌더(트랙 T)가 U1 앞으로 서고, UI 저작의 소유 레이어를 C++로 확정했다(D-5). |
| [SerializationPlan.md](plans/SerializationPlan.md) | PHASE 17 · 진행 — 저작 텍스트·쿠킹 바이너리 경계와 잔여 성능 판정. |
| [PhysicsRedesignPlan.md](plans/PhysicsRedesignPlan.md) | PHASE 19 — 물리 컴포넌트·backend·스레딩 전면 재설계. |
| [NetworkFrameworkPlan.md](plans/NetworkFrameworkPlan.md) | PHASE 20 — 네트워크 신원·fixed tick·replication 기반. |
| [EditorWorkspaceRedesignPlan.md](plans/EditorWorkspaceRedesignPlan.md) | PHASE 21 — 테마·도킹·ViewportHost와 편집·플레이 전환. |
| [AudioBackendModernizationPlan.md](plans/AudioBackendModernizationPlan.md) | PHASE 22 — FMOD 은퇴와 miniaudio 통합·오디오 회귀. |
| [EngineDistributionAndLauncherPlan.md](plans/EngineDistributionAndLauncherPlan.md) | PHASE 23 — 엔진 배포·Launcher·프로젝트 관리와 설치 회귀. |
| [SimulationEffectContractPlan.md](plans/SimulationEffectContractPlan.md) | PHASE 24 · 현재 리팩토링 이후의 미래 계획 · 구현 미착수 |

이전 PHASE 4 계열 원장과 Asset-first SRP 판단은
[Phase4UnifiedPlan.md](plans/Phase4UnifiedPlan.md),

## plans/archive/

완료·중단·대체된 **19개 문서**는 [보관 색인](plans/archive/README.md)으로 옮겼다.
대시보드의 [종료 페이즈 이력](RefactoringPlanDashboard.html#closed-phases)은 기본으로 접혀 있으며,
기존 페이즈 링크를 열면 해당 이력이 펼쳐진다. 열린 페이즈 요약 집계에서는 종료 페이즈를 제외한다.

보관 원문에는 과거 측정·폐기된 제안·미검증 범위가 남아 있다. 보관일을 새 구현·검증일로 해석하지 않는다.

## design/

| 문서 | 대상 |
|---|---|
| [RenderPipelineTargetArchitecture.md](design/RenderPipelineTargetArchitecture.md) | 기존 C++ Pass·LivePipelineDesc·RenderGraph 실행 경계. SRP/C# 저작 전환 취소 반영 |
| [LatticeNodeSystem.md](design/LatticeNodeSystem.md) | Lattice(LX) 공통 그래프 계약·편집 UI 초안과 [화면 와이어프레임](design/LatticeEditorWireframe.svg) |
| [EngineVersionPolicy.md](design/EngineVersionPolicy.md) | 확정 — 제품 세대·기능 릴리스·네 자리 엔진 빌드·API 계약·채널. 적용은 PHASE 23 DL5·DL6·DL10 |
| [ContainerLibraryDesign.md](design/ContainerLibraryDesign.md) | `ce::dynamic_array` — 자체 컨테이너 설계와 기각 근거 |
| [RhiGpuMemoryLifetimeDesign.md](design/RhiGpuMemoryLifetimeDesign.md) | RHI GPU 메모리 수명 |
| [ResourceOwnershipDesign.html](design/ResourceOwnershipDesign.html) | 자원 소유권 |
| [ReflectionDesign.md](design/ReflectionDesign.md) | 리플렉션 현재 설계 정본 — reflgen 서술·빌드 연동·런타임 등록소·소비자와 기각 근거 |
| [ReflectionRetentionDecision.md](design/ReflectionRetentionDecision.md) | 리플렉션 존치 결정 |

## analysis/

| 문서 | 대상 |
|---|---|
| [Phase475AxisClassification.md](analysis/Phase475AxisClassification.md) | 분리 전 PHASE 4.75의 22개 활성 행을 분류한 당시 정적 점검(2026-09-23) |
| [CreatorBuildToolValidation.md](analysis/CreatorBuildToolValidation.md) | 독립 BuildTool EXE·private runtime·패키징 검증과 기존 모델 씬 제한(2026-09-13) |
| [EngineStructureAnalysis.html](analysis/EngineStructureAnalysis.html) | 엔진 구조 전반 |
| [ReflectionSystemAnalysis.md](analysis/ReflectionSystemAnalysis.md) | 리플렉션 시스템 실측 |
| [MAT9BlenderImageComparison.md](analysis/MAT9BlenderImageComparison.md) | Core/Layered 동일 입력 24장·Debug/Release 일치·박막 잔여 차이와 기준 노이즈(2026-09-30) |
| [MAT9MaterialScenePerformance.md](analysis/MAT9MaterialScenePerformance.md) | 모델 배치 후 Debug/Release 렌더 CPU 회귀와 청크·LOD·binding 개선(2026-09-30) |
| [MAT9NodeEditorPerformance.md](analysis/MAT9NodeEditorPerformance.md) | 다른 세션의 캔버스·Scene 잠금 대기·Inspector preview 실측 통합(2026-09-30) |
| [ReflgenPhase425Integration.md](analysis/ReflgenPhase425Integration.md) | reflgen PR #114·VS 확장 설치, 4.25 충돌 해결과 통합 검증(2026-09-30) |
| [PPLContainerMigrationAnalysis.md](analysis/PPLContainerMigrationAnalysis.md) | PPL 컨테이너 이관 |
| [RectTransformAnalysis.html](analysis/RectTransformAnalysis.html) | RectTransform |
| [RendererPortingLog.html](analysis/RendererPortingLog.html) | 렌더러 포팅 이력 |
| [EditorMenuSurfaceAndPhase21Preflight.md](analysis/EditorMenuSurfaceAndPhase21Preflight.md) | 에디터 메뉴 표면 CLI↔GUI 대조 · 메뉴 확장성 · PHASE 21 재정찰(2026-09-10) |
| [EditorWidgetInheritanceW2.md](analysis/EditorWidgetInheritanceW2.md) | PHASE 21 W2 승계 결정표 확정 — ImGuiHelper 자산의 소비자 실측(2026-09-11) |

## 문서를 추가할 때

- **계획**이면 `plans/`. 슬라이스와 완료 기준, 판정 수치를 반드시 넣는다.
- **계획이 종료·대체**되면 `plans/archive/`로 이동하고 보관 색인·내부 상대 링크·대시보드를 함께 갱신한다. 보류·차단·미래 계획은 종료로 처리하지 않는다.
- **결정**이면 `design/`. 채택뿐 아니라 **기각한 대안과 그 근거**를 함께 적는다 —
  이 저장소는 같은 후보를 반복해서 재검토하는 비용이 컸다.
- **측정**이면 `analysis/`. 측정 시점을 명시하고 이후 갱신하지 않는다.
- 어느 쪽이든 **틀린 것으로 드러난 판단은 지우지 말고 정정 이력으로 남긴다.**
  이 저장소의 관례이며, 실제로 그 기록이 재실수를 막았다.

</details>
