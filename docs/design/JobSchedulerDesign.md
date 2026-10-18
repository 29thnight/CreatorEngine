# 공용 thread_pool · job_scheduler

2026-09-20 확정. [코딩 컨벤션](CodingConventions.md)과
[PHASE 13 계획](../plans/AnimationSchedulerPlan.md)의 공용 실행 기반이다.

## 결정

목표는 실행 기반의 통일이다. 백엔드는 enkiTS 1.12로 고정한다. 2026-10-18의
기록 구간 spin은 [저장소 오버레이](../../ports/enkits/README.md)의 명시적인 확장이다. 성능 비교는 최초 통일의
완료 조건에서 제외한다. 애니메이션의 워커 수 기준 청크와 제출 저장소 정리는
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
- `job_handle::wait(spin_budget, statistics)`: 명시한 합류에서만 완료 atomic을
  확인하고 deadline이 지나면 기존 조건 변수 대기로 돌아간다. 일반 API 상한은
  1ms이며 렌더 환경 설정은 200µs까지 받는다. 0은 시계 조회 없는 기존 대기다.
  미완료 토큰에 대한 worker의 중첩 대기 금지와 예외 전달은 동일하다.

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

종료 시 새 제출을 막고 기록 구간의 추가 spin을 먼저 중단한다. 이미 접수한 미실행 의존 작업까지 완료한 다음 enkiTS
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

## 명령 기록 DAG·한정 spin (2026-10-18)

