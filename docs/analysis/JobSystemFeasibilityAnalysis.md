# JobSystem 도입 타당성 분석

작성: 2026-09-07 · 목적: "JobSystem을 들이면 동시성 최적화가 되는가"에 답한다 · 근거: 코드·계획서 정독 12관점, high 관련도 발견마다 적대 검증(원문 대조·최신성 2렌즈 합산), 설계안 3각도 심판 3인, 종합 1, 완결성 비평 1. 라이브 실측은 하지 않았다(§8). 방법과 한계는 부록 B.

## 요약

**판정: 타당하다 — 단 외부 라이브러리를 들이는 형태가 아니라, 기존 `Core.ThreadPool`의 구조적 결함을 최소 확장으로 고치는 형태다.**

> **2026-09-07 추가 실측(§10).** 문서 최초 작성 뒤 헤더를 그대로 포함하는 독립 벤치를 돌렸다. 그 결과 **더 급한 결함이 나왔다: `NotifyAllAndWait`은 배리어가 아니다.** 잡이 남아 있는데도 2.5~5.8% 확률로 즉시 반환한다. 아래 "부분집합 대기 불가"는 여전히 사실이지만 **2순위**이며, §10이 §2.1·§2.4·§2.8·§3·§6.2의 일부 서술을 정정한다.
>
> **2026-09-07 적용·비교(§11).** 그 배리어 결함만 `Core.ThreadPool.h`에 수정 적용했다(J-1, 미커밋). 재검증에서 거짓 반환이 89,000시행 0건이다. 이어 Dispenso·Taskflow·BS::thread_pool·dp::thread_pool을 실제로 받아 같은 하네스에 태웠다. **결론: 수정된 엔진 풀은 BS·dp와 같은 급이고, 남은 최대 손해 요인은 라이브러리 선택이 아니라 `THREAD_PRIORITY_HIGHEST` 기본값이다.** 외부 라이브러리 도입은 성능으로 정당화되지 않는다.

- **병목은 "잡이 없다"가 아니라 "대기 원시가 하나뿐이다."** `ThreadPool::NotifyAllAndWait`은 인스턴스 전역 단일 카운터 `m_taskCounts`가 0이 될 때까지 기다린다(`Engine/Utility_Framework/Core.ThreadPool.h:68, 80-86, 125, 144`). 풀을 공유하는 두 클라이언트는 "내 잡만" 기다릴 수 없다. 세 관점(계획 교차·물리·외부 조사)이 같은 줄을 독립적으로 지목했다.
- **이 결함 하나가 두 계획의 공통 하드 블로커다.** PHASE 19 T1(Jolt `JobSystemWithBarrier` 어댑터를 `WorkerPool` 위에)은 전역 카운터 위에서는 물리 배리어가 무관한 자산 로드까지 기다리게 되고, PHASE 13 S3.5(워커별 포즈 풀·steal-in-place)는 워커 인덱스 없이는 구현이 불가능하다. S6의 청크 분할만은 정적 분할로 우회할 수 있다(§5.1). 어느 계획서도 JobSystem을 자기 항목으로 소유하지 않는다.
- **외부 라이브러리는 지금 정당화되지 않는다.** enkiTS·Wicked·Jolt API 세부 주장 4건이 전부 "이 저장소에 벤더링돼 있지 않아 원문 대조 불가"로 반박됐고, 외부 이식안 스스로 "실패 시 자체 확장안으로 회귀"를 인정했으며, 기존 계획 합의("Jolt는 엔진 WorkerPool 공유")와 충돌한다.
- **흡수 대상이 아닌 것이 더 많다.** 렌더 병렬 기록 풀(`RHIParallelCommandPool`, DX12·Vulkan 모두 지속 워커 3)은 워커 인덱스=자원 슬롯·연속 블록 배분·fork-join-only라는 다른 계약이고, PhysX 디스패처는 벤더 소유, 코루틴·트윈은 락 없는 GT 전용이다.
- **이 조사가 찾은 유일한 정확성 결함은 JobSystem이 고치는 것이 아니다.** AI `std::async`가 읽는 트랜스폼을 다음 프레임 `AllUpdateWorldMatrix`가 쓴다(`Engine/SceneRuntime/Scene.cpp:2559, 2784`, `AIManager.cpp:65`). 수정은 PhysicsRedesignPlan T0의 "무조건 join"이 소유한다.
- **권고 순서**: 선행 사슬이 없는 국소 수정 다섯(J8b AI 레이스 봉인 — T0 소유, J10 PSO 상한 1단계, J6 죽은 코드 삭제, J4 프로파일러 등록, J5 우선순위 정책)은 **즉시**, J0 기준선 → J1 `JobCounter`(부분집합 대기) → J1a 계층 게이트 → J1b 잡 콜백 관리 코드 금지 정적 검사와 **병행**한다. Foliage·자산 로드 논블로킹·PSO 큐 이관(J3·J8a·J9·J10 2단계)은 씬당 규모 실측 전에는 착수하지 않는다. 이 저장소 자신의 선례(매 프레임 스레드 생성 병렬 기록이 순차보다 1.7배 느렸다, `EnhancedRenderGraph.cpp:777-780`)가 게이팅 없는 병렬화의 손해를 이미 증명했다.
- **성능 수치는 대부분 미측정이다.** 확정적으로 말할 수 있는 것은 구조적 위험 제거와 이후 계획의 블로커 해소뿐이다. Release 바이너리가 HEAD보다 낡아 라이브 실측을 하지 않았다.

## 1. 지금 무엇이 있는가 — 정독 실측

### 1.1 하드웨어와 스레드 인구

기준 기기: Xeon W-2223 **4코어/8스레드**, 64GB. 워커 수 예산이 작다는 것이 이 문서의 모든 판단을 지배한다.

런타임에 서는 스레드·풀(에디터 재생 중):

| 스레드/풀 | 개수 | 우선순위 | 생성 지점 | 소비자 | 수명 |
|---|---|---|---|---|---|
| GameThread | 1 | 기본 | 프로세스 메인 | 시뮬레이션 전체 | 상시 |
| PresentationThread | 1 | 기본 | `Editor/EngineEntry/EditorMain.cpp:278` | ImGui 셸·표시 | 상시 |
| RenderThread | 1 | 기본 | `Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp:3946` | GPU 씬 기록(큐 용량 2) | 상시 |
| RHISubmissionThread | 1 | 기본 | `Engine/RenderEngine/RHI/RHISubmissionThread.cpp:379` | 큐 제출·생명주기 명령 | 상시 |
| 커맨드 풀 지속 워커 (DX12) | 3 (workerCount 4, 워커 0 = 호출 스레드) | 기본 | `RHI/DX12/EnhancedSceneRendererLiveDX12Adapter.cpp:65`, `DX12CommandListPool.cpp:83-84` | `EnhancedRenderGraph` 병렬 기록 | 상시 |
| 커맨드 풀 지속 워커 (Vulkan) | 3 (동일 계약) | 기본 | `EnhancedSceneRenderer.cpp:443`, `RHI/Vulkan/VulkanCommandBufferPool.cpp:61-63` | 동일 | 상시 |
| `WorkerPool`(=`Core.ThreadPool`) | **8** (`GetActiveProcessorCount`) | **`THREAD_PRIORITY_HIGHEST`** | `Core.ThreadPool.h:20-23`, `WorkerPool.h:25`, `SceneManager.cpp:322` | `DataSystem::LoadAssetBundle` 하나 | `ManagerInitialize`~`Shutdown` |
| `AnimationJob` 전용 풀 | **8** | **`THREAD_PRIORITY_HIGHEST`** | `Engine/SceneRuntime/AnimationJob.cpp:39` | 애니메이터 틱 fork-join | 상시(`RenderScene.h:133` 값 멤버) |
| `AssetLoadJob` 풀 | 16 | HIGHEST | `Engine/RenderEngine/AssetJob.cpp:3` (멤버 선언 `AssetJob.h:11`) | **소비자 0 — 죽은 코드** | (생성되지 않음) |
| PhysX `CpuDispatcher` | 4 (`core - 4`) | PhysX 내부(NORMAL 추정, 미검증) | `Engine/Physics/Physx.cpp:166-173` | 솔버 내부 병렬 | 상시 |
| Foliage 컬링 `std::async` | 프레임마다 컴포넌트당 **최대 17** (`hw*2+1`; 인스턴스 <17이면 인스턴스 수만큼, `:334` 조기 break) | ConcRT 풀 | `Engine/SceneRuntime/FoliageComponent.cpp:315-341, 389-396` | 인스턴스 컬링 | 프레임 내 |
| AI `std::async` | 프레임마다 1 | ConcRT 풀 | `Engine/SceneRuntime/Scene.cpp:2784` | `AIManager::InternalAIUpdate` | 다음 프레임까지 |
| 씬 로드 `std::async` | 로드당 1 | ConcRT 풀 | `Engine/SceneRuntime/SceneManager.cpp:936, 969` | 씬 파싱 → 다시 `WorkerPool` 호출·대기(이중 레이어) | 로드 중 |
| PSO 컴파일 `std::async` | 미스당 1, 상한 없음 | ConcRT 풀 | `Engine/RenderEngine/RHI/DX12/DX12PSOManager.cpp:719` | PSO 생성 | 요청 중 |
| SoundManager 로더 | 1 | 기본 | `Engine/SceneRuntime/SoundManager.cpp:20` | 사운드 로드 | 상시 |
| CommandService accept · ConsoleCommand stdin · efsw 워처 · ModelPlacement 워커(BELOW_NORMAL) · ProgressWindow | 각 1 | 기본 | `Engine/CommandService/CommandService.cpp:133`, `Editor/EngineEntry/ConsoleCommandSystem.cpp:496`, `EditorDirectoryWatcher.cpp:255`, `EditorModelPlacement.cpp:91`, `ProgressWindow.h:36` | 에디터 부대 기능 | 상시/작업 중 |

백엔드 공통 고정 스레드 4(GT·Presentation·RT·RHISubmission)에 커맨드 워커 3을 더하면 **7**로 8논리코어를 거의 채우고, 그 위에 HIGHEST 우선순위 워커 **16개**(WorkerPool 8 + AnimationJob 8)와 PhysX 4가 affinity 없이 얹힌다(`Core.ThreadPool.h:38` `affinityMask=0`). MSVC의 `std::async(launch::async)`는 매번 OS 스레드를 만드는 것이 아니라 ConcRT/PPL 풀에 태스크를 올리므로(`VC/Tools/MSVC/14.51.36231/include/future:656, 1394`), Foliage 17연발은 "스레드 폭증"이 아니라 **네 번째 스케줄러가 코어를 나눠 쓰는 것**이다. 관리 측 CoreCLR의 GC·ThreadPool 스레드 인구는 이 조사에서 세지 않았다(§9).

### 1.2 `Core.ThreadPool`의 대기 의미 — 전용 풀이 생긴 구조적 이유

`ThreadPool::Enqueue`는 전역 `m_taskCounts` 하나를 올리고(`Core.ThreadPool.h:68`), 워커는 태스크 실행 후 내리며 0이 되면 이벤트를 세운다(`:125-128`). `NotifyAllAndWait`은 그 카운터가 0이 되는 이벤트를 `WaitForSingleObject(INFINITE)`로 기다린다(`:80-86`). 즉 풀을 공유하는 두 클라이언트는 **"내 잡만" 기다릴 수 없고, 논블로킹 완료 통지도 없다**. `WorkerLoop(int threadIndex)`(`:101`)의 인덱스는 태스크에 전달되지 않는다(`:123` `task()` 무인자, `WorkerPool.h:25` `TaskType = std::function<void()>`). `SetThreadInitCallback/SetThreadExitCallback`(`:90, 95`)은 `WorkerLoop`(`:103`)에 스레드당 1회로 배선돼 있으나 호출자가 0이다.

`AnimationJob`이 `WorkerPools`를 쓰지 않고 8스레드 전용 풀을 따로 둔 것은 이 제약과 정합하지만, `AnimationSchedulerPlan.md:89`(E9)는 그것을 "코어 점유 낭비"로 적고 S6에서 실측 후 재검토하라고 할 뿐 부분 대기 불가를 "이유"로 명시하지는 않는다(부록 A). 부분 집합 대기가 없는 풀은 잡 시스템이 아니다.

