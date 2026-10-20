# 공용 thread_pool · job_scheduler

<!-- master-doc-review:start -->
## 2026-10-20 master 구현 대조

기준: `27de3f5a02aadde7456aab680db3184c36ee735e`. 대시보드는 변경하지 않는다. 아래 현행 대조와 소스 선언이 이전 시점의 구조·미구현 표기보다 우선한다.

[전체 대조·링크 정리 원장](../analysis/MasterDocumentationAudit20261020.html)

### 현행 계약과 이 문서의 적용 범위

- 공용 인프라의 작업 실행·완료와 컨테이너 저장 수명은 호출자 계약을 따른다. job handle의 완료·join과 저장소/자원 owner를 구분한다.
- 과거 PPL/독립 thread pool 이행 제안은 현재 공용 구현과 실제 소비자를 먼저 대조한다. 기반 구현의 존재가 모든 병렬 소비자의 취소·종료·비용 수용을 뜻하지 않는다.

### 해당 구현에서 확인한 선언·소유 경계

- [Engine/Utility_Framework/JobScheduler.h:13](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/Utility_Framework/JobScheduler.h#L13) — `class job_handle`
- [Engine/Utility_Framework/JobScheduler.h:28](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/Utility_Framework/JobScheduler.h#L28) — `explicit job_handle(std::shared_ptr<job_detail::completion_state> state) : state_(std::move(state)) {}`
- [Engine/Utility_Framework/JobScheduler.h:50](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/Utility_Framework/JobScheduler.h#L50) — `void on_complete(std::function<void(std::exception_ptr)> observer) { terminal_observer_ = std::move(observer); }`
- [Engine/Utility_Framework/JobScheduler.cpp:198](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/Utility_Framework/JobScheduler.cpp#L198) — `throw std::logic_error("pool workers must express dependencies, not block on job_handle::wait");`
- [Engine/Utility_Framework/JobScheduler.cpp:270](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/Utility_Framework/JobScheduler.cpp#L270) — `job_handle job_scheduler::submit_after(std::span<const job_handle> dependencies, job_group group)`
- [Engine/Utility_Framework/JobScheduler.cpp:318](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/Utility_Framework/JobScheduler.cpp#L318) — `job_handle job_scheduler::submit_indexed(std::size_t count, std::function<void(std::size_t)> task)`
- [Engine/Utility_Framework/ThreadPool.h:5](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/Utility_Framework/ThreadPool.h#L5) — `class job_scheduler;`
- [Engine/Utility_Framework/ThreadPool.h:8](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/Utility_Framework/ThreadPool.h#L8) — `class thread_pool`
- [Engine/Utility_Framework/ThreadPool.h:23](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/Utility_Framework/ThreadPool.h#L23) — `struct worker_hooks`
- [Tools/regression/test-recording-jobs.py:63](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/test-recording-jobs.py#L63) — `for identity, unit in enumerate(trace["units"]):`
- [Tools/regression/test-recording-jobs.py:114](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/test-recording-jobs.py#L114) — `class RecordingJobsTests(unittest.TestCase):`
- [Tools/regression/test-recording-jobs.py:439](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/test-recording-jobs.py#L439) — `class RecordingTopologyTests(unittest.TestCase):`
- [Tools/regression/analyze-recording-jobs.py:128](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/analyze-recording-jobs.py#L128) — `raise ValueError("schema 2 policy must contain only ownerWaitMode='blocking'")`
- [Tools/regression/analyze-recording-jobs.py:130](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/analyze-recording-jobs.py#L130) — `raise ValueError("schema 2 must not contain removed ownerWait/workerSpin sections")`
- [Tools/regression/analyze-recording-jobs.py:259](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/analyze-recording-jobs.py#L259) — `raise ValueError("available topology must not have truncated nodes, units, or topology")`
- [Tools/regression/recording_task_graph_probe.cpp:80](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/recording_task_graph_probe.cpp#L80) — `Require(thread_pool::is_worker_thread(), "caller executed recording work");`
- [Tools/regression/recording_task_graph_probe.cpp:274](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/recording_task_graph_probe.cpp#L274) — `Require(graph.nodes.empty() && topology.unitNodes.empty(), "empty topology retained previous units");`
- [Tools/regression/recording_task_graph_probe.cpp:479](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/recording_task_graph_probe.cpp#L479) — `Require(std::this_thread::get_id() != submitter && thread_pool::is_worker_thread(),`
- [Tools/regression/verify-recording-task-graph.ps1:4](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-recording-task-graph.ps1#L4) — `$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))`
- [Tools/regression/verify-recording-task-graph.ps1:34](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-recording-task-graph.ps1#L34) — `$stdout = [IO.File]::ReadAllText("$out/$name.out")`
- [Tools/regression/verify-recording-task-graph.ps1:35](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-recording-task-graph.ps1#L35) — `$stderr = [IO.File]::ReadAllText("$out/$name.err")`
- [Engine/RenderEngine/DataSystem.h:307](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/DataSystem.h#L307) — `own::shared_owner<AssetBundlePreparation> LoadAssetBundleAsync(const AssetBundle& bundle);`
- [Engine/RenderEngine/DataSystem.h:522](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/DataSystem.h#L522) — `std::string& error, own::shared_owner<const Material>* published = nullptr);`
- [Engine/RenderEngine/DataSystem.h:809](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/DataSystem.h#L809) — `job_handle SubmitAssetWorkLocked(job_group work,`

### 이전 근거의 유효 범위

기존 세부 계약은 위 현행 구현과 모순되지 않는 범위에서 유지한다. 과거 수치·실행 통과·해시·장치 조건은 당시 기록이다. 로컬 산출물이 사라진 항목은 링크 대신 경로와 미보존 상태를 표시하며, 현재 master의 재실행 통과로 승격하지 않는다.

<!-- master-doc-review:end -->

<details data-master-history><summary>세부 계약·원래 분석과 당시 측정 기록</summary>

2026-09-20 확정. [코딩 컨벤션](CodingConventions.md)과
[PHASE 13 계획](../plans/AnimationSchedulerPlan.md)의 공용 실행 기반이다.

## 결정

목표는 실행 기반의 통일이다. 백엔드는 기존 vcpkg baseline의 enkiTS 1.12로 고정한다.
2026-10-18 추가 기록 spin은 재측정에서 이득이 재현되지 않아 전용 오버레이와 함께 제거했다.
성능 비교는 최초 통일의 완료 조건에서 제외한다. 애니메이션의 워커 수 기준 청크와 제출 저장소 정리는
PHASE 13 S6에서 완료했다. 워커 수 정책 자체와 다른 소비자의 최적화는 후속이다.

엔진이 시작 때 워커를 만들고 종료 때 회수한다. Editor와 Player 모두
`EngineBootstrap::InitializeRuntime`에서 공용 스케줄러를 시작한다. 씬이나 소비자는
풀을 생성·종료하지 않는다. 유휴 워커의 sleep/wake와 작업 스틸링은 enkiTS가 담당한다.
모든 워커를 항상 spin시키거나 작업마다 스레드를 만드는 설계가 아니다.

공용 실행 유틸리티는 STL 표기인 `thread_pool`, `job_scheduler`, `job_group`,
`job_handle`을 쓴다. 접근자는 `ce::get_thread_pool()`과 `ce::get_job_scheduler()`이다.
파일명은 저장소 규칙에 따라 `ThreadPool.h/.cpp`, `JobScheduler.h/.cpp`이다.
씬·애니메이션 정책을 맡는 엔진 객체 `AnimationScheduler`는 C# 표기를 유지한다.

## 소유와 API

- `thread_pool`: enkiTS 워커·실행 배치의 수명 소유자. 소비자의 직접 제출은 막고
  `job_scheduler`만 내부 `dispatch`에 접근한다.
- `job_scheduler`: 그룹 제출, 완료 토큰, 의존 작업, 범위 분할을 제공한다.
  별도 실행 큐나 work stealing 알고리즘은 구현하지 않는다.
- `job_group`: 한 제출자가 작업을 모으는 이동 전용 builder. `submit(std::move(group))`
  시 봉인한다. 실행 중인 그룹에 작업을 추가하지 않는다.
- `job_handle`: 복사 가능한 완료 토큰. `wait()`는 해당 그룹의 콜백과 캡처 해제가
  끝날 때까지 기다린다. 다른 그룹이나 풀 전체를 기다리지 않는다. 토큰을 버려도
  작업을 취소하거나 호출자를 대기시키지 않는다.
- `submit_after(dependencies, group)`: 같은 스케줄러의 유효 토큰만 받는다.
  선행 작업이 모두 끝난 뒤 제출하며, 기다리는 그래프는 워커를 점유하지 않는다.
  선행 오류는 종속 작업 본문을 건너뛰고 전달한다. 빈/실패 의존 체인은 반복적으로
  전파하여 긴 체인이 워커 스택을 소진하지 않게 한다.
- `parallel_for(count, grain, callback)`: `[begin,end)` 범위로 분할한다.
  `grain == 0`은 오류, `count == 0`은 정상적인 빈 작업이다.
- `submit_indexed(count, callback)`: 한 콜백을 인덱스 범위의 enkiTS 배치로
  제출한다. 인덱스마다 `std::function`을 만들지 않는다. 호출자는 완료까지
  콜백이 캡처한 저장소를 유지한다. 0개는 본문 없는 완료 토큰이다.
- `submit_graph(nodes, callback, diagnostics)`: 위상 순서로 고정한 전체 DAG를
  한 번 수락하고 한 개의 dispatcher handoff로 게시한다. 간선은 중복 없는 이전
  노드 ID만 받으며 잘못된 그래프는 일부 본문이 시작되기 전에 거절한다.
  후속 노드는 enki 의존성으로 직접 게시한다. 선행 실패의 후손은 본문을 건너뛰고
  독립 가지는 끝까지 실행한다. 최종 토큰은 공유 콜백 캡처를 해제한 뒤 완료한다.

각 그룹은 첫 예외를 보존하고 나머지 그룹 구성원은 끝까지 실행한다. `wait()`가
그룹 오류를 다시 던진다. 버린 토큰의 오류는 별도 전역 오류 큐로 전달하지 않으므로,
결과를 무시하는 제품 작업은 본문에서 실패를 자신의 완료 경로로 보고해야 한다.

워커에서 아직 끝나지 않은 토큰을 `wait()`하면 `logic_error`를 낸다. 풀 고갈을
피하려면 의존 작업을 사용한다. 제품의 동기식 LoadAssetBundle·AnimationScheduler·Foliage
진입점은 호스트/게임 스레드에서 호출한다. 임의의 공용 워커 안에서 이 동기 경로를
중첩 호출하는 것은 지원하지 않는다.

## 실행과 종료 경계

외부 제출자는 그룹당 한 개의 pinned handoff를 전용 dispatcher에 게시한다. dispatcher가 일반
enkiTS task set을 제출하여 범위 분배와 스틸링이 작동한다. enkiTS 큐가 가득 차면
dispatcher 또는 선행 완료 worker가 본문을 inline 실행할 수 있다. 그래도
사용자 콜백은 제출자/UI 스레드에서 실행하지 않는다. handoff에는 제출 호출과 완료
각각의 소유 참조가 있어, enkiTS의 게시 후 필드 접근이 끝나기 전에 해제하지 않는다.

기본 워커 수는 기존 공용 풀의 논리 CPU 수 정책을 유지한다. `start(n)`으로 지정할
수 있다. CPU 예산/우선순위 조정은 후속 최적화다. 라이프사이클 호출은 엔진 소유
스레드가 직렬화하며 워커에서 종료할 수 없다. 한 풀의 제출·종료 소유자는 한
스케줄러다. 풀은 스케줄러보다 오래 살아야 한다.

종료 시 새 제출을 막고 이미 접수한 미실행 의존 작업까지 완료한 다음 enkiTS
워커를 join한다. 중지 중 제출과 시작 전/종료 후 제출은 실패하며 inline fallback은 없다.
Editor/Player는 새 프레임 생산이 끝난 뒤 씬 로드와 Scene의 AI 작업을 먼저 회수하고 CLR을
종료한다. 씬 해체는 공용 풀이 살아 있는 동안 끝내고, 공용 풀은 DataSystem보다 먼저
종료한다. AI 작업은 Entity/Component 파괴·DDOL 이송 경계에서도 회수한다.

## 이관 범위

DataSystem 번들 로드, BrowserThumbnailCache, AnimationScheduler, Foliage 범위 처리,
Scene AI 갱신, DX12 PSO 비동기 컴파일, SceneManager 비동기 씬 준비와 DX12/Vulkan 명령 기록이 공용 스케줄러를 사용한다. 구 WorkerPool과 Animation 전용 풀 및
사용처가 없어진 Core.ThreadPool/Core.Thread/Core.CountingSemaphore는 제거한다.
AnimationScheduler의 평가 알고리즘·게임 스레드 이벤트 전달·포즈 게시 순서는 보존한다.

Presentation/Render/GPU 제출·소켓·파일 감시 등 장기 루프는 전용 스레드 역할이다.
명령 기록 자원 풀은 백엔드에 남지만 자체 실행 스레드는 소유하지 않는다.

재현 가능한 검사와 실행 결과는
[TaskSchedulerUnificationPlan §9](../plans/TaskSchedulerUnificationPlan.md#9-공용-스케줄러-구현-2026-09-20)에 기록한다.

## DX12 PSO 비동기 요청 (2026-09-20)

`DX12PSOManager::Request`는 공용 `job_scheduler`에 컴파일을 제출하고
`job_handle`과 결과 저장소를 함께 보관한다. 같은 키의 미완료 요청은 하나로 합치며,
완료 확인 후 render owner가 캐시에 게시한다. 워커에서 자원 표를 조회하지 않도록
루트 시그니처 COM 참조를 제출 시 확보한다. 셰이더 바이트코드·입력 원소·semantic은
`RHIGraphicsPipelineRequest::Prepare`가 복사하여 원본 수명과 분리한다.

캐시 종료·전체 무효화는 새 비동기 요청을 닫고 해당 캐시의 작업만 기다린다.
전체 무효화는 완료 결과를 폐기한 뒤 요청을 다시 열고, 종료는 닫힌 상태를 유지한다.
공용 스케줄러를 종료하거나 무관한 작업까지 기다리지 않는다. 스케줄러 중지·입력
오류·컴파일 실패는 `Failed`로 반환하며 inline 실행은 없다. 수명/무효화와 동기
취득은 render owner가 직렬화하고, 공용 워커에서는 수명/무효화를 호출하지 않는다.

이 비동기 API의 현재 소비자는 `dx12.psocache` 검사다. 실제 렌더링의
`GetOrCreate` 동기 취득은 유지하며, 런타임 전체를 비동기 PSO 요청으로 전환했다는
뜻은 아니다. 검증 결과는 [부속 계획 §10](../plans/TaskSchedulerUnificationPlan.md#10-dx12-pso-비동기-컴파일-이관-2026-09-20)을 따른다.

## SceneManager 비동기 씬 준비 (2026-09-20)

`LoadSceneAsync`와 `LoadSceneAsyncAndWaitCallback`은 공용 워커에 문서 파싱과
자산 준비를 제출한다. 파싱 작업은 `DataSystem::LoadAssetBundleAsync`를 제출한 뒤
반환한다. 워커 안에서 하위 자산 작업을 기다리지 않으므로 단일 워커에서도 실행
의존성이 교착을 만들지 않는다. 기존 동기 `LoadAssetBundle`의 완료 배리어는 유지한다.

SceneManager가 경로·파싱 문서·번들·두 완료 토큰·promise를 소유한다. 게임 스레드는
파싱 토큰 완료 후 자산 토큰을 읽고, 두 작업이 끝난 뒤에만 엔티티/컴포넌트·계층·
프리팹·DDOL을 구성한다. 완료 게시 지점은 Editor/Player의
`ApplyPendingSceneStructureChange`다. 명시적으로 완료를 확인할 때도 같은 씬 구조 변경
경계에서 비차단 `PollSceneLoads()`를 호출한다. 대기 중인 요청이 있으면 false를 반환하므로
다음 경계에서 다시 호출한다. 완료된 준비 결과만 구성하며 자동 활성화는 프레임 경계에
남긴다. 이전 `WaitForSceneLoad()`는 제거했다. 종료의 내부 취소·drain 경계만 별도로 유지하며
게임/렌더 스레드에서 미완료 granular asset 작업을 기다리지 않는다. DDOL은 기존 API별 목적지를 유지한다: 반환형 로드는
결과 씬, callback 로드는 활성 씬에서 기존 이송 절차로 넘어간다. 저장 파일의 일반
엔티티/DDOL 두 절에 같은 instance ID가 있으면 DDOL 경로에서 한 번만 구성한다.

`std::future<Scene*>`는 실행 주체가 아닌 결과 통로로 유지한다. 소유 스레드에서
펌프 없이 `get()`으로 기다리는 호출은 지원하지 않는다. 이관 전 두 API의 제품
호출자는 없었다. 성공한 씬은 반환 future를 버려도 SceneManager 목록이 소유하며,
실패/취소는 `nullptr`로 완료한다. 겹친 callback 요청은 최신 요청만 자동 활성화한다.
동기 Create/Load는 앞선 준비를 취소·회수한다. `IsSceneLoading()`은 준비 중도 포함한다.

제출·완료·회수는 SceneManager 소유 스레드로 제한한다. 종료는 새 요청을 막고 해당
씬 준비/자산 작업을 회수하며, 취소한 결과로 엔티티를 만들지 않는다. Editor/Player가
CLR 종료 전에 `DrainSceneLoads()`를 호출하고 씬 해체·소멸 시에도 회수한다. 공용 풀
전체를 기다리거나 종료하지 않는다. 재현/실행 결과는 [부속 계획 §11](../plans/TaskSchedulerUnificationPlan.md#11-씬-로딩-2곳-이관-2026-09-20)을 따른다.

## DX12/Vulkan 명령 기록 (2026-09-20)

두 백엔드의 전용 스레드·조건 변수·작업 포인터를 제거했다. 공통
`IRHIParallelCommandPool::RunParallel`이 기록 구간들을 한 `submit_indexed`
콜백으로 공용 스케줄러에 한 번 제출한다. 단일 구간도 같은 경로로 실행하며
호출자 실행이나 별도 스레드 생성은 없다. 스케줄러를 시작·종료하는 소유자는 엔진이다.

`worker` 인자는 물리 스레드 ID가 아닌 논리적인 기록 구간이다. DX12의
allocator/list와 Vulkan의 command pool/buffer는 `[frame slot][recording lane]`으로
분리되어 있다. 물리 워커 하나가 여러 구간을 맡아도 각 구간은 한 콜백만 기록한다.
RenderGraph의 연속 패스 배분과 구간 번호 순 제출을 유지하여 배리어 순서를 보존한다.

소유 스레드는 Initialize/BeginFrame/Prepare/Open/RunParallel/Close/Shutdown을
직렬화한다. GPU frame fence를 기다린 뒤 해당 슬롯의 allocator/pool을 Reset하고,
기록 Job이 전부 끝난 뒤에만 encoder를 닫고 batch를 만든다. `job_handle::wait()`는
해당 그룹만 회수하며, 예외가 나도 나머지 구간의 완료를 기다린다. 반환 후 기록 Job은
자원을 참조하지 않는다. 풀 해체 전 RHI 제출 drain·GPU fence 대기는 기존 호스트 경계다.
CPU Job 완료를 GPU 완료로 취급하지 않는다.

공용 워커의 동기 중첩 기록은 작업 제출 전에 거절한다. RenderGraph도 native 자원을
건드리기 전에 거절한다. 중지 중 제출은 실패하고 inline fallback은 없다. 제출/패스
오류는 RenderGraph의 실패 결과로 전달하며, 열린 target을 닫고 실패 batch는 게시하지
않는다. 다음 프레임에서 같은 슬롯을 다시 Reset할 수 있다. RHIRecordedBatch가 기록
당시 frame slot을 캡처하는 기존 계약과 제출 스레드의 queue 소유는 유지한다.

검증은 [부속 계획 §12](../plans/TaskSchedulerUnificationPlan.md#12-dx12vulkan-명령-기록-이관-2026-09-20)를 따른다.


Player Debug/Release에서도 공용 Job을 통한 비동기 씬 준비 → 소유 프레임 활성화 →
새 화면 표시 → 정상 종료를 확인했다. 관리 스크립트는 씬마다 다시 시작했고,
SceneManager 해체 뒤 공용 scheduler 종료가 완료됐다. 범위와 재현은
[부속 계획 §13](../plans/TaskSchedulerUnificationPlan.md#13-player-실제-실행씬-전환종료-2026-09-20)에 기록한다.


## 애니메이션 S1 진단 경계

`AnimationMeasurementScope`는 opt-in 소유 스레드 표본이다. 작업별 timing slot을
그룹 제출 전에 확보하고 worker가 자기 슬롯만 기록한다. owner가 `wait()` 반환 뒤
집계하며 기존 Profiler의 가변 TLS EventBuffer에 worker marker를 붙이지 않는다.
worker 합산/실행 구간은 owner 제출·대기와 겹친다. 전체 경로에 중복 합산하지 않으며
ProxyCommand의 팔레트 할당·복사 시간도 render commit의 부분집합으로 기록한다.

실제 10/50/100체 기선과 표본 조건은 [AnimationSchedulerPlan §10](../plans/AnimationSchedulerPlan.md#10-s1-제품-경로-비용-기선-2026-09-20)을 따른다.
측정은 공용 풀의 워커 수나 스케줄링 정책을 변경하지 않는다. 지속적인 worker 이벤트
수집은 여전히 PHASE 14 P1b/P2의 안전한 전달 경계 이후에 진행한다.

## 애니메이션 S6 청크 제출 (2026-09-27)

`AnimationScheduler`는 `SceneManager`가 소유한다. Update와 Execute 각각에서
`min(대상 수, 워커 수×2)`개의 인덱스 청크를 `submit_indexed`로 보낸다.
한 청크는 여러 Animator를 처리하고 각 인스턴스의 측정 슬롯·워커 로컬 포즈
저장소는 분리한다. 두 배리어와 사이의 CPU 예산 결정, 이벤트 큐의 게임 스레드
전달은 유지한다. 청크 캡처가 가리키는 작업·타이밍·인덱스 저장소는 스케줄러가
소유하며 각 제출의 `wait()` 이후에만 재사용한다. 결과와 증거는
[AnimationSchedulerPlan §45](../plans/AnimationSchedulerPlan.md#45-s6-완료--워커-수-기준-청크와-sceneruntime-소유-2026-09-27)에 기록한다.

## 명령 기록 DAG·완료 경로 진단 (2026-10-18)

구현 기준은 `55b865fee8debb924eeb91ea89eefe12bb51aec0`다.
[동기화 재측정 6](../analysis/LiveRenderWiring20261012.html#performance-remeasure-6-20261018)의
추가 spin 후보는 재검증에서 기록 p50/p95와 Game 처리율 개선을 재현하지 못했다.
추가 owner/worker spin과 전용 enki 오버레이를 제거하고 native DAG 실행은 유지한다.
이 절은 소스 계약이며 **이 정리의 빌드·테스트·GPU·성능 측정 결과가 아니다.**
이전 실행 파일의 검증을 새 소스의 통과 결과로 옮기지 않는다.

### 일괄 제출과 수명

CPU 기록의 target 배분·분할 callback·선행 간선·최종 GPU 제출 순서는 유지한다.
기록 DAG 전체를 고정한 뒤 한 번 게시하여 노드별 완료 상태·continuation·dispatcher
왕복을 줄이는 구조다. 준비된 후속 노드는 enki 의존성으로 게시한다. 같은 target의
노드는 직전 노드에 의존하므로 같은 allocator/list에 두 callback이 동시에 기록하지 않는다.

native 작업과 간선 저장소는 enki의 의존성 완료 순회와 게시 호출이 모두 반환할 때까지
참조로 보관한다. 큐가 가득 차 inline 실행이 발생해도 게시 호출의 뒤쪽이 남아 있는
상태에서 해제하지 않는다. 공유 사용자 callback 및 observer 캡처를 해제한 뒤 완료를
게시하며 실패한 후손은 건너뛰고 독립 가지는 끝까지 회수한다. GPU 자원 회수는 기존
GPU 완료 경계에 남는다. 이번 정리는 이 native 실행·완료 gate 구현을 변경하지 않는다.

`job_handle::wait()`는 완료를 게시하는 mutex 아래에서 완료 여부를 확인하고 조건 변수로
기다린다. 같은 mutex 아래에서 미완료 worker 대기를 거절하며, 이미 완료된 worker 대기와
예외 전달은 허용한다. 추가 완료 atomic, spin overload, worker scope·controller는 제거했다.
enki 원본의 유휴 대기·작업 스틸링 정책은 기존 baseline에 맡긴다.

native DAG의 `jobDispatches`·`dispatcherHandoffs`·`ownerJoins`는 각 1이고
`workerJobs`는 실제 노드 수다. **이는 OS wake 횟수나 성능 개선율의 측정값이 아니다.**
wave/단일 target 경로도 실제 제출 횟수를 유지하며 단일 target에서는 사용하지 않는
pass 의존성 임시 배열을 만들지 않는다.

### 실행 옵션

설정은 프로세스에서 처음 읽은 뒤 고정한다. 값을 바꿀 때 Editor를 재시작한다.
부호·공백 없는 십진 정수만 허용하고 범위 밖/잘못된 값은 기본값으로 돌아간다.
기존 `CREATOR_RECORDING_TASK_GRAPH=0`은 wave 비교 경로,
`CREATOR_RECORDING_SINGLE_TARGET=1`은 분할 없는 단일 target 비교 경로다.

| 환경 변수 | 기본값 | 허용 범위 | 동작 |
|---|---:|---:|---|
| `CREATOR_RECORDING_BATCH_SUBMIT` | 1 | 0–1 | DAG의 native 일괄 제출. 0이면 기존 노드별 제출 |
| `CREATOR_RECORDING_JOB_TRACE` | 0 | 0–1 | 작업·pass/slice 시간과 제한된 DAG 연결 정보 |

`CREATOR_RECORDING_OWNER_SPIN_US`, `CREATOR_RECORDING_WORKER_SPIN_US`,
`CREATOR_RECORDING_SPIN_FRAME_US`, `CREATOR_RECORDING_SPIN_WORKERS`는 제거했다.
설정이 남아 있어도 추가 spin을 활성화하지 않는다.

### 진단과 연결 정보

`dx12.live`의 마지막 성공 CPU 기록에 있는 `nativeRecord.schedulingTrace`는
**schemaVersion 2**다. trace가 꺼지면 상세 스냅샷·노드별 시계·topology 재구성을 하지 않는다.
최대 4,096개 unit과 4,096개 node의 시간 슬롯을 제출 전에 마련하고 worker가 자기 슬롯에
기록한다. unit 한도는 잘린 prefix로, node 한도는 작업 trace 전체 미제공으로 표시한다.
작업 실행 자체는 생략하지 않는다. 이름은 소유 문자열이고 callback·encoder·GPU 소유자를
보관하지 않는다. trace 구성 비용은 `RenderCommandRecord.Diagnostics` marker로 구분한다.

| 필드 | 계약 |
|---|---|
| `policy.ownerWaitMode` | `blocking`. 대기 방식이며 실제 OS sleep 관측값이 아님 |
| `jobs.topologyAvailable` | native batch의 완전한 unit/node 정보와 실제 선택 DAG의 일치를 확인한 경우만 true |
| `jobs.topologyTruncated` | 4,096 unit/node 또는 16,384 unique dependency 상한 초과 |
| `jobs.lastCompletedNodes` | 양수 `completeNs` 최댓값과 동률인 모든 node ID. 미제공이면 빈 배열 |
| node `dependencies` | 이전 node ID 및 `same-target`/`cpu-producer` 이유. 동일 간선의 두 이유는 병합 |
| node `units` / unit `unit`, `node` | pass/slice를 실제 실행 node에 대응하는 양방향 매핑. unit ID는 배열 순서 |

연결 정보는 같은 빌더로 현재 unit과 pass 의존성을 재구성하고, 캐시를 포함한 선택 DAG의
node target·wave·선행 목록과 일치할 때만 공개한다. 진단 때문에 실행 계획을 교체하지 않는다.
상한 초과·불일치·wave/기존 노드별 제출에서는 부분 topology를 공개하지 않는다.
`unit` ID는 항상 제공하되 `node` 역매핑과 node의 간선·unit 목록은 topology가 있을 때만 제공한다.

분석기는 마지막 완료 node에서 `completeNs`가 가장 늦은 선행 node를 따라 관측된 완료 경로를
복원한다. 완료 시각이 같은 마지막 node와 선행 후보는 모두 표시하며 대표 경로의 선택 규칙을
명시한다. 이는 CPU 실행 시간 합이 가장 긴 경로나 OS Ready 경로가 아니다. 같은 target의
직전 간선, unit 소속의 유일성, 양방향 매핑, 시간 인과를 검증하지 못하면 경로를 만들지 않는다.

| 값/차이 | 의미 | 해석의 한계 |
|---|---|---|
| `registeredNs → readyNs` | 등록부터 CPU 의존성 준비까지 | 등록·첫 dispatcher 전달도 포함 가능 |
| `readyNs → publishBeginNs` | 준비 gate부터 게시 호출까지 | 순수 OS Ready가 아님 |
| `publishBeginNs → publishEndNs` | enki 게시 호출 앞뒤 | 실제 큐 삽입 시각이 아니며 inline callback 포함 가능 |
| `publishBeginNs → startNs` | 게시 시작부터 실행 시작 | enqueue·선점·큐 대기가 섞인 경과 |
| `startNs → bodyEndNs` | 노드 실행과 기록 처리 | barrier/encoder 처리를 포함 |
| unit `beginNs → endNs` | `RecordPassBody` 전체 | 반복 phase barrier 포함, 바깥 pre/final barrier 제외 |
| unit `callbackNs` / `callbackInvocations` | callback의 누적 wall 시간 / 호출 수 | 반복 호출 합계이며 CPU 점유·GPU 시간이 아님 |
| `bodyEndNs → completeNs` | 본문 이후 native 완료 gate까지 | 완료 처리·선점 포함 가능 |
| jobs `completedNs → returnNs` | graph callback 캡처 해제부터 owner 반환까지 | observer·완료 게시·통지도 포함 |

`node.completeNs`는 native complete gate에서 후속 의존성 알림 전에 기록한다.
`jobs.completedNs`는 native 순회와 게시 참조가 빠지고 공유 graph callback 캡처를 해제한 뒤다.
그 다음 observer 처리·캡처 해제·handle 완료 상태 게시·CV 통지가 이어질 수 있다.
따라서 어느 시각도 실제 OS sleep 종료나 모든 완료 처리의 종료로 읽지 않는다.

`startNs < publishEndNs`는 inline 실행 또는 다른 worker의 빠른 실행에서 유효하다.
enki 스레드 인덱스와 Windows OS TID는 별도로 제공한다. steady-clock ns와 ETW QPC를
근거 없이 동일 축으로 합산하지 않는다. OS Ready·sleep 귀속에는 시간축과 스레드를 맞춘 ETW가 필요하다.

### 분석기 호환성과 검증 경계

`Tools/regression/analyze-recording-jobs.py`는 저장한 JSON/NDJSON을 받는다.
기존 schema 1의 spin 통계 해석과 엄격한 결측 검증은 이전 파일을 위해 유지한다.
schema 2에서는 제거된 `ownerWait`/`workerSpin` 객체의 부재를 진단 손실로 세지 않으며,
서로 다른 스키마·정책을 같은 비교 집단으로 섞지 않는다. 중복 조회, 상충하는 동일 표본,
미제공·잘린 시간·skipped node를 정상 실행 표본으로 꾸미지 않는다. topology coverage와
시간 coverage를 구분한다. 마지막 성공 기록 조회는 전체 프레임 수집기가 아니므로 표본의
p95/max를 전체 실행의 p95/max로 확대하지 않는다.

오버레이 제거는 enki C++ ABI를 원본으로 되돌린다. 검증 환경에서는 기존 vcpkg baseline의
헤더·정적/import 라이브러리·DLL을 함께 다시 설치하고 소비자를 모두 다시 빌드해야 한다.
이전 패치 DLL과 원본 헤더 또는 그 반대의 혼용을 수용하지 않는다. 다른 port를 위한
`overlay-ports` 설정과 `enkits` 의존성 선언은 유지한다.

회귀 소스는 `verify-recording-task-graph.ps1`, `recording_task_graph_probe.cpp`,
`test-recording-jobs.py`에 있다. native/기존 제출의 같은 target 직렬화·분할 생산자 합류·
한 worker 진행, callback/observer 캡처 해제, 실패/종료 drain과 wave 장벽 변이를 유지한다.
추가로 topology 이유 병합·독립 pass를 거친 선행 전달·매핑·크기 상한 및 schema 1/2를 다룬다.
**빌드·분석기·테스트·GPU·벤치마크는 실행하지 않았다.** 정적 검토를 실행 통과로 기록하지 않는다.

후속 측정은 trace OFF에서 동일 입력·소비자·baseline 의존성을 맞춘 전후 반복 비교를 한다.
Game 및 Record p50/p95/max, 완료 뷰/s, CPU 사용을 비교하고 원인 분석만 trace ON/ETW로
별도 수집한다. 완료 뷰/s는 Scene+Game 합계이며 실제 표시 FPS가 아니다. Material/Temporal의
callback 내부 visibility SRV 재사용과 Prepare/Declare 하위 marker는
[정리 구현 기록](../analysis/LiveRenderWiring20261012.html#performance-recording-cleanup-20261018)을 따른다.

</details>
