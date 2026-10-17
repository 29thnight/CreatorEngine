# 최근 4일 PR 적용 감사 — 2026-10-07

## 기준과 판정

- 기간: **2026-10-04 00:00 KST부터 2026-10-07 조회 시점까지**(오늘 포함 4개 달력 날짜). UTC 시작은 2026-10-03 15:00이다.
- GitHub mergedAt으로 조회한 병합 PR **11개**. 로컬 HEAD와 원격 master는 `3afe1daaee7b75f644ac10b12d96fb684a0e74c8`로 일치하고, 모든 merge SHA가 HEAD의 조상임을 확인했다.
- PR 본문은 작성 당시의 범위·검증 선언으로 읽는다. 본문의 Draft/미병합 표현보다 현재 GitHub 병합 메타데이터와 Git 조상 관계가 적용 여부의 근거다.
- 이번 감사는 PR 본문·병합 이력·현재 소스 배선·기존 계획/분석 기록을 대조한 문서 작업이다. 엔진 빌드·테스트·실행·GPU 측정을 새로 수행하지 않았다.
- 작업 시작 전 존재한 GPU/RenderGraph 계획·대시보드 변경과 로컬 캡처 파일은 보존했다. 아래 제한된 실행 수치는 기존 로컬 기록에서 인용했으며 원시 실행 산출물을 새로 재검증한 결과가 아니다.

## 병합별 영향과 남은 완료 조건