구현 기준은 `50cc903f2ad44419182161f08704e4c38540d2d2`다. 이 절은 새 소스의
계약이며 **이번 변경의 빌드·실행 테스트·GPU 검사·성능 측정 결과가 아니다.**
직전 의존성 기록의 A/B 측정은 [렌더 배선 기록](../analysis/LiveRenderWiring20261012.html#performance-recording-dependencies-20261018)에
보존한다. 직전 결과에서 기록 중앙값과 callback 겹침은 개선됐으나 전체 Game 처리율과
tail 개선은 확정되지 않았으므로 추가 변경의 효과를 수치로 가정하지 않는다.

### 일괄 제출과 수명

CPU 기록의 target 배분·분할 callback·선행 간선·최종 GPU 제출 순서는 유지한다.
기록 DAG 전체를 고정한 뒤 한 번 게시하여 기존 노드별 `submit_after`의 완료 상태,
continuation, dispatcher 왕복을 줄인다. 준비된 후속 노드는 선행 완료 스레드의
enki pipe에 직접 게시한다. 같은 target의 노드는 직전 노드에 의존하므로 같은
allocator/list에 두 callback이 동시에 기록하지 않는다.

native 작업과 간선 저장소는 최종 본문뿐 아니라 enki의 의존성 완료 순회와
게시 호출이 모두 반환할 때까지 참조로 보관한다. 큐가 가득 차 inline 실행이
발생해도 게시 호출의 뒤쪽이 남은 상태에서 해제하지 않는다. 모든 참조가 빠진
뒤 공유 사용자 callback을 먼저 해제하고, 그 뒤 완료 atomic·조건 변수·오류를
게시한다. CPU 완료 뒤 encoder를 닫으며 GPU 자원 회수는 기존 GPU 완료 계약을 따른다.

이 경로의 `nativeRecord.jobDispatches`와 `dispatcherHandoffs`는 기록 DAG당 1,
`ownerJoins`는 1이다. `workerJobs`는 실제 노드 수다. 예전 7노드 스냅샷이면
handoff는 구조적으로 7→1이지만 callback 실행 수는 7이다. **OS wake 횟수나
측정 시간이 같은 비율로 줄었다는 뜻은 아니다.** wave/단일 target 경로의 카운터도
실제 `RunParallel` 호출과 제출 작업 수를 기록한다. 단일 target은 사용하지 않을
pass 의존성 임시 배열을 만들지 않는다.

### 실행 옵션

설정은 프로세스에서 처음 읽은 뒤 고정한다. 비교 조건을 바꿀 때 Editor를 재시작한다.
값은 부호·공백 없는 십진 정수여야 한다. 범위를 벗어나거나 잘못된 값은 기본값으로
돌아간다. 기존 `CREATOR_RECORDING_TASK_GRAPH=0`은 wave 비교 경로,
`CREATOR_RECORDING_SINGLE_TARGET=1`은 분할 없는 단일 target 비교 경로다.

| 환경 변수 | 기본값 | 허용 범위 | 동작 |
|---|---:|---:|---|
| `CREATOR_RECORDING_BATCH_SUBMIT` | 1 | 0–1 | DAG의 native 일괄 제출. 0이면 기존 노드별 제출 |
| `CREATOR_RECORDING_JOB_TRACE` | 0 | 0–1 | 작업별 게시/실행과 개별 callback wall 시간 |
| `CREATOR_RECORDING_OWNER_SPIN_US` | 0 | 0–200 | 기록 DAG의 마지막 owner 합류에서만 추가 확인 |
| `CREATOR_RECORDING_WORKER_SPIN_US` | 0 | 0–200 | 기록 범위에서 실제 작업 이후 유휴 구간의 추가 spin 한도 |
| `CREATOR_RECORDING_SPIN_FRAME_US` | 200 | 0–2000 | 같은 frame ID의 모든 worker·Scene/Game이 공유하는 예약 예산 |
| `CREATOR_RECORDING_SPIN_WORKERS` | 1 | 0–8 | 동시에 추가 spin을 허용할 worker 수, 풀 크기로 제한 |

owner 예산은 DAG 합류 **호출당** 한도다. worker의 **프레임 전체** 예약 예산과
별개다. owner spin이 적용되지 않는 wave/단일 target/외부 CPU 부작용 경로의
진단에는 유효 owner 예산을 0으로 표시한다. 기록 부작용으로 owner에서 직접
실행하는 경로에는 worker 범위도 열지 않는다.

worker spin은 `RecordParallel`의 DispatchJoin 안에서만 범위를 연다. 잠든
worker를 범위 개방만으로 깨우지 않으며 첫 제출의 정상 wake 경로를 유지한다.
실제 작업이 끝나고 원본 enki의 기본 spin까지 소진한 유휴 구간에서 추가 예산을
받는다. 빈 큐 확인·허위 깨움은 deadline을 갱신하지 않는다. 추가 반복은 실제
공용 큐를 확인하고, 작업을 찾으면 사용자 callback 실행 전에 허가와 spin 계측을
끝낸다. 원본 대기자 등록·최종 큐 재검사·semaphore 순서는 보존한다.

예산은 허가 때 남은 프레임 예산에서 미리 차감한다. 사용하지 않은 부분도
돌려주지 않으므로 짧은 작업이 반복되어도 예약 합계가 프레임 한도를 넘지 않는다.
같은 프레임의 중첩/연속 뷰는 예산을 공유하며 마지막 범위가 닫히면 추가 허가를
막는다. 이전 프레임의 scope 또는 선점된 worker 참조가 남으면 새 프레임의
spin은 비활성 처리한다. 이전 예산을 재충전하거나 그 worker를 기다리지 않는다.

### 진단과 해석

`dx12.live`의 마지막 성공 CPU 기록 스냅샷에서
`nativeRecord.schedulingTrace`를 읽는다. 시간 계측과 spin이 모두 꺼지면 이
상세 스냅샷과 노드별 시계 조회를 만들지 않는다. 시간 계측이 켜지면 최대
4,096개 callback과 4,096개 작업의 값을 준비하고 worker가 자기 슬롯에 기록한다.
callback 한도를 넘으면 잘린 prefix임을 표시하며, 작업 수가 한도를 넘으면
node trace 전체를 미제공 처리한다. 작업 실행 자체를 생략하지 않는다.
게시된 이름은 소유 문자열이며 callback·encoder·GPU 소유자를 보관하지 않는다.

| 값/차이 | 의미 | 해석의 한계 |
|---|---|---|
| `registeredNs → readyNs` | 노드 등록 이후 CPU 의존성 준비까지 | 등록·첫 dispatcher 전달도 포함할 수 있음 |
| `readyNs → publishBeginNs` | 준비 gate에서 게시 호출까지 | 후속 전달의 일부, 순수 OS Ready 아님 |
| `publishBeginNs → publishEndNs` | enki 게시 호출의 앞뒤 | 실제 큐 삽입 시각이 아니며 inline callback도 포함 가능 |
| `publishBeginNs → startNs` | 게시 호출 시작부터 노드 실행 시작 | enqueue·선점·큐 대기가 섞인 경과, 순수 wake 지연 아님 |
| `startNs → bodyEndNs` | 노드 실행과 기록 처리 | barrier/encoder 처리 등을 포함 |
| unit `beginNs → endNs` | 개별 pass/slice의 `RecordPassBody` 전체 | 반복 phase barrier·루프 포함, 바깥 pre/final barrier 제외 |
| unit `callbackNs` / `callbackInvocations` | 실제 callback 호출의 누적 wall 시간 / 호출 수 | 반복 callback은 합계이며 CPU 점유·GPU 시간이 아님 |
| `bodyEndNs → completeNs` | 본문 이후 native 완료 gate까지 | enki 완료 처리·선점이 포함될 수 있음 |
| jobs `completedNs → returnNs` | 최종 CPU 완료부터 owner 반환까지 | owner 깨움뿐 아니라 완료 게시/반환 처리도 포함 |

`startNs < publishEndNs`는 유효하다. 큐 포화 시 게시 호출 안에서 실행하거나
다른 worker가 먼저 가져갈 수 있다. enki 스레드 인덱스와 Windows OS TID를
별도 제공한다. steady-clock ns를 ETW QPC와 근거 없이 직접 같은 축으로 더하지
않는다. OS Ready·sleep 귀속은 동일 실행 파일·스레드·시간축을 맞춘 ETW로 확인한다.

`ownerWait.available`이 false인 wave/단일 target 경로에서는 owner 대기 카운터를
관측값으로 해석하지 않는다. `ownerWait.blockingPathEntered`는 완료 mutex 아래에서 CV predicate가 false였다는
뜻이다. 실제 OS sleep 횟수와 다르다. `workerSpin.scopeAccepted`는 해당 뷰의
범위가 열렸는지 표시한다. `attempts`는 허가된 추가 spin 횟수이고 `taskHits`는
공용 큐의 어떤 작업이든 찾은 횟수다. 특정 기록 작업의 wake 회피로 단정하지 않는다.
`reservedBudgetNs`는 예약 합계, `wallNs`는 실제 spin 경과다. 후자는 OS 선점과
poll 간격의 초과분을 포함할 수 있어 예약 상한과 같은 시간 축의 보장이 아니다.

worker 통계는 **프레임 누적 atomic snapshot**이다. Scene과 Game 값을 더하지
않는다. `activeSpinners > 0`이면 닫힌 범위를 빠져나오는 worker의 후속 값이
아직 반영되지 않을 수 있다. 0이어도 뒤쪽 뷰가 시작할 수 있어 프레임 최종값을
보장하지 않는다. 카운터들을 한 원자적 트랜잭션으로 읽는 것도 아니다.

저장한 명령 응답 JSON 또는 NDJSON은 다음 도구로 분석한다.

```powershell
python Tools/regression/analyze-recording-jobs.py trace.json
python Tools/regression/analyze-recording-jobs.py trace.json --json
```

반복 조회한 같은 기록은 중복 표본으로 세지 않는다. 미제공·0·잘린 시간·skipped
노드는 정상 실행 시간으로 꾸미지 않는다. 이 스냅샷 조회는 모든 프레임을 보존하는
수집기가 아니므로 표본의 p95/max를 전체 실행의 p95/max로 확대하지 않는다.
trace가 켜지면 이름 복사·시계 조회·JSON 처리 비용이 생기므로 처리율 비교는
trace를 끄고, 원인 분석 수집은 같은 계측 조건끼리 별도로 비교한다.

### 검증과 비교 순서

새 [enkiTS 오버레이](../../ports/enkits/README.md)의 헤더와 라이브러리/DLL을 함께
설치하고 모든 소비자를 다시 빌드해야 한다. 원본 헤더는 API 매크로 검사로,
원본 라이브러리 혼용은 새 export symbol로 링크/로드 단계에서 거절한다.
이 작성 환경에서는 빌드나 실행을 수행하지 않았다.

작성한 회귀 검사는 `verify-recording-task-graph.ps1` 및
`test-recording-jobs.py`에 있다. native/기존 제출의 선행 조건·같은 target
직렬화·한 worker 진행, 실패 가지 건너뛰기와 독립 가지 완료, 캡처 해제 후
완료, 게시 타임스탬프, 종료 중 접수 거절·drain을 다룬다. 주입한 가짜 시계로
worker 예산·동시 허가·중첩 범위·epoch·재충전 금지·종료를 검사하도록 작성했다.
유효 DAG에 불필요한 전체 wave barrier를 넣는 변이가 실제 합류 회귀를 잡는지도
검사한다. **검사 코드 작성과 정적 검토를 실행 통과로 기록하지 않는다.**

Windows 검증에서는 먼저 기존 문자열·기록 수명·GPU 정확성 회귀를 확인한다.
그 뒤 같은 DLL에서 DAG 유지·spin 0·trace 0인 상태로 batch 0/1을 반복 비교한다.
다음으로 batch 1을 고정하고 owner만 20/50/100µs, worker만 20/50/100µs와
프레임 예산·동시 worker 수를 독립적으로 비교한 후 필요한 조합을 판단한다.
두 뷰 완료/초, Game 및 Record p50/p95/max, CPU 사용·ETW Ready/대기,
spin 예약·발견 수를 함께 본다. spin 기본값은 측정 근거가 생길 때까지 0이다.
