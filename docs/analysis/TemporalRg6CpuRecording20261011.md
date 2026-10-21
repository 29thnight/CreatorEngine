# TR0 — RG6 제품 CPU 그래프 기록 비용 개선

<!-- master-doc-review:start -->
## 2026-10-20 master 구현 대조

기준: `27de3f5a02aadde7456aab680db3184c36ee735e`. 대시보드는 변경하지 않는다. 아래 현행 대조와 소스 선언이 이전 시점의 구조·미구현 표기보다 우선한다.

[전체 대조·링크 정리 원장](MasterDocumentationAudit20261020.html)

### 현행 계약과 이 문서의 적용 범위

- camera cut revision·성공한 submission의 history·provider 설정과 profiler provenance를 보존한다. saved intent와 실제 지원/활성 결과를 구분한다.
- SDK/CPU Present 반환은 생성 프레임 신원이나 물리 표시 시각이 아니다. 과거 일부 품질 통과를 현재 모든 route·장치·FG/지연 수용으로 확대하지 않는다.

### 해당 구현에서 확인한 선언·소유 경계

- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h:78](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h#L78) — `enum class RGMeasurementDomain : uint8_t { Normal, Capture };`
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h:79](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h#L79) — `enum class RGQueueFallbackReason : uint8_t`
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h:108](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h#L108) — `enum class RGQueuePlacement : uint8_t`
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp:34](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp#L34) — `std::free(value);`
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp:4195](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp#L4195) — `bool EnhancedRenderGraph::RecordParallel(IRHIParallelCommandPool& pool, uint32_t workerCount,`
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp:4226](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp#L4226) — `bool EnhancedRenderGraph::RecordParallelInternal(IRHIParallelCommandPool& pool, uint32_t workerCount,`

### 이전 근거의 유효 범위

기존 세부 계약은 위 현행 구현과 모순되지 않는 범위에서 유지한다. 과거 수치·실행 통과·해시·장치 조건은 당시 기록이다. 로컬 산출물이 사라진 항목은 링크 대신 경로와 미보존 상태를 표시하며, 현재 master의 재실행 통과로 승격하지 않는다.

<!-- master-doc-review:end -->

<details data-master-history><summary>세부 계약·원래 분석과 당시 측정 기록</summary>

이전 동일 fixture A/B의 CPU record 중앙값은 Debug legacy 1.62630/product 3.66040ms,
Release legacy 1.09585/product 2.25195ms였다. 측정 구간은 기본 DX12 queue mode 0의
`EnhancedRenderGraph::RecordParallel` 전체다. Compile/prepare·queue mode 2 planning·GPU 시간·
전체 frame time과 구분한다. GPU validation은 계속 켜고 기능/패스/측정 경계를 줄이지 않는다.

## 원인 계측

첫 가설은 동일 command target의 불필요한 wave 경계와 idle target Job이었다. 이를 줄인
v2 전체 A/B는 회귀 게이트를 통과했지만 제품 CPU 중앙값은 Debug 3.62335ms,
Release 2.31115ms로 의미 있게 줄지 않았다. 실제 제품은 4 recording waves를 이미 하나의
공용 Job/worker job으로 합쳐 처리했고, legacy는 1 wave/1 dispatch/4 worker jobs였다.
따라서 Job 호출 횟수 자체를 주원인으로 판정하지 않는다.

별도 Release 진단 실행에서 normal-domain graph만 bounded 환경 계측했다.
준비 완료 프레임 1435/1499/1563/1630의 패스 기록 wall-time 합은 2.0477/2.4921/1.8359/2.0534ms,
각 4 target의 최대 합은 0.5865/0.6992/0.5059/0.6809ms였다. 패스 몸체 외 profiler/배리어 구간도
포함한다. 제품은 이 네 구간을 차례로 기록했다. 현재 파이프라인의 GPU RAW/WAR/WAW 의존성을
콜백 사이의 CPU 데이터 전달처럼 보수적으로 join하여 병렬성이 사라진 것이 주원인이다.
제품의 추가 current-depth occlusion도 비용은 있지만, 9단계 binding 생성 중앙값 합은 약
0.04ms이며 단계별 전체 callback은 대체로 0.02ms 정도다. cold PSO/transform 준비의 큰 표본은
정상 benchmark로 재사용하지 않았다.

진단 정본은 `Artifacts/PR169/tr0-rg6-cpu-diagnostic-20261011/diagnosis.json`과 editor.stderr.log다.
계측 소스 2개를 같은 디렉터리 instrumented-source에 보존하고 최종 제품 코드에서는 제거했다.
진단 실행 자체도 두 프로세스 정상 종료·native/픽셀/validation gate를 통과했다.
최종 성능 표본에는 계측 로그 출력/clock 구간을 섞지 않는다.

## 구현과 소유권

`DeclareRecordingIndependent(pass)`는 명시적인 저자 계약이다. unsplit callback은 준비된
불변 CPU 입력만 읽으며 다른 callback의 CPU 결과를 소비하지 않는다. 자체 관측값은
전용 저장소 또는 atomic에 쓰고 전체 recording join 이후에만 소비한다. compute queue 적합성과
별개이며, 외부 CPU side effect 또는 split callback의 선언을 거부한다. 미선언 소비자의
기존 split/cross-target CPU join, 외부 side-effect owner 경로와 예외 폐기는 유지한다.

이 계약을 확인한 current-depth pyramid/culler, SSAO/SSGI compute stages, post-chain,
camera/object motion, LX GBuffer·opaque Color, Deferred, SkyBox에 각각 명시했다.
특수/블렌드/전송 경로를 묶어서 자동으로 분류하지 않는다. GPU executeOrder·barriers·연속 target
분할·list submission 순서는 그대로다. 선언된 consumer만 cross-target GPU edge의 CPU join을
생략한다. 자체 target append 순서와 활성 target만 Job으로 호출하는 v2 개선도 유지한다.

CPU 기록 snapshot에 recordingWaves/jobDispatches/workerJobs를 동일 native provenance로
발행한다. owned queue mode의 미계측 카운터 0을 실제 Job 0회로 해석하지 않는다.
RG4 native 회귀는 기존 immediate/1·2·4 workers/fallback/split/CPU join 검사를 유지하고,
독립 GPU consumer의 four-target parallel recording·RAW pixel 동일 및 잘못된 side-effect/split/
compile 이후 독립 선언 거부를 추가한다. GPU retirement 이후에만 진단 assertion으로 종료한다.

## 검증 계약과 범위

최종 정본 경로는 `Artifacts/PR169/tr0-rg6-cpu-20261011-v3/final-result.json`이다.
SDK 활성 legacy/product × Debug/Release × 독립 2프로세스의 전체 RG6 행렬을 같은 현재 소스로
실행한다. 총 일반 CPU/GPU 표본 800개 및 graph capture 408개, 실제 원 명령 응답 일치·
입력/소스/장치 identity·정상 종료·GPU validation·pixel·120 population/3 comparator mutation
거부를 요구한다. 전후 제품 17 attachments도 그대로 비교한다. CPU 개선은 두 구성 모두
10% 이상과 독립 실행별 개선을 요구해 작은 시간 변동으로 완료 판정하지 않는다.

정적 authored plane/camera/light 332×182 범위이며 역사적 lattice/모든 asset/일반 제품 budget
또는 실제 UI·generated presentation/외부 물리 계측을 수용하지 않는다. 이전 결과 대비는
historical before/after이고 새 legacy/product A/B는 동일 현재 source inventory의 비교다.

## 결과

최종 SDK 활성 4빌드와 8독립 프로세스 모두 정상 종료했고 native RG 회귀·800 정상 CPU/GPU 표본·408캡처·120 population/3 cutover comparator 변이 거부를 통과했다. GPU validation 문제/누락 0, A/B 네 비교의 display 차이 및 전체 허용 초과 0, 제품 D/R 17 attachments 차이 0이다.

| 구성 | 현재 legacy CPU 중앙값 | 이전 제품 | 현재 제품 | 제품 감소 | 현재 제품 p95 |
| --- | ---: | ---: | ---: | ---: | ---: |
| Debug | 1.61310ms | 3.66040ms | 2.37030ms | 35.24% | 2.67290ms |
| Release | 1.12330ms | 2.25195ms | 1.48505ms | 34.05% | 2.01790ms |

독립 제품 실행별 중앙값은 Debug 2.36675/2.37150ms, Release 1.47425/1.51140ms다. 각 실행과 legacy 대비 비율 모두 10% 이상의 개선을 통과했다. 현재 정상 제품의 recording waves/dispatches/worker-job 호출 수는 2/2/5, legacy는 1/1/4다. 제품의 주요 4 target을 병렬로 기록하고 마지막 보수적 tail을 별도로 기록한다. worker-job 5는 작업 호출 합계이며 물리 worker 5개를 뜻하지 않는다.

변경 전후 제품은 두 구성 모두 17 attachments의 최대 오차 0이다. 최초 일반 동일-backend 비교는 그래프 진단 해시 불일치를 거부했고 실패 로그를 보존했다. 원본 그래프 차이는 generation/graphEpoch와 queueExecution.execution.recordingWaveCount(기존 compile depth 48 → 실제 capture recording waves 3)뿐이다. historical-cpu-compare.py는 원본을 먼저 전체 감사하고 이 입증된 CPU 진단 필드만 메모리에서 정규화한다. 입력·나머지 GPU graph hash·17개 픽셀 배열의 완전 동일을 요구하며 원본 manifest/픽셀을 수정하지 않는다. 리소스 역할·version edge·GPU 실행 순서·픽셀 훼손의 구성별 4변이, 총 8변이도 모두 거부했다. 전체 RG6 및 같은 구성/제품 D/R의 엄격한 비교기는 수정하지 않았다.

현재 제품은 legacy보다 Debug 46.94%, Release 32.20% 높은 CPU 기록 시간이다. 추가 제품 작업과 남은 보수적 기록 제약을 포함한 결과이며 동일 비용으로 닫지는 않는다. Release 최대 6.8659ms 표본도 제외하지 않았고 GPU/전체 frame budget 또는 모든 장면 성능의 수용으로 확대하지 않는다. TR0 progress·기성 0, PHASE 4.5 12/71 유지한다.

## 보관

30GiB 하한과 bounded capture 예산을 매 실행 검사한다. 각 성공 baseline은 비교에 필요한
두 process capture-0만 잠시 pin하고 나머지 raw는 즉시 정리하며, 최종 비교 후 하나로 줄인다.
변이 payload는 거부 직후 삭제하고 로그/manifest/hash/verdict는 보존한다. v2는 첫 가설 기각
기록으로 보존하며 재검증 원본은 대표만 남긴다. 최초 실패의 3.50GiB dump 삭제는 자동 정책
검사가 차단했고 구체적인 이유가 반환되지 않아 그대로 남겼다. 텍스트 stack과 실패 원인은 보존한다.

최종 대표 raw는 legacy/product × D/R의 4개다. 전후 비교 종료 뒤 이전 A/B·v2·진단 원본과 중복 reference 바이너리를 정리했고 historical verdict/manifest/hash/log는 보존했다. 이전 픽셀/바이너리 replay는 불가하며 최신 대표와 reference 최소 바이너리는 유지한다. v3 실행 중 raw 정리 실제 회수는 구성별 약 1.50/1.50/0.95/0.95GiB, 마지막 정리는 0.35GiB이며 최종 실제 여유 88.21GiB다. retained failed dump는 3.50GiB이며 자동 정책 차단으로 남는다.

</details>
