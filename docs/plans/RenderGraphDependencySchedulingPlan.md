# RenderGraph 리소스 의존성 스케줄링 계획 (PHASE 4.3 · 트랙 RG)

> 2026-10-20 정리 · master `27de3f5a02aadde7456aab680db3184c36ee735e` · 잔여 구현·수용 계획

판정 기준은 위 master의 커밋된 소스다. 작업 중인 로컬 코드 변경은 포함하지 않는다. 완료된 구현 단계·날짜별 실행 일지는 제거했다. 대시보드는 SRP 저작 전환 취소에 해당하는 항목만 제거하고, 나머지 상태·공수·링크를 보존한다.

[전체 문서 대조·충돌 판정](MasterPlanAudit20261020.html) · [정리 전 원문](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/docs/plans/RenderGraphDependencySchedulingPlan.md)

## 2026-10-23 RG8 제품 수용 — 현재 판정

현행 master `0509753a3`와 보존 로컬 변경을 제품 수용한다. DeclareAsyncCompute 명시적 요청, graphics-only 공통 primary 경로, 실제 compute의 owned 큐·GPU wait·retirement가 수용 범위다. 기본 mode 0과 제품 async 태그 0을 유지한다. 자동 비용 모델·배치 탐색은 제거된 상태다.

D/R 빌드와 native 441·계획 82·전체 회귀가 통과했고 제품 128출력 오차는 0이다. 진단 OFF 양방향 전체 RT p50은 mode 2가 1.49/2.84% 낮고 p95는 -7.32/+1.67%로 일관된 개선은 없었다. 사용자 결정에 따라 RG8을 done·수용 16인일로 종료한다. 아래 10월 22일 미수용 판정은 당시 이력이며 현재 판정은 이 절을 따른다. [측정 원장](../analysis/RenderRg8MasterRefresh20261023.md).

<span id="rg8-f"></span>

## RG8-F 미래 계획 — 제품 장면 전체 프레임 개선

현재 리팩토링 공수와 RG8 종료 조건에서 제외한다. 착수 시 대표 장면·대상 pass·예산을 다시 산정한다.

- 제품 compute pass의 독립 구간과 graphics 병렬 실행 여지를 측정하고 필요한 pass에만 DeclareAsyncCompute를 적용한다.
- reader join·경계 전이·제출 비용을 계측하고 RG7 aliasing OFF/ON 조합의 정확성·수명·메모리·성능을 검증한다.
- 같은 바이너리·장면·뷰 갱신·pacing·진단 OFF 조건에서 OFF/ON 순서를 뒤집어 전체 RenderThreadFrame과 GPU 프레임 p50/p95를 비교한다. 실제 compute/overlap과 제출 비용을 함께 기록한다.
- 픽셀·validation 회귀와 일관된 전체 프레임 개선을 확인한 대상만 opt-in한다. 기본 ON 전환은 해당 증거를 갖춘 뒤 결정한다. 자동 배치 탐색 재도입은 계획하지 않는다.

<span id="rg9"></span>

## 2026-10-23 RG9 제품 수용

기존 subresource 선언·의존성·barrier·snapshot을 재사용하고 transient mip 생성·풀/alias 키·program 서명, 동일 반복 pass phase 사이의 DX12 split과 Full fallback, Inspector 범위·split 단계·큐 표시를 마쳤다. 미세 buffer 접근은 DX12 whole-buffer 상태 단위를 지키며 Vulkan은 range 전이를 사용한다.

D/R 빌드 오류 0. 각각 512프레임·13,315검사에서 픽셀 오차·GPU validation 오류 0. 기존 큐 실행 441·계획 82·전체 RenderGraph 회귀 D/R, software RHI Debug, CPU 범위/복구/풀/캐시, ImGui 및 할당 10항목도 통과했다. RG9 done·10인일 수용으로 PHASE 4.3은 100/100인일이다. RG8 기본 OFF와 RG8-F 미래 개선을 유지한다. Vulkan 실제 장치 비교는 PHASE 4.9가 소유한다. [구현·지원 행렬·실행 원장](../analysis/RenderRg9Implementation20261023.md).

## 이전 판정 이력

### 2026-10-22 제품 장면·전체 프레임 비용 후속

