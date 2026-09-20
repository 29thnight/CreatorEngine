# 태스크 스케줄러 단일화 — enkiTS 공용 실행 기반 (PHASE 13 S0.5)

2026-09-15 수립 · 2026-09-20 결정 개정. **통일을 먼저 완료하고 최적화는 이후에 한다.**
현재 설계 정본은 [JobSchedulerDesign](../design/JobSchedulerDesign.md),
PHASE 13 진행 상태는 [AnimationSchedulerPlan](AnimationSchedulerPlan.md)이다.

## 0. 2026-09-20 현행 결정

- 엔진이 지속적인 enkiTS 워커를 소유한다. 공용 `thread_pool` 위의 `job_scheduler`가
  `job_group` 제출·`job_handle` 완료·의존 작업·범위 분할을 제공한다.
- 공용 유틸리티는 STL 표기, 애니메이션 도메인 객체는 C# 표기를 따른다.
  이전 `WorkerPool`/`WorkerPools` 별칭은 남기지 않는다.
- AnimationJob 전용 풀을 S6까지 유지한다는 결정을 철회한다. 현재 평가 단위를
  유지한 채 공용 기반으로 먼저 이관하고, S6에서는 청크화·소유 계층을 정리한다.
- S0.5에 DataSystem·썸네일·AnimationJob·Foliage·AI를 이관한다. SceneManager에서
  공용 풀의 수명을 떼어 Editor/Player 공통 EngineBootstrap으로 옮긴다.
- 별도 풀 비교·동시 경합 우선순위 벤치마크는 이번 완료 조건에서 제외한다.
  새 자체 work stealing 구현도 추가하지 않는다. enkiTS의 분배 정책을 사용한다.
- DX12 PSO 비동기 컴파일(§10)과 씬 로딩 `std::async` 2곳(§11)은 공용 스케줄러로
  이관했다. 씬 객체 구성은 게임 스레드의 씬 구조 변경 경계에서 완료한다.
  DX12/Vulkan command recording도 구간별 자원 수명을 유지하며 이관했다(§12).
  Render/Presentation/GPU 제출 등 장기 전용 루프는 유지한다.
- 워커 프로파일러 마커는 [ProfilingCapturePlan](ProfilingCapturePlan.md) §0.5.5의
  안전한 전달 경계 이후에 추가한다.

**§1~§8은 이 결정 이전의 조사·측정·중간 구현 기록이다.** 우선순위 실측 선행,
Animation 전용 풀 유지, SceneManager 풀 소유, WorkerPool API 관련 서술은 현행
지시가 아니다. 공용 기반 구현·검증은 §9, PSO 이관은 §10, 씬 로딩은 §11, 명령 기록은 §12를 따른다. 과거 성능 수치를 이번 구현의
성능 향상 근거로 사용하지 않는다.
## 1. 당시 구조와 측정 (2026-09-15 · 현행 차이는 §0 우선)

### 1.1 `Core.ThreadPool` 인스턴스 — 3개 중 1개는 죽어 있다

| 인스턴스 | 스레드 | 우선순위 | 소비자 |
|---|---:|---|---|
| `AnimationJob::m_UpdateThreadPool`<br>(`SceneRuntime/AnimationJob.cpp:39`) | 8 | HIGHEST | `AnimationJob::Update` — 애니메이터 1개 = 태스크 1개, fan-out 후 `NotifyAllAndWait` |
| `WorkerPool` 싱글톤<br>(`Utility_Framework/WorkerPool.h:31`) | 8 (`GetActiveProcessorCount`) | HIGHEST | `DataSystem.cpp:1678`·`:1702` **단 한 곳** |
| `AssetLoadJob`<br>(`RenderEngine/AssetJob.cpp:3`) | 16 (선언상) | HIGHEST | **인스턴스화 0건 — 죽은 코드** |

우선순위는 전부 생성자 기본값이다. 소비자 중 인자를 넘기는 곳이 없다.

```cpp
// Core.ThreadPool.h:30
ThreadPool(int numThreads = 0, DWORD_PTR affinityMask = 0,
           int priority = THREAD_PRIORITY_HIGHEST)
```

즉 **논리 프로세서 8개에 HIGHEST 워커 16개**가 뜬다(`AssetLoadJob`이 죽어
있으므로 24가 아니다).

### 1.2 스레드 풀이 사실 3벌이다

`Core.ThreadPool` 외에 같은 구조가 둘 더 있다. 둘 다 `vector<std::thread>` +
`mutex` + `condition_variable` 2개 + job 함수 포인터로, fan-out 후 배리어를
자기 손으로 다시 구현한 것이다.

| 위치 | 워커 수 | 용도 |
|---|---|---|
| `RHI/DX12/DX12CommandListPool.h:119` | `workerCount - 1` | 병렬 커맨드 리스트 기록 |
| `RHI/Vulkan/VulkanCommandBufferPool.h:72` | 동일 | 동일 |

### 1.3 `std::async` 5곳 — 성격이 갈린다

| 위치 | 하는 일 | 지속 시간 | 값 반환 |
|---|---|---|---|
| `SceneRuntime/FoliageComponent.cpp:396` | range 분할 병렬 처리 | 짧음 | 없음 |
| `SceneRuntime/Scene.cpp:2784` | AI 갱신, `m_AIFuture`로 폴링 | 프레임 단위 | 없음(`future<void>`) |
| `RHI/DX12/DX12PSOManager.cpp:719` | PSO/셰이더 컴파일 | **수십~수백 ms** | `shared_future<ComPtr<ID3D12PipelineState>>` |
| `SceneRuntime/SceneManager.cpp:950` | `LoadSceneAsync` — **공개 API** | **수백 ms~초** | `future<Scene*>` |
| `SceneRuntime/SceneManager.cpp:1060` | 씬 전환 로드 | 동일 | 동일 |

### 1.4 `std::thread` — 51건 중 실제 생성은 18곳

나머지 33건은 `std::thread::id`(12)와 `std::this_thread::`(23)다.

실제 생성 18곳은 **전부 장기 블로킹 전용**이다:

