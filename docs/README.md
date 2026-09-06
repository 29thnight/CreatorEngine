# CreatorEngine 문서

엔지니어링 문서 트리. 루트에 흩어져 있던 30개 문서를 2026-08-17에 여기로 옮겼다.

- **[RefactoringPlanDashboard.html](RefactoringPlanDashboard.html)** — 전체 리팩터링 페이즈 색인.
  진행 상태·의존 관계·슬라이스 단위를 한 눈에 본다. **여기서 시작하는 것이 빠르다.**

## 구성

| 디렉터리 | 성격 | 판별 기준 |
|---|---|---|
| [`plans/`](plans) | 계획 | 슬라이스와 완료 기준이 있고, 진행에 따라 갱신된다 |
| [`design/`](design) | 설계 결정 | 무엇을 왜 그렇게 짓는가. 결정과 기각 근거가 본문이다 |
| [`analysis/`](analysis) | 실측·분석 | 특정 시점의 측정 기록. 사후 갱신하지 않는다 |

`index.html`·`style.css`는 GitHub Pages 소개 페이지, `generate_scriptbinder_docs.py`는
ScriptBinder API 문서 생성기(출력은 `API_DOCS/`)로 위 셋과 무관하다.

## plans/

실행 계획 25종과 미래 계획 1종. 진행 상태는 [대시보드](RefactoringPlanDashboard.html#doc-index)를 함께 본다.

| 문서 | 대상 |
|---|---|
| [PBRWiringStabilizationPlan.md](plans/PBRWiringStabilizationPlan.md) | PHASE 4 · 진행 — 현행 PBR 배선·재질 의미·backend 동등성과 실장면 회귀. |
| [BlenderMaterialGraphPlan.md](plans/BlenderMaterialGraphPlan.md) | PHASE 4.25 · 미착수 — Principled 기반 Material Graph와 artist workflow. |
| [Phase4UnifiedPlan.md](plans/Phase4UnifiedPlan.md) | PHASE 4 계열 · 정본 — PHASE 4·4.25·4.75의 책임·순서·공수 통합 기준. |
| [RenderGraphDependencySchedulingPlan.md](plans/RenderGraphDependencySchedulingPlan.md) | PHASE 4.75 · RG — 리소스 의존성 스케줄링과 단계별 제품 전환. |
| [LightmapBakerPlan.md](plans/LightmapBakerPlan.md) | PHASE 4.75 · L — 라이트맵 베이커 재작성과 비동기 베이킹 계약. |
| [ScriptableRenderPipelinePlan.md](plans/ScriptableRenderPipelinePlan.md) | PHASE 4.75 · SRP — Pipeline Asset·Custom Pass·Shader Graph 저작과 제품 배선. |
| [EditorAutomationCLIPlan.md](plans/EditorAutomationCLIPlan.md) | PHASE 6 · CLI — Editor 명령 결과·인자·등록·모듈·JSONL 정식화. MCP 보류. |
| [ScriptSurfacePlan.md](plans/ScriptSurfacePlan.md) | PHASE 9.5 — 현재 네이티브 계약에 맞춘 C# 스크립트 표면 재설계. |
| [EnginePackagingPlan.md](plans/EnginePackagingPlan.md) | PHASE 10·11 등 — EffectSystem·Terrain의 의존 역전과 패키지 경계. 잔여 작업 유지. |
| [ModelGeometryTextureImprovementPlan.md](plans/ModelGeometryTextureImprovementPlan.md) | 비동기 배치 완료 · 나머지 구조 개선은 제안 단계. 기존 PHASE의 완료·공수와 구분 |
| [TexturePipelinePlan.md](plans/TexturePipelinePlan.md) | PHASE 12 · 미착수 — 텍스처 import 설정·mip·cook 트랜스코딩·런타임 소비. |
| [BuildPipelinePlan.md](plans/BuildPipelinePlan.md) | PHASE 12.5 · 진행 — 게임 빌드·cook·stage·managed·CI 파이프라인. B/L 잔여 유지. |
| [EngineLayerSeparationPlan.md](plans/EngineLayerSeparationPlan.md) | E0~E7 · 잔여 있음 — Runtime Core·Editor·Host 경계. E2 writer와 E7 잔여 유지. |
| [AnimationSchedulerPlan.md](plans/AnimationSchedulerPlan.md) | PHASE 13 — 애니메이션 스케줄러·LOD·CPU 버짓 재설계. |
| [ProfilingCapturePlan.md](plans/ProfilingCapturePlan.md) | PHASE 14 · 진행 — 수집 코어·녹화·구간 분석과 프로파일러 소비 경로. |
| [RenderFrameDebuggerPlan.md](plans/RenderFrameDebuggerPlan.md) | PHASE 14 확장 — 불변 프레임 캡처·그리기 출처·선택적 픽셀 재현. |
| [UtilityFrameworkModernizationPlan.md](plans/UtilityFrameworkModernizationPlan.md) | PHASE 15 · 진행 — 유틸리티의 실제 소비·계약을 기준으로 정리. |
| [MathematicsMigrationPlan.md](plans/MathematicsMigrationPlan.md) | 구조 완료 · 검증 잔여 — 수학 라이브러리 이주. pixel·Physics runtime gate가 남아 있다. |
| [UISystemRedesignPlan.md](plans/UISystemRedesignPlan.md) | PHASE 16 · 진행 — Scene 소유 UI Runtime과 값 타입 렌더 제출. |
| [SerializationPlan.md](plans/SerializationPlan.md) | PHASE 17 · 진행 — 저작 텍스트·쿠킹 바이너리 경계와 잔여 성능 판정. |
| [PhysicsRedesignPlan.md](plans/PhysicsRedesignPlan.md) | PHASE 19 — 물리 컴포넌트·backend·스레딩 전면 재설계. |
| [NetworkFrameworkPlan.md](plans/NetworkFrameworkPlan.md) | PHASE 20 — 네트워크 신원·fixed tick·replication 기반. |
| [EditorWorkspaceRedesignPlan.md](plans/EditorWorkspaceRedesignPlan.md) | PHASE 21 — 테마·도킹·ViewportHost와 편집·플레이 전환. |
| [AudioBackendModernizationPlan.md](plans/AudioBackendModernizationPlan.md) | PHASE 22 — FMOD 은퇴와 miniaudio 통합·오디오 회귀. |
| [EngineDistributionAndLauncherPlan.md](plans/EngineDistributionAndLauncherPlan.md) | PHASE 23 — 엔진 배포·Launcher·프로젝트 관리와 설치 회귀. |
| [SimulationEffectContractPlan.md](plans/SimulationEffectContractPlan.md) | PHASE 24 · 현재 리팩토링 이후의 미래 계획 · 구현 미착수 |

## plans/archive/

완료·중단·대체된 **19개 문서**는 [보관 색인](plans/archive/README.md)으로 옮겼다.
대시보드의 [종료 페이즈 이력](RefactoringPlanDashboard.html#closed-phases)은 기본으로 접혀 있으며,
기존 페이즈 링크를 열면 해당 이력이 펼쳐진다. 열린 페이즈 요약 집계에서는 종료 페이즈를 제외한다.

보관 원문에는 과거 측정·폐기된 제안·미검증 범위가 남아 있다. 보관일을 새 구현·검증일로 해석하지 않는다.

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
- **계획이 종료·대체**되면 `plans/archive/`로 이동하고 보관 색인·내부 상대 링크·대시보드를 함께 갱신한다. 보류·차단·미래 계획은 종료로 처리하지 않는다.
- **결정**이면 `design/`. 채택뿐 아니라 **기각한 대안과 그 근거**를 함께 적는다 —
  이 저장소는 같은 후보를 반복해서 재검토하는 비용이 컸다.
- **측정**이면 `analysis/`. 측정 시점을 명시하고 이후 갱신하지 않는다.
- 어느 쪽이든 **틀린 것으로 드러난 판단은 지우지 말고 정정 이력으로 남긴다.**
  이 저장소의 관례이며, 실제로 그 기록이 재실수를 막았다.
