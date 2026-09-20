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
Editor/Player는 새 프레임 생산이 끝난 뒤 Scene의 AI 작업을 먼저 회수하고 CLR을
종료한다. 씬 해체는 공용 풀이 살아 있는 동안 끝내고, 공용 풀은 DataSystem보다 먼저
종료한다. AI 작업은 Entity/Component 파괴·DDOL 이송 경계에서도 회수한다.

## 이관 범위

DataSystem 번들 로드, BrowserThumbnailCache, AnimationJob, Foliage 범위 처리,
Scene AI 갱신이 공용 스케줄러를 사용한다. 구 WorkerPool과 Animation 전용 풀 및
사용처가 없어진 Core.ThreadPool/Core.Thread/Core.CountingSemaphore는 제거한다.
AnimationJob의 평가 알고리즘·게임 스레드 이벤트 전달·포즈 게시 순서는 보존한다.

다음은 아직 별도 구현이다. 공용 실행 기반의 완성과 전 경로 이관 완료를 구분한다.

- SceneManager 씬 로드의 `std::async` 2곳, DX12 PSO 컴파일 1곳:
  값 반환·캐시·블로킹 의존 관계를 정리한 후 이관한다. `std::future` 자체는
  결과 전달 타입이므로 존재만으로 별도 스레드 풀이라고 판단하지 않는다.
- DX12/Vulkan 명령 기록 전용 풀: 워커별 command allocator/buffer 소유와 reset
  경계를 먼저 매핑한 뒤 공용 실행 기반에 연결한다.
- Presentation/Render/GPU 제출·소켓·파일 감시 등 장기 루프는 전용 스레드 역할이다.

재현 가능한 검사와 실행 결과는
[TaskSchedulerUnificationPlan §9](../plans/TaskSchedulerUnificationPlan.md#9-공용-스케줄러-구현-2026-09-20)에 기록한다.