| 분류 | 위치 |
|---|---|
| 소켓 블로킹 | `CommandService.cpp:133`(accept), `:212`(클라이언트 워커) |
| stdin 블로킹 | `ConsoleCommandSystem.cpp:524` |
| 파일 감시 | `EditorDirectoryWatcher.cpp:255` |
| 표시 스레드 | `EditorMain.cpp:332`, `PlayerMain.cpp:335` |
| GPU 제출 | `RHISubmissionThread.cpp:379` |
| 기타 전용 | `SoundManager.cpp:20`, `DataSystem.h:323`, `EnhancedSceneRenderer.cpp:4088`, `EditorModelPlacement.cpp:98` |
| 테스트 자극 | `ProfilerSelfTest.cpp`(2), `EnhancedSceneRendererSelfTest.cpp`(3), `VulkanSelfTest.cpp` |
| 주석 처리 | `Physx.h:305` |

---

## 2. 측정 — 무엇이 실제로 서 있는가

`scratchpad/poolprobe/`의 독립 프로브 3차. 헤더를 그대로 포함해 `cl /O2 /MD
/DNDEBUG`로 빌드했고, **풀과 무관한 독립 완료 카운터**로 판정했다(9-6 조사와
같은 방법). 워크로드는 64 태스크/라운드, 매 16번째가 200µs 스핀, 라운드마다
fork-join 배리어.

### 2.1 확인된 것 — enkiTS가 p50 32% 빠르다

정방향 3회 + 역방향 3회, 워밍업 200라운드, 측정 2000라운드.

| 구성 | p50 (µs) × 6회 | 중앙 |
|---|---|---:|
| engine HIGHEST | 408.9 / 411.8 / 382.9 / 403.2 / 370.0 / 467.6 | **~406** |
| engine NORMAL | 335.2 / 335.2 / 326.0 / 337.6 / 343.1 / 350.0 | **~336** |
| enkiTS range min=1 | 229.1 / 227.5 / 230.0 / 225.4 / 230.8 / 230.8 | **~229** |
| enkiTS range min=4 | 229.4 / 229.3 / 227.2 / 223.0 / 228.8 / 228.4 | **~228** |
| enkiTS 64 tasksets | 229.0 / 225.7 / 227.4 / 263.9 / 225.8 / 226.7 | **~227** |

**enkiTS 세 모드가 전부 같은 값**이라는 점이 중요하다. `64 tasksets`는 엔진
풀과 1:1 대응(람다 하나당 태스크 하나)인데도 227µs다 — 즉 enkiTS가 다른
분해를 허락받아 이긴 것이 아니라 **스케줄러 자체가 빠르다.**

부수 확인: **NORMAL이 HIGHEST보다 17% 빠르다**(336 vs 406µs, 6/6 일관).
9-6 조사(`JobSystemFeasibilityAnalysis.md` §11)가 같은 결론에 닿았는데
**코드에는 반영되지 않았다.**

### 2.2 반증된 것 — 자체 풀의 배리어는 정직하다

착수 전 코드 독해로 결함 셋을 지목했으나 **6가지 자극 전부에서 발현 0**이었다.

| 지목한 결함 | 결과 |
|---|---|
| 배리어 조기 반환 | **반증** — 6만 라운드 이상에서 0건. `a6da7899` 수정이 작동한다 |
| 세마포어 토큰 복제·소실 | **미발현** — 대칭/비대칭/1토큰/오버서브스크립션 전부 0 |
| 토큰 도둑질 → 워커 기아 | **반증** — 분포 1.01~1.10x |
| `try_pop` 실패 → 영구 블록 | **자극 불가** — 정상 API로 "토큰 수 > 큐 원소 수"를 만들 수단이 없다 |

즉 **교체의 명분에 정확성은 없다.** 코드에 `try_pop` 실패 보정 분기가 없는
것은 사실이나, 그것이 발화하는 경로를 세우지 못했다.

### 2.3 측정 불가 — 꼬리 지연

같은 구성의 `max/mean`이 실행마다 이렇게 튄다:

```
engine NORMAL :  2.72x / 5.24x / 20.39x / 23.64x / 48.78x / 137.33x
enkiTS min=1  :  3.52x / 8.90x / 10.18x / 22.06x / 23.65x /  58.75x
```

순서·구성과 무관한 기계 노이즈다. **이 축은 판정에서 빼되 수는 계속
기록한다**(허용치를 늘려 통과시키지 않는다). 조용한 기계에서 다시 재기
전까지 꼬리를 근거로 쓰지 않는다.

---

## 3. 판정 — 무엇을 옮기고 무엇을 남기는가

### 3.1 옮긴다

| 대상 | 근거 |
|---|---|
| `AssetLoadJob` | 소비자 0. 옮기는 게 아니라 **삭제** |
| `WorkerPool` | 소비자 1곳. 인터페이스가 이미 백엔드 교체 모양 |
| `FoliageComponent`의 `std::async` | range 분할 = enkiTS `ITaskSet`의 본래 형태 |
| `Scene`의 AI `future<void>` | 완료 폴링뿐 → `ICompletable::GetIsComplete()` |

> `AnimationJob`은 이 목록에 **없다.** §3.3을 볼 것.

### 3.2 남긴다 — 그리고 그 이유

**장기 블로킹 스레드 18곳.** enkiTS 워커에 올리면 그 워커가 영구 점유된다.
work stealing은 블로킹된 워커를 구제하지 못한다. `IPinnedTask`도 1회 실행
단위이지 무한 루프용이 아니다.

**`SceneManager::LoadSceneAsync`.** 수백 ms~초 블로킹 + `future<Scene*>` 값
반환 + **공개 API**다. 8워커 중 하나를 씬 로드가 잡으면 프레임 잡이 1/8
느려진다. Unity가 Job System과 비동기 로딩을 분리한 이유가 같다.

**`DX12PSOManager`의 `shared_future`.** 셰이더 컴파일이 수십~수백 ms고 값을
반환한다. enkiTS에는 값 채널이 없다.

> 따라서 "`std::async`/`future` 전면 제거"는 성립하지 않는다. 5곳 중 2곳,
> `future` 관련 14건 중 절반이 남는다. 이것은 타협이 아니라 **올바른 경계**다.

### 3.3 `AnimationJob`은 S6이 가져간다 — 순서 제약

착수 전 `AnimationSchedulerPlan.md`(PHASE 13)를 대조한 결과, **같은 작업이 이미
그 계획의 S6에 있다.**