현재 작업트리의 TestShadow에서는 mode 2가 실제 compute와 overlap을 실행한다. 직전 빈 SampleScene의 `insufficient-gain`을 제품 장면 전체의 탈락으로 확대하지 않는다. 같은 compiled 구조·queue assignment의 CPU hazard topology와 planning scratch를 재사용해 의존성 계산 중앙값을 2.12→0.56~0.58ms로 줄였다. Debug/Release native 437·planning 131·전체 RenderGraph GPU 회귀가 통과했다.

Release 양방향 수집에서 수정 후 전체 RenderThreadFrame p50은 OFF 3.18~3.45ms, mode 2 10.07~10.89ms였다. GPU span도 mode 2가 증가했다. 48 batch·23 wait·약 1,968 barrier의 제출/전이 비용을 약 0.20ms overlap이 회수하지 못한다. 이 측정으로 자동 배치 탐색 실험을 중단했다. 기본 OFF·progress·제품 수용 0을 유지하며, 후속 구현은 아래 명시적 async 정책을 따른다. [측정 조건·전후 p50/p95·검증 원장](../analysis/RenderRg8Overlap20261022.md).

### 2026-10-22 명시적 async compute로 전환

로컬 UE 5.6.1의 `FRDGPass` 파이프라인 지정과 RDG fork/join 구현을 대조했다. `DeclareComputeCompatible`은 실행 가능성만 나타내며, 새 `DeclareAsyncCompute`가 실제 compute 큐 실행을 요청한다. 그래프는 컴파일된 순서를 유지하고 RAW/WAR/WAW·물리 상태 소유권에 필요한 wait를 한 번 구성한다. GPU 비용 측정, 후보 그룹/개별 toggle, 실행 시간 시뮬레이션, 최소 예상 이득 판정과 탐색 전용 topology 캐시는 제거한다.

제품 패스에는 async 요청을 일괄 적용하지 않는다. 기존 제품 compute-compatible 패스는 graphics에 남고, mode 2도 명시적 요청이 없으면 `no-async-passes`로 graphics 실행한다. 기본 mode 0을 유지한다. 향후 제품 패스를 선택할 때 해당 패스 묶음의 OFF/ON 전체 프레임 개선을 확인한 뒤 선언을 추가한다. 전환 후 TestShadow 양방향 mode 2는 compute·탐색 0, 계획 p50 약 0.02ms이며, 전체 CPU는 OFF 3.19~3.28ms 대비 4.57~4.62ms로 owned graphics 비용이 남았다. RG7 조합과 제품 수용은 계속 미완료다. [전환 내용·검증](../analysis/RenderRg8ExplicitAsync20261022.md).

### 2026-10-22 후속 — graphics-only 제출 통합

compute가 없어도 mode 1·2가 owned 세 배치를 동기 제출해 RenderCommandRecord 안에서 RHI 번역 완료를 기다렸다. compiled plan에 실제 compute가 없으면 기존 primary graphics 비동기 경로를 재사용하도록 수정했다. 실제 async만 owned 경계를 열며 공통 준비의 미소비 upload/descriptor 예약을 넘겨받는다. compute queue/profiler 초기화와 빈 compute query도 실제 사용 시점으로 옮겼다.

D/R native 441·계획 82·전체 RG 회귀 통과, 제품 8캡처·128출력 오차 0. TestShadow 양방향 mode 2 전체 CPU p50은 4.606/4.624→3.340/3.347ms, 같은 순서 OFF 대비는 +52.0/+32.2%→-2.6/+4.7%다. 기본 OFF와 제품 async 미지정은 유지하며 순이득 채택으로 계산하지 않는다. [조건·대조군·GPU scope·원장](../analysis/RenderRg8OwnedGraphics20261022.md).

- RG8: 물리 자원 RAW/WAR/WAW·subresource/state·reader join·alias reuse/retirement를 공통 실행 계획으로 보존하며 명시적으로 async를 요청한 패스의 큐 간 동기화를 구성한다.
- 이미 구현된 explicit version/DAG·task dependency recording·batch/queue pool·barrier 계획·재사용 arena를 재작성하지 않는다. normal frame OFF/ON 전체 GPU 비용·overlap·fallback·RG7 조합을 현재 소스에서 수용해 기본 채택 여부를 판정한다.
- RG9: 운용 진단·실패 복구·장기 실행과 지원 행렬을 닫는다. 10월 9일의 RG8 done 기록은 후속 철회된 판정이며 활성 완료로 사용하지 않는다.

