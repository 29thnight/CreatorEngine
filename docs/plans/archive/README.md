# 종료·대체된 계획 보관함

정리일: 2026-09-06. 완료·선택 중단·후속 계획으로 승계된 문서 19종을 보관한다.
보관은 원래 범위의 종료를 뜻한다. 새 빌드·런타임 검증을 수행했다는 뜻이 아니며,
선택 중단·이관 작업·검증 한계는 아래와 각 원문에 남긴다. 원문의 과거 제안·측정은 당시 기록으로 읽는다.

[활성 계획 색인](../../README.md#plans) · [대시보드](../../RefactoringPlanDashboard.html) · [종료 페이즈 이력](../../RefactoringPlanDashboard.html#closed-phases)

| 문서 | 종료 분류 | 근거와 남은 경계 |
|---|---|---|
| [AssetResidencyPlan.md](AssetResidencyPlan.md) | 완료 | 2026-08-15 갱신: DX12/Vulkan 상주·퇴출·completion 수명 경로 완료. 후속 메모리 설계는 별도 범위. |
| [BehaviorTreeManagedPlan.md](BehaviorTreeManagedPlan.md) | 엔진 범위 완료 | PHASE 9의 9-8·9-10 완료 기록. 기존 게임 BT 콘텐츠 43종 이식은 별도 잔여이며 엔진 완료로 대신하지 않는다. |
| [LifecycleRedesignPlan.html](LifecycleRedesignPlan.html) | 완료 | PHASE 9 종료. 현재 컴포넌트 수명 계약은 SceneGraphRedesignPlan과 스크립트 안정화 기록을 함께 참조한다. |
| [LivePipelineDescPlan.md](LivePipelineDescPlan.md) | 완료 | PHASE 3의 3-13 완료. 초기 문서의 전용 CLI 제안은 채택하지 않았고 진단은 기존 창·회귀 명령에 통합했다. 후속은 PHASE 4.75. |
| [MaterialPipelinePlan.md](MaterialPipelinePlan.md) | 완료 | PHASE 3.5 M0~M7 종료. 현재 PBR 배선 수정은 PHASE 4, Material Graph는 PHASE 4.25에서 진행한다. |
| [ModelAssetBigBangCutoverPlan.md](ModelAssetBigBangCutoverPlan.md) | 완료 | 2026-09-04 MBC11 cutover·PHASE 3.75 종료 기록. 임베디드 텍스처 디코드는 PHASE 12 T1a/T2로 이관했고 비회귀 기준은 유지한다. |
| [ModelAssetBigBangCutoverBaseline.md](ModelAssetBigBangCutoverBaseline.md) | 완료 계획의 기준선 | PHASE 3.75 MBC0/MBC11의 동결·재유도 근거. 기존 예산·회귀 스크립트의 참조 자료로 계속 보존한다. |
| [MultiCameraRenderPlan.md](MultiCameraRenderPlan.md) | 완료 | 다중 뷰 구현 기록. §0의 2026-08-24 카메라 아키텍처 이행 계약과 검증 근거를 보존한다. |
| [Phase5CouplingPlan.md](Phase5CouplingPlan.md) | 완료·선택 중단 | PHASE 5 종료. 5-5의 포괄적 폴더 재구성은 중단했고 후속 계층·빌드 작업은 각 활성 계획이 소유한다. |
| [ReflectionRedesignPlan.md](ReflectionRedesignPlan.md) | 완료 | PHASE 18 CT0~CT11 종료. 매크로 없는 reflect()/meta::schema 계약과 검증 기록을 보존한다. |
| [RenderSceneViewPlan.md](RenderSceneViewPlan.md) | 완료 | PHASE 3의 3-14 완료. 카메라 후속 이행은 MultiCameraRenderPlan §0을 참조한다. |
| [RhiBoundaryPlan.md](RhiBoundaryPlan.md) | 완료 | PHASE 3의 3-1 및 필수 RHI 구조 작업 종료. GPU 메모리 후속 설계와 PHASE 4.75 확장은 별도 범위. |
| [SceneGraphRedesignPlan.md](SceneGraphRedesignPlan.md) | 완료 | PHASE 8.5의 전체 트랙 종료. 조건부 S4는 PHASE 8.75 X8로 승계되어 완료됐다. |
| [ScriptLifecycleContractHardeningPlan.md](ScriptLifecycleContractHardeningPlan.md) | 완료·검증 한계 명시 | LC0~LC7 종료. LC6 변경은 측정 후 보류. LC7 일시 정지 중 대기와 세대 게이트 변이 판정은 미검증으로 남아 있으며 PrSM 도입 완료를 뜻하지 않는다. |
| [TransformUpdatePlan.md](TransformUpdatePlan.md) | 완료·선택 중단 | PHASE 8.75 X0~X8 완료, X9 선택 확장 중단. Windows 제품 TSan 미지원 등 검증 경계는 원문 그대로 유지한다. |
| [ModelImportPipelinePlan.md](ModelImportPipelinePlan.md) | 대체됨 | 2026-09-02 PHASE 3.75로 대체. 구 I/V experiment 계획은 구현·측정·실패의 역사 자료다. |
| [ScriptApiMigrationPlan.html](ScriptApiMigrationPlan.html) | 승계됨 | 기존 API 이전 기록. 폐기된 T1~T4 표면 등급 대신 PHASE 9.5 ScriptSurfacePlan을 따른다. |
| [TransformExecutionGraphPlan.md](TransformExecutionGraphPlan.md) | 통합됨 | 2026-08-25 TransformUpdatePlan으로 통합된 제안과 절 매핑. |
| [TransformUpdatePipeline.html](TransformUpdatePipeline.html) | 구판 | 폐기된 T0~T5 기준의 시각화. 최종 X0~X9 계약은 TransformUpdatePlan에 있다. |

후속 작업의 정본은 [PBR 배선](../PBRWiringStabilizationPlan.md), [Material Graph](../BlenderMaterialGraphPlan.md),
[텍스처 파이프라인](../TexturePipelinePlan.md), [스크립트 표면](../ScriptSurfacePlan.md),
[미래 효과 계약](../SimulationEffectContractPlan.md) 등 활성 계획을 따른다. 미래 계획은 보관 대상이 아니다.