```
AnimationSchedulerPlan.md:541
| S6 | Job 배치 전환 | 청크 분할 · 전용 풀 vs WorkerPools 실측 비교 후 결정 ·
                      이벤트 큐 정식화(락프리) · AnimationScheduler로 개명·이주 |
                      태스크 수 = O(워커 수) | 2일 |
```

겹치는 것이 하나가 아니다.

- `:100` — "전역 워커 풀 `WorkerPools` … **전용 풀 은퇴 후보지 (S6에서 실측 비교)**"
- `:453` — "애니메이터 1개=태스크 1개(E9) 대신 … **워커 수에 맞춘 청크**로 분할"
- `:145`·`:245`·`:292` — "**워커별 포즈 풀** + steal-in-place"

세 번째가 특히 중요하다. 워커별 버퍼 풀은 enkiTS의 `threadnum_`(0..
`GetNumTaskThreads()-1` 보장)을 필요로 하므로, **Pose 타입과 버퍼 풀 설계가
먼저 서야 어떤 스케줄러 API가 필요한지 정해진다.**

그리고 두 번째가 결정적이다. PHASE 13은 분해 자체를 "애니메이터당 태스크"에서
"워커 수 청크"로 바꾸려 한다. **그 전에 현행 분해를 enkiTS로 옮기면 곧
버려질 모양을 이식하는 것이다.** PHASE 13은 S6 앞에 S2′~S5(Pose 값 타입 ·
태스크 레시피 · 관측 본 · Significance · 버짓)를 두고 있고 합계 16일이며,
`:551`이 "S2′ 이후는 `AnimationJob.cpp`를 크게 다시 쓴다"고 적어 두었다.

**판정: `AnimationJob` 이관은 S0.5가 아니라 S6에서 한다.** 대신 PHASE 13 S6의
비교 후보에 enkiTS를 추가한다 — 그 항목은 지금 "전용 풀 vs `WorkerPools`"
2지선다인데, S1이 끝나면 `WorkerPools`가 곧 enkiTS이므로 비교가 자동으로
성립한다. S0.5가 S6에 남기는 것은 **결정이 아니라 측정된 선택지**다.

> 초안(PHASE 26)은 이 둘을 한 페이즈에 묶으려 했다. 대조 결과 순서 제약이
> 드러나 PHASE 13의 S0.5·S6으로 갈라 편입했다(2026-09-15).

### 3.4 정확성과 스케줄러 교체의 경계

9-15 문서의 R1·R2 판정은 8월 계획에 남아 있던 legacy AnimationJob 경로를
현재 구현으로 오인했다. 9-20 소스 대조에서 해당 경로의 제거를 확인했다:
`AnimationJob`은 const ModelAssetGeneration과 본 인덱스 track 표를 읽는다.
`Skeleton::curKey`/공유 Bone 트랜스폼/map 삽입을 S2′의 남은 작업으로 세지 않는다.

이 사실이 새 스케줄러의 정확성을 증명하지는 않는다. 배리어·task 수명·완료 계수는
교체 후 별도 검증하고, 애니메이션 상태/레이어/이벤트는 정본 S0에서 검증한다.

### 3.5 보류 — DX12/Vulkan 커맨드 풀

§1.2의 두 풀(§3.5)은 구조상 통합 대상이지만, RHI 워커가 스레드 친화적 자원
(커맨드 할당자·디스크립터 힙)을 워커 인덱스로 소유할 가능성이 있다. enkiTS의
`threadnum_`은 0..`GetNumTaskThreads()-1`로 안정적이므로 매핑은 가능하나,
**소유 구조를 읽기 전에는 착수하지 않는다.** S5에서 판정한다.

---

## 4. 단계 — PHASE 13 어디에 붙는가

단계 번호는 `AnimationSchedulerPlan.md` §3을 따른다. 이 문서는 항목별 근거만 적는다.

### PHASE 13 S0.5 — 태스크 스케줄러 기반 정비 (2일, P0, 즉시 착수 가능)

**① `AssetLoadJob` 삭제 — 완료**
`317ab497`에서 파일과 vcxproj·filters 항목이 제거됐다. 새 구현 범위에 중복 산입하지 않는다.
게이트: 전체 솔루션 빌드 exit 0 (mtime이 아니라 **종료 코드로** 판정).

**② enkiTS 도입 + `WorkerPool` 백엔드 교체 — 완료(§8)**
- `vcpkg.json`에 `enkits` 추가(포트 존재 확인 완료, zlib 라이선스)
- `Enqueue`/`NotifyAllAndWait` 시그니처를 유지한 채 내부만 교체
- 소비자는 DataSystem 번들 로드와 BrowserThumbnailCache다. 공개 제출 표면은 유지하되 썸네일의 비동기 task 수명·완료·종료 경계도 검증한다.

**게이트 S0.5-G1**: `DataSystem` 경로의 완료 수를 독립 카운터로 세어
`NotifyAllAndWait` 반환 시 == 제출 수.
**변이**: 배리어 호출을 제거하면 이 단정이 붉어야 한다. 안 붉으면 게이트가 눈멀었다.

**③ 우선순위 기본값 HIGHEST → NORMAL**
§2.1이 NORMAL 우위를 6/6으로 보였고 9-6 조사도 같은 결론이었다. 한 줄이다.
단 지금 측정은 **풀 하나만 돌린 조건**이므로, 워커 16개가 동시 경합하는 자극을
추가해 확인한 뒤 바꾼다. ②로 `WorkerPool`이 enkiTS가 되면 남는 자체 풀
인스턴스는 `AnimationJob` 하나뿐이므로, 이 항목의 수명도 S6까지다.

**④ `FoliageComponent`·`Scene` AI의 `std::async` 이관**
`future<void>` 2건 제거. `Scene::m_AIFuture`는 `GetIsComplete()` 폴링으로.

**주의**: `Scene.h:428` 주석이 "`std::future` 멤버 때문에 복사 불가 + 사용자
선언 소멸자"를 기록하고 있다. 멤버 타입이 바뀌면 **그 특수 멤버 함수 상태가
달라진다** — 복사/이동 가능성이 조용히 바뀌지 않는지 확인한다.

**게이트 S0.5-G2**: `std::async` 잔존이 정확히 3건(`SceneManager` 2 +
`DX12PSOManager` 1). 0이 아니라 3이다 — §3.2가 그 셋을 남기기로 했으므로
**0을 기대하는 단정은 틀린 단정**이다.