| PR · 병합일(KST) · merge | 적용 내용 | 계획 소유자 | 검증 경계·다음 gate |
|---|---|---|---|
| [#117](https://github.com/29thnight/CreatorEngine/pull/117) · 10-04 17:07 · `2ee83fcd` | 표시 이미지와 카메라 신원, 비동기 집계, CEPROF v3 전체 세션 spool | PHASE 14 · 14-REC / PHASE 21 W8 | 장시간 녹화·복구·입력 일치. 본문 미실행 선언과 checked checklist가 상충하므로 재현 산출물 없이 통과로 세지 않음 |
| [#118](https://github.com/29thnight/CreatorEngine/pull/118) · 10-05 09:37 · `da81c281` | consumer lease·GPU 완료까지 이미지 보유, 최신 packet 및 Host GPU 진입 제한 | PHASE 4.5 FG 기반 / PHASE 21 W8 | DX12 lease·resize·실패·종료와 지연 실측; FG 자체 미구현 |
| [#120](https://github.com/29thnight/CreatorEngine/pull/120) · 10-05 17:53 · `41f9dd0a` | Player native DX12/Vulkan Present, Editor DX12 고정, Development/Shipping·명령 경계 | EngineLayerSeparation / BuildPipeline | Windows 구성별 빌드·package·Present·입력·device loss; Vulkan은 CPU bridge 유지 |
| [#121](https://github.com/29thnight/CreatorEngine/pull/121) · 10-05 20:37 · `c31ccb1c` | miniaudio·PlaybackHandle·SoundGraph·native/C# 소비 및 FMOD 제거 | PHASE 22 AU0~AU9 | 과거 cloud 각 3,825단정·retirement 1,732점검; Windows/CLR/실장치/PE·package/soak 잔여 |
| [#122](https://github.com/29thnight/CreatorEngine/pull/122) · 10-06 12:17 · `33f44d6c` | indexed indirect, versioned graph 제품 전환·viewer, split-sum·특수 재질 타일 | PHASE 4.3 RG5/RG6/RG-V · MAT-9 | 병합 당시 정적 검토; 후속 로컬 기록의 제한된 실행과 구분. 전체 제품 수용 미완료 |
| [#123](https://github.com/29thnight/CreatorEngine/pull/123) · 10-06 18:15 · `5ca4e82a` | meshlet/static LOD/HZB·light-space shadow·재질 보존 indirect, CEMC v11 | PHASE 4.8 GD0~GD3/HY1 · MAT-9 | 기존 로컬 기록: codec 466/466, 단일 DX12 장면 비교; decal/rendergraph 예외·제품 parity/성능 잔여 |
| [#125](https://github.com/29thnight/CreatorEngine/pull/125) · 10-06 22:40 · `94e2e911` | scene.open_async·모델 공유 준비·환경 CPU 준비, 하단 진행 표시 | PHASE 21 W2/W7/W8 · CLI | 취소·stale 완료·작은 worker 풀·owner 게시와 UI 회귀. CPU 준비/applied는 GPU 완료가 아님 |
| [#126](https://github.com/29thnight/CreatorEngine/pull/126) · 10-07 09:10 · `865a4e5d` | CPU scope publication·Stop 경계, DX12 GPU tail drain·incomplete 진단 | PHASE 14 · 14-REC | Windows D/R·반복 녹화·실제 fence·오류·종료·69 supported mutations 실행 잔여 |
| [#127](https://github.com/29thnight/CreatorEngine/pull/127) · 10-07 09:10 · `1c839113` | 별도 ProfilerViewer·ETW helper·인증 IPC·.cedx·profile.deep 제어 | PHASE 14 · 14-VIEW/14-DX · CLI | 빌드/fixture/ETW/실행 미검증, ordinary privilege·SDK opt-in·helper 종료/ETW 정리 확인 |
| [#128](https://github.com/29thnight/CreatorEngine/pull/128) · 10-07 10:14 · `605e8d3e` | 45 물리 px 제목 행·15페이지 셸·정보 패널·소유 viewer job 수명 | PHASE 14 · 14-VIEW / PHASE 21 W8 | DPI·입력·좁은 창·정상/비정상 종료·독립 offline viewer 수명 검증 |
| [#129](https://github.com/29thnight/CreatorEngine/pull/129) · 10-07 11:40 · `3afe1daa` | 명시적 Profiler 열기만 실행·전용 아이콘·조밀한 레일·하단 Cmd 입력 UI | PHASE 14 · 14-VIEW / PHASE 21 W2/W8 | 명시적 열기/설정 복원 무실행·UI/DPI 회귀; Cmd 실행과 Revision Control 연결은 미구현 |

#123의 GitHub mergeCommit 필드는 `be0688d3`를 반환했으나, 실제 master의 first-parent 병합은 `5ca4e82a`다. 두 부모는 `ef829373`/`bf3fe4f3`이며 `be0688d3`도 HEAD의 조상이다. 표에는 실제 통합 커밋을 사용했다. 병합 시각은 모든 행에서 GitHub mergedAt을 KST로 변환했다.

## 현재 소스와 계획 연결

| 소비 경로 | 확인한 소스/계약 | 정본 |
|---|---|---|
| Scene graph 실행 → compiled viewer | [EnhancedSceneRenderer.cpp](../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp)의 ExplicitVersioned/DependencyOrder → [EnhancedRenderDebugWindow.cpp](../../Editor/EngineGUIWindow/EnhancedRenderDebugWindow.cpp) snapshot 표시 | [RG5/RG6/RG-V](../plans/RenderGraphDependencySchedulingPlan.md) |
| Profiler 명시적 열기 → 소유 child | [ProfilerLauncher.cpp](../../Editor/ImGuiHelper/ProfilerLauncher.cpp) → [ProfilerViewerProcess.cpp](../../Engine/EngineDiagnostics/ProfilerViewerProcess.cpp), kill-on-close job 및 1.5초 close 유예/2.5초 owner wait | [14-REC/14-VIEW/14-DX](../plans/ProfilingCapturePlan.md) |
| Scene Open → 비동기 명령 | [MenuBarWindow.cpp](../../Editor/EngineGUIWindow/MenuBarWindow.cpp) → [SceneObjectCommands.cpp](../../Editor/EngineEntry/Commands/SceneObjectCommands.cpp)의 scene.open_async 등록 | [PHASE 21](../plans/EditorWorkspaceRedesignPlan.md), [비동기 준비 계약](../design/AsyncAssetPreparation.md) |
| 심층 수집 명령 → 엔진 서비스 | [DiagnosticsCommands.cpp](../../Editor/EngineEntry/Commands/DiagnosticsCommands.cpp)와 [descriptor](../../Engine/RuntimeHost/CommandCore/CommandDescriptorSeeds.cpp)의 profile.deep.start/status/stop | [CLI](../plans/EditorAutomationCLIPlan.md), [DX12 계약](../design/Dx12DeepCapture.md) |
| Player native 출력·consumer 소유 | [PlayerDX12Presentation.cpp](../../Player/PlayerDX12Presentation.cpp), [PlayerVulkanPresentation.cpp](../../Player/PlayerVulkanPresentation.cpp), [소유권 계약](../design/OwnedBoundedPresentation.md) | [레이어](../plans/EngineLayerSeparationPlan.md), [빌드](../plans/BuildPipelinePlan.md), [FG 기반](../plans/TemporalReconstructionPlan.md) |
| 오디오 소비·FMOD 철거 | [cloud 검증 보고서](Phase22AudioCloudValidation.md)의 source/build provenance와 acceptance 구분 | [AU0~AU9](../plans/AudioBackendModernizationPlan.md) |

## PR 밖의 같은 기간 변경

PR만으로 현재 HEAD를 설명하지 않는다. first-parent 이력에서 `7d81da38` RG5 혼합 수용/Decal·Sprite·SSS·SSR, `c8eeaddc` Physics M1/affine/export, 재질·Slang 캐시/선컴파일, `58f31856` render fence 대기, `d7e37857` transient 풀 잠금, `90649f07` scene 해체, `ef829373` 단일-worker wave 묶기, `25fac974` viewer 자료형/변수 수정도 확인했다. 이들은 병합 PR 11개 수에 넣지 않는다. Physics M1 등 기존 상세 계획의 진행 판정을 임의로 완료로 올리지 않는다.

## 2026-10-07 후속 검사

[RG5 종결 검사](RenderRg5Closure20261007.md)에서 visibility 준비 누락·구식 pass-count 검사를 보완했고 Debug/Release rendergraph/decal 명령이 정상 종료·GPU validation 0으로 통과했다. [reference API 후속 이관](RenderRg5ReferenceMigration20261007.md)도 D/R 3정책·일반 63 frames·공유 depth 72 frames·validation 0으로 수용했다. [최종 소비자·capture/fixture 수용](RenderRg5FinalAcceptance20261007.md)은 D/R 각각 129 frames·9단계 및 독립 4프로세스/8캡처·16개 이미지 오차 0·validation 0·현재 해시 계약으로 RG5 완료/기성 10인일을 회수했다. PR 감사 당시의 예외/우선순위/공수 기록은 아래 역사로 보존한다. 현행 PHASE 4 계열은 총 355/기성 98/잔여 257(+미산정)이며 RG6 전환 전후 픽셀·비용 수용과 RG-V UI 수용은 남는다.

## 우선순위와 회계

1. RG5/RG6 수용 전에 기존 로컬 기록의 `dx12.decal`·`dx12.rendergraph` 예외를 재현하고 fixture의 GPU visibility 준비를 보완한다. GD2/GD3 visible-set/overflow/cut/resize 및 재질 route parity를 이어서 확인한다.
2. PHASE 14의 연속 파일/Stop tail, 별도 viewer, deep capture를 아래 세 후속 행에서 닫는다. 엔진 빌드와 source fixture 작성, 실제 Windows 실행 성공은 서로 다른 증거다.
3. W8에서 45 물리 px·하단 바·명시적 viewer 실행·비동기 준비의 취소/실패를 현재 화면 기준으로 검증한다. PR #125의 45 logical px 설명은 #128의 배율 적용 후 45 physical px로 대체됐다.
4. Player 구성별 native Present·Shipping 격리/배포, AU Windows/CLR·WASAPI/장치/soak를 각 정본에서 수용한다. Viewer의 D3D11 UI 호스트는 Editor DX12 정책이나 Vulkan 심층 수집 지원을 바꾸지 않는다.

RG6·RG-V는 소스가 병합됐으므로 dashboard의 todo를 progress로 정정하고 **earnedDays=0**을 명시한다. 기존 완료 기반은 유지한다. PHASE 4 계열 355인일/기성 88/잔여 267(+미산정)은 그대로다. PHASE 14 신규 후속 3행은 days=null, earnedDays=0으로 두며 임의의 구현률·공수·납기를 만들지 않는다. AU0~AU9는 progress를 유지한다.

문서 검증 결과: `verify-plan-dashboard.ps1 -RequireFullParse` 통과(TASKS 442개, 문자열/구조 오류 0, 전체 JavaScript 파싱·렌더 실행 성공, 진행률 유한값). 신규 상대 링크 37개가 존재하며, 439→442행의 차이는 14-REC/14-VIEW/14-DX 세 행뿐이다. 기존 행의 추정 공수 및 전체 기성 합계가 작업 전과 동일하고, RG6/RG-V와 신규 행의 상태·기성 0을 확인했다. `git diff --check`도 통과했다. 이 항목은 문서 검사이며 엔진 실행 수용이 아니다.
