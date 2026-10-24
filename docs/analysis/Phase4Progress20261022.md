# PHASE 4.x 현재 진척 재평가 — 2026-10-22

> 2026-10-23 갱신: RG8 현행 구현(16인일)과 RG9 범위·split·Inspector(10인일) 제품 수용. RG9 D/R 빌드·각 512프레임 GPU 및 전체 회귀 완료. 기본 OFF 유지. 추가 개선은 [RG8-F 미래 계획](../plans/RenderGraphDependencySchedulingPlan.md#rg8-f). TR3도 D/R 수명·SSGI GPU·제품 장면 픽셀 검증으로 5/5·5인일 수용했다. TR2도 native 7-route·SDK 5품질×3해상도·멀티뷰의 Debug/Release 픽셀 검증으로 4/4·5인일 수용했다. RG8·RG9·TR2·TR3 외 항목은 10월 22일 평가를 유지한다.

현재 master `ea87639ece7b6068456940ddddd83a9d23f86c58`와 작업트리의 구현, 전문 계획, 보존된 실행 기록을 대조했다. 대시보드 4.x 작업 90행 중 미완료 55행을 현재 구현과 남은 작업으로 다시 썼다. 기존 완료 기반은 유지했다.

## 현재 집계

| PHASE | 작업 진척 | 제품 수용 | 수용/기존 산정 인일 | 미산정 잔여 체크리스트 |
|---|---:|---:|---:|---|
| 4 | 100% | 100% | 18/18 | 없음 |
| 4.25 | 88% | 80% | 32/40 | LX-1 1/3, LX-3 3/5, LX-3H 2/4 |
| 4.3 | 100% | 100% | 100/100 | 없음 |
| 4.5 | 68% | 31% | 22/71 | 없음 |
| 4.7 | 6% | 6% | 2/35 | 없음 |
| 4.75 | 34% | 0% | 0/28 | 없음 |
| 4.8 | 37% | 0% | 0/9 | 없음 |
| 4.85 | 0% | 0% | 0/208 | 없음 |
| 4.9 | 0% | 0% | 0/22 | 없음 |
| **전체** | **41%** | **33%** | **174/531** | **LX 3행 별도** |

## 계산 규칙

- 작업 진척: 각 행의 명시적 완료 하위 산출물 수/전체 하위 산출물 수. 하위 산출물은 같은 비중이다. 페이즈는 이 비율을 기존 산정 인일로 가중한다. 이 수치는 작업 분해에 따른 계획 평가이며 실제 투입 시간·코드 커버리지·성능 향상률이 아니다.
- 제품 수용: 기존 완료 게이트로 인정한 `earnedDays / days`. 4.x 모든 행에 earnedDays를 명시했다. progress 상태가 자동으로 50%를 얻지 않는다. 2026-10-23 RG8은 최신 빌드·회귀·성능 기록과 사용자 수용 결정으로 16인일을 반영했다.
- 미산정 LX-1/3/3H는 공수 가중 분모에 넣지 않는다. 각각 1/3, 3/5, 2/4 하위 산출물을 별도로 보여 준다. 4.25의 88%/80%는 산정된 MAT 범위다.
- 전체는 531인일을 분모로 유지한다. 그중 208인일은 RTP의 과거 중앙 추정이다. 새 잔여 일정 견적을 확정한 수치가 아니다. 이전 대시보드의 355/106 같은 고정 합계는 현재 TASKS와 불일치하여 제거했다.
- 대시보드 상단 전체 링·공수 차트는 제품 수용 원장을 계속 사용한다. 4.x 비교표·페이즈 헤더·요약 카드에서 작업 진척과 제품 수용을 함께 표시한다.

## 평가로 바뀐 작업

- 4.25: Material Graph 공통 소비 경로는 기존 완료 기반이다. typed Reroute·그룹 명령·Blackboard schema/노드별 parameter 편집·revision/Save/Apply를 다시 만들 일에서 제외했다. 전용 저작 패널·전체 조작/HTTP 수명과 MAT-9 전 route/비용이 남았다.
- 4.3: explicit version/DAG·dependency recording·queue/batch·공통 barrier·native 병렬 번역/조기 handoff·arena 재사용이 있다. RG8은 2026-10-23 현행 범위를 제품 수용했다. 실제 제품 overlap/순성능 개선과 RG7 조합은 RG8-F 미래 계획으로 이관했고, RG9도 2026-10-23 범위·transient mip·동일 반복 pass split·Inspector와 D/R 검증을 수용했다. 현행 헤더는 subresource를 whole-resource로 보수 추적한다고 명시한다.
- 4.5: view별 제출 확정 history, SR 3종 adapter와 host 폴백, Player FG proxy/token/fence·HUD-less·latency marker가 있다. XeSS FG source도 Player provider 경계에 포함된다. 전체 품질/장치 행렬·실제 생성 output identity·물리 cadence/latency·최종 비용은 남았다. TR2의 7-route·SDK 품질·해상도·멀티뷰와 TR3의 수명·복구를 수용해 작업 진척 68%, 제품 수용 31%다.
- 4.75: IBL specular AO, 방향광 cascade/bias/blend, LX 그림자 동치 기록, PostChain AgX/bloom 구현을 반영해 RND-1/2/3을 todo에서 progress로 바꿨다. local probe, point/spot atlas·품질 단계, 자동 노출, 독립 IBL/profile 및 전체 품질/비용 수용은 남았다. ToneMapPassSetting의 auto exposure 필드만으로 자동 노출 실행을 완료 처리하지 않았다.
- 4.8: GPU-1은 GD raster/resident packet/REAC 기반과 제한 범위 실행 기록을 반영했다. GPU-9는 GD/GPU/RTP 소유권 분해가 이미 있어 progress로 바꿨다. stochastic·장치 probe·실험 예산·세 축 재견적이 남았다. 이 9인일은 원래 설계 규모이며 GD 제품 개발 전체 비용이 아니다.
- 4.7/4.85/4.9: L0 외 bake 구현, RT/PT 제품, 양 backend RenderDoc 비교는 미완료 그대로 유지했다. 공통 RHI나 래스터 구현을 이들 기능의 완료로 중복 계산하지 않았다.

## 확인한 최신 실행 기록의 범위

`Artifacts/master-integration-20261022/completion.json` · `Artifacts/master-integration-20261022/verification.json`은 master 통합의 Debug build·marker regression·native GPU 확인을 기록한다. 이 결과를 4.x 품질·SDK 전수·RG8 채택의 새 완료 게이트로 사용하지 않았다. 10월 10~12일 Temporal/그림자 결과는 해당 소스·바이너리/장치의 당시 기록으로 체크리스트에 반영했다. 이번 문서 작업에서는 엔진 빌드나 GPU 실행을 추가하지 않았다.

## 항목별 완료 산출물과 잔여

### LX-1 — 주석 저작·wire Reroute 삽입과 편집 왕복

작업 진척 1/3 · 기존 산정 미산정 · 제품 수용 0인일.

- [x] typed Reroute 노드와 compiler lowering
- [ ] annotation schema·저장 왕복
- [ ] wire 삽입 transaction·Undo·진단

근거: [Lattice/Material/LXMaterialNodes.cpp](https://github.com/29thnight/CreatorEngine/blob/ea87639ece7b6068456940ddddd83a9d23f86c58/Lattice/Material/LXMaterialNodes.cpp) · [Lattice/Material/LXMaterialCompiler.cpp](https://github.com/29thnight/CreatorEngine/blob/ea87639ece7b6068456940ddddd83a9d23f86c58/Lattice/Material/LXMaterialCompiler.cpp) · [Lattice/Core/LXDocument.h](https://github.com/29thnight/CreatorEngine/blob/ea87639ece7b6068456940ddddd83a9d23f86c58/Lattice/Core/LXDocument.h).

### LX-3 — Material 그룹·Blackboard 편집과 제품 조작 마감

작업 진척 3/5 · 기존 산정 미산정 · 제품 수용 0인일.

- [x] 그룹·boundary socket 문서 명령
- [x] typed Blackboard schema·IR·codegen
- [x] Material 창의 revision·저장·Apply 연결
- [ ] 그룹/interface·Blackboard 전용 저작 패널 완결
- [ ] 전체 제스처·DPI·저장 재개방 제품 수용

근거: [Lattice/Core/LXDocument.h](https://github.com/29thnight/CreatorEngine/blob/ea87639ece7b6068456940ddddd83a9d23f86c58/Lattice/Core/LXDocument.h) · [Lattice/Material/LXMaterialGraph.cpp](https://github.com/29thnight/CreatorEngine/blob/ea87639ece7b6068456940ddddd83a9d23f86c58/Lattice/Material/LXMaterialGraph.cpp) · [Editor/EngineGUIWindow/MaterialGraphWindow.cpp](https://github.com/29thnight/CreatorEngine/blob/ea87639ece7b6068456940ddddd83a9d23f86c58/Editor/EngineGUIWindow/MaterialGraphWindow.cpp).

### LX-3H — 열린 Material 문서 명령·HTTP 수명 수용

작업 진척 2/4 · 기존 산정 미산정 · 제품 수용 0인일.

- [x] 문서 ID·expected revision·Undo/Redo 명령
- [x] 제품 창의 source/settings revision·Save/Apply 관리
- [ ] 인증 HTTP의 동일 문서 명령 전수 대조
- [ ] host health·문서/씬 전환·종료·stale 무변경 수용

근거: [Lattice/Core/LXDocument.cpp](https://github.com/29thnight/CreatorEngine/blob/ea87639ece7b6068456940ddddd83a9d23f86c58/Lattice/Core/LXDocument.cpp) · [Editor/EngineEntry/Commands/MaterialGraphCommands.cpp](https://github.com/29thnight/CreatorEngine/blob/ea87639ece7b6068456940ddddd83a9d23f86c58/Editor/EngineEntry/Commands/MaterialGraphCommands.cpp) · [Editor/EngineGUIWindow/MaterialGraphWindow.cpp](https://github.com/29thnight/CreatorEngine/blob/ea87639ece7b6068456940ddddd83a9d23f86c58/Editor/EngineGUIWindow/MaterialGraphWindow.cpp).

### MAT-9 — 공통 재질의 전체 route 품질·비용 수용

작업 진척 2/5 · 기존 산정 8인일 · 제품 수용 0인일.

- [x] Core/Layered 상수·두 HDRI 제한 범위 비교
- [x] 균질 Volume 제한 범위 비교
- [ ] SSS·폐곡면 투과·texture/factor/normal-map 대조
- [ ] Deferred/Forward·alpha/transmission 전체 route 대조
- [ ] 모델·혼합 transport·scratch·cold/warm 전체 프레임 비용

근거: [docs/analysis/MAT9HdriConvergence.md](MAT9HdriConvergence.md) · [docs/analysis/MAT9SpecialProfileCorrection.md](MAT9SpecialProfileCorrection.md) · [docs/analysis/MAT7ForwardTransportComposition.md](MAT7ForwardTransportComposition.md).

### RG8 — 명시적 async compute·다중 큐 제품 수용

작업 진척 5/5 · 기존 산정 16인일 · 제품 수용 16인일.

- [x] queue capability·signal/wait·owner retirement
- [x] 의존 batch·allocator/list 재사용·공통 barrier 계획
- [x] 명시적 async 요청·graphics-only primary 경로
- [x] D/R 전체 회귀·제품 128출력 오차 0
- [x] 동일 뷰 OFF/ON 비교 후 현행 범위 제품 수용·추가 개선 미래 이관

2026-10-23 사용자 결정으로 현행 명시적 async·primary fallback 구현을 제품 수용. 기본 mode 0 유지, 제품 async 태그 0. 0509753a3+보존 로컬 변경 D/R 빌드·native 441/계획 82·전체 회귀 통과, 제품 128출력 오차 0. 진단 OFF 양방향 mode 2 전체 RT p50 -1.49/-2.84%, p95 -7.32/+1.67%. 추가 순성능·제품 pass opt-in·RG7 조합 검증은 RG8-F 미래 계획으로 이관. 수용 16인일; 성능 개선을 기본 활성화 조건으로 유지.

근거: [최신 통합·측정](RenderRg8MasterRefresh20261023.md) · [RG8-F 미래 계획](../plans/RenderGraphDependencySchedulingPlan.md#rg8-f). 기존 originalPhase4Tasks는 평가 전 이력으로 보존한다.

### RG9 — 범위·transient mip·split barrier·Inspector 제품 수용

작업 진척 4/4 · 기존 산정 10인일 · 제품 수용 10인일.

- [x] texture mip/layer/aspect·buffer byte-range 의존성 및 transient mip 생성/재사용
- [x] 동일 반복 pass 내 DX12 split BEGIN/END와 Full fallback
- [x] snapshot·HTTP·Inspector 범위/state/alias/queue 표시
- [x] 실패 복구·D/R 각 512프레임 반복·backend 지원 행렬

2026-10-23 RG9 완료·제품 수용 10인일. 기존 range 의존성·barrier·snapshot 재사용, transient mip 생성/풀/alias 키/program 서명 보완. 동일 반복 pass의 독립 phase 사이 DX12 split BEGIN/END 및 미지원 Full fallback, Inspector 범위/단계/큐 표시 구현. D/R 빌드 오류 0·각 512프레임/13,315검사·픽셀/validation 오류 0. 큐 실행 441·계획 82·전체 RG 회귀 D/R, software RHI Debug·CPU 범위/복구/풀/캐시·ImGui·할당 10항목 통과. Vulkan 실제 장치 비교는 4.9. RG8 기본 OFF와 RG8-F 미래 계획 유지.

근거: [구현·지원 행렬·검증](RenderRg9Implementation20261023.md) · [원장](RenderRg9Implementation20261023.json).

### FG0 — Player proxy present·수명 이후 실제 cadence 수용

작업 진척 2/5 · 기존 산정 8인일 · 제품 수용 0인일.

- [x] Player 전용 presenter·provider 선택
- [x] proxy swapchain·입력 fence·teardown 경계
- [ ] 실제 생성 output identity
- [ ] 물리 cadence·latency·frame pacing
- [ ] resize/device loss/teardown 전체 제품 수용

근거: [Player/PlayerTemporalDX12.cpp](https://github.com/29thnight/CreatorEngine/blob/ea87639ece7b6068456940ddddd83a9d23f86c58/Player/PlayerTemporalDX12.cpp) · [Player/PlayerDX12Presentation.cpp](https://github.com/29thnight/CreatorEngine/blob/ea87639ece7b6068456940ddddd83a9d23f86c58/Player/PlayerDX12Presentation.cpp) · [docs/analysis/TemporalClosureCheckpoint20261010.md](TemporalClosureCheckpoint20261010.md).

### FG1 — Player simulation·present latency marker와 물리 측정

작업 진척 2/4 · 기존 산정 4인일 · 제품 수용 0인일.

- [x] 게임 루프 InputSample/SimulationEnd marker
- [x] Player present marker·DLSS latency 연결
- [ ] provider별 지연 기능·실장치 지원 수용
- [ ] 외부 물리 latency·계측 비용 수용

근거: [Player/PlayerTemporalDX12.cpp](https://github.com/29thnight/CreatorEngine/blob/ea87639ece7b6068456940ddddd83a9d23f86c58/Player/PlayerTemporalDX12.cpp) · [Player/PlayerDX12Presentation.cpp](https://github.com/29thnight/CreatorEngine/blob/ea87639ece7b6068456940ddddd83a9d23f86c58/Player/PlayerDX12Presentation.cpp) · [docs/analysis/TemporalClosureCheckpoint20261010.md](TemporalClosureCheckpoint20261010.md) · [Player/PlayerMain.cpp](https://github.com/29thnight/CreatorEngine/blob/ea87639ece7b6068456940ddddd83a9d23f86c58/Player/PlayerMain.cpp).

### FG2 — FSR FG proxy·입력 연결 이후 실제 출력 수용

작업 진척 2/4 · 기존 산정 6인일 · 제품 수용 0인일.

- [x] FSR FG adapter·업스케일 독립 선택
- [x] Player proxy·입력/최종 소비 fence 연결
- [ ] 실제 generated identity·HUD·실패 복구
- [ ] 물리 pacing·장치별 품질/비용 수용

근거: [Player/PlayerTemporalDX12.cpp](https://github.com/29thnight/CreatorEngine/blob/ea87639ece7b6068456940ddddd83a9d23f86c58/Player/PlayerTemporalDX12.cpp) · [Player/PlayerDX12Presentation.cpp](https://github.com/29thnight/CreatorEngine/blob/ea87639ece7b6068456940ddddd83a9d23f86c58/Player/PlayerDX12Presentation.cpp) · [docs/analysis/TemporalClosureCheckpoint20261010.md](TemporalClosureCheckpoint20261010.md) · [Engine/RenderEngine/RHI/Temporal/Fsr/FsrBackendDX12.cpp](https://github.com/29thnight/CreatorEngine/blob/ea87639ece7b6068456940ddddd83a9d23f86c58/Engine/RenderEngine/RHI/Temporal/Fsr/FsrBackendDX12.cpp).

### FG3 — DLSS FG token·present 연결 이후 지원 장치 수용

작업 진척 2/4 · 기존 산정 4인일 · 제품 수용 0인일.

- [x] DLSS FG capability·options·fallback
- [x] Player token·HUD-less 입력·최종 소비 수명
- [ ] FG 지원 GPU에서 actual generated 출력 수용
- [ ] 장치별 MFG·latency/pacing·실패 복구 수용

근거: [Player/PlayerTemporalDX12.cpp](https://github.com/29thnight/CreatorEngine/blob/ea87639ece7b6068456940ddddd83a9d23f86c58/Player/PlayerTemporalDX12.cpp) · [Player/PlayerDX12Presentation.cpp](https://github.com/29thnight/CreatorEngine/blob/ea87639ece7b6068456940ddddd83a9d23f86c58/Player/PlayerDX12Presentation.cpp) · [docs/analysis/TemporalClosureCheckpoint20261010.md](TemporalClosureCheckpoint20261010.md) · [Engine/RenderEngine/RHI/Temporal/DlssTemporalAdapter.cpp](https://github.com/29thnight/CreatorEngine/blob/ea87639ece7b6068456940ddddd83a9d23f86c58/Engine/RenderEngine/RHI/Temporal/DlssTemporalAdapter.cpp).

### FG4 — HUD-less 입력·Editor 금지 이후 합성 출력 수용

작업 진척 2/4 · 기존 산정 4인일 · 제품 수용 0인일.

- [x] HUD-less color 입력과 Player 합성 경계
- [x] Editor viewport FG fail-closed 정책
- [ ] 실제 생성 프레임 HUD 합성·최종 소비 수용
- [ ] resize·device loss·복구 픽셀 수용

근거: [Player/PlayerTemporalDX12.cpp](https://github.com/29thnight/CreatorEngine/blob/ea87639ece7b6068456940ddddd83a9d23f86c58/Player/PlayerTemporalDX12.cpp) · [Player/PlayerDX12Presentation.cpp](https://github.com/29thnight/CreatorEngine/blob/ea87639ece7b6068456940ddddd83a9d23f86c58/Player/PlayerDX12Presentation.cpp) · [docs/analysis/TemporalClosureCheckpoint20261010.md](TemporalClosureCheckpoint20261010.md).

### TFG9 — 지원 행렬·generated 신원·whole-frame 최종 판정

작업 진척 1/4 · 기존 산정 4인일 · 제품 수용 0인일.

- [x] RTX 2080 Ti DX12 D/R 7-route·provider 부분 기록
- [ ] NVIDIA/AMD/Intel/통합 GPU 전수 행렬
- [ ] FG actual generated·실패·물리 latency/pacing
- [ ] 동조건 master 전체 프레임 성능·최종 수용

근거: [docs/analysis/TemporalClosureCheckpoint20261010.md](TemporalClosureCheckpoint20261010.md) · [docs/analysis/TemporalPhase45Closure20261011.md](TemporalPhase45Closure20261011.md).

### TR0 — 측정 provenance 연결 이후 전체 소비자·예산 수용

작업 진척 3/5 · 기존 산정 3인일 · 제품 수용 0인일.

- [x] 실/생성·render/display provenance 구조
- [x] renderer·profiler·저장 행 admission 연결
- [x] DX12 fixture·누락/변조 거부 검사 기록
- [ ] 진단 UI·기존 소비자 전수 및 제품 budget 정책
- [ ] actual generated identity·외부 물리 표시 계측

근거: [Engine/RenderEngine/Render/Temporal/TemporalMeasurementProvenance.h](https://github.com/29thnight/CreatorEngine/blob/ea87639ece7b6068456940ddddd83a9d23f86c58/Engine/RenderEngine/Render/Temporal/TemporalMeasurementProvenance.h) · [docs/analysis/TemporalMeasurementSurfaces20261011.md](TemporalMeasurementSurfaces20261011.md) · [docs/analysis/TemporalMeasurementConsumers20261010.md](TemporalMeasurementConsumers20261010.md).

### TR2 — render/display·jitter 구현 이후 다중 view 품질

작업 진척 4/4 · 기존 산정 5인일 · 제품 수용 5인일.

- [x] render/display extent 분리
- [x] 제출 기준 결정적 jitter·reset
- [x] DLSS 5품질·resize/lens D/R 비교 기록
- [x] 멀티뷰·전체 route/해상도 품질 수용

근거: [Engine/RenderEngine/Render/Temporal/TemporalUpscalerHost.cpp](https://github.com/29thnight/CreatorEngine/blob/ea87639ece7b6068456940ddddd83a9d23f86c58/Engine/RenderEngine/Render/Temporal/TemporalUpscalerHost.cpp) · [Engine/RenderEngine/Render/Scene/EnhancedTemporalViewState.h](https://github.com/29thnight/CreatorEngine/blob/ea87639ece7b6068456940ddddd83a9d23f86c58/Engine/RenderEngine/Render/Scene/EnhancedTemporalViewState.h) · [docs/analysis/TemporalClosureCheckpoint20261010.md](TemporalClosureCheckpoint20261010.md) · [docs/analysis/TemporalDlssLens20261011.md](TemporalDlssLens20261011.md).

2026-10-23: [TR2 구현·Debug/Release 전체 행렬 수용](TemporalTr2Resolution20261023.html). 2,632캡처·14,168개 전체 이미지 비교의 최대 차이는 0이다.

### TR3 — 제출 확정 history 이후 전체 수명·SSGI 정책

작업 진척 5/5 · 기존 산정 5인일 · 제품 수용 5인일.

- [x] view별 이전 camera/object/skin 입력
- [x] abort 시 history 미전진·성공 제출 commit
- [x] cut·resize·lens reset 검사 기록
- [x] 멀티뷰·투영 전환 전체 수명 수용
- [x] SSGI history 공용 정책 최종 수용

근거: [Engine/RenderEngine/Render/Temporal/TemporalUpscalerHost.cpp](https://github.com/29thnight/CreatorEngine/blob/ea87639ece7b6068456940ddddd83a9d23f86c58/Engine/RenderEngine/Render/Temporal/TemporalUpscalerHost.cpp) · [Engine/RenderEngine/Render/Scene/EnhancedTemporalViewState.h](https://github.com/29thnight/CreatorEngine/blob/ea87639ece7b6068456940ddddd83a9d23f86c58/Engine/RenderEngine/Render/Scene/EnhancedTemporalViewState.h) · [docs/analysis/TemporalClosureCheckpoint20261010.md](TemporalClosureCheckpoint20261010.md).

2026-10-23: [TR3 구현·D/R 픽셀 수용](TemporalTr3History20261023.html). SDK 전체 품질은 TR2/TU 소유 항목에서 추적한다.

### TU0 — provider 질의·폴백 구현 이후 제품 배포 수용

작업 진척 3/4 · 기존 산정 3인일 · 제품 수용 0인일.

- [x] 벤더 중립 capability·adapter 인터페이스
- [x] FSR/DLSS/XeSS host·실패 폴백
- [x] 12방향 전환·6오류 복구 D/R 기록
- [ ] optional SDK 배포·전체 제품 행렬 수용

근거: [Engine/RenderEngine/Render/Temporal/TemporalUpscalerHost.cpp](https://github.com/29thnight/CreatorEngine/blob/ea87639ece7b6068456940ddddd83a9d23f86c58/Engine/RenderEngine/Render/Temporal/TemporalUpscalerHost.cpp) · [Engine/RenderEngine/Render/Scene/EnhancedTemporalViewState.h](https://github.com/29thnight/CreatorEngine/blob/ea87639ece7b6068456940ddddd83a9d23f86c58/Engine/RenderEngine/Render/Scene/EnhancedTemporalViewState.h) · [docs/analysis/TemporalClosureCheckpoint20261010.md](TemporalClosureCheckpoint20261010.md) · [docs/analysis/TemporalPhase45Closure20261011.md](TemporalPhase45Closure20261011.md).

### TU1 — native 대조 경로의 전체 입력·품질 수용

작업 진척 2/3 · 기존 산정 2인일 · 제품 수용 0인일.

- [x] provider None native 경로
- [x] native 강제·A/B fixture 비교 기록
- [ ] 독립 대조 입력·전체 route/제품 수용

근거: [Engine/RenderEngine/Render/Temporal/TemporalUpscalerHost.cpp](https://github.com/29thnight/CreatorEngine/blob/ea87639ece7b6068456940ddddd83a9d23f86c58/Engine/RenderEngine/Render/Temporal/TemporalUpscalerHost.cpp) · [Engine/RenderEngine/Render/Scene/EnhancedTemporalViewState.h](https://github.com/29thnight/CreatorEngine/blob/ea87639ece7b6068456940ddddd83a9d23f86c58/Engine/RenderEngine/Render/Scene/EnhancedTemporalViewState.h) · [docs/analysis/TemporalClosureCheckpoint20261010.md](TemporalClosureCheckpoint20261010.md).

### TU2 — FSR adapter·실행 연결 이후 전체 품질 수용

작업 진척 2/3 · 기존 산정 5인일 · 제품 수용 0인일.

- [x] FSR DX12 adapter·입력/dispatch
- [x] host 선택·실패 복구와 D/R 부분 실행 기록
- [ ] 전체 품질·해상도·장면·비용 수용

근거: [Engine/RenderEngine/Render/Temporal/TemporalUpscalerHost.cpp](https://github.com/29thnight/CreatorEngine/blob/ea87639ece7b6068456940ddddd83a9d23f86c58/Engine/RenderEngine/Render/Temporal/TemporalUpscalerHost.cpp) · [Engine/RenderEngine/Render/Scene/EnhancedTemporalViewState.h](https://github.com/29thnight/CreatorEngine/blob/ea87639ece7b6068456940ddddd83a9d23f86c58/Engine/RenderEngine/Render/Scene/EnhancedTemporalViewState.h) · [docs/analysis/TemporalClosureCheckpoint20261010.md](TemporalClosureCheckpoint20261010.md) · [Engine/RenderEngine/RHI/Temporal/Fsr/FsrBackendDX12.cpp](https://github.com/29thnight/CreatorEngine/blob/ea87639ece7b6068456940ddddd83a9d23f86c58/Engine/RenderEngine/RHI/Temporal/Fsr/FsrBackendDX12.cpp).

### TU3 — DLSS SR 구현·5품질 기록 이후 제품 품질 수용

작업 진척 3/4 · 기존 산정 3인일 · 제품 수용 0인일.

- [x] NGX/Streamline adapter·capability·dispatch
- [x] 수동 노출·preset·startup/reset 연결
- [x] 5품질 warmed·startup·lens D/R 비교 기록
- [ ] 실제 모델·가변 시간·전체 route/해상도·비용 수용

근거: [Engine/RenderEngine/Render/Temporal/TemporalUpscalerHost.cpp](https://github.com/29thnight/CreatorEngine/blob/ea87639ece7b6068456940ddddd83a9d23f86c58/Engine/RenderEngine/Render/Temporal/TemporalUpscalerHost.cpp) · [Engine/RenderEngine/Render/Scene/EnhancedTemporalViewState.h](https://github.com/29thnight/CreatorEngine/blob/ea87639ece7b6068456940ddddd83a9d23f86c58/Engine/RenderEngine/Render/Scene/EnhancedTemporalViewState.h) · [docs/analysis/TemporalClosureCheckpoint20261010.md](TemporalClosureCheckpoint20261010.md) · [Engine/RenderEngine/RHI/Temporal/DlssTemporalAdapter.cpp](https://github.com/29thnight/CreatorEngine/blob/ea87639ece7b6068456940ddddd83a9d23f86c58/Engine/RenderEngine/RHI/Temporal/DlssTemporalAdapter.cpp) · [docs/analysis/TemporalDlssFinalBuild20261011.md](TemporalDlssFinalBuild20261011.md).

### TU4 — XeSS adapter·실행 연결 이후 전체 품질 수용

작업 진척 2/3 · 기존 산정 3인일 · 제품 수용 0인일.

- [x] XeSS DX12 adapter·입력/dispatch
- [x] host 선택·실패 복구와 D/R 부분 실행 기록
- [ ] 전체 품질·해상도·장면·비용 수용

근거: [Engine/RenderEngine/Render/Temporal/TemporalUpscalerHost.cpp](https://github.com/29thnight/CreatorEngine/blob/ea87639ece7b6068456940ddddd83a9d23f86c58/Engine/RenderEngine/Render/Temporal/TemporalUpscalerHost.cpp) · [Engine/RenderEngine/Render/Scene/EnhancedTemporalViewState.h](https://github.com/29thnight/CreatorEngine/blob/ea87639ece7b6068456940ddddd83a9d23f86c58/Engine/RenderEngine/Render/Scene/EnhancedTemporalViewState.h) · [docs/analysis/TemporalClosureCheckpoint20261010.md](TemporalClosureCheckpoint20261010.md) · [Engine/RenderEngine/RHI/Temporal/XeSSUpscaler.cpp](https://github.com/29thnight/CreatorEngine/blob/ea87639ece7b6068456940ddddd83a9d23f86c58/Engine/RenderEngine/RHI/Temporal/XeSSUpscaler.cpp).

### L1 — UV1 언랩 — xatlas 벤더링

작업 진척 0/1 · 기존 산정 5인일 · 제품 수용 0인일.

- [ ] 라이트맵은 non-overlapping UV 언랩이 전제인데 현 자산에 UV1 이 하나도 없다. xatlas 는 언랩과 아틀라스 패킹을 함께 해결하므로 삭제분의 패킹 문제 3종(Shelf FF · 오름차순 정렬 · 표면적 무관 렉트 크기)을 한 번에 닫는다. ThirdParty 원본 무수정 벤더링(fastgltf·ufbx·mikktspace 와 같은 규약).

근거: [docs/plans/LightmapBakerPlan.md](../plans/LightmapBakerPlan.md).

### L2 — BVH 재작성 — SAH + nth_element · off-by-one 수정

작업 진척 0/1 · 기존 산정 4인일 · 제품 수용 0인일.

- [ ] 삭제분은 median split + 축 라운드로빈(axis = depth % 3)이라 SAH 도 최장축도 아니고, 분할마다 std::sort 를 돌려 O(N log²N)이다(nth_element 면 O(N log N)). 비교자가 데이터를 쓰기까지 한다(t2.tempColor = ... — std::sort Compare 요구 위반).

근거: [docs/plans/LightmapBakerPlan.md](../plans/LightmapBakerPlan.md).

### L3 — 직접광 패스 — DX12 RHI 재작성 · 디스패치 낭비 제거

작업 진척 0/1 · 기존 산정 6인일 · 제품 수용 0인일.

- [ ] ★ 이 슬라이스의 본체는 디스패치를 렉트 크기로 바꾸는 것이다. 삭제분은 렉트마다 전체 캔버스를 디스패치하고 셰이더가 Offset/Size 밖에서 return 한다 — canvasSize=1024·rectSize=400 이면 유효 스레드가 15.6%, 84%가 스케줄되어 즉시 반환한다(렉트가 작을수록 악화, 128²면 98.5% 낭비). 렉트 표를 StructuredBuffer 로 올려 배치 디스패치 1회로 만들면 Dispatch N→1 이 되고 상태 변경도 사라진다. 라이트 루프를 MAX_LIGHTS(20) 고정에서 lightSize 로 바꾼다.

근거: [docs/plans/LightmapBakerPlan.md](../plans/LightmapBakerPlan.md).

### L4 — 백그라운드 베이킹 — 에디터를 막지 않는다

작업 진척 0/1 · 기존 산정 7인일 · 제품 수용 0인일.

- [ ] L4-a 별도 COMPUTE 큐 + 큐 간 펜스(현재 CreateCommandQueue 는 TYPE_DIRECT 하나뿐이고 AsyncCompute 코드 0건 — 같은 DIRECT 큐에 베이크를 넣으면 에디터 프레임과 직렬화된다). L4-b GPU 타임슬라이스(디스패치를 렉트·샘플 단위로 쪼개 프레임당 예산 안에). L4-c 씬 밀봉 스냅샷 + 편집 시 무효화. 진행률·취소·재개를 명시적 체크포인트로 — 코루틴 프레임에 상태를 숨기지 않는다.

근거: [docs/plans/LightmapBakerPlan.md](../plans/LightmapBakerPlan.md).

### L5 — 간접광 — progressive 누적

작업 진척 0/1 · 기존 산정 5인일 · 제품 수용 0인일.

- [ ] 샘플을 분할 디스패치로 쪼개 누적한다(indirectSampleCount 512 · indirectCount 2). numthreads occupancy 측정 후 조정 — 현재 32×32 = 1024 스레드/그룹으로 API 한계치인데 스레드마다 uint stack[64] BVH 스택을 들어 레지스터·스크래치 압박이 크다. 바운스마다 CreateTexture2D 로 텍스처 배열을 재생성하는 것도 제거(누수 의심).

근거: [docs/plans/LightmapBakerPlan.md](../plans/LightmapBakerPlan.md).

### L6 — dilate·seam 처리 — padding 원인 수정

작업 진척 0/1 · 기존 산정 3인일 · 제품 수용 0인일.

- [ ] 삭제분에서 주석 처리돼 있던 DilateLightMap 복원. ★ padding 계산을 고친다 — rect.y = nextPoint.h + padding 인데 shelf 증가는 maxHeight + padding 이라 의도한 padding 이 실제로 확보되지 않고 이웃 차트가 블리딩한다. MSAA 블러를 4회 도는 것이 이 증상을 뭉개고 있을 가능성이 크다.

근거: [docs/plans/LightmapBakerPlan.md](../plans/LightmapBakerPlan.md).

### L7 — 병렬화 판정 — 재고 나서 연다

작업 진척 0/1 · 기존 산정 3인일 · 제품 수용 0인일.

- [ ] L3(디스패치 낭비 제거)·L4(페이싱 제거)·L2(BVH)를 끝낸 뒤 재측정하고, 남은 병렬 축(async compute 겹침 · CPU 병렬 BVH 또는 GPU LBVH · 메시별 삼각형 수집)이 실제로 이득인지 판정한다.

근거: [docs/plans/LightmapBakerPlan.md](../plans/LightmapBakerPlan.md).

### RND-1 — 기존 specular AO 위에 local probe 제품 추가

작업 진척 1/4 · 기존 산정 5인일 · 제품 수용 0인일.

- [x] 공용 IBL specular occlusion 계산
- [ ] local probe 자산·캡처
- [ ] probe 우선순위·바인딩·제품 연결
- [ ] 독립 golden·비용 수용

근거: [Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/Ibl.slang](https://github.com/29thnight/CreatorEngine/blob/ea87639ece7b6068456940ddddd83a9d23f86c58/Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/Ibl.slang) · [docs/plans/RendererQualityPlan.md](../plans/RendererQualityPlan.md).

### RND-2 — 방향광 cascade 기반 위에 local atlas·품질 단계

작업 진척 2/5 · 기존 산정 8인일 · 제품 수용 0인일.

- [x] 방향광 cascade·bias·blend·공용 shadow 기반
- [x] LX masked/double-sided·indexed/meshlet 그림자 비교 기록
- [ ] point/spot atlas
- [ ] normal offset·far fade·3/4 cascade·quality tier 완결
- [ ] 전체 shadow 품질·비용 수용

근거: [Engine/RenderEngine/Render/Passes/Geometry/EnhancedShadowPass.h](https://github.com/29thnight/CreatorEngine/blob/ea87639ece7b6068456940ddddd83a9d23f86c58/Engine/RenderEngine/Render/Passes/Geometry/EnhancedShadowPass.h) · [Engine/RenderEngine/MaterialGraphSceneShadowStage.inl](https://github.com/29thnight/CreatorEngine/blob/ea87639ece7b6068456940ddddd83a9d23f86c58/Engine/RenderEngine/MaterialGraphSceneShadowStage.inl) · [docs/analysis/LxShadowCases20261012.md](LxShadowCases20261012.md).

### RND-3 — 기존 AgX·bloom 이후 자동 노출·display 수용

작업 진척 2/4 · 기존 산정 5인일 · 제품 수용 0인일.

- [x] PostChain AgX·ACES·display 변환 shader
- [x] bloom chain·post 순서의 제품 graph 연결
- [ ] 자동 노출 적응 경로 구현·연결
- [ ] pre-tone/OETF·효과별 golden·비용 수용

근거: [Dynamic_CPP/Assets/Shaders/DefaultPassShader/PostChainUber.slang](https://github.com/29thnight/CreatorEngine/blob/ea87639ece7b6068456940ddddd83a9d23f86c58/Dynamic_CPP/Assets/Shaders/DefaultPassShader/PostChainUber.slang) · [Engine/RenderEngine/Render/Passes/PostProcess/EnhancedPostChainPass.cpp](https://github.com/29thnight/CreatorEngine/blob/ea87639ece7b6068456940ddddd83a9d23f86c58/Engine/RenderEngine/Render/Passes/PostProcess/EnhancedPostChainPass.cpp) · [Engine/RenderEngine/Interfaces/ToneMapPassSetting.h](https://github.com/29thnight/CreatorEngine/blob/ea87639ece7b6068456940ddddd83a9d23f86c58/Engine/RenderEngine/Interfaces/ToneMapPassSetting.h).

### RND-ENV — 기존 배경 토글 이후 독립 IBL·환경 profile

작업 진척 1/4 · 기존 산정 10인일 · 제품 수용 0인일.

- [x] SkyBox 배경 토글·중립 fallback·기본 forest 선택
- [ ] reflected schema·GUID/manifest·default profile/Scene override
- [ ] 배경 색/세기·diffuse/specular 독립 weight 전 route
- [ ] 캐시/PSO 불변·Editor override 격리·cook·cold/warm 수용

근거: [docs/plans/EnvironmentRenderingPlan.md](../plans/EnvironmentRenderingPlan.md) · [Engine/RenderEngine/Assets/CookedEnvironment.cpp](https://github.com/29thnight/CreatorEngine/blob/ea87639ece7b6068456940ddddd83a9d23f86c58/Engine/RenderEngine/Assets/CookedEnvironment.cpp) · [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp](https://github.com/29thnight/CreatorEngine/blob/ea87639ece7b6068456940ddddd83a9d23f86c58/Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp).

### GPU-1 — GPU-driven 설계·raster 구현 이후 geometry 수용

작업 진척 3/5 · 기존 산정 3인일 · 제품 수용 0인일.

- [x] sealed geometry·meshlet·indirect·fallback 계약
- [x] resident packet·visibility·HZB·primitive cache 제품 연결
- [x] DX12 메시렛/그림자 동치의 제한 범위 실행 기록
- [ ] CPU 정답 가시 집합·용량 초과·cut/resize·전 route
- [ ] Release whole-frame 비용·VRAM·최종 geometry 수용

근거: [Engine/RenderEngine/GpuGeometryVisibility.cpp](https://github.com/29thnight/CreatorEngine/blob/ea87639ece7b6068456940ddddd83a9d23f86c58/Engine/RenderEngine/GpuGeometryVisibility.cpp) · [Engine/RenderEngine/GpuGeometryOcclusion.cpp](https://github.com/29thnight/CreatorEngine/blob/ea87639ece7b6068456940ddddd83a9d23f86c58/Engine/RenderEngine/GpuGeometryOcclusion.cpp) · [Engine/RenderEngine/MaterialGraphPrimitiveCache.h](https://github.com/29thnight/CreatorEngine/blob/ea87639ece7b6068456940ddddd83a9d23f86c58/Engine/RenderEngine/MaterialGraphPrimitiveCache.h) · [docs/analysis/REACPipelineImplementation20261019.md](REACPipelineImplementation20261019.md) · [docs/plans/GpuDrivenGeometryImplementationPlan.md](../plans/GpuDrivenGeometryImplementationPlan.md).

### GPU-2 — Stochastic Tile-Based Lighting 계약·실험 설계

작업 진척 0/2 · 기존 산정 2인일 · 제품 수용 0인일.

- [ ] sampling·reservoir·noise/denoise 계약
- [ ] 수직 슬라이스·지원/fallback·비용/품질 예산

근거: [docs/plans/GpuFeaturePlanningPlan.md](../plans/GpuFeaturePlanningPlan.md).

### GPU-3 — RT/PT/Hybrid 설계 이후 실제 지원·예산 확정

작업 진척 2/4 · 기존 산정 2인일 · 제품 수용 0인일.

- [x] 공통 AS/table·hit material·history와 첫 hard-shadow 계약
- [x] RTP-0~13·효과/연구별 소유권 분리
- [ ] 장치 capability probe·현재 코드 기준 상세 계약
- [ ] 실험별 자원/품질/whole-frame 예산 확정

근거: [docs/plans/PathTracingHybridPipelinePlan.md](../plans/PathTracingHybridPipelinePlan.md) · [docs/plans/DxrShaderTraversalExperimentPlan.md](../plans/DxrShaderTraversalExperimentPlan.md) · [docs/plans/GpuFeaturePlanningPlan.md](../plans/GpuFeaturePlanningPlan.md).

### GPU-9 — 세 GPU 축 의존 관계 이후 수직 슬라이스·재견적

작업 진척 1/4 · 기존 산정 2인일 · 제품 수용 0인일.

- [x] GD raster·GPU 설계·RTP 제품/연구 소유자 분리
- [ ] 세 축의 공통 의존·최소 수직 슬라이스 최종 확정
- [ ] 현행 구현에서 잔여 공수·예산 재견적
- [ ] 채택·지원 행렬 최종 설계 감사

근거: [docs/plans/GpuFeaturePlanningPlan.md](../plans/GpuFeaturePlanningPlan.md) · [docs/plans/GpuDrivenGeometryImplementationPlan.md](../plans/GpuDrivenGeometryImplementationPlan.md) · [docs/plans/PathTracingHybridPipelinePlan.md](../plans/PathTracingHybridPipelinePlan.md).

### RTP-0 — 현재 call path·장치 지원·세 모드 계약·baseline/예산·연구/코드 출처 봉인

작업 진척 0/1 · 기존 산정 5인일 · 제품 수용 0인일.

- [ ] 선행: GPU-3 상세 계약, 현재 코드. 수용: source/dirty diff/shader/asset/binary/driver manifest, fixture/tolerance·CPU/GPU/VRAM 예산, 지원·fallback 행렬

근거: [docs/plans/PathTracingHybridPipelinePlan.md](../plans/PathTracingHybridPipelinePlan.md).

### RTP-1 — RT RHI·정적 BLAS/TLAS·table·graph AS 의존/수명

작업 진척 0/1 · 기존 산정 18인일 · 제품 수용 0인일.

- [ ] 선행: RTP-0, GD0의 필요한 중립 계약, 제품 설치는 RG6. 수용: CPU ray-triangle oracle, instance remap, cold/warm/transform 갱신, abort/retirement·validation 0

근거: [docs/plans/PathTracingHybridPipelinePlan.md](../plans/PathTracingHybridPipelinePlan.md).

### RTP-10 — cage RT 변형·ray 변환·수명·두 모드 통합 평가

작업 진척 0/1 · 기존 산정 18인일 · 제품 수용 0인일.

- [ ] 선행: RTP-2/9, 기존 BLAS deformation reference. 수용: shared static geometry와 instance별 cage, hit/normal/t/alpha 의미, 기존 변형 대비 정확도·전체 비용/메모리

근거: [docs/plans/PathTracingHybridPipelinePlan.md](../plans/PathTracingHybridPipelinePlan.md).

### RTP-11 — Raster/Hybrid/PathTracing 설정·진단·프로파일

작업 진척 0/1 · 기존 산정 6인일 · 제품 수용 0인일.

- [ ] 선행: RTP-4/7, 선택 가능한 RTP-5/8/10 결과. 수용: frame seal에서 모드 확정, unsupported 이유·fallback 표시, sample/bounce/history/예산·저장/재로드

근거: [docs/plans/PathTracingHybridPipelinePlan.md](../plans/PathTracingHybridPipelinePlan.md).

### RTP-12 — 세 모드 제품 회귀·스트레스·계측 수용

작업 진척 0/1 · 기존 산정 18인일 · 제품 수용 0인일.

- [ ] 선행: RTP-4/7/11, 통합 대상 연구 결과. 수용: Mesh×RT, 두 view/in-flight, reload/resize/abort/device loss, D/R 실제 Editor/Player·validation 0·정상 종료

근거: [docs/plans/PathTracingHybridPipelinePlan.md](../plans/PathTracingHybridPipelinePlan.md).

### RTP-13 — 효과·연구별 채택 판정과 최종 페이즈 감사

작업 진척 0/1 · 기존 산정 5인일 · 제품 수용 0인일.

- [ ] 선행: RTP-5/8/10/12의 결과 또는 사유 있는 보류/기각 기록. 수용: 공통 RT·Hybrid·기본 PT 제품 수용, 두 연구의 재현/평가/통합 판정, 지원 행렬·raw evidence·잔여 범위/공수

근거: [docs/plans/PathTracingHybridPipelinePlan.md](../plans/PathTracingHybridPipelinePlan.md).

### RTP-2 — RT hit material·BSDF/light/environment 공통 evaluator

작업 진척 0/1 · 기존 산정 20인일 · 제품 수용 0인일.

- [ ] 선행: RTP-1, MAT 공통 산출물. 수용: Graph→paired output→RT hit 소비, sampling/PDF·재질 의미·invalid generation 거부

근거: [docs/plans/PathTracingHybridPipelinePlan.md](../plans/PathTracingHybridPipelinePlan.md).

### RTP-3 — RT/PT accumulation·재투영·denoising 입출력·history

작업 진척 0/1 · 기존 산정 16인일 · 제품 수용 0인일.

- [ ] 선행: RTP-2, 필요한 TR motion/view 계약. 수용: raw/accumulated/denoised 분리, 움직임·disocclusion·cut/resize/reload reset, 두 view 독립성

근거: [docs/plans/PathTracingHybridPipelinePlan.md](../plans/PathTracingHybridPipelinePlan.md).

### RTP-4 — Hybrid 첫 directional hard shadow

작업 진척 0/1 · 기존 산정 6인일 · 제품 수용 0인일.

- [ ] 선행: RTP-1/2, RG6. 수용: direct-only 적용, off-camera caster, light 단위 raster fallback, 기존 RT1 기준

근거: [docs/plans/PathTracingHybridPipelinePlan.md](../plans/PathTracingHybridPipelinePlan.md).

### RTP-5 — Hybrid 효과별 반사/GI·굴절·volume 경계·fog visibility 평가/통합

작업 진척 0/1 · 기존 산정 28인일 · 제품 수용 0인일.

- [ ] 선행: RTP-2/3/4, 관련 EXP 결과. 수용: 효과별 oracle/예산/합성·fallback, screen-space/RT/hybrid 비교 및 채택 기록

근거: [docs/plans/PathTracingHybridPipelinePlan.md](../plans/PathTracingHybridPipelinePlan.md).

### RTP-6 — 기본 offscreen progressive Path Tracer·reference

작업 진척 0/1 · 기존 산정 12인일 · 제품 수용 0인일.

- [ ] 선행: RTP-1/2. 수용: camera ray·다중 bounce·next-event estimation/MIS·Russian roulette, 고정 seed, 고샘플 수렴·에너지/PDF 검증

근거: [docs/plans/PathTracingHybridPipelinePlan.md](../plans/PathTracingHybridPipelinePlan.md).

### RTP-7 — interactive PathTracing 제품 graph·Editor Scene/Game·Player

작업 진척 0/1 · 기존 산정 10인일 · 제품 수용 0인일.

- [ ] 선행: RTP-3/6, RG6. 수용: 저샘플 trace→denoise→HDR/post/display, 지원 재질 범위, 예산·전환·실제 view 실행

근거: [docs/plans/PathTracingHybridPipelinePlan.md](../plans/PathTracingHybridPipelinePlan.md).

### RTP-8 — ReSTIR PT 기본 reservoir 계약 및 Enhanced 개선 구현/평가

작업 진척 0/1 · 기존 산정 28인일 · 제품 수용 0인일.

- [ ] 선행: RTP-3/6/7, 출처·라이선스 기록. 수용: reuse off/base PT/ReSTIR PT/Enhanced 비교, temporal/spatial validity·편향/상관·오차/총비용 판정

근거: [docs/plans/PathTracingHybridPipelinePlan.md](../plans/PathTracingHybridPipelinePlan.md).

### RTP-9 — tetrahedral cage 자산 전처리·cook/LOD·reference

작업 진척 0/1 · 기존 산정 18인일 · 제품 수용 0인일.

- [ ] 선행: RTP-0/1, 기존 deformation/cook 조사. 수용: cage·piece/remap·정적 mini-BLAS 입력, cook 재현성, 변형/경계/LOD 오차와 기존 자산 보존

근거: [docs/plans/PathTracingHybridPipelinePlan.md](../plans/PathTracingHybridPipelinePlan.md).

### BP-0 — 비교 입력·시간축·지원 행렬 고정

작업 진척 0/1 · 기존 산정 3인일 · 제품 수용 0인일.

- [ ] scene/asset/material/카메라/광원/seed/노출/해상도/히스토리 프레임열과 binary·driver 신원 고정

근거: [docs/plans/BackendParityPlan.md](../plans/BackendParityPlan.md).

### BP-1 — RenderDoc 양 backend 실제 프레임 캡처

작업 진척 0/1 · 기존 산정 3인일 · 제품 수용 0인일.

- [ ] DX12/Vulkan 별도 프로세스의 .rdc와 event/Pass 대응표, 캡처 재생 성공 및 반복 캡처

근거: [docs/plans/BackendParityPlan.md](../plans/BackendParityPlan.md).

### BP-2 — Pass별 입력·출력 리소스와 바인딩 대조

작업 진척 0/1 · 기존 산정 5인일 · 제품 수용 0인일.

- [ ] format/extent/mip/layer/채널/상수/descriptor/sampler/PSO/depth·blend·cull/수명 확인, 최초 불일치 event 특정

근거: [docs/plans/BackendParityPlan.md](../plans/BackendParityPlan.md).

### BP-3 — 선형 리소스 추출·픽셀별 수치 비교

작업 진척 0/1 · 기존 산정 4인일 · 제품 수용 0인일.

- [ ] GBuffer/depth/HDR/history/final의 RMSE·최대 오차·초과 좌표·차영상, NaN/Inf와 alpha 별도 검사

근거: [docs/plans/BackendParityPlan.md](../plans/BackendParityPlan.md).

### BP-4 — 불일치 원인 수정과 RenderDoc 재캡처

작업 진척 0/1 · 기존 산정 5인일 · 제품 수용 0인일.

- [ ] 확인된 backend/binding/state/history 결함 수정 후 같은 입력으로 재캡처, 기존 DX12 기준선 회귀 확인

근거: [docs/plans/BackendParityPlan.md](../plans/BackendParityPlan.md).

### BP-5 — 반복·실패 변이·회귀 패키지와 수용 보고서

작업 진척 0/1 · 기존 산정 2인일 · 제품 수용 0인일.

- [ ] Debug/Release·지원 장치별 결과, 누락/손상/다른 입력 거부, 미지원과 미통과 분리, .rdc/리소스/비교 결과 보존

근거: [docs/plans/BackendParityPlan.md](../plans/BackendParityPlan.md).


## 재계산 자료

[기계 판독 원장](Phase4Progress20261022.json)은 모든 미완료 항목의 체크리스트·근거·페이즈 합계와 수정 전 4.x 항목을 보존한다. 원래 일지의 상세 실행 수치와 이후 변경은 이 원장으로 역추적한다. 4.x 이외 TASKS는 수정 전후 객체 비교로 동일함을 확인했다.