### PHASE 13 S6 — Job 배치 전환 (2일, P1, S3.5 의존)

`AnimationJob` 이관은 여기서 한다. 이 문서가 S6에 남기는 것은 **결정이 아니라
측정된 선택지**다:

- S0.5 이후 `WorkerPools`가 곧 enkiTS이므로 S6의 비교가 자동으로
  "전용 풀 vs enkiTS"가 된다. 3지선다(전용 풀 / `WorkerPools` / 전용 enkiTS
  인스턴스)로 넓혀 두었다
- 프로브 `scratchpad/poolprobe/probe3.cpp`를 재사용한다. 파라미터: 64 태스크/
  라운드, 매 16번째 200µs 스핀, 워밍업 200 + 측정 2000라운드, **순서 역전
  대조 필수**(§2.1이 그걸로 갈렸다)
- enkiTS의 `threadnum_`은 0..`GetNumTaskThreads()-1`이 보장되므로 S3.5의
  워커별 포즈 풀 키로 그대로 쓸 수 있다

### PHASE 13 S6 이후 — 자체 풀 은퇴

S0.5·S6이 끝나면 `Core.ThreadPool`·`Core.CountingSemaphore`·`Core.Thread`의
소비자를 다시 센다.

**게이트**: 소스 전수에서 해당 심볼 등장 0 (주석·이력 제외).
그냥 지우지 않고 **`git ls-files`로 추적 여부부터 확인**한다.

### 페이즈 미배정 — DX12/Vulkan 커맨드 풀

§1.2의 두 풀(§3.5)은 애니메이션과 무관하므로 PHASE 13에 넣지 않았다. 착수
전에 RHI 워커의 스레드 친화 자원 소유 구조를 읽어야 한다(§3.5). 그 조사
결과에 따라 별건으로 제안한다.

## 5. 게이트 설계 규율

이 저장소가 반복해서 데인 함정들을 미리 적는다.

- **변이로 증명한다.** 새 게이트가 초록이면 결함을 심어 붉어지는지 본다.
  붉어지지 않으면 셋 중 하나다 — 못 잡았다 / 자극하지 못했다 / 단정이
  결함이 지나가는 자리에 없다. §2.2가 세 번째 경우의 실례다.
- **Release로만 잰다.** Debug는 25배 느리고 방향까지 뒤집는다.
- **워밍업 없이 재지 않는다.** 워밍업 0인 1·2차 프로브는 우선순위 축의
  답을 거꾸로 냈다.
- **대조군은 독립 유도를 갖는다.** 같은 프로세스에서 같은 출처를 두 번 읽는
  것은 대조가 아니다.
- **변경에 닿는 게이트를 묶어 실행한다.** `run-all.ps1`은 9-16 폐지됐다.
  현행 규칙은 `Tools/regression/README.md`이며, 공용 풀은 독립 검사·제품 번들·
  실제 썸네일 경로를 한 묶음으로 기록한다.
- **빌드 성공은 exit code로만 판정한다.** mtime은 LTCG 실패도 갱신한다.

---

## 6. 미해결 · 미측정

| 항목 | 상태 |
|---|---|
| PHASE 13과의 최종 경계 | §3.3으로 갈랐고 PHASE 13 계획서·대시보드에 S0.5로 반영 완료(2026-09-15) |
| 과거 R1·R2 공유 애셋 레이스 | typed generation 전환으로 해당 경로 제거. 새 풀의 수명·배리어 검증과 S0 제품 회귀는 별개 |
| 꼬리 지연 | 기계 노이즈가 신호보다 크다. 조용한 환경 재측정 필요 |
| enkiTS가 빠른 이유 | 가설: `WaitforTask` 중 호출 스레드도 일한다(엔진 풀은 메인이 순수 대기). **미검증** |
| 워커 16개 동시 경합에서의 우선순위 | 미측정. S6의 선행 조건 |
| `try_pop` 실패 보정 부재 | 발화 경로 미확인. 은퇴하면 자연 소멸 |
| RHI 커맨드 풀의 스레드 친화 자원 | 미조사. S5의 선행 조건 |

---

## 7. 관련 문서

- `docs/analysis/JobSystemFeasibilityAnalysis.md` — 9-6 조사. 외부 풀 4종
  비교와 배리어 결함. 본 문서 §2.1이 그 §11 결론을 독립적으로 재확인했다.
- `docs/analysis/PPLContainerMigrationAnalysis.md` — PPL 동시성 컨테이너 실태.
  `Core.ThreadPool`의 큐가 `concurrency::concurrent_queue`인 근거.
- `docs/plans/AnimationSchedulerPlan.md` (PHASE 13) — **정본.** 단계·공수·
  상태는 저쪽이 갖는다. 이 문서를 고칠 때는 **반드시 저쪽과 대시보드
  (`docs/RefactoringPlanDashboard.html` 의 `TASKS` 배열 `13-11`·`13-8`)도
  함께 본다.** 세 곳이 어긋나면 이 저장소가 이미 겪은 "계획서 5종 충돌 13건"이
  재현된다.
- `docs/design/ContainerLibraryDesign.md` — "STL이 기본값, 자체 제작은 측정된
  축에서만". 본 문서는 같은 규율을 스케줄러에 적용한 것이다.

## 8. S0.5 ② 공용 풀 이관 (2026-09-20)

- enkiTS 1.12는 기존 vcpkg baseline으로 고정한다. `WorkerPool` 공개 제출·대기 표면과
  SceneManager의 시작·종료 소유권을 유지하며 구현은 Utility_Framework의 cpp로 옮겼다.