### 1.3 프레임 임계 경로 — 게임 스레드 직렬 순서

`Runtime::TickSimulationFrame`(`Engine/SceneRuntime/RuntimeFrame.cpp:67`)이 재생 중 순서를 단독 소유한다(E3-7, `verify-frame-orchestration.ps1`).

```
Initialization → InputEvents → TickManagedPrePhysics(C#)
→ Physics = Scene::FixedUpdate (Scene.cpp:2557)
    m_AIFuture wait_for(0s) 비블로킹 체크 (:2559)
    AllUpdateWorldMatrix(FixedUpdate) → SetInternalPhysicData → CCT FixedUpdate
    → PhysicsManagers->Update: simulate() ; fetchResults(true) 연속 (Physx.cpp:441-451) → GetPhysicData() pull (PhysicsManager.cpp:93)
    → yield_WaitForFixedUpdate
→ GameLogic (SceneManager.cpp:443)
    Scene::Update (Scene.cpp:2653): AllUpdateWorldMatrix(PreUpdate) → AnimatorSystem(FSM) → Decal → Foliage(std::async×17/컴포넌트)
        → UITick → Sound → Camera → Light → PlayerInput → Tween → AllUpdateWorldMatrix(LateUpdate)
    YieldNull(코루틴)
    InternalAnimationUpdateEvent → AnimationJob::Update: 애니메이터당 태스크 1개 → 전용 풀 8 → NotifyAllAndWait (fork-join)
        → PublishAnimatorPose GT 직렬 루프 (AnimationJob.cpp:173)
    Scene::LateUpdate → UpdateRenderData → CommitRenderProxies(직렬 — "락 규약 미검증", Scene.cpp:2072)
→ TickManagedPostPhysics(C#) — "GT 전용, CoreCLR GC가 스레드를 정지시키기 때문" (RuntimeFrame.cpp:27)
→ SceneManager::EndFramePass (Scene.cpp:2753): DrainAIUpdate(무조건 join, :851-854) → FlushPendingDestroy → Destroy* → AI std::async 발사 (:2784)
→ EditorMain::Update m_sceneStructureMutex 구간 (EditorMain.cpp:543-556): ModelPlacement Tick · ApplyPendingSceneStructureChange · yield_OnRender · DisableOrEnable · EndOfFrame
→ App: packet + delta batch를 RenderThread 큐(용량 2)에 발행 — 큐 포화+병합 상한 초과 시에만 GT 블록 (EnhancedSceneRenderer.cpp:4066, 4082)
```

트랜스폼 전수 동기화가 프레임당 **3회**(FixedUpdate·PreUpdate·LateUpdate) 돈다. X5 이후 dirty epoch가 같으면 O(1) 조기 반환하므로(`Scene.cpp:4936-4941`) 정지 씬에서 "4,058µs×3"이라는 옛 수치는 더 이상 유효하지 않다. 움직인 프레임의 sparse resolve(`Scene.cpp:4185~`)는 단일 스레드 range 루프(`:4248`)이고 병렬 range resolve(X9)는 "선택 확장 중단"이다(`docs/plans/archive/TransformUpdatePlan.md:3, 18`).

### 1.4 이미 있는 병렬화 — 형태가 여섯이다

| 자리 | 형태 | 대기 | 비고 |
|---|---|---|---|
| `DataSystem::LoadAssetBundle` (`DataSystem.cpp:1672-1703`) | `WorkerPools->Enqueue` N건 → `NotifyAllAndWait` | 전역 카운터 | 씬 로드 시 1회, GT 블로킹(`SceneManager.cpp:862`) |
| `AnimationJob::Update` (`AnimationJob.cpp:94-173`) | 애니메이터당 람다 → 전용 풀 8 → `NotifyAllAndWait` | 전역 카운터 | 매 프레임 fork-join, 프레임마다 스냅샷 벡터·weak_ptr 벡터·클로저 힙 할당 |
| `EnhancedRenderGraph` 병렬 기록 (`EnhancedRenderGraph.cpp:600-830`) | `RHIParallelCommandPool::RunParallel(job, workers)` — 지속 워커 3 + 호출 스레드 | 풀 내부 join | **실측 임계값** `kParallelRecordCostThreshold=256`(`.h:199-221`)으로 병렬을 되무른다. 매 프레임 `std::thread` 생성은 순차 1.3761ms vs 병렬 2.3419ms로 손해였다는 실측 주석(`:777-780`) |
| `ResolveSpatialTransformsLegacy` (`Scene.cpp:4157`) | `std::for_each(std::execution::par, 루트 자식)` | STL 내부 | 토폴로지 변경 직후 첫 resolve·A/B off 경로만(`:4956`) |
| `FoliageComponent::UpdateFoliageCullingData` (`FoliageComponent.cpp:389-400`) | `std::async(launch::async)` × (hw×2+1) | future 전부 get | **매 프레임·컴포넌트마다**, 컴포넌트 간 직렬 중첩(`FoliageSystem.cpp:71`) |
| `Scene::EndFramePass` AI (`Scene.cpp:2784`) | `std::async(launch::async)` 1 | 다음 프레임 `DrainAIUpdate` | 워커가 씬을 읽는 동안 GT는 다음 프레임을 진행 |

즉 "병렬화가 없어서" JobSystem이 필요한 것이 아니다. 병렬화가 **여섯 양식으로 흩어져** 있고, 대기 의미론·우선순위·스케줄러가 제각각이다.

### 1.5 계획서가 이미 전제하는 것

| 계획 | 항목 | 상태 | 무엇을 전제하나 |
|---|---|---|---|
| PHASE 13 `AnimationSchedulerPlan` | S3.5 태스크 레시피·워커별 포즈 풀 · **S6 Job 배치 전환**(청크 분할, 전용 풀 vs `WorkerPools` 실측, 락프리 이벤트 큐) | 전부 todo | 워커 index 안정성·parallel_for·GT 드레인 큐 |
| PHASE 19 `PhysicsRedesignPlan` | T0 스레딩 계약 · **T1 잡 시스템 통합**(Jolt면 `JobSystemWithBarrier` 어댑터를 `WorkerPool` 위에, PhysX면 simulate↔fetch 창에 작업 배치) · T2 쿼리 병렬화 | 전부 todo, 백엔드 미결(X0 스파이크) | 부분 대기·배리어 |
| PHASE 4.75 `RenderGraphDependencySchedulingPlan` | RG4 dependency wave 병렬 기록 · RG8 multi-queue | 미착수 | 기존 record-cost 휴리스틱 재사용 명시(`:188-189`), "JobSystem" 언급 0건 |
| PHASE 14 `ProfilingCapturePlan` | P2 thread stream · sealed chunk handoff · `ThreadEnd` | P0·P1a 완료, P2~ todo | 워커 계측이 서려면 선행 |
| PHASE 15 `UtilityFrameworkModernizationPlan` | S1 `Thread`→`jthread` · S2 `CountingSemaphore`→`std::counting_semaphore` · `ThreadPool`/`WorkerPool` "존치" | todo | 풀 코어를 손대는 슬라이스와 겹침 |
| PHASE 8.75 `TransformUpdatePlan`(보관) | X9 병렬 range resolve | 선택 중단 | `ParallelFor`·false sharing 방어 |
| PHASE 4.75 `LightmapBakerPlan` | L4-a 베이크 전용 스레드 | todo | `RunParallel`은 프레임을 못 넘는 fork-join이라 못 쓴다고 명시(`:217-218`) |
| `docs/design/PhysicsComponentDesign.md:257, 360` | "Jolt JobSystem은 새 풀을 만들지 않고 엔진 WorkerPool을 공유" · "물리 전용 스레드는 하지 않는다" | 설계 결정 | 잡 시스템의 존재 |

**JobSystem을 자기 항목으로 소유한 계획서는 없다.** 셋(S6·T1·RG4)이 각자 다른 이름으로 같은 것을 전제하거나 자기 풀을 유지한다. `AnimationSchedulerPlan.md:475`의 `WorkerPools` 경합 목록에는 PhysicsRedesignPlan T1이 같은 풀을 쓰기로 한 사실이 없다(`PhysicsRedesignPlan.md:341`).

## 2. 발견 — 검증 통과분

각 항목은 원문 대조·최신성 두 렌즈를 합친 적대 검증을 통과했다. 반박된 것은 부록 A.

### 2.1 풀 원시의 결함 — 세 관점이 같은 줄을 지목했다

| 발견 | 근거 | 뜻 |
|---|---|---|
| **`NotifyAllAndWait`은 잡이 남아 있어도 반환한다 — 배리어가 아니다(실측 2.5~5.8%)** | `Core.ThreadPool.h:68-72, 125-129`; §10 | 카운터와 이벤트를 반대 순서로 갱신한다. **1순위 결함** |
| `NotifyAllAndWait`은 호출자별이 아니라 풀 전체의 단일 `m_taskCounts`/`m_waitEvent`로 동작한다 | `Core.ThreadPool.h:66-68, 80-86`, `WorkerPool.h:68` | 특정 클라이언트의 잡만 골라 기다릴 수 없다(계획 교차·외부 조사·물리 관점 모두 확인) |
| Jolt `JobSystemWithBarrier`를 이 위에 얹으면 물리 스텝의 `Barrier::Wait()`가 그 순간 큐에 남은 무관한 태스크까지 기다린다 | `Core.ThreadPool.h:81-82` | T1 어댑터는 `Enqueue`만 재사용하고 완료 판정은 자체 카운터로 새로 짜야 한다 — 그것이 곧 J1이다 |
| 완료 콜백·카운터 기반 논블로킹 API가 없다 — `WaitForSingleObject(INFINITE)`뿐 | `Core.ThreadPool.h:85` | 스트리밍·논블로킹 로드의 전제 부재 |
| 워커 인덱스가 태스크에 노출되지 않는다 | `Core.ThreadPool.h:101, 66`, `WorkerPool.h:25` | S3.5 "워커별 포즈 풀"이 현재 API로 불가 |
| `SetThreadInitCallback/SetThreadExitCallback`은 `WorkerLoop`에 스레드당 1회로 배선돼 있으나 설정 호출자가 0개 | `Core.ThreadPool.h:90, 95`(설정), `:103`(소비) | **정정(§10)**: 생성자가 스레드를 먼저 띄우므로 setter는 경합이다 — 실측 8워커 중 **5회만** 발화. 연결만으로는 안 되고 생성자 인자로 받아야 한다 |
| 엔진에 실행 가능한 범용 JobSystem은 없다 — 문자열은 vcxproj 필터 폴더 이름과 미착수 트랙으로만 존재 | `Engine/RenderEngine/RenderEngine.vcxproj.filters:7` | "부분적으로 존재한다"는 전제는 틀렸다 |

### 2.2 계획서 충돌과 소유권 부재

| 발견 | 근거 |
|---|---|
| 어느 계획서도 JobSystem을 소유하지 않는다. T1은 `WorkerPool` 위에 어댑터만, S6은 전용 풀 유지 여부를 실측 후 결정으로 미룸 | `docs/RefactoringPlanDashboard.html:2324`, `AnimationSchedulerPlan.md:541` |
| `AnimationSchedulerPlan`의 `WorkerPools` 경합 목록(UI 렌더 데이터·지형 로드)에 T1의 공유 계획이 빠져 있다 | `AnimationSchedulerPlan.md:475`, `PhysicsRedesignPlan.md:341` |
| T1은 `LoadAssetBundle`이 이미 같은 풀을 `Enqueue+NotifyAllAndWait`으로 쓰는 것과의 충돌을 재검토하지 않았다 | `PhysicsRedesignPlan.md:341`, `DataSystem.cpp:1678, 1702` |
| T0(스레딩 계약, P0, todo)가 코드에 미반영 — AI 회수는 여전히 비차단 폴링 | `Scene.cpp:2559`, `PhysicsRedesignPlan.md:235` |
| `AnimationJob.h`는 RenderEngine 소속 헤더(`RenderScene`이 값으로 소유)인데 유일한 구현 `AnimationJob.cpp`는 SceneRuntime에서 컴파일된다 — S6 완료 조건이 겨냥한 계층 기형이 그대로다 | `RenderEngine.vcxproj:458`, `SceneRuntime.vcxproj:167`, `RenderScene.h:133`, `AnimationSchedulerPlan.md:541` |
| `AnimationJob.h`는 풀 타입을 Utility_Framework에서 가져온다 — 새 JobSystem 타입도 RenderEngine과 같거나 낮은 계층에 있어야 헤더 참조가 성립한다 | `Engine/RenderEngine/AnimationJob.h:3, 52` |
| S6은 JobSystem 없이 완결될 수 없다 — 청크 분할·워커별 풀은 현재 API로 불가, 이벤트 큐 락프리화는 선택 항목 | `Core.ThreadPool.h:80`, `AnimationSchedulerPlan.md:474` |
| PhysicsComponentDesign과 PhysicsRedesignPlan T1은 "Jolt JobSystem은 기존 WorkerPool 공유"로 정합하다(미검증이나 원문 확인) | `PhysicsComponentDesign.md:257`, `PhysicsRedesignPlan.md:337` |

