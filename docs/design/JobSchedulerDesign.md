# 공용 thread_pool · job_scheduler

2026-09-20 확정. [코딩 컨벤션](CodingConventions.md)과
[PHASE 13 계획](../plans/AnimationSchedulerPlan.md)의 공용 실행 기반이다.

## 결정

목표는 실행 기반의 통일이다. 백엔드는 enkiTS 1.12로 고정하고 성능 비교는 이번
완료 조건에서 제외한다. 청크 크기, 워커 수, 작업 저장소 최적화는 이후에 진행한다.

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

각 그룹은 첫 예외를 보존하고 나머지 그룹 구성원은 끝까지 실행한다. `wait()`가
그룹 오류를 다시 던진다. 버린 토큰의 오류는 별도 전역 오류 큐로 전달하지 않으므로,
결과를 무시하는 제품 작업은 본문에서 실패를 자신의 완료 경로로 보고해야 한다.

워커에서 아직 끝나지 않은 토큰을 `wait()`하면 `logic_error`를 낸다. 풀 고갈을
피하려면 의존 작업을 사용한다. 제품의 동기식 LoadAssetBundle·AnimationJob·Foliage
진입점은 호스트/게임 스레드에서 호출한다. 임의의 공용 워커 안에서 이 동기 경로를
중첩 호출하는 것은 지원하지 않는다.

## 실행과 종료 경계

외부 제출자는 그룹당 한 개의 pinned handoff를 워커에 게시한다. 그 워커가 일반
enkiTS task set을 제출하여 범위 분배와 스틸링이 작동한다. enkiTS 큐가 가득 차도
사용자 콜백은 제출자/UI 스레드에서 실행하지 않는다. handoff에는 제출 호출과 완료
각각의 소유 참조가 있어, enkiTS의 게시 후 필드 접근이 끝나기 전에 해제하지 않는다.

기본 워커 수는 기존 공용 풀의 논리 CPU 수 정책을 유지한다. `start(n)`으로 지정할
수 있다. CPU 예산/우선순위 조정은 후속 최적화다. 라이프사이클 호출은 엔진 소유
스레드가 직렬화하며 워커에서 종료할 수 없다. 한 풀의 제출·종료 소유자는 한
스케줄러다. 풀은 스케줄러보다 오래 살아야 한다.

종료 시 새 제출을 막고, 이미 접수한 미실행 의존 작업까지 완료한 다음 enkiTS
워커를 join한다. 중지 중 제출과 시작 전/종료 후 제출은 실패하며 inline fallback은 없다.
Editor/Player는 새 프레임 생산이 끝난 뒤 씬 로드와 Scene의 AI 작업을 먼저 회수하고 CLR을
종료한다. 씬 해체는 공용 풀이 살아 있는 동안 끝내고, 공용 풀은 DataSystem보다 먼저
종료한다. AI 작업은 Entity/Component 파괴·DDOL 이송 경계에서도 회수한다.

## 이관 범위

DataSystem 번들 로드, BrowserThumbnailCache, AnimationJob, Foliage 범위 처리,
Scene AI 갱신, DX12 PSO 비동기 컴파일, SceneManager 비동기 씬 준비와 DX12/Vulkan 명령 기록이 공용 스케줄러를 사용한다. 구 WorkerPool과 Animation 전용 풀 및
사용처가 없어진 Core.ThreadPool/Core.Thread/Core.CountingSemaphore는 제거한다.
AnimationJob의 평가 알고리즘·게임 스레드 이벤트 전달·포즈 게시 순서는 보존한다.

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
`ApplyPendingSceneStructureChange`다. 명시적으로 기다려야 하면 같은 씬 구조 변경
경계에서 `WaitForSceneLoad()`를 호출한다. 이 함수는 준비 결과를 구성하지만 자동
활성화는 프레임 경계에 남긴다. DDOL은 기존 API별 목적지를 유지한다: 반환형 로드는
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
`IRHIParallelCommandPool::RunParallel`이 기록 구간마다 `job_group`에 하나의
콜백을 넣고 공용 스케줄러에 한 번 제출한다. 단일 구간도 같은 경로로 실행하며
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