추가된 master 변경도 대조했다. 선언 usage/import 인덱스 재사용, frozen recording의 독립 target 병렬 번역과 sealed batch 조기 handoff는 구현 기반에 포함한다. 이 변경을 새 저작 체계나 RG8 전체 제품 수용 완료로 확대하지 않는다.

추가된 master 변경도 대조했다. 선언 usage/import 인덱스 재사용, frozen recording의 독립 target 병렬 번역과 sealed batch 조기 handoff는 구현 기반에 포함한다. 이 변경을 새 저작 체계나 RG8 전체 제품 수용 완료로 확대하지 않는다.

## 적용할 구조와 경계

`live pipeline → explicit graph versions → dependency recording → queue execution → completion retirement`

위 경로는 소스에서 확인한 구현 기반이다. 아래 요구의 수용 결과를 뜻하지 않는다. 구현을 다시 만드는 항목과 제품 검증을 혼합하지 않는다.

## 현재 추적 상태

| 추적 ID | 소유 단위 (잔여 작업은 위 목록) | 대시보드 보존 상태 |
|---|---|---|
| `RG8` · PHASE 4.3 | 명시적 async·primary fallback 제품 수용 | done · 16인일 |
| `RG8-F` · 미래 계획 | 제품 pass opt-in·RG7 조합·전체 프레임 개선 | 보류 · 현재 공수 제외 |
| `RG9` · PHASE 4.3 | 범위·transient mip·동일 반복 pass split·Inspector | done · 10인일 |

대시보드 상태는 인계용 식별 정보다. 위 구현 기반에서 이미 충족한 하위 요구는 재작성하지 않고, 남은 제품/실패/비용 수용을 수행한다.

## master 소스 근거

모든 행의 파일·행 번호·Git blob은 고정 master에서 대조했다. 소스 존재·배선 확인과 실행 성공을 구분한다.

- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h:75](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h#L75) — `enum class RGSchedulingMode { DeclarationOrder, ExplicitSingleWriter, ExplicitVersioned };`
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp:188](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp#L188) — `if (graph->m_scheduling == RGSchedulingMode::ExplicitVersioned && !graph->ValidVersionHandle(handle)) return {};`
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraphQueues.cpp:6](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Graph/EnhancedRenderGraphQueues.cpp#L6) — `void EnhancedRenderGraph::RequireQueueIdle() const`
- [Engine/RenderEngine/Render/Graph/RenderGraphDeclarationTask.h:8](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Graph/RenderGraphDeclarationTask.h#L8) — `class render_graph_declaration_lock final`
- [Engine/RenderEngine/RHI/IRHICommandTranslator.h:54](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/RHI/IRHICommandTranslator.h#L54) — `virtual bool Translate(RHISoftwareBatch&& softwareBatch, const RHICommandTranslationOptions& options,`
- [Engine/RenderEngine/RHI/RHICommandTranslator.cpp:6](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/RHI/RHICommandTranslator.cpp#L6) — `bool RHICommandTranslator::Translate(RHISoftwareBatch&& softwareBatch, const RHICommandTranslationOptions& options,`
- [Engine/RenderEngine/RHI/RHISubmissionThread.cpp:1099](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/RHI/RHISubmissionThread.cpp#L1099) — `bool RHISubmissionThread::EnqueueSoftwareBatch(const void* owner, IRHIDeviceResources& completionSource,`
- [Engine/RenderEngine/RHI/IRHICommandTranslator.h:54](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/RHI/IRHICommandTranslator.h#L54) — `virtual bool Translate(RHISoftwareBatch&& softwareBatch, const RHICommandTranslationOptions& options,`
- [Engine/RenderEngine/RHI/RHICommandTranslator.cpp:6](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/RHI/RHICommandTranslator.cpp#L6) — `bool RHICommandTranslator::Translate(RHISoftwareBatch&& softwareBatch, const RHICommandTranslationOptions& options,`
- [Engine/RenderEngine/RHI/RHISubmissionThread.cpp:1099](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/RHI/RHISubmissionThread.cpp#L1099) — `bool RHISubmissionThread::EnqueueSoftwareBatch(const void* owner, IRHIDeviceResources& completionSource,`

## 검증 경계

2026-10-20 정리는 master 소스·문서 전체의 소유권/충돌/링크/HTML 검증이며 새 엔진 실행 결과가 아니다. 2026-10-22 작업트리의 빌드·회귀·제품 장면 성능 실행은 위 후속 절과 연결 원장에 별도로 기록했다. 과거 통과는 해당 소스/바이너리 신원에만 유효하다. 남은 제품 수용은 위 요구를 실제 산출물로 닫아야 한다.

<details><summary>이전 절 링크 호환</summary>

이전 절의 완료 이력·구안은 제거했다. 이 링크로 들어온 경우 위의 남은 작업과 단일 소유 계획을 따른다.

<span id="0-결정-요약"></span>
<span id="1-현재-코드-기준선"></span>
<span id="2-목표-계약"></span>
<span id="2026-10-05-소스-구현-동결독립-최종-정적-검토-완료--실행-수용과-분리"></span>
<span id="2026-10-06-병합-뒤-첫-실행--성능-회귀-하나-수정-붉은-검사-남음"></span>
<span id="2026-10-07-rg5-종결-검사-후속"></span>
<span id="2026-10-07-rg6-제품-수용-완료"></span>
<span id="2026-10-07-최근-병합-반영"></span>
<span id="21-리소스와-버전"></span>
<span id="22-의존-edge"></span>
<span id="23-compile-순서"></span>
<span id="24-저작-순서와-실행-순서"></span>
<span id="3-구현-순서와-공수"></span>
<span id="4-슬라이스별-구현-경계"></span>
<span id="5-회귀와-실패-판정"></span>
<span id="6-비목표와-금지선"></span>
<span id="7-다른-페이즈트랙과의-의존성"></span>
<span id="q0--큐timeline-rhi-공통-기반-2026-10-08-완료"></span>
<span id="rg-v--compiled-graph-읽기-전용-viewer-2026-10-01-추가"></span>
<span id="rg0--기준선을-먼저-잠근다"></span>
<span id="rg1--최소-접근-선언으로-단일-writer-dag부터-정렬한다"></span>
<span id="rg2--버전으로-다중-writermodify-dag를-연다"></span>
<span id="rg3rg4--기존-기능을-새-순서에-다시-연결한다"></span>
<span id="rg5-남은-이관-순서--2026-10-04-기준선-이력"></span>
<span id="rg5rg6--제품을-한-번만-전환한다"></span>
<span id="rg7--aliasing은-정확성-완료-뒤-연다"></span>
<span id="rg7-종료-기준-고정--2026-10-08"></span>
<span id="rg8--async-compute는-계측으로-채택한다"></span>
<span id="rg9--rdg-운용-성숙도를-닫는다"></span>
<span id="sec-0-결정-요약"></span>
<span id="sec-1-현재-코드-기준선"></span>
<span id="sec-2-목표-계약"></span>
<span id="sec-2026-10-05-소스-구현-동결독립-최종-정적-검토-완료--실행-수용과-분리"></span>
<span id="sec-2026-10-06-병합-뒤-첫-실행--성능-회귀-하나-수정-붉은-검사-남음"></span>
<span id="sec-2026-10-07-rg5-종결-검사-후속"></span>
<span id="sec-2026-10-07-rg6-제품-수용-완료"></span>
<span id="sec-2026-10-07-최근-병합-반영"></span>
<span id="sec-3-구현-순서와-공수"></span>
<span id="sec-4-슬라이스별-구현-경계"></span>
<span id="sec-5-회귀와-실패-판정"></span>
<span id="sec-6-비목표와-금지선"></span>
<span id="sec-7-다른-페이즈트랙과의-의존성"></span>
<span id="sec-rg-v--compiled-graph-읽기-전용-viewer-2026-10-01-추가"></span>
<span id="sec-rg5-남은-이관-순서--2026-10-04-기준선-이력"></span>

</details>