### 2.3 정확성 결함 — 한 건, 그리고 이미 닫힌 것들

| 발견 | 근거 | 상태 |
|---|---|---|
| **AI `std::async`의 트랜스폼 읽기와 다음 프레임 `AllUpdateWorldMatrix(FixedUpdate)`의 쓰기가 동기화 없이 겹친다** — `FixedUpdate` 초입의 `wait_for(0s)`는 준비된 경우에만 회수한다 | `Scene.cpp:2559, 2564`, `AIManager.cpp:57-65` | **열림**. 이 조사의 유일한 실제 데이터 레이스 |
| 레이스 창은 "다음 프레임 FixedUpdate"보다 넓다 — 발사 직후 같은 프레임의 `EndOfFrame()`이 드레인 없이 실행된다 | `EditorMain.cpp:555-556`, `SceneManager.cpp:484` | 열림 |
| 파괴(UAF)류 레이스는 닫혀 있다 — `EndFramePass`가 `DrainAIUpdate()`를 `Destroy*`보다 항상 먼저 부르고(`:2757`), `Scene` 소멸자도 재호출한다 | `Scene.cpp:851-854, 2753-2790, 361` | 닫힘 |
| 계획서의 Z-①("AI 스레드가 BT 액션을 통해 PhysX 컨테이너를 락 없이 만진다")은 현재 코드에 없다 — BT 틱은 이미 GT로 옮겨졌고 AI 스레드는 `QueueAITick`으로 담기만 한다 | `BehaviorTreeComponent.cpp:64-69`, `ClrHost.cpp:2996`, `RuntimeFrame.cpp:53, 83` | 닫힘 — 남은 것은 위 트랜스폼 레이스뿐 |
| 오늘 AI 스레드에서 도는 게임플레이 로직은 사실상 없다 — `StateMachineComponent`는 갱신 로직이 주석 처리, `InternalAIUpdate` 미오버라이드(미검증) | `StateMachineComponent.cpp:24`, `IAIComponent.h:16` | 범위 확정 |
| R6(워커에서 C# 이벤트 직접 발화)은 SpinLock 큐 + GT 전용 Flush로 이미 해소 — 대시보드의 "R1·R2·R4·R5·R6 전부 살아있다"는 낡았다. R4는 죽은 코드, **R5만 살아 있다** | `AnimationEventBridge.cpp:199`, `ClrHost.cpp:3168`, `RuntimeFrame.cpp:47`; R5: `AnimationController.cpp:59` `if (!StateVec.size() >= 2)` | 정정 |
| LC5-c `Entered()` 게이트는 관리 측 정적 스레드 검사라 잡 스레드가 큐잉 경로만 쓰는 한 막을 호출이 없다. 반면 `InvokeCallableStatic`의 `g_userCodeAllowed`는 비원자 plain bool로 실행 스레드 검사가 없다(미검증) | `ScriptCore/Native.cs:362, 368`; `ClrHost.cpp:2734, 2765` | 진입점마다 강제 수준이 다르다 |
| `TickManagedPostPhysics`는 "CoreCLR GC가 스레드를 정지시키기 때문"에 GT 전용이라고 주석이 명시한다(PrePhysics 주석에는 이 근거가 없다) | `RuntimeFrame.cpp:27` | 관리 경계는 워커 이관 금지 |
| `CoroutineManager`는 9개 LinkedList 큐를 락 없이 조작하고 GT의 `YieldNull`/`FixedUpdate`에서만 구동된다 | `Core.Coroutine.cpp:16, 19`, `Core.Coroutine.h:40`, `Scene.cpp:2597, 2730` | 병렬화 금지 |

### 2.4 낭비 — JobSystem이 없어도 보이는 것

| 발견 | 근거 | 크기 |
|---|---|---|
| WorkerPool·AnimationJob 풀은 HIGHEST 8+8, 렌더 파이프라인 스레드(RT·RHISubmission·Presentation·커맨드 워커)는 `SetThreadPriority` 호출이 없어 NORMAL — 우선순위가 거꾸로다 | `Core.ThreadPool.h:20`, `Core.Thread.h:64`, `AnimationJob.cpp:39`, `WorkerPool.h:31` | **실측(§10)**: 8워커 HIGHEST는 8워커 NORMAL 대비 풀 처리량 −41%, 이웃 스레드 최대 지연 1.8배 — 두 축 모두 손해 |
| 고정 7 + WorkerPool 8 + AnimationJob 8 = 최소 23 스레드가 affinity 없이 8코어를 경쟁 | `SceneManager.cpp:322`, `RenderScene.h:133`, `Core.ThreadPool.h:38` | 미측정 |
| PhysX 디스패처 4가 Animation 8·WorkerPool 최대 8과 조정 없이 같은 코어를 다툰다 — 8코어 기준 200%+ 이론 과다구독 | `Physx.cpp:167-169` | 산수만 확정 |
| Foliage: 분할 수가 하드웨어 상수(`hw*2+1`=17)에 고정 — 인스턴스 <17이면 인스턴스마다 태스크 1개, 그 이상이면 항상 17; 컴포넌트별 팬아웃→조인이 직렬 중첩이라 컴포넌트 간 오버랩 0; 실측 임계값 게이트가 없다 | `FoliageComponent.cpp:315-341, 389-400`, `FoliageSystem.cpp:71` | 미측정(씬당 인스턴스 규모 불명) |
| `AnimationJob`: 태스크 입도 "애니메이터 1개 = 태스크 1개"(S6이 요구한 O(워커 수) 청크 없음); 프레임마다 스냅샷 벡터·weak_ptr 벡터·클로저 힙 할당; 포즈 저장소 고정 512본 인라인(Animator 64KB·Controller 32KB) | `AnimationJob.cpp:94, 112, 126`, `Animator.h:194`, `AnimationController.h:38` | 미측정(계획서 표 수치만) |
| `LoadAssetBundle`이 씬 전환 시 GT를 번들 전체 완료까지 블록한다 | `SceneManager.cpp:862`, `DataSystem.cpp:1702`, `SceneObjectCommands.cpp:192`, `App.cpp:260` | 미측정 |
| 한 모델 안의 임베디드 텍스처 디코드는 완전 직렬 — 병렬성은 모델 단위에서만 | `ModelAssetGeneration.cpp:819, 853` | 미측정 |
| PSO 캐시 미스마다 무제한 `std::async` — 자산 로드 8스레드와 겹치면 과다구독 | `DX12PSOManager.cpp:719` | 미측정 |
| 저장소 안에 완료-폴링 IO 잡 패턴이 이미 있다(`EditorModelPlacement`, BELOW_NORMAL 워커 + `Tick()` 폴링) | `EditorModelPlacement.cpp:91, 181` | 청사진 |
| `DataSystem` 캐시 락은 조회·삽입만 감싸고 IO는 락 밖 — 병렬화에 우호적(미검증) | `DataSystem.cpp:1219`, `ModelAssetGeneration.h:435` | 구조 확인 |
| 아카이브의 `textureDecodeMs.scene=550ms`는 단일 스레드 모델 벤치 값이지 씬 로드 wall-time이 아니다 | `AssetAuthoringCommands.cpp:2860`, `verify-model-cutover-budget.ps1:86` | 오독 경고 |

### 2.5 렌더 워커 풀은 별개 계약이다 — 흡수 대상이 아니다

| 발견 | 근거 |
|---|---|
| RT는 공용 풀과 분리된 자체 지속 워커 풀을 갖는다(DX12 workerCount=4, Vulkan도 동일 계약으로 4) | `EnhancedSceneRendererLiveDX12Adapter.cpp:65`, `DX12CommandListPool.cpp:84`; `EnhancedSceneRenderer.cpp:443`, `VulkanCommandBufferPool.cpp:61-63` |
| 워커 0은 호출 스레드 자신, 워커가 1개면 인라인 실행 — 비교 기준에 동기화 비용을 섞지 않으려는 설계 | `DX12CommandListPool.cpp:129, 134` |
| 얼로케이터/리스트가 (프레임 슬롯, 워커 인덱스) 2차원 고정 배열 — 워커 인덱스가 곧 자원 슬롯 신원 | `DX12CommandListPool.cpp:45` |
| 기록 조각을 라운드로빈이 아니라 연속 블록으로 배분 — 커맨드 리스트가 통째로 한 번만 제출되는 단위이기 때문 | `EnhancedRenderGraph.cpp:678, 683` |
| 매 프레임 `std::thread` 생성은 실측으로 기각(순차 1.3761ms vs 병렬 2.3419ms, 1.7배 손해) | `EnhancedRenderGraph.cpp:777-778` |
| RG4 계획서는 기존 휴리스틱 유지를 명시하고 "JobSystem"이 한 번도 등장하지 않는다 | `RenderGraphDependencySchedulingPlan.md:188-189` |
| 대시보드는 `RunParallel`을 "join까지 대기하는 fork-join"으로 규정해 프레임을 넘는 수명(L4-a)에는 못 쓴다고 적는다 | `RefactoringPlanDashboard.html:2601`, `LightmapBakerPlan.md:217-218` |
| `TickLive`의 컬링·정렬·풀링은 전부 직렬이고 RG4/RG8 범위 밖 — 규모 근거가 없어 이득 단정 불가 | `EnhancedSceneRenderer.cpp:2819, 3372` |
| 병렬 기록 경로에 워커 구간 계측이 없다 | `EnhancedRenderGraph.cpp:777, 784` |

### 2.6 트랜스폼 — X9를 되살릴 근거는 약하다

| 발견 | 근거 |
|---|---|
| sparse range 순회는 무조건 순차 for — X9는 코드에 없다 | `Scene.cpp:4248` |
| canonical range는 preorder 정렬+병합으로 항상 서로소 — 병렬 자체의 정당성은 있다 | `Scene.cpp:4237` |
| 그러나 `SpatialResolveMetrics` 카운터가 range 루프 안에서 비원자 `++` | `Scene.cpp:4239, 4355` |
| `PublishRenderProxyDirty`는 `registry.dirtyMutex` 하나로 잠근다 — 병렬 worldWrite가 여기서 직렬화 | `Scene.cpp:2015` |
| 벤치는 N=10,000·random 1%에서 100노드를 9.5µs(범위당 ~95ns)에 처리한다 — 이 크기에서 디스패치 비용이 이득을 삼킬 개연성이 크다 | `TransformUpdatePlan.md:1135-1137` |

### 2.7 물리 창 — T1(b)는 그대로 적용할 수 없다

| 발견 | 근거 |
|---|---|
| `simulate()`~`fetchResults(true)` 창 길이는 어디에도 실측이 없고 계측 스코프도 없다 | `Physx.cpp:441, 447` |
| `fetchResults` 직후 같은 함수에서 `GetPhysicData()` pull이 끝나고 나서야 GameLogic이 돈다 — 창에 애니메이션·오디오를 넣으면 그 시스템들이 이전 프레임 트랜스폼을 보게 되는 의미 변화 | `PhysicsManager.cpp:93`, `RuntimeFrame.cpp:83-84` |
| 오늘은 Physics 전체가 끝난 뒤 GameLogic이 시작되므로 겹침이 설계상 0 — (b)는 두 호출의 경계 자체를 바꿔야 성립 | `RuntimeFrame.cpp:83-84` |
| T0 계약: 물리 잡 내부는 `PxScene`/`PhysicsBodyStore`를 읽지 못하고 잡 시작 시점의 불변 스냅샷만 읽는다 — 백엔드와 무관한 공통 요구 | `PhysicsComponentDesign.md:237, 239` |
| Jolt는 벤더링돼 있지 않다 — Jolt 인터페이스 진술은 일반 지식 추정 | `vcpkg.json:47` |

### 2.8 관측 준비도 — 낮다

| 발견 | 근거 |
|---|---|
| 지속 워커는 어디도 `gCPUProfiler.RegisterThread`를 부르지 않는다 — 호출자는 GT(`EditorMain.cpp:77`)와 selftest 합성 워커뿐 | `EditorMain.cpp:77`, `ProfilerSelfTest.cpp:290`, `DX12CommandListPool.cpp:92` |
| `PROFILE_CPU_*` 매크로는 세 파일의 GT 함수에만 있다 | `Scene.cpp:2679, 2784` |
| `Profiler.cpp:119` 주석이 자인: `Tick()`이 `pTLS->EventBuffer[i]`를 읽는 동안 워커가 `BeginEvent`에서 resize하면 옛 버퍼가 해제된다. 지금 안 터지는 이유는 계약이 아니라 GT가 유일한 writer이기 때문 | `Profiler.cpp:115-122`, `ProfilingCapturePlan.md:690` |
| `Event`에 `EngineFrameId`가 없고 `ThreadEnd`도 없다 — P1 잔여·P2로 미완료 | `Profiler.h:230`, `ProfilingCapturePlan.md:152` |
| 대시보드 14-2 note: "PresentationThread/RenderThread를 계측하려면 sealed handoff가 먼저" | `RefactoringPlanDashboard.html:2092, 2095` |
| `run-all.ps1`은 `profile.selftest` 게이트를 부르지 않는다 | `Tools/regression/run-all.ps1` 전체 grep 0건 |
| ASan 게이트는 R1/R2 수명 시나리오만 잰다 | `verify-asan-lifecycle.ps1:1, 4` |
| selftest의 멀티스레드 픽스처는 이벤트 하나 찍고 즉시 대기해 resize 경합을 재현하지 않는다 | `ProfilerSelfTest.cpp:288, 293` |

### 2.9 외부 라이브러리 — 저장소 안에서 확인된 것만

| 발견 | 근거 |
|---|---|
| `ThirdParty/README.md` 정책은 "vcpkg로 못 가져오거나 가져오면 안 되는 것"만 허용, mikktspace처럼 stand-alone 파일은 복사 모델 전례 | `ThirdParty/README.md:5, 12` |
| enkiTS는 표준 헤더만 포함하는 헤더+cpp 쌍, zlib(외부 API 호출·GitHub 메타데이터 — 로컬 대조 불가) | 외부 |
| enkiTS DAG API·pinned task, Wicked 3풀 구조, Jolt `JobSystemWithBarrier` 5함수 — **전부 반박**(부록 A) | — |
| marl archived, Taskflow C++20·`executor.hpp` 2554줄(미검증) | 외부 |

## 3. JobSystem과 무관하게 먼저 고칠 결함·낭비

JobSystem 결정과 독립적으로 고쳐야 하고, 각각 소유자가 있거나 정해야 한다.

| # | 항목 | 근거 | 소유 | 비고 |
|---|---|---|---|---|
| **D0** | **`NotifyAllAndWait`이 배리어가 아니다** — 잡이 남아 있어도 2.5~5.8% 반환. `AnimationJob`의 두 안전 근거(포즈 커밋 순서 `:167-170`, raw `Animator*` 수명 `:117-119`)가 모두 이 불변식 위에 서 있다 | `Core.ThreadPool.h:68-72, 125-129`; §10 | **신설 — 최우선** | 성능이 아니라 정확성. 20줄로 고쳐지고 고치면 더 빠르다(§10) |
| D1 | **AI 비동기 ↔ 트랜스폼 데이터 레이스** — `wait_for(0s)` 폴링을 물리 스텝 전 무조건 join으로 | `Scene.cpp:2559`, `AIManager.cpp:65` | PhysicsRedesignPlan **T0** (Z-① 재정의: "끊는다"가 아니라 "재발을 계약으로 봉인") | J8b. 창이 `EndOfFrame`까지 이어지므로 join 위치를 실측으로 확정 |
| D2 | `AssetLoadJob` 죽은 코드 삭제 | `AssetJob.h:11` | 정리 슬라이스 | J6. 인스턴스 0건 grep 재확인 선행(미검증 발견) |
| D3 | Foliage 분할 수가 하드웨어 상수(17)에 고정·임계값 게이트 부재 | `FoliageComponent.cpp:315-341` | 신설 | J8a. 인스턴스 규모 실측 전 착수 금지 |
| D4 | PSO `std::async` 상한 부재 | `DX12PSOManager.cpp:719` | RHI PSO 소유자 | J10 |
| D5 | `LoadAssetBundle` GT 블록 | `DataSystem.cpp:1702` | 자산 스트리밍(계획서 부재) | J7·J9 |
| D6 | R5 연산자 우선순위 버그 `if (!StateVec.size() >= 2)` | `AnimationController.cpp:59` | PHASE 13 S0 | 항상 false 비교 — JobSystem 무관 |
| D7 | 대시보드 서술 갱신: R1·R2·R4·R6 해소/사망, PHASE 19 M1 행이 PHASE 4.75 L3 텍스트 복사(데이터 오류, 미검증) | `RefactoringPlanDashboard.html` | 대시보드 | 이 문서 범위 밖(수정 안 함) |
| D8 | `profile.selftest`가 run-all에 없다 | `run-all.ps1` | PHASE 14 | 라이브 캡처 교란(§3.2 자인) 때문에 격리 실행 구간 필요 |
| D9 | `Profiler.cpp:119` EventBuffer resize 잠복 결함 | `Profiler.cpp:115-122` | PHASE 14 **P2** | 워커 계측을 넣는 순간 발동 — 모든 워커 계측 슬라이스의 선행 |
| D10 | `SetThreadPriority` 비대칭(작업 풀 HIGHEST, 렌더 NORMAL) + 풀마다 논리코어 수만큼 워커 | `Core.ThreadPool.h:20, 23` | 신설 | J5. **실측(§10)**: HIGHEST는 처리량 −41%. 워커 8→3에서 이웃 스레드 지연이 거의 사라지고 처리량은 75% 유지 |
| D13 | `SetThreadInitCallback`이 생성자 스레드 기동과 경합 — 배선해도 일부 워커에서만 발화(실측 5/8) | `Core.ThreadPool.h:31-43, 90, 103`; §10 | 신설 | J4의 전제를 바꾼다 |
| D11 | `DisableOrEnable()`이 이름과 달리 `EndFramePass` 위임(미검증) | `SceneManager.cpp:496-499` | 씬 그래프 | 이름 정정만 |
| D12 | `Canvas::TickCanvasOrder`의 `needSort` 공유 플래그 락 없음(미검증) | `Canvas.cpp:120, 129` | UI | 지금은 단일 스레드라 안전, UITick 병렬화 시 선행 |

## 4. JobSystem이 주는 것과 안 주는 것

**주는 것(구조):**
- **부분집합 대기** — S6(청크 분할)·T1(Jolt 배리어)·T0 계약이 같은 풀 위에서 서로를 기다리지 않게 된다. 이것이 유일하게 "지금" 필요한 것이다.
- **워커 인덱스** — S3.5 워커별 포즈 풀·steal-in-place, render 풀과 같은 "인덱스=자원 슬롯" 패턴을 공용 풀에서도 쓸 수 있다.
- **실측 게이팅이 붙은 `ParallelFor`** — Foliage 17분할·애니메이터당 태스크 1개라는 두 낭비를 한 원시로 대체하되, 임계값 미만은 직렬 폴백.
- **핸들 반환 제출** — AI·자산 로드·PSO의 fire-and-forget을 명시적 드레인 계약으로 바꾼다.
- **스케줄러 통합 가능성** — ConcRT(std::async)·전용 풀·WorkerPool 셋을 하나로 줄이면 4코어에서 과다구독이 준다. 크기는 미측정.

**안 주는 것:**
- **트랜스폼 속도** — X5가 이미 정지 O(1)·변경분 O(k)를 달성했다. X9가 벌 수 있는 몫은 수 µs~수백 µs의 sparse 열이고, 비원자 카운터·단일 mutex·false sharing 선행 결함이 남아 있다.
- **렌더 기록 속도** — 자체 풀이 3차 실측을 거쳐 굳었고 RG4/RG8이 소유한다.
- **물리 백엔드 결정** — X0 스파이크(PhysX vs Jolt) 미결. T1(b)의 창 배치는 pull 시점 재설계 없이는 의미 변화를 낳는다.
- **관리 경계 안전** — 큐잉(`Queue*`/`Flush*`) 규약은 이미 있다. JobSystem은 그 규약을 게이트로 강제할 뿐 새로 풀지 않는다.
- **코루틴·트윈 병렬화** — 락 없는 GT 전용 구조.
- **지금 당장의 ms 수치** — 어떤 슬라이스도 실측 이득을 갖고 있지 않다.

## 5. 설계안 비교

세 각도의 설계안을 독립 심판 3인이 채점했다(이득/공수·위험·계획 정합·검증 가능성, 각 10점).

| 각도 | 명제 | 공수 | 심판 1 | 심판 2 | 심판 3 | 치명 |
|---|---|---|---|---|---|---|
| **A. `Core.ThreadPool` 최소 확장** | 전역 카운터를 클라이언트별 `JobCounter`로 쪼개고(J1) 그 위에 워커 인덱스·`ParallelFor`·프로파일러 등록·핸들 제출을 얹어 산재한 소비자를 하나씩 옮긴다 | 15.5일 → 접목 후 17일 | **8.2** | **8.2** | **8.0** | J1의 default-counter 경로와 신규 counter 경로 간 우선순위 역전을 스트레스 테스트로 남겨 둠 |
| B. 외부 잡 시스템 벤더링 + 계층 계약 | enkiTS/Wicked/Jolt 중 하나를 ThirdParty 정책 예외로 들여 `Utility_Framework::Jobs` 얇은 계약으로 감싼다 | 10일 | 5.4 | 5.0 | 5.6 | 핵심 근거(외부 API 4건) 전부 반박; S0 실사 실패 시 A로 회귀를 스스로 인정; "Jolt는 WorkerPool 공유" 기존 합의와 충돌; bshoshany-thread-pool 제거 전례와 배치 |
| C. JobSystem 없이 국소 패치(대조군) | — | — | 1.2 | 0.4 | 2.0 | **자리표시자로 돌아와 논증되지 않았다**(부록 B). 별도 재논증은 §5.1 |

**A가 만장일치**로 채택됐고, B에서 접목한 것은 셋이다: 계층 위반 회귀 게이트(J1a), 잡 콜백 내 관리 코드 호출 금지 정적 검사(J1b, PHASE 24 완성을 기다리지 않는 독립 슬라이스), "외부 근거는 원문을 fetch해 대조한 뒤에만 채택"이라는 실사 원칙. Jolt 자체 `JobSystemThreadPool`을 국소적으로 쓰는 대안은 T1 각주로만 남긴다.

비평(critic)은 판정을 뒤집지 않았으나 A의 세부 넷을 결함으로 지적했고 §6·§9에 반영했다: `EnqueueIndexed`는 오버로드 추가가 아니라 `TaskType` 변경을 요구한다, `JobCounter`/`JobHandle` 수명 규약이 없다, 측정 선행 공수가 17일에 합산돼 있지 않다, Vulkan 비대칭 논거(이 문서에서 정정 — 부록 A).

### 5.1 대조군 재논증 — "JobSystem 없이 국소 패치"

심판을 거치지 않은 단독 에이전트가 대조군을 최대한 강하게 논증했다(읽기 전용, 코드 재확인 55회). 요지와 판정.

**대조군의 명제.** 실측 결함·낭비 9건 중 8건은 공유 원시를 한 줄도 건드리지 않고 **9일**에 고칠 수 있고, 그중 Foliage·PSO·프로파일러 등록·AI join은 찬성안의 선행 사슬(J0→J1→J1a→J1b, 4일) 없이 오늘 착수할 수 있다. 부분집합 대기는 `DX12CommandListPool.h:100-131`이 이미 뮤텍스+condition_variable+세대 카운터(`m_generation`)로 프로덕션에서 구현한 선례가 있으니 "공유 카운터를 일반화한다"가 유일한 길이 아니다. 우회 원시는 클라이언트별 별도 풀(가장 비쌈 — AnimationJob이 이미 그 길을 갔고 계획서가 낭비로 적음)도, PPL `task_group`(이 저장소가 PPL 상위 알고리즘을 0건으로 유지해 왔고 ConcRT 워커는 프로파일러 등록 지점이 없음)도 아니라 **기존 `Enqueue` + 로컬 `std::latch`/원자 카운터**여야 한다.

| 대조군 슬라이스 | 공수 | 찬성안 대응 | 비교 |
|---|---|---|---|
| C1 AI 레이스 무조건 join | 1 | J8b 1 (J1b 선행) | 수정 내용 동일, 선행만 다름 |
| C2 Foliage 인스턴스 기반 분할 + 임계값 | 1.5 | J8a 1.5 (선행 7.5) | 게이트가 검증하는 것은 "임계값이 있는가"이지 "공용 원시를 쓰는가"가 아니다 |
| C3 PSO 세마포어 상한(`Core.CountingSemaphore.h` 기존 원시) | 1 | J10 1.5 (J2 선행) | PSO는 카운팅 문제라 워커 인덱스가 필요 없다 |
| C4 `AssetLoadJob` 삭제 | 0.5 | J6 0.5 | 동일 |
| C5 `LoadAssetBundle` 로컬 원자 카운터 + 폴링(`EditorModelPlacement.cpp:29, 181` 패턴 복제) | 2.5 | J7+J9 3.5 (J1 선행) | 호출부 넷(`SceneManager.cpp:745, 862, 969, 1077`)의 계약 통일 필요 |
| C7 `AnimationJob` 정적 청크(`GetThreadCount()`, `:88`) | 1 | S6 일부 — "J2 없이 불가"를 반증 | steal-in-place 제외 |
| C8 프로파일러 등록 | 0.5 | J4 1 (J1 선행) | 기존 setter 호출만 |
| C9 우선순위 통일 | 1 | J5 1 (J1 선행) | 동일 |
| C10 `ThreadPool` permit 유실 방지(양쪽 공통, 합계 밖) | 0.5~1 | — | 찬성안 §6.1에 이 문장이 없었다 |
| **합계** | **9** (+T1 조건부 2~3) | 17.5 | 커버리지가 절반 — 대조군 자신의 표현으로 "이득이 아니라 범위를 좁힌 값" |

**대조군이 맞힌 것 — 채택.**
1. J8b는 JobSystem 원시가 필요 없다. 기존 `std::future`를 join하는 한 줄이다. 선행 사슬에서 뗐다(§6.2).
2. J10은 워커 인덱스가 필요 없다. J2 의존은 과잉 결합이었다. 1단계는 세마포어 상한, 2단계만 J1 위 이관으로 갈랐다(§6.2).
3. S6의 "태스크 수 = O(워커 수)"는 정적 분할로 J2 없이 충족된다. "S6 하드 블로커"는 정확히는 **S3.5 워커별 포즈 풀·steal-in-place의 하드 블로커**다(§요약 정정).
4. `ThreadPool` permit 유실 의심(`PPLContainerMigrationAnalysis.md:337-339`: `acquire()` 후 `try_pop` 실패 시 카운터 미보정) — 코드 구조(`Core.ThreadPool.h:68-77` 카운터·큐·세마포어 3중 비원자 갱신, `:113-131` 실패 경로 보정 없음)는 재확인했으나 레이스 재현은 없다(**미검증**). 세마포어 permit이 push 수를 넘지 않으므로 정상 경로에서는 `try_pop`이 실패하지 않아야 하지만, J1이 같은 스켈레톤 위에 얹히면 결함이 있을 경우 N배로 상속한다. J0 exit 게이트에 "카운터 = 큐 잔량" 스트레스 단정을 넣어 먼저 가른다.
5. `PPLContainerMigrationAnalysis.md`(08-08)가 인용한 `WorkerPools` 소비자(카메라 루프 10건·`Terrain.cpp:479`·`ModelSceneBridge`)는 HEAD에 없다 — 한 달 사이 소비자가 줄어 `LoadAssetBundle` 하나만 남았다. "풀 클라이언트가 늘어난다"는 전제는 현재 추세와 반대이며, 늘어나는 시점은 T1·S6 착수 때다.

**대조군이 무너지는 곳 — 판정 유지.**
1. S3.5 워커별 포즈 풀·steal-in-place는 물리적 워커 정체성이 필요하다. `WorkerLoop(int threadIndex)`(`:101`)의 인덱스를 `TaskType = std::function<void()>`(`WorkerPool.h:25`)가 막고 있어, 로컬 우회는 `DX12CommandListPool` 규모(약 140줄)의 세 번째 풀을 Animation 전용 파일 안에 또 짜는 것이다. 대조군 스스로 "국소 패치라는 이름값을 깨뜨린다"고 인정했다.
2. T1(Jolt 배리어)은 "그때 가서 짠다"뿐이다. X0 미결은 양쪽 공통이지만, 찬성안은 얹을 API(§6.1)가 있고 대조군은 약속만 있다.
3. C5(로컬 카운터)는 `WorkerPools` 소비자가 하나일 때만 안전하다. 물리가 풀을 공유하는 순간 "레거시 `NotifyAllAndWait` 사용자 0"이 깨져 **C5를 물리 착수보다 먼저 끝내야 한다는 순서 제약**이 새로 생긴다. `JobCounter`는 클라이언트별 격리라 이 제약을 만들지 않는다.
4. 9일 대 17.5일은 이득이 아니라 커버리지 절반의 값이다.

**결론.** 판정은 바뀌지 않는다 — J1은 T1·S3.5·풀 공유의 전제로 여전히 필요하다. 다만 **착수 순서는 대조군이 옳다.** 선행 사슬이 없는 국소 수정 다섯(J8b·J10 1단계·J6·J4·J5)을 J0/J1과 병행해 즉시 착수하고, 자산 로드(J7/J9)는 로컬 카운터가 아니라 J1 위에서 해 순서 제약을 피하며, J2/J3은 S3.5·S6 착수 결정 시점까지 보류한다.

## 6. 권고안 — `Core.ThreadPool` JobSystem 최소 통합

### 6.1 API 스케치

```cpp
// Engine/Utility_Framework/Core.ThreadPool.h 확장.
// 계층: Utility_Framework에 둔다. SceneRuntime/RenderEngine 타입을 include하지 않는다(J1a 게이트).

struct JobCounter {                       // J1. 소유자는 호출자. 잡이 참조하는 동안 살아 있어야 한다(§6.3 수명 규약).
    std::atomic<int> pending{0};
    std::mutex m;
    std::condition_variable cv;
};

struct JobHandle {                        // J9/J10. Utility_Framework 소유 — RenderEngine(DataSystem)·RHI(PSO)가 공유.
    void Wait();
    bool IsDone() const;
};

class ThreadPool {
public:
    // 기존 API — 그대로. 내부 default counter로 위임해 하위호환 유지.
    void Enqueue(std::function<void()> job);
    void NotifyAllAndWait();

    // J1: 클라이언트별 부분집합 대기.
    void Enqueue(JobCounter& counter, std::function<void()> job);
    void Wait(JobCounter& counter);       // counter.pending==0까지만. 다른 클라이언트 잡과 무관.

    // J2: 워커 인덱스 노출. ★ 오버로드 추가로 끝나지 않는다 —
    //     TaskType이 std::function<void()>(WorkerPool.h:25)이고 WorkerLoop가 task()를 무인자로 부르므로(:123),
    //     (a) TaskType을 std::function<void(int)>로 바꾸거나 (b) WorkerLoop에 thread_local 워커 인덱스를 두고
    //     조회 API를 제공해야 한다. (b)가 기존 소비자를 깨지 않는다.
    void EnqueueIndexed(JobCounter& counter, std::function<void(int workerIndex)> job);
    static int CurrentWorkerIndex();      // (b) 경로. 워커 밖에서는 -1.

    // J3: 청크 분할 parallel_for. 청크 수 = O(워커 수). count < minCountForParallel이면 호출 스레드에서 직렬.
    void ParallelFor(JobCounter& counter, size_t count, size_t minCountForParallel,
                     std::function<void(size_t begin, size_t end, int workerIndex)> chunkFn);

    // J9/J10: fire-and-forget 대신 핸들. 호출자가 프레임 경계에서 명시적으로 드레인.
    JobHandle SubmitDrainable(std::function<void()> job);

    // J4: 이미 존재하는 콜백(Core.ThreadPool.h:90,95)을 프로파일러에 연결. 등록/은퇴만.
    void SetThreadInitCallback(std::function<void()> cb);   // -> gCPUProfiler.RegisterThread
    void SetThreadExitCallback(std::function<void()> cb);   // -> gCPUProfiler.UnregisterThread
};
```

### 6.2 슬라이스

| # | 슬라이스 | 공수 | 선행 | 소유 | exit 게이트(변이 증명 포함) |
|---|---|---|---|---|---|
| **J-1** | **배리어 정직성 복구** — `m_waitEvent`를 카운터+`condition_variable`로 교체(§10.4 V1). `AnimationJob`·`LoadAssetBundle`의 안전 근거가 이것에 의존한다 · **적용 완료(§11.1, 미커밋)** | 0.5 | 없음 | 신설(JobSystem 트랙) | 독립 완료 카운터로 배리어 반환 직후 잔여 잡 수를 단정 → **89,000시행 0건 확인**. 변이(Event 배리어 원복)로 §10.1의 2~6% 재현. **잔여: 이 단정을 저장소 안 회귀로 고정하는 일** |
| J0 | 현재 `ThreadPool` 계약 회귀 고정 — 전역 카운터 의미론·각 풀 우선순위를 합성 테스트로 기준선화 | 0.5 | J-1 | 신설(JobSystem 트랙) | 태스크 100개 투입 후 `NotifyAllAndWait`이 무관한 호출자의 잡까지 기다림을 단정(지금은 반드시 참). **추가(§5.1)**: 고빈도 push/pop 스트레스에서 `m_taskCounts`가 큐 실제 잔량과 항상 일치함을 단정 — permit 유실 의심(`PPLContainerMigrationAnalysis.md:337-339`, 미검증)을 J1이 상속하지 않도록 먼저 가른다 |
| **J1** | **`JobCounter` 도입 — 부분집합 대기** | 2 | J0 | 신설 — S6·T1이 이 위에 얹힌다 | A(긴 태스크 100, counterA)·B(짧은 태스크 1, counterB) 동시 투입 → `Wait(counterB)` 즉시 반환 단정; 변이(카운터 합침)로 실패. **추가**: default-counter 경로와 신규 경로가 같은 큐를 채우는 스트레스로 상호 지연을 수치화해 허용 오차로 명시 — 계측 없이 "해결"로 닫지 않는다 |
| J1a | 계층 위반 회귀 게이트 `verify-jobsystem-layering.ps1` | 0.5 | J1 | 신설 — S6·T1 공통 전제로 문서화 | JobSystem 헤더의 include에 SceneRuntime/RenderEngine 없음. 변이(임시 include 추가)로 빨강. run-all 배선 |
| J1b | 잡 콜백 내 `ClrHost::*`/`Native.*` 호출 금지 정적 검사 | 1 | J1a | 신설 — PHASE 24를 전제하지 않음 | `Enqueue*/ParallelFor` 람다 본문 grep. 변이(가짜 `ClrHost::` 호출)로 빨강. 참조 구현: `QueueScriptMessage`/`QueueAITick` |
| J2 | 워커 인덱스 노출 | **1.5** (비평 반영: `TaskType`/`thread_local` 내부 변경) | J1b | PHASE 13 S3.5/S6 | N개 투입 시 인덱스가 0..workerCount-1이고 동시에 두 태스크가 같은 인덱스를 관측하지 않음. 변이(항상 0)로 실패 |
| J3 | `ParallelFor` + 실측 게이팅 임계값 | 2 | J2 | PHASE 13 S6 | 인스턴스 1/17/100/1000에서 직렬 대비 손익분기점 실측 → `minCountForParallel` 고정. 변이(게이트 제거)로 소규모 손해 재현 |
| J4 | 프로파일러 스레드 등록 배선(등록/은퇴만) — **콜백을 setter가 아니라 생성자 인자로 옮기는 것이 선행**(§10: 현재 setter는 5/8만 발화) | 1 | J1 | PHASE 14 P1 잔여 | `ProfilerSelfTest`에 워커 N 기동/종료 시 Register/Unregister N/N 단정. 변이(미배선)로 0. **`BeginEvent`는 P2 전 금지** |
| **J5** | 우선순위·워커 수 정책 통일 + 자가진단 — **§11 이후 J1보다 우선순위가 높다** | 1 | 없음 | 신설 | 각 풀 생성 시 `GetThreadPriority` 로그, run-all에서 정책 불일치 시 실패. **§10.5·§11.2 근거**: HIGHEST 기본값 폐기(같은 풀에서 배속 2.47→4.52), 풀 합계 워커 수를 논리코어 미만으로 |
| J6 | `AssetLoadJob` 삭제 | 0.5 | J0 | 정리 | grep 0건 재확인 → 삭제 → 빌드 exit code 0 + 회귀 세트 통과 |
| J7 | `LoadAssetBundle` → J1 counter 경로 | 1 | J1 | 자산 스트리밍 | 이관 전/후 총 완료 시간 동일(동일 워크로드) |
| J8a | Foliage `std::async` → `ParallelFor` | 1.5 | J3 | 신설 | 교체 전/후 동일 컴포넌트 수 프레임 CPU 비교. 컴포넌트 간 오버랩은 이 슬라이스가 고치지 않음을 명시 |
| **J8b** | **AI async → 무조건 join 계약** | 1 | **없음** (기존 `std::future` join — `SubmitDrainable` 이관은 J1 이후 선택, §5.1) | **PhysicsRedesignPlan T0** — JobSystem은 `SubmitDrainable`만 제공 | FixedUpdate 쓰기와 `InternalAIUpdate` 읽기를 인위 동시 실행하는 합성 레이스: 적용 전 불일치 재현, 후 통과. join 위치는 `EndOfFrame` 창까지 닫히는지 확인 |
| J9 | `LoadAssetBundle` 논블로킹화(GT 블록 제거) | 2.5 | J7, J1b | 자산 스트리밍 | 블로킹판은 GT 프레임타임 스파이크 관측, 논블로킹판은 폴링 프레임당 수 µs 이하. 변이(폴링 제거)로 스파이크 재현. `LoadSceneAsync` 이중 레이어 중 어느 지점을 바꿀지 먼저 확정 |
| J10 | PSO `std::async` 상한 — 1단계 `Core.CountingSemaphore.h` 기존 원시로 동시 컴파일 수 상한(선행 없음), 2단계 J1 `SubmitDrainable` 위 bounded 큐로 이관 | 1.5 | 1단계 없음 · 2단계 J1 (워커 인덱스 불필요 — J2 의존은 과잉 결합, §5.1) | RHI PSO 소유자 | 동시 미스 N에서 활성 스레드 수 ≤ 상한. 변이(unbounded)로 N까지 치솟음. 상한이 너무 작으면 폴백 셰이더 프레임 드롭이 생기는 반대 위험 |

합계 **18일**(J2 0.5 증가, J-1 0.5 신설). **여기에 §8의 측정 선행 공수는 포함돼 있지 않다.**

**§10 이후 J1의 성격이 바뀐다.** J-1이 배리어를 정직하게 만들면 `JobCounter`는 "그 카운터를 클라이언트별로 쪼개는 것"이 되어 J-1의 자연스러운 연장이 된다. 반대로 J-1 없이 J1만 하면 클라이언트마다 거짓 배리어를 하나씩 갖게 된다.

착수 순서(§5.1 반영): **선행 사슬이 없는 다섯 — J8b(정확성)·J10 1단계·J6·J4·J5 — 는 J0→J1과 병행해 즉시.** 그 뒤 J1a → J1b → J7 → J2 → J3 → (실측 후) J8a·J9·J10 2단계. J2·J3은 S3.5/S6 착수가 결정될 때까지 보류해도 된다.

### 6.3 수명 규약 — 비평이 비운 자리

- `JobCounter`는 호출자 소유 값이며 **`Wait(counter)`가 반환하기 전에 파괴돼서는 안 된다.** 프레임 스코프 스택 객체로 두는 경우(AnimationJob 패턴) 조기 반환·예외·씬 전환 경로마다 `Wait`가 반드시 도달함을 J1 exit 게이트에 단정으로 넣는다. 도달하지 않는 경로가 있으면 디버그 빌드에서 소멸자가 `pending != 0`을 검출해 abort한다(정책을 assert로만 적지 말 것 — Release에서도 도는 검출 축을 둔다).
- `JobHandle`은 공유 상태(`shared_ptr` 또는 풀 소유 슬롯)를 가리키는 값 타입이며, 소유자가 핸들을 버려도 잡은 완료까지 실행되고 결과는 버려진다. 프레임 경계 드레인이 필요한 소비자(AI·자산 로드)는 핸들을 프레임 경계까지 보관해 `Wait`한다 — 이것이 J1b가 강제하는 "fire-and-forget 금지"의 실체다.
- 풀 `Shutdown`은 미완 `JobCounter`가 있으면 대기 후 종료한다(현재 `SceneManager::Shutdown`(`:532`) 경로에 단정 추가).

## 7. 경계 규칙

1. **렌더 병렬 기록 풀은 흡수하지 않는다.** `RHIParallelCommandPool` → `DX12CommandListPool` / `VulkanCommandBufferPool`은 **두 백엔드 모두** 지속 워커 3 + 호출 스레드, 워커 인덱스=자원 슬롯, 연속 블록 배분, fork-join-only 계약이다. RG4/RG8이 소유한다.
2. **PhysX 디스패처 교체·PhysX→Jolt 전환은 이 트랙이 결정하지 않는다.** PhysicsRedesignPlan §2.3 X0 스파이크 소유, 미결.
3. **AI/트랜스폼 레이스 수정은 T0가 소유한다.** JobSystem은 `SubmitDrainable` API만 제공한다.
4. **워커 콜백 안에서 `ClrHost::*`/`Native.*` 직접 호출 금지.** 관리 콜백은 `Queue*`/`Flush*` 큐잉만, Flush는 GT 전용. J1b 정적 검사로 강제하며 PHASE 24 완성을 기다리지 않는다.
5. **프로파일러 `BeginEvent`/`PROFILE_CPU_SCOPE`는 PHASE 14 P2 완료 전 워커에 넣지 않는다.** 등록/은퇴만.
6. **`CoroutineManager`·`TweenManager`는 병렬화 대상이 아니다.**
7. **외부 잡 라이브러리는 이번 트랙 범위 밖이다.** 재검토는 실제 소스를 fetch해 원문 대조하는 S0 스파이크 이후에만.
8. **새 API는 Utility_Framework에 두고 상위 계층 타입을 include하지 않는다.** `JobHandle`도 같은 계층 — DataSystem(RenderEngine)과 PSO(RHI)가 역방향 결합 없이 공유한다.
9. **PHASE 15 S1/S2(`jthread`·`counting_semaphore`)와 J1은 같은 파일을 만진다.** 착수 순서를 한쪽으로 고정한다(J1 먼저를 권고 — S1/S2는 타입 교체라 J1 위에 얹기 쉽다).

## 8. 먼저 재야 할 것 — 이 조사의 측정 공백

이 공수는 §6 합계에 **포함되지 않았다.**

| # | 측정 | 왜 | 선행 대상 | 추정 공수 |
|---|---|---|---|---|
| M1 | HEAD 기준 x64-Release 재빌드 — **exit code로만 판정**(mtime 금지) — 후 기존 회귀 세트 재통과 | `Bin/x64-Release/Editor/CreatorEditor.exe`가 08:02 빌드, HEAD가 16:58로 낡음. Debug는 25배 느리고 방향까지 뒤집힌다 | 모든 A/B | 0.5 |
| M2 | 씬당 규모 진단 커맨드 `scene.stats` 신설 — Foliage 컴포넌트·인스턴스, Animator, Decal, UI 요소, Sound 개수 | J3의 `minCountForParallel`과 J8a 착수 여부가 이 수치에 전적으로 좌우. 저장소에 씬 자산이 없어 grep 불가 | J3·J8a | 1 |
| M3 | 물리 창(`simulate`~`fetchResults`) 길이 계측 | T1(b) 채택 여부. P2 전에는 GT 스코프로만(호출 스레드가 GT) | T1 | 0.5 |
| M4 | `profile.selftest`를 run-all에 격리 구간으로 배선 | 게이트가 있어도 도는 세트에 없으면 없는 것과 같다. 라이브 캡처 교란 자인(§3.2) | J4 | 0.5 |
| M5 | ~~ETW 컨텍스트 스위치 추적으로 우선순위 역전·과다구독 실측~~ → **§10.5로 일부 대체**(처리량·지연 간접 지표). ETW 직접 추적은 여전히 미실시 | J5·J10의 이득 주장이 산수뿐이었다 | J5·J10 | 0.5 잔여 |
| M6 | CoreCLR 스레드 인구(GC·ThreadPool·finalizer) 집계 — `ScriptCore.runtimeconfig.json`에 GC 관련 키 0개라 기본값(Workstation, Concurrent) 의존 | §1.1 인구표가 관리 측을 빠뜨림 | J5 | 0.5 |

CLI 관측 표면은 `profile.stats`(프로파일러 자체 비용·용량)뿐이다. 스코프별 프레임 분해를 덤프하는 명령이 없다.

## 9. 조사가 못 본 것

- **CoreCLR 스레드 인구**를 어느 관점도 세지 않았다. `Bin/x64-Release/Managed/ScriptCore.runtimeconfig.json`의 `configProperties`에 GC·ThreadPool 키가 없어 기본값에 의존한다. 과다구독 계산이 불완전하다(M6).
- **Vulkan 비대칭 논거는 틀렸다.** 검증 단계의 정정문과 비평이 "Vulkan에는 지속 워커 풀이 없다"고 했으나, `VulkanCommandBufferPool::Initialize`가 `workerCount-1`개의 지속 `WorkerLoop` 스레드를 만들고(`VulkanCommandBufferPool.cpp:61-63`) `EnhancedSceneRenderer.cpp:443`이 4로 초기화한다. 두 백엔드는 대칭이며 경계 규칙 1은 둘 다에 적용된다.
- **`JobCounter`/`JobHandle` 수명 규약**이 설계안에 없었다 — §6.3으로 보강했으나 구현 전 재검토 대상.
- **`EnqueueIndexed`의 실제 비용** — `TaskType` 고정 때문에 "오버로드 추가"로 끝나지 않는다. J2를 1.5일로 올렸다.
- **측정 선행 공수**(§8, 약 4일)가 17.5일에 합산돼 있지 않다.
- **Player/Shipping 빌드의 스레드 토폴로지**를 보지 않았다. 조사 전체가 에디터 프로세스 범위다. Shipping 구성 자체가 부재라는 기존 정찰(PHASE 14, 9-3)과 겹친다.
- **대조군(C)이 설계 단계에서 자리표시자로 돌아왔다.** §5.1의 재논증은 심판을 거치지 않은 단독 에이전트 결과이며, 그 결과가 찬성안의 선행 사슬 과잉 결합(J8b·J10) 둘과 줄 번호 오류 셋(`AssetJob` 생성자 위치, Exit setter 줄, Foliage 분할 수)을 잡아냈다 — 첫 실행에서 대조군이 제대로 돌았다면 심판 단계에서 잡혔을 것들이다.
- **`ThreadPool` permit 유실 의심**(`PPLContainerMigrationAnalysis.md:337-339`)은 레이스 재현 없이 코드 구조만 확인했다(◻미검증). J0에서 가른다.
- **검증 2건이 스키마 오류로 탈락**했다(thread-census·frame-critical-path 각 1건). 해당 발견은 문서에 싣지 않았다.
- **씬 로드 중 텍스처 GPU 업로드가 어느 스레드에서 일어나는지**, `scene.switch`(GT 블로킹)와 `LoadSceneAsync`(백그라운드) 중 실제 게임 시작 시나리오가 어느 쪽을 쓰는지 미확인.
- **`InvokeClipEvents`의 `m_clipOverrides`** 워커/에디터 동시 편집 경합 가능성 — 에디터 UI 스레드 모델을 더 봐야 확정.
- **FMOD 채널별 동시 호출 스레드 안전성**, **SoundManager 로더 종료 플래그**(미검증 결함) — 벤더 문서·실행 확인 필요.
- **`m_transformStore`에 물리 결과를 쓰는 `ApplyWorldWriteBatch`**(`PhysicsManager.cpp:945`)와 `PublishAnimatorPose`의 1프레임 지연 가능성(미검증) — 본에 부착된 비-Socket 자식이 있으면 확인 필요.

## 10. 추가 실측 — `Core.ThreadPool` 독립 벤치 (2026-09-07)

문서 최초 작성 뒤, 엔진을 건드리지 않고 `Core.ThreadPool.h`·`Core.Thread.h`·`Core.CountingSemaphore.h`를 **그대로 포함하는** 독립 벤치를 MSVC `/O2`로 빌드해 돌렸다. 기기 8논리/4물리. 이 절이 §2·§3·§6·§8의 "미측정" 항목 일부를 대체한다.

재현 소스는 세션 스크래치패드에 있다(`poolbench.cpp`·`poolbench2.cpp`·`poolbench3.cpp`·`poolbench4.cpp`·`FixedThreadPool.h`). 임시 디렉터리라 유실될 수 있으므로, §10.1을 회귀로 고정하려면 J-1 슬라이스에서 저장소 안(`Tools/` 또는 자가 검증 커맨드)으로 옮겨야 한다. 빌드:

```bash
cl /O2 /EHsc /std:c++20 /MD /DNOMINMAX /I Engine/Utility_Framework poolbench3.cpp
```

### 10.1 `NotifyAllAndWait`은 배리어가 아니다 — 1순위 결함

풀과 무관한 독립 완료 카운터를 태스크마다 증가시키고, 배리어가 반환한 직후 그 카운터를 읽었다. 배리어가 정직하면 항상 N이어야 한다.

| 배치 간 유휴 간격 | 시행 | 잡이 남았는데 반환 |
|---|---|---|
| 0 ms(연속) | 3,000 | 173 (5.77%) |
| 0.5 ms | 3,000 | 63 (2.10%) |
| 2 ms | 600 | 17 (2.83%) |
| 8 ms(프레임 페이싱) | 600 | 15 (2.50%) |

배치당 태스크 N=1이면 0%, N≥8이면 ~5%다(20,000 시행). **간격을 벌려도 사라지지 않는다** — 배치 사이의 문제가 아니라 배치 *안*의 문제이기 때문이다.

**원인.** 카운터와 이벤트가 반대 순서로 갱신된다.
- 워커는 `m_taskCounts.fetch_sub`로 0을 만든 **뒤에** `SetEvent`를 부른다(`Core.ThreadPool.h:125-129`).
- 생산자는 `fetch_add`로 0→1을 관측한 **뒤에** `ResetEvent`를 부른다(`:68-72`).
- 그 사이가 벌어지면 생산자의 `ResetEvent`를 직전 배치의 뒤늦은 `SetEvent`가 덮는다. 이후 잡이 남아 있어도 이벤트는 신호 상태이고, `WaitForSingleObject`(`:85`)는 즉시 반환한다.
- 워커 8이 생산자 1보다 빠르게 큐를 비우므로(§10.2) 한 배치를 넣는 동안 카운터가 0을 여러 번 스쳐 창이 반복해서 열린다.

**무엇이 무너지나.** `AnimationJob`의 안전 근거 둘이 정확히 이 불변식 위에 서 있다.
- `AnimationJob.cpp:167-170`: "worker는 Animator 소유 pose/socket staging만 쓴다 … 모든 job이 끝난 이 barrier 뒤에서 메인 스레드가 직렬 commit한다." → 배리어가 거짓이면 `PublishAnimatorPose`가 워커가 쓰는 중인 포즈를 읽는다. 배리어 직후라 겹칠 확률이 가장 높은 지점이다.
- `AnimationJob.cpp:117-119`: raw `Animator*` 캡처의 근거가 "`NotifyAllAndWait`까지 반드시 도달해 잡을 프레임 안에서 완결한다". → 창은 좁지만 같은 프레임 `EndFramePass`의 파괴와 겹치면 UAF다.
- `DataSystem::LoadAssetBundle`(`DataSystem.cpp:1702`)도 같은 배리어를 쓴다 → 번들이 덜 로드된 채 반환할 수 있다.

엔진에서 재현하지는 않았다(Release exe가 낡음). 코드 경로상의 귀결이며, 60fps·프레임당 배리어 1회·2.5%면 **초당 1~2프레임**꼴이다.

### 10.2 태스크당 비용은 4.3µs이고 전액 생산자가 낸다

빈 태스크 4,096개(완료 검증 포함, 200회 평균):

| 구간 | 태스크당 |
|---|---|
| 생산자 측 `Enqueue`만 | 4,291 ns |
| `Enqueue` + 배리어 왕복 전체 | 4,295 ns |

둘이 같다 = **워커는 놀고 생산자가 병목이다.** `Enqueue`마다 `ResetEvent`(조건부)와 `CountingSemaphore::release`의 `ReleaseSemaphore`(워커가 파킹돼 있으면 매번) 두 커널 호출이 붙기 때문이다. 게임 스레드가 애니메이터 100개를 뿌리면 분배에만 ~430µs를 쓴다.

### 10.3 그래서 실제 병렬 이득이 얼마나 깎이나

10µs짜리 작업 N개, 완료를 독립 카운터로 검증한 시간(직렬 대비 배속):

| N | 직렬 | 태스크당 1개 | 청크(=워커 수) |
|---|---|---|---|
| 8 | 0.084 ms | 1.17× | 1.25× |
| 64 | 0.676 ms | 2.30× | 2.88× |
| 256 | 2.855 ms | 3.12× | 4.04× |
| 1024 | 11.395 ms | 2.00× | **4.66×** |

4물리코어에서 이론 상한 4×. **청크 분할은 상한에 닿고, 태스크당 1개 방식은 절반을 버린다.** `AnimationJob`은 애니메이터당 1개(`AnimationJob.cpp:126`)라 정확히 버리는 쪽이다 — PHASE 13 S6의 청크 분할이 여기에 직접 대응한다.

### 10.4 고치면 되는가 — 최소 수정 두 단계 실측

같은 워크로드를 세 풀에 태웠다. V1·V2는 스크래치패드 프로토타입이다(엔진 미변경).

| 풀 | N=64 배치 | N=1024 배치 | 1024 배속 | 거짓 반환/3,000 |
|---|---|---|---|---|
| 원본(Event 배리어) | 0.313 ms | 3.718 ms | 2.97× | **77** |
| V1 = 원본 + 카운터·`condition_variable` 배리어 | 0.286 ms | 2.827 ms | 3.90× | **0** |
| V2 = V1 + 배치당 1회 웨이크 + 대기 스레드가 잡을 함께 실행 | **0.143 ms** | **2.179 ms** | **5.07×** | **0** |

- **V1(약 20줄)**: 거짓 반환이 0이 되고 *동시에 더 빨라진다*(1024 배치 −24%). 정확성과 성능이 상충하지 않는다 — 배치당 커널 호출이 사라지기 때문이다.
- **V2**: N=64에서 **2.2배**. 세 가지를 함께 바꿨다 — 배치당 웨이크 1회, 대기 스레드의 작업 참여(`DX12CommandListPool`이 이미 쓰는 "워커 0 = 호출 스레드" 계약, `DX12CommandListPool.cpp:129`), 워커 수 `hw-1`·NORMAL. 셋을 분리 측정하지는 않았다.

### 10.5 우선순위와 워커 수

NORMAL 우선순위 "렌더 스레드"(1ms 작업 300회)를 돌리면서 풀에 64태스크 배치를 계속 던졌다. **양쪽을 다 쟀다** — 이웃 지연만 재면 "풀이 일을 덜 했다"와 구별되지 않는다.

| 워커 | 우선순위 | 렌더 반복 평균 | 렌더 최대 | 풀 배치/s |
|---|---|---|---|---|
| 8 | HIGHEST(현재 기본값) | 2.105 ms | 8.731 ms | 2,769 |
| 8 | NORMAL | 2.160 ms | 4.951 ms | **4,675** |
| 4 | NORMAL | 1.390 ms | 18.188 ms | 4,274 |
| 3 | NORMAL | **1.036 ms** | 4.123 ms | 3,522 |

- **HIGHEST는 두 축 모두 손해다**: 같은 8워커에서 NORMAL 대비 풀 처리량 −41%, 이웃 스레드 최대 지연 1.8배. 기본값 `THREAD_PRIORITY_HIGHEST`(`Core.ThreadPool.h:20`)에 근거가 없다.
- **워커 8→3이면** 이웃 스레드가 사실상 무간섭(0.952ms 공칭 대비 1.036ms)이 되고 풀 처리량은 최고치의 75%를 지킨다. 풀마다 `GetActiveProcessorCount()`를 쓰는 기본값(`:23`)이 문제이고, 엔진은 그런 풀을 **둘**(`WorkerPool` 8 + `AnimationJob` 8) 세운다.
- 4워커 행의 최대 18.188ms는 단발 스케줄링 잡음으로 보이며 평균만 취했다.

### 10.6 벤치가 답하지 못한 것

- 엔진 안에서 재현하지 않았다. 프레임 페이싱·실제 애니메이터 수·실제 태스크 길이가 다르면 §10.1의 비율은 달라진다.
- §10.4의 V2는 세 변경을 묶은 값이라 기여도를 못 가른다. 프로토타입이지 설계안이 아니다(다중 생산자 미지원).
- `try_pop` 실패로 permit이 유실되는지(`PPLContainerMigrationAnalysis.md:337-339`)는 여전히 관측하지 못했다 — 모든 배치가 완료됐으므로 이 워크로드에서는 발생하지 않았다.
- ETW 컨텍스트 스위치 추적은 하지 않았다. §10.5는 처리량·지연이라는 간접 지표다.

## 11. 배리어 수정 적용과 외부 풀 4종 비교 (2026-09-07)

### 11.1 적용한 것

`Engine/Utility_Framework/Core.ThreadPool.h`에 **J-1(배리어 정직성)만** 적용했다. 파일은 CP949·CRLF이므로 인코딩을 보존하는 스크립트로 편집했고, 한글 주석 11줄 왕복을 확인했다.

| 바뀐 것 | 전 | 후 |
|---|---|---|
| 완료 통지 | manual-reset `HANDLE m_waitEvent` + `SetEvent`/`ResetEvent` | `std::mutex m_doneMutex` + `std::condition_variable m_doneCondition` |
| `Enqueue` | `fetch_add` 후 0→1 전이면 `ResetEvent` | `fetch_add`만 |
| `NotifyAllAndWait` | `WaitForSingleObject(m_waitEvent, INFINITE)` | 술어 `m_taskCounts == 0`으로 `wait` |
| 워커 완료 | `fetch_sub(release)` 후 `SetEvent` | `fetch_sub(acq_rel)` 후 **락을 잡고** `notify_all` |

공개 API·생성자 시그니처·워커 수·우선순위 기본값은 그대로 두었다. 배치당 웨이크 1회, 호출자 작업 참여, `JobCounter`는 **적용하지 않았다**.

**검증.** 같은 하네스를 수정된 헤더로 다시 돌렸다.

| 시나리오 | 수정 전 | 수정 후 |
|---|---|---|
| 배치 격리, N=8/64/256 각 20,000시행 | 5.29% / 5.12% / 4.71% | **0 / 0 / 0** |
| 연속 배치(AnimationJob 패턴), N=8/64/256 각 3,000시행 | 4.47% / 5.87% / 5.83% | **0 / 0 / 0** |

### 11.2 외부 풀 4종을 실제로 받아 같은 하네스에 태웠다

이번에는 저장소 밖 주장을 쓰지 않았다. 네 라이브러리를 직접 clone해 빌드했다.

| 라이브러리 | 커밋 | 형태 | 비고 |
|---|---|---|---|
| BS::thread_pool 5.1 | `bd4533f` | 단일 헤더 | 의존 0 |
| dp::thread_pool | `0de593c` | 헤더 2개 | C++20 concepts·`std::jthread` |
| Taskflow 4.1.0 | `83f90a2` | 헤더 다수 | C++20 |
| Dispenso | `68b6dc3` | **.cpp 16개 + moodycamel concurrentqueue** | 정적 라이브러리 빌드 필요, `Synchronization.lib`·`Winmm.lib` 링크 |

측정은 모두 풀과 무관한 독립 완료 카운터로 검증했다. 워커 8, 항목당 작업 10.2µs, 직렬 기준 N=64 0.696ms · N=256 3.086ms · N=1024 10.537ms.

**태스크당 1개 제출 후 배리어 대기 (AnimationJob의 패턴). 직렬 대비 배속.**

| 풀 | N=64 | N=256 | N=1024 |
|---|---|---|---|
| CreatorEngine, HIGHEST(현재 기본값) | 2.47 | 3.26 | 2.52 |
| **CreatorEngine, NORMAL** | **4.52** | **6.40** | **5.80** |
| BS::thread_pool 5.1 | 4.34 | 6.28 | 5.90 |
| dp::thread_pool | 4.15 | 7.15 | 6.15 |
| Taskflow 4.1.0 | 3.68 | 3.50 | 4.11 |
| Dispenso | 1.61 | 3.01 | 3.74 |

**청크 분할(태스크 수 = 워커 수).**

| 풀 | N=64 | N=256 | N=1024 |
|---|---|---|---|
| CreatorEngine, HIGHEST | 3.40 | 4.43 | 4.57 |
| CreatorEngine, NORMAL | 3.43 | 4.84 | 4.43 |
| BS::thread_pool 5.1 | 3.64 | 5.23 | 4.60 |
| dp::thread_pool | 4.08 | 6.62 | 4.63 |
| Taskflow 4.1.0 | 3.23 | 3.95 | 4.24 |
| Dispenso | 2.91 | 3.81 | 4.03 |

**라이브러리 자체 parallel_for.** 엔진과 dp에는 없다.

| 풀 | N=64 | N=256 | N=1024 |
|---|---|---|---|
| BS `detach_loop` | 3.60 | **5.36** | **4.65** |
| Taskflow `for_each_index` | 3.36 | 4.89 | 4.28 |
| Dispenso `parallel_for` | 3.37 | 4.38 | 4.20 |

**빈 태스크 4,096개 왕복(순수 디스패치 비용)과 대기의 정직성.**

| 풀 | 태스크당 | 거짓 반환 |
|---|---|---|
| CreatorEngine, HIGHEST | 3,507 ns | 0 |
| CreatorEngine, NORMAL | 3,194 ns | 0 |
| BS::thread_pool 5.1 | 2,503 ns | 0 |
| dp::thread_pool | 1,821 ns | 0 |
| **Taskflow 4.1.0** | **335 ns** | 0 |
| Dispenso | 660 ns | 0 |

### 11.3 읽는 법

- **거짓 반환은 모든 풀에서 0이다.** 수정된 엔진 포함. §10.1의 결함은 엔진 고유였고 이제 없다.
- **가장 큰 손해 요인은 라이브러리 선택이 아니라 우선순위 기본값이다.** 같은 엔진 풀에서 `THREAD_PRIORITY_HIGHEST`를 NORMAL로 바꾸는 것만으로 per-item이 2.47→4.52, 3.26→6.40, 2.52→5.80이다. 어떤 외부 풀로 교체해도 이보다 크게 얻지 못한다.
- **수정 + NORMAL이면 엔진 풀은 BS·dp와 같은 급이다.** 셋의 차이는 실행 간 변동(약 ±20%, §11.4) 안이다. 외부 라이브러리 도입을 성능으로 정당화할 근거가 없다. §5의 각도 A 채택이 이번엔 추측이 아니라 실측으로 뒷받침된다.
- **Taskflow·Dispenso는 순수 디스패치에서 압도적이고(각각 10배·5배 저렴) fork-join 배속에서는 오히려 낮다.** 워크스틸링 그래프 실행기라 제출이 싸지만, 이 저장소의 워크로드는 의존 그래프가 아니라 고정 배치 fork-join이라 그 강점이 발현되지 않는다. 태스크가 마이크로초 이하로 잘게 쪼개지고 단계 간 의존이 생기는 날에만 다시 볼 값이다.
- **parallel_for가 필요하면 BS가 가장 빠르다.** 다만 J3(`ParallelFor` 자체 구현)과 비교하려면 J3이 먼저 있어야 한다.

### 11.4 이 비교의 한계

- **Dispenso가 정적 초기화에서 `timeBeginPeriod(1)`을 부른다.** 프로세스 전역 타이머 해상도라 같은 실행의 모든 행에 동일하게 적용된다. 표 내부 비교는 유효하지만 §10의 다른 실행과 절대값을 비교하면 안 된다. 별도 대조 실행에서 이 설정만으로 엔진 per-item N=256이 3.72→5.32배로 바뀌었고, **per-item과 chunked의 우열까지 뒤집혔다**.
- **배속 4배가 상한이 아니다.** 벤치의 작업이 부동소수 의존 사슬이라 SMT가 잘 먹는다. 4물리코어에서 5.9배가 관측된다. §10.3에 적은 "이론 상한 4배"는 틀렸다.
- **실행 간 변동이 약 ±20%다.** 같은 엔진 풀·같은 설정에서 per-item N=256이 6.40배와 5.32배로 갈렸다. 표에서 0.5배 이내 차이는 순위로 읽지 말 것.
- **워크로드가 하나뿐이다.** 균일한 CPU 바운드 10µs 작업이다. 불균형 작업·IO 대기·중첩 병렬·의존 그래프는 재지 않았고, 그 축들이 Taskflow·Dispenso의 설계 목적이다.
- 스레드 수를 8로 고정했다. §10.5는 3~4가 더 낫다고 시사하므로 이 표는 모든 풀에 불리한 설정에서 잰 값이다.
- Dispenso의 `TaskSet`은 배치마다 생성했고 Taskflow의 `Taskflow` 객체는 재사용했다. 각 라이브러리의 관용 사용법을 따랐으나 완전한 동일 조건은 아니다.

## 부록 A. 반박된 발견

| 관점 | 발견 | 왜 반박됐나 | 정정 |
|---|---|---|---|
| thread-census F2 | "`AnimationSchedulerPlan.md:89`가 부분 대기 불가를 전용 풀의 이유로 명시" | 코드 인용은 정확하나 계획서는 전용 풀을 E9 비효율로 적고 S6에서 실측 후 재검토하라고 할 뿐 | 부분 대기 불가는 코드 사실. 계획서는 이유로 명시하지 않음 |
| thread-census F4 | "고정 파이프라인 스레드 7개" | 백엔드 조건성을 숨김 — 커맨드 워커 3은 DX12 어댑터 초기화 경로 | 공통 4 + DX12 3 = 7. **정정문의 "Vulkan에는 지속 풀이 없다"는 부분은 이 문서에서 다시 정정** — Vulkan도 3(§9) |
| frame-critical-path F3 | "`AnimationJob`이 게임로직 구간 중 유일한 병렬화 지점" | Foliage `std::async`·AI `std::async`도 같은 구간 | "유일"이 아니라 "하나" |
| frame-critical-path F9 | "PrePhysics/PostPhysics 모두 GC 정지 근거 주석" | `RuntimeFrame.cpp:27`은 PostPhysics에만, PrePhysics 주석(:8-13)은 순서 이유만 | PostPhysics만 근거 명시 |
| transform F2 | "sparse가 legacy 대비 0.235~0.382배, random 1%에서 9.5µs vs 3,353µs" | 표 열을 잘못 짝지음 — 9.5µs는 sparse random 1%, 3,353µs는 legacy 100% 이동, 0.235는 100%끼리의 비율 | 표(`:1135-1137`)에 legacy random 1% 열이 없어 그 배수는 계산 불가 |
| render F10 | "공용 풀을 RG4가 빌리면 RT join이 무관 태스크를 기다린다" | `Core.ThreadPool.h:20` 인용이 두 줄(20·23)을 섞은 재구성이고, RG4가 공용 풀을 빌린다는 전제가 코드에 없음 | 전역 카운터 사실은 성립(`:68, 80, 125, 144`). 전제는 추론 |
| managed-ai-boundary F11 | "fire-and-forget 잡은 매 프레임 명시적 블로킹 드레인이 보장돼야 한다"의 근거로 `Scene.cpp:2559` | 그 줄은 비블로킹 조기 회수이고, 진짜 블로킹 드레인은 `EndFramePass`(`:2753-2757`)에 이미 있다 | 규칙(1)은 이미 실장. 규칙(2)(3)은 별도 |
| external-survey F1/F2/F4/F5 | enkiTS DAG·pinned task, Wicked 3풀 무-DAG, Jolt 5함수 | 파일이 저장소·로컬 어디에도 없어 원문 대조 불가. F5는 `PhysicsRedesignPlan.md:341`("QueueJob·QueueJobs·GetMaxConcurrency 정도")와도 목록이 다름 | **여전히 미해소.** §11은 다른 네 라이브러리(Dispenso·Taskflow·BS·dp)를 실제로 clone해 빌드·측정한 것이고, enkiTS·Wicked·Jolt는 이번에도 받지 않았다 |

## 부록 B. 방법과 한계

- **정독 12관점**: 스레드 인구 · 프레임 임계 경로 · 트랜스폼 · 애니메이션 · 물리 · 렌더 · 관리/AI 경계 · 자산 IO · 기타 시스템 · 계획 교차 · 외부 조사 · 관측/게이트. 관점당 발견 10~14건, 관련도 high/medium/low 태깅.
- **적대 검증**: high 관련도 발견마다 검증자 1명이 원문 대조(file:line을 다시 열어 인용이 그 줄에 있는가)와 최신성(그 줄이 최근 커밋으로 바뀌었는가) 두 렌즈를 합쳐 반박을 시도. 줄 번호가 몇 줄 어긋나도 원문이 있으면 반박하지 않고 실제 줄을 기록. medium은 미검증으로 표기해 실었다(◻미검증). 2건은 구조화 출력 스키마 실패로 탈락.
- **설계 3각도 → 심판 3인 → 종합 → 비평**. 각도 C("JobSystem 없이")는 자리표시자로 돌아와 채점 불가였고, §5.1에서 단독 에이전트(읽기 전용, 도구 호출 55회, 약 18분)로 재논증했다(심판 미경유). 그 결과로 §요약·§6.2의 착수 순서와 J8b·J10 선행을 고쳤다.
- **첫 실행은 세션 요청 한도(5시간)에 걸려 검증 148건이 실패**했다. 한도 해제 후 검증 부하를 줄여(두 렌즈 → 한 검증자, high만) 완료된 정독 9건을 캐시로 재개했다. 총 에이전트 106, 완료 104.
- **엔진 라이브 실측 0.** Release exe가 HEAD보다 낡아 하지 않았다. §1~§9의 "이득"은 모두 미측정이다. 예외는 **§10**으로, 엔진 헤더를 그대로 포함한 독립 벤치(MSVC `/O2`, 8논리/4물리)를 돌린 실측이며 엔진 코드는 건드리지 않았다.
- **코드 변경 0.** 이 문서만 산출했다. 대시보드·계획서는 수정하지 않았다(§3 D7).