- `AddTaskSetToPipe`는 큐가 차면 호출 스레드에서 일을 실행할 수 있다.
  썸네일 디코딩이 Presentation으로 돌아오지 않도록 임시 pinned 작업으로 워커에
  전달한 뒤 일반 task set 큐에 넣는다. pinned API는 외부 스레드 등록 없이 제출할 수 있다.
  근거: [enkiTS 1.12 API](https://github.com/dougbinks/enkiTS/blob/v1.12/src/TaskScheduler.h).
- 일반 task는 enkiTS completion action에서 해제한다. 전달 작업은 제출 측과 완료 측의
  참조 2개를 모두 놓은 뒤 해제한다. `AddPinnedTaskInt`가 큐에 게시한 뒤에도 작업의
  `threadNum`을 읽기 때문에 완료 측 단독 해제는 안전하지 않다. 제출 수는 실행 완료와
  callback capture 해제 뒤에 감소한다. 종료는 새 접수를 닫고 기존 작업을 모두 마친 뒤
  워커를 join한다. 일반 대기 호출자는 기존처럼 일을 실행하지 않고 완료를 기다린다.
  실행 예외는 다른 작업을 마친 뒤 대기 호출자에 전달한다.
- DataSystem 번들 로드는 스케줄러 계수와 별개인 완료 수를 반환한다.
  `worker.pool.probe`는 CreatorRobot 실제 번들 32건과 외부 생산자의 GLB 읽기 64건을
  동시에 제출한다. 실제 이미지 디코딩·게시·무효화·축출은 기존 썸네일 제품 게이트가 맡는다.
- `verify-worker-pool.ps1`: Debug/Release 각각 `WORKER_POOL_OK checks=10257`.
  반복 배치, 네 외부 생산자, 대기 없는 비동기 완료, 중첩 제출, capture 해제, 예외,
  종료 drain, 재시작을 검사했다. 실제 어댑터의 대기를 제거한 복사본은 두 구성 모두
  독립 완료 계수 단정에서 실패했다(`WORKER_POOL_MUTATION_OK`).
- 재실행 중 초기 전달 작업 해제 방식에서 접근 위반을 발견했다(enkiTS.dll `0xc0000005`).
  `verify-worker-pool-lifetime.ps1`은 enkiTS 1.12 소스 복사본의 게시/후속 읽기 사이를
  1ms 넓히고 실제 어댑터를 ASan으로 함께 빌드한다. 제출 측 참조를 제거한 변이가
  `AddPinnedTaskInt`의 `heap-use-after-free`로 실패해 해당 경합을 검출함을 확인했다.
  수정본은 같은 자극의 집중 검사 273개를 모두 통과했다(`WORKER_LIFETIME_OK`).
  고의 지연을 넣는 검사만 반복 배치를 줄이며, 일반 풀 검사는 10257개를 유지한다.
- 수명 경합 수정 후 Debug/Release 전체 CreatorEditor 빌드·링크 exit 0. 두 구성의 제품 번들 검사는
  `WORKER_PRODUCT_OK bundleSubmitted=32 bundleCompleted=32 externalReads=64 inlineReads=0 model=CreatorRobot`.
  `enkiTS.dll`은 기존 런타임 배포의 의존성 탐색으로 `Runtime/Common`에 포함됐다.
- 실제 썸네일 제품 회귀는 Release에서 `BROWSER_THUMBNAIL_CONTRACT_OK (56 checks)`.
  초기 3요청 중 이미지 2건 디코딩·게시, 손상 이미지 1건 실패, 수정 후 무효화 1건과 재게시,
  40000바이트 예산에서 축출을 확인했다. 늦은 완료 폐기는 이번 실행에서 0건으로
  해당 축의 성공 근거로 쓰지 않는다. 종료 drain은 별도의 독립 풀 검사로 확인했다.
- S0.5 전체 완료는 아니다. ③ 전용 풀 기본 우선순위의 동시 경합 실측과 변경,
  ④ Foliage/AI 이관, `std::async` 3곳 잔존 확인은 후속이다.
  이번 정확성 검사는 §2의 과거 독립 측정이나 S1 제품 성능 기선을 대체하지 않는다.

## 9. 공용 스케줄러 구현 (2026-09-20)

- 사용자 결정대로 성능 비교를 건너뛰고 공용 실행 기반부터 통일했다.
  [JobSchedulerDesign](../design/JobSchedulerDesign.md)에 소유·API·종료 계약을 정리했다.
- `thread_pool`은 enkiTS 실행과 지속 워커를, `job_scheduler`는 그룹·완료 토큰·
  의존 그래프·범위 분할을 맡는다. 엔진 시작/종료가 수명을 소유하고 소비자별
  `job_handle.wait()`가 자기 작업만 기다린다. 완료 토큰은 캡처를 붙들지 않는다.
- DataSystem·썸네일·AnimationJob·Foliage·Scene AI가 이 경로를 사용한다.
  구 WorkerPool과 Animation 전용 풀, 사용처 0이 된 Core.ThreadPool/Core.Thread/
  Core.CountingSemaphore를 소스·프로젝트에서 삭제했다. `std::async` 실행은
  씬 로드 2곳·DX12 PSO 1곳으로 줄었다. 전역 `std::future` 제거가 목표인 것은 아니다.
- Editor/Player 모두 엔진 부팅에서 시작한다. 새 프레임 생산 종료 후 AI 그룹을
  CLR보다 먼저 회수하고, 씬 해체 중에는 풀이 살아 있으며 DataSystem보다 먼저 종료한다.
  종료는 이미 수락한 의존 그래프까지 완료한다. 작업당 스레드 생성과 caller inline 실행은 없다.
- 독립 검사: Debug/Release 각각 `JOB_SCHEDULER_OK checks=10332 workers=4 backend=enkiTS`.
  그룹 독립 대기, 네 외부 제출자, persistent worker 신원, 반복 배치, fan-out/fan-in,
  4096단계 빈 의존 체인, parallel_for 꼬리 범위, 실패 전파와 종속 본문 생략,
  capture 해제, 워커 blocking wait 거부, 종료 drain·재시작을 검사했다.
  실제 대기를 제거한 변이는 두 구성 모두 독립 완료 단정으로 실패했다.
- enkiTS 게시/후속 읽기 구간을 넓힌 ASan 검사: 정상 구현 348검사 통과.
  제출 측 참조를 제거한 변이는 `AddPinnedTaskInt`의 `heap-use-after-free`로 실패했다.
  설치된 enkiTS 라이브러리를 바꾸지 않는 격리된 검사다.
- 재현: `verify-worker-pool.ps1`, `verify-worker-pool-lifetime.ps1`.
  산출물은 `Build/Obj/Phase13Jobs/Pool-{Debug,Release}`, `SubmissionLifetime`이다.

제품 검증(성능 측정 수치로 사용하지 않는다):

- VS18/v145 Debug/Release 전체 Editor 빌드·링크 exit 0.
  같은 Release 엔진 라이브러리를 사용하는 Player 컴파일·링크도 exit 0
  (`BuildProjectReferences=false`). Player 런타임 실행은 이번 검증에 포함하지 않았다.
  Player 링크에는 여러 라이브러리의 PDB 형식 레코드 경고 LNK4020이 남았으며,
  디버거의 일부 기호·타입 가용성은 별도 확인이 필요하다.
- Debug/Release `worker.pool.probe`: 각각 실제 번들 제출/완료 32/32, 외부 파일 읽기 64,
  제출 스레드 실행 0, 실제 Foliage 항목 73개 완료. 정적인 등록만 확인하지 않고
  초기 culled 값을 제품 범위 처리로 바꾼 결과를 기다려 검사한다.
- Debug/Release 애니메이션 제품 검사: 각각 CreatorRobot 100체 × 12회,
  `checks=69514 managedThreadErrors=0`. 실제 generation·포즈·씬 본/소켓·CLR 경로다.
- Release DX12 화면 비교: `captures=13 checks=264`. contact sheet도 확인했다.
  Walk/Run·블렌드·레이어·마스크·초록 손 소켓·숨김/복귀가 정상이다.
- Release 실제 썸네일 56검사 통과. `lateDropped=0`이므로 늦은 완료 폐기 경로를
  자극했다고 주장하지 않는다. 독립 스케줄러 검사가 종료 drain을 검증한다.
- Release BT: 트리 0→3, 재생 후 543틱·건너뜀 0, 프레임당 경계 통과 0.972376,
  씬 교체 후 트리 0. 저작 블랙보드 조건과 Running을 거친 행동 완주도 확인했다.
  기존 검사기는 폐기된 stdout 형식을 읽었으므로 현재 JSON 결과 계약으로 수정했다.
- Release AI registry: 최초 등록→DDOL handle 재등록→파괴 해지, 20명령 모두 성공.
  일반 명령 표 골든 133개 동일. BT/AI의 기존 로컬 fixture는 원본 바이트로 복원했다.
- 결과: `Build/Obj/Phase13Jobs/{Product,Animation,Visual,Thumbnails,BT,AI,Registry}-Release`.
  Debug 결과는 같은 루트의 `Product-Debug`, `Animation-Debug`다.
  테스트 모델 SHA-256은 §8의 CreatorRobot과 동일하다.

§9 이후 PSO 컴파일은 §10, SceneManager 값 반환/블로킹 로드는 §11에서 이관했다.
RHI 명령 기록은 §12에서 공용 실행 기반으로 후속 이관했다. S6은 공용 스케줄러 위의 애니메이션 청크화·계층 정리이며 전용 풀
선택 비교를 다시 하지 않는다. 장기 서비스 루프는 공용 Job 실행과 역할이 다르다.

## 10. DX12 PSO 비동기 컴파일 이관 (2026-09-20)

- `std::async`/`shared_future` 실행·대기를 공용 `job_scheduler` 제출과
  `job_handle` 완료 확인으로 교체했다. 제품 소스의 실행용 `std::async`는
  당시 SceneManager 씬 로드 2곳만 남았고, §11에서 이관했다.
- 입력 복사는 기존 `RHIGraphicsPipelineRequest`의 소유 저장소를 재사용한다.
  `Prepare`는 컴파일 없이 바이트코드·input element·semantic을 복사한다.
  루트 시그니처는 COM 참조로 유지하므로 워커가 자원 표를 다시 조회하지 않는다.
- 미완료 키 중복 병합, Pending 동안 fallback/skip, 완료 후 render owner 게시,
  2회차 디스크 캐시 복원과 기존 동기 그래픽/컴퓨트 취득을 보존했다.
- 종료/전체 무효화는 admission을 닫고 캐시별 작업을 lock 밖에서 회수한다.
  무효화 결과는 폐기하며 종료 후 재제출은 거절한다. 소멸자도 같은 회수를 수행한다.
  별도 스레드 풀 소유·종료는 없고, 검사에서만 독립 scheduler를 주입한다.
- 현재 Request/Resolve 비동기 소비자는 `dx12.psocache`다. 라이브 렌더 패스의
  동기 `GetOrCreate`를 비동기로 전환한 것은 아니다.
- `dx12.psocache`에 실제 enkiTS 워커 점유 검사를 추가했다: 중지 중 제출 거절,
  대기 중 원본 바이트코드 덮어쓰기/해제, 동일 키 8요청의 단일 제출, 실제 워커
  실행, 드라이버 컴파일 실패, 무효화/종료 중 요청 거절·접수 작업 회수·결과 폐기,
  무효화 후 재요청, 무관한 작업과 독립적인 대기를 검사한다.
- 최종 VS18/v145 x64 Editor 및 참조 프로젝트 빌드: Debug/Release 모두 exit 0,
  오류 0. 최종 증분 Debug 경고 0, Release 경고 19(기존 RS2008·IL2026·IL2075·
  LNK4229). 이번에는 Player 실행·Vulkan 화면·성능 비교를 수행하지 않았다.
- 최종 Debug/Release `dx12.psocache` 모두 `status=succeeded`, `passed=true`,
  프로세스 exit 0, stderr 0. 위 PSO Job 검사를 비롯해 기존 캐시·무효화·폴백·
  그래픽/컴퓨트 디스크 복원 검사가 통과했다. 2회차 그래픽 컴파일 0/라이브러리 히트 3,
  컴퓨트 컴파일 0/라이브러리 히트 1, 폴백 이후 Skip 0을 유지했다.
- 빌드/실행 후 변경 소스 4파일의 SHA-256 일치를 확인했다. 기존 검사 목록을 사용해
  명령 등록 표를 바꾸지 않았으며, 테스트 모델은 기존 Triangle.slang을 유지한다.
- 재현: `Tools/dx12-validation/Invoke-Dx12Suite.ps1 -Only dx12.psocache
  -NoRenderWait -Exe <구성별 CreatorEditor.exe> -OutDir <결과 경로>`.
  이 검사는 자체 디바이스/PSO를 사용하므로 라이브 씬 예열은 필요 없다.
  결과 경로: `Build/Obj/Phase13Jobs/PsoMigration`.
- 이 슬라이스 직후 남은 씬 로딩은 §11, DX12/Vulkan 명령 기록은 §12에서 이관했다.
  성능 비교·CPU 예산·우선순위·애니메이션 S6 청크화는 후속이며 정확성 검사로 대체하지 않는다.

## 11. 씬 로딩 2곳 이관 (2026-09-20)

- `LoadSceneAsync` / `LoadSceneAsyncAndWaitCallback`의 `std::async`를 공용
  `job_scheduler` + 소유 요청 레코드로 교체했다. Engine/Editor/Player C++ 제품
  소스에서 실행용 `std::async(...)` 검색 결과는 0건이다.
- 문서 파싱 후 비동기 자산 배치를 제출하고 워커를 반환한다. 게임 스레드가 두
  토큰을 확인하여 엔티티/컴포넌트·리맵·프리팹·DDOL을 구성한다. 실제 게임 객체를
  워커에서 생성하던 동작은 제거했다. callback DDOL의 기존 활성 씬 경유를 유지한다.
- 첫 실제 저장/재로딩 검사에서 일반 엔티티/DDOL 두 절의 동일 ID가 중복 구성되는
  것을 잡았다. 이관한 두 경로는 DDOL 절의 ID를 일반 절에서 제외하고 한 번만 구성한다.
- 반환 future는 유지하되 결과 전달만 맡는다. 정상 프레임 경계 또는 명시적
  `WaitForSceneLoad()`가 구성까지 완료한다. 소유 스레드의 pump 없는 future.get은
  지원하지 않는다. 이관 전 두 API의 제품 호출자는 없었으며 동기 제품 로드는 유지한다.
- 버린 future의 성공 씬도 SceneManager가 소유한다. 실패/취소는 nullptr, 겹친 callback은
  최신 요청만 활성화, 동기 Create/Load는 이전 준비 취소·회수, 준비 중 loading 상태를 제공한다.
- Editor/Player CLR 종료 전에 새 요청을 닫고 준비/자산 작업을 회수한다. 종료 시 취소한
  결과로 씬을 구성하지 않는다. SceneManager 해체/소멸에도 회수를 배치했다.
- `scene.loadjobs` 격리 commandlet과 `Tools/regression/verify-scene-load-jobs.ps1`을 추가했다.
  실제 저장한 씬과 기존 CreatorRobot 번들을 사용한다. 테스트 모델 변경 없음.
  명령은 fixture 씬을 만들고 마지막에 종료 상태로 전환하므로 격리 프로세스로만 실행한다.
- VS18/v145 x64 Editor 및 참조 프로젝트 Debug/Release 빌드 오류 0. 최종 증분
  Debug 경고 0, Release 경고 2(기존 Terrain C4244, LNK4229)다.
- Player Debug/Release 빌드도 오류 0. Debug 경고 1(LNK4229), Release 경고 34
  (LNK4229 및 기존 LNK4020 PDB 형식 레코드)다. Player 실행과 디버거 기호 가용성은
  이번에 확인하지 않았다.
- Debug/Release 각각 격리 `scene.loadjobs`: 성공, exit 0, stderr 0. 반환 경로/호출자 경로 소유,
  future 폐기 시 비차단 및 씬 소유 3건, 최신 callback 선택, 누락 문서 nullptr,
  콜드 모델 번들 게시, DDOL 2개 이송·동일 ID 단일 구성, 동기 로드에 의한 취소,
  워커의 SceneManager 제출 거절, 정상 프레임 완료·활성화, 종료 전 회수/거절을 확인했다.
- 별도 Debug/Release 프로세스의 기존 `worker.pool.probe`도 각각 번들 32/32, 외부 읽기 64,
  호출 스레드 실행 0, Foliage 73개를 통과했다. 두 검사는 캐시를 공유하지 않는다.
- 워커 점유 검사는 개별 외부 제출을 사용하면 pinned handoff가 이미 막힌 워커에
  걸릴 수 있어, 하나의 stealable 그룹으로 워커 수만큼 제출한다. 검사 장치의 시간
  초과를 이 방식으로 수정한 뒤 최종 두 구성에서 모두 통과했다.
- 재현: `Tools/regression/verify-scene-load-jobs.ps1 -Configuration Debug|Release
  -Work <새 결과 폴더>`. 결과는 `Build/Obj/Phase13Jobs/SceneMigration`의
  `Debug-pass`, `Release`와 각 `worker` 하위 폴더에 있다. 변경 소스 13파일의
  빌드/실행 후 SHA-256 일치, `git diff --check`를 확인했다.
- CreatorRobot SHA-256: `58D779AFDE1A7FBD13332999C8B11890FB7E10FF2036CA6AE3645D14BEDBA402`.
  성능 비교·단일 워커 제품 실행·Player 실행·Vulkan 화면 검증은 이번에 수행하지 않았다.
- 이 슬라이스 이후 DX12/Vulkan 명령 기록 풀도 §12에서 이관했다.
  성능 비교, CPU 예산/우선순위, 애니메이션 S6 최적화는 후속이다.

## 12. DX12/Vulkan 명령 기록 이관 (2026-09-20)

- DX12CommandListPool/VulkanCommandBufferPool의 전용 std::thread·조건 변수·작업
  포인터를 제거했다. 공통 RunParallel이 구간별 Job 하나를 공용 job_scheduler에
  묶어 제출하고 해당 그룹만 기다린다. 단일 구간도 공용 워커에서 실행한다.
- 구간 번호는 물리 워커 ID와 독립적이다. `[frame slot][lane]` allocator/list 또는
  command pool/buffer 소유는 유지한다. Reset → 기록 Job 완료 → Close → batch 제출,
  GPU fence 완료 후 슬롯 재사용 순서를 유지한다. 풀 종료는 공용 스케줄러를 종료하지 않는다.
- 공용 워커의 중첩 동기 호출은 제출 전에 거절한다. RenderGraph는 native Prepare/Reset
  전에 거절하고, scheduler 중지·패스 예외는 실패 결과로 반환한다. 나머지 Job 완료 후
  열린 command target을 닫아 다음 프레임의 재사용을 가능하게 한다. 실패 batch는 만들지 않는다.
- dx12.parallel을 확장하고 활성 이관 검증을 위해 vk.parallel commandlet을 복원했다.
  일반 제품 명령으로 등록하지 않는다. 모델 교체는 없으며 기존 색상/띠 clear 픽셀 fixture다.
- 검사: 물리 워커 1/4 × 논리 구간 1/4/상한 초과의 정확히 한 번 실행, caller 실행 0,
  예외 후 모든 구간 완료, 중첩/중지 제출 거절, 실제 native 자원 reset/실패 복구,
  단일 물리 워커의 4개 command target, 반복 frame slot 재사용 픽셀 비교,
  batch의 기록 당시 슬롯 유지, 자원 풀 종료 뒤 스케줄러 생존을 확인한다.
- 재현: `Tools/regression/verify-command-recording-jobs.ps1 -Configuration Debug|Release
  -Work <새 결과 폴더>`. 결과 경로: `Build/Obj/Phase13Jobs/CommandRecordingMigration`.
- VS18/v145 x64 Editor 및 참조 프로젝트 Debug/Release 빌드 오류 0. 두 최종 빌드 모두
  기존 `/DELAYLOAD:vulkan-1.dll` LNK4229 경고 1건이다.
- Player Debug/Release 빌드도 오류 0. Debug 경고 1(LNK4229), Release 경고 34
  (LNK4229 및 기존 LNK4020 PDB 형식 레코드)다. Player 실행과 디버거 기호 가용성은
  이번 검증 범위에 포함하지 않았다.
- Debug/Release × dx12.parallel/vk.parallel 네 실행 모두 `succeeded`, `passed=true`,
  exit 0, stderr 0. DX12의 65,536픽셀 띠 덮임 오류 0, 1/4구간 결과의 다른 바이트 0.
  Vulkan도 순차/병렬 픽셀 일치·최종 색 통과, 미구현 0·validation 문제 0이다.
- 물리 워커 1개로 4개 native target을 기록하고, 워커 4개에서 세 프레임 슬롯을
  두 차례 순환하는 추가 6프레임의 픽셀도 기준과 일치했다. 중지/패스 예외 뒤
  target 재사용, 제출 직전 current slot 변경, pool 종료 후 scheduler 생존을 통과했다.
  Vulkan shutdown의 pending task/batch/retirement는 0/0/0이다.
- 호스트 경로 확인: Editor/Player의 StopLiveRenderThread가 먼저 완료되고, 이후
  EngineBootstrap::FinalizeRuntime이 공용 스케줄러를 종료한다. 백엔드 자원 해체는
  기존 RHI lifecycle drain 이후다. 공용 워커 ID에 묶인 recording TLS는 없다.
- 정적 검사에서 두 RHI 풀의 전용 thread/condition_variable/WorkerLoop는 0건,
  Engine/Editor/Player C++ 실행용 std::async 호출은 0건이다. 빌드/실행한 입력 13파일의
  SHA-256 일치와 `git diff --check`를 확인했다. 테스트 모델 교체·성능 비교는 없다.
- 성능 비교는 수행하지 않는다. CPU 예산/우선순위·애니메이션 S6 청크화는 후속이다.

## 13. Player 실제 실행·씬 전환·종료 (2026-09-20)

- 기존 smoke에 `--smoke-reload`를 추가했다. 첫 표시 슬롯 회전 뒤 시작 씬을
  `LoadSceneAsync`로 준비하고, 정상 소유 프레임 경계에서 ActivateScene을 요청한다.
  교체된 씬의 활성화와 이후 발행된 프레임의 표시 완료를 요구한 뒤 정상 종료한다.
- `verify-player-job-lifecycle.ps1`은 지정한 cooked fixture를 격리 복사하고 최신 구성의
  Player·Runtime/Common·Managed를 배치한다. native DLL/exe 해시를 대조하고, 자신이
  시작한 프로세스만 종료한다. 기존 Player 전부를 종료하는 예전 검증 방식은 사용하지 않는다.
- 최초 재사용한 과거 fixture에는 최신 Decal.slang이 없었다. 현재 BuildTool로 제한된
  fixture를 다시 cook/PAK/Player 검증했고 Release/DX12 `verification=passed`를 얻었다.
  그 과정에서 은퇴한 WorldSprite.hlsl을 요구한 BuildTool 검사도 수정했다(`f517fddc`).
- 최종 Debug/Release 두 구성에서 reload와 normal-exit 총 4회 모두 exit 0·stderr 0.
  reload는 관리 초기화/시뮬레이션 시작이 씬당 1회씩 총 2회, 전환 이후 표시 완료,
  pending scene load 없음, text parser 0을 확인했다. 정상 서비스 실행은 `/health`의
  frame 증가·idle·gameStart를 확인하고 quit 뒤 프로세스 runtime 폴더 제거까지 통과했다.
- shutdown_trace의 `app.Finalize 완료` → `SceneManager::Destroy()` →
  `ce::get_job_scheduler().shutdown()` → `FinalizeRuntime 완료` 순서도 확인했다.
  정상 표시를 시작한 Player의 RHI 종료 stdout은 보장되지 않아 그 문자열을 완료 증거로
  요구하지 않는다. GPU task/batch/retirement 0/0/0의 개별 증거는 §12의 RHI 검사다.
- 최종 Editor/Player Debug·Release 빌드도 0오류. 기존 LNK4229/LNK4020와 관리 코드
  분석 경고는 남아 있다. 소스와 실행 바이너리의 검증 기록은
  `Build/Obj/Phase13S1/build-verification.json`, Player 기록은
  `Build/Obj/Phase13Jobs/PlayerRuntime/{Debug-pass,Release-pass}/verification.json`이다.
- 재현: `Tools/regression/verify-player-job-lifecycle.ps1 -Configuration Debug|Release
  -CookedFixture <BuildToolSmoke.creator와 PackageSmokeProbe를 포함한 최신 패키지>`.
  이번 fixture는 `PlayerRuntime/Packages/Game-6a91b88da394424c9f66447279e10cf4`다.
- 시작 씬은 카메라·조명·관리 스크립트만 포함한다. 모델은 cook 입력에만 있으므로
  실제 모델 렌더링·Vulkan Player·Shipping·clean VM까지 검증했다고 보지 않는다.
  일반 작업 실행기 통일과 이 Player 수명 검증을 마쳤으며 CPU 예산·우선순위·S6 최적화는 후속이다.
