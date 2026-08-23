# CreatorEngine 문서

엔지니어링 문서 트리. 루트에 흩어져 있던 30개 문서를 2026-08-17에 여기로 옮겼다.

- **[RefactoringPlanDashboard.html](RefactoringPlanDashboard.html)** — 전체 리팩터링 페이즈 색인.
  진행 상태·의존 관계·슬라이스 단위를 한 눈에 본다. **여기서 시작하는 것이 빠르다.**

## 구성

| 디렉터리 | 성격 | 판별 기준 |
|---|---|---|
| [`plans/`](plans/) | 계획 | 슬라이스와 완료 기준이 있고, 진행에 따라 갱신된다 |
| [`design/`](design/) | 설계 결정 | 무엇을 왜 그렇게 짓는가. 결정과 기각 근거가 본문이다 |
| [`analysis/`](analysis/) | 실측·분석 | 특정 시점의 측정 기록. 사후 갱신하지 않는다 |

`index.html`·`style.css`는 GitHub Pages 소개 페이지, `generate_scriptbinder_docs.py`는
ScriptBinder API 문서 생성기(출력은 `API_DOCS/`)로 위 셋과 무관하다.

## plans/

| 문서 | 대상 |
|---|---|
| [BuildPipelinePlan.md](plans/BuildPipelinePlan.md) | 게임 빌드·패키징 파이프라인 (PHASE 12) |
| [SceneGraphRedesignPlan.md](plans/SceneGraphRedesignPlan.md) | 씬 그래프·엔티티·컴포넌트·프리팹 재설계 |
| [ReflectionRedesignPlan.md](plans/ReflectionRedesignPlan.md) | 리플렉션 재설계 (PHASE 18) |
| [RhiBoundaryPlan.md](plans/RhiBoundaryPlan.md) | RHI 경계·멀티백엔드 |
| [MaterialPipelinePlan.md](plans/MaterialPipelinePlan.md) | 머테리얼·셰이더 파이프라인 (PHASE 3.5) |
| [ScriptableRenderPipelinePlan.md](plans/ScriptableRenderPipelinePlan.md) | 스크립터블 렌더 파이프라인 |
| [ProfilingCapturePlan.md](plans/ProfilingCapturePlan.md) | 프레임 프로파일러 수집·녹화 (PHASE 14) |
| [UISystemRedesignPlan.md](plans/UISystemRedesignPlan.md) | UI 시스템 재설계 (PHASE 16) |
| [UtilityFrameworkModernizationPlan.md](plans/UtilityFrameworkModernizationPlan.md) | 유틸리티 프레임워크 현대화 (PHASE 15) |
| [SerializationPlan.md](plans/SerializationPlan.md) | 직렬화 이원화 — 저작 텍스트·런타임 쿠킹 (PHASE 17) |
| [PhysicsRedesignPlan.md](plans/PhysicsRedesignPlan.md) | 물리 재설계 — 컴포넌트·백엔드·스레딩 (PHASE 19) |
| [NetworkFrameworkPlan.md](plans/NetworkFrameworkPlan.md) | 네트워크 준비형 런타임·Replication (PHASE 20) |
| [EditorWorkspaceRedesignPlan.md](plans/EditorWorkspaceRedesignPlan.md) | ImGui 에디터 테마·도킹·ViewportHost (PHASE 21) |
| [EngineDistributionAndLauncherPlan.md](plans/EngineDistributionAndLauncherPlan.md) | MSI 엔진 배포·Launcher 프로젝트 관리 (PHASE 22) |
| [AnimationSchedulerPlan.md](plans/AnimationSchedulerPlan.md) | 애니메이션 스케줄러·LOD·CPU 버짓 (PHASE 13) |
| [MultiCameraRenderPlan.md](plans/MultiCameraRenderPlan.md) | 멀티 카메라 렌더 |
| [RenderSceneViewPlan.md](plans/RenderSceneViewPlan.md) | 렌더 씬 뷰 |
| [LivePipelineDescPlan.md](plans/LivePipelineDescPlan.md) | 라이브 파이프라인 기술 |
| [AssetResidencyPlan.md](plans/AssetResidencyPlan.md) | 애셋 상주·GPU 캐시 수명 |
| [EngineLayerSeparationPlan.md](plans/EngineLayerSeparationPlan.md) | 3계층 분리 |
| [EnginePackagingPlan.md](plans/EnginePackagingPlan.md) | 엔진 패키징 |
| [Phase5CouplingPlan.md](plans/Phase5CouplingPlan.md) | 커플링 절단 (PHASE 5) |
| [LifecycleRedesignPlan.html](plans/LifecycleRedesignPlan.html) | 생명주기 재설계 |
| [ScriptApiMigrationPlan.html](plans/ScriptApiMigrationPlan.html) | 스크립트 API 이관 |
| [BehaviorTreeManagedPlan.md](plans/BehaviorTreeManagedPlan.md) | 행동 트리 관리 측 이관 |

## design/

| 문서 | 대상 |
|---|---|
| [ContainerLibraryDesign.md](design/ContainerLibraryDesign.md) | `ce::dynamic_array` — 자체 컨테이너 설계와 기각 근거 |
| [RhiGpuMemoryLifetimeDesign.md](design/RhiGpuMemoryLifetimeDesign.md) | RHI GPU 메모리 수명 |
| [ResourceOwnershipDesign.html](design/ResourceOwnershipDesign.html) | 자원 소유권 |
| [ReflectionRetentionDecision.md](design/ReflectionRetentionDecision.md) | 리플렉션 존치 결정 |

## analysis/

| 문서 | 대상 |
|---|---|
| [EngineStructureAnalysis.html](analysis/EngineStructureAnalysis.html) | 엔진 구조 전반 |
| [ReflectionSystemAnalysis.md](analysis/ReflectionSystemAnalysis.md) | 리플렉션 시스템 실측 |
| [PPLContainerMigrationAnalysis.md](analysis/PPLContainerMigrationAnalysis.md) | PPL 컨테이너 이관 |
| [RectTransformAnalysis.html](analysis/RectTransformAnalysis.html) | RectTransform |
| [RendererPortingLog.html](analysis/RendererPortingLog.html) | 렌더러 포팅 이력 |

## 문서를 추가할 때

- **계획**이면 `plans/`. 슬라이스와 완료 기준, 판정 수치를 반드시 넣는다.
- **결정**이면 `design/`. 채택뿐 아니라 **기각한 대안과 그 근거**를 함께 적는다 —
  이 저장소는 같은 후보를 반복해서 재검토하는 비용이 컸다.
- **측정**이면 `analysis/`. 측정 시점을 명시하고 이후 갱신하지 않는다.
- 어느 쪽이든 **틀린 것으로 드러난 판단은 지우지 말고 정정 이력으로 남긴다.**
  이 저장소의 관례이며, 실제로 그 기록이 재실수를 막았다.
