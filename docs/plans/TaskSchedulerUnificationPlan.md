# 태스크 스케줄러 단일화 — enkiTS 이관 (PHASE 13 S0.5 · S6 부속)

2026-09-15 수립. **독립 페이즈가 아니다** — `AnimationSchedulerPlan.md`(PHASE 13)의
S0.5(기반 정비)와 S6(Job 배치 전환)의 근거 문서다. 단계·공수·상태는 저쪽이
정본이고, 이 문서는 그 판정에 이른 실태 조사와 측정을 담는다.

> 초안은 별도 PHASE 26으로 세웠으나, 착수 전 대조에서 PHASE 13 S6이 이미 같은
> 작업("전용 풀 vs WorkerPools 실측 비교")을 갖고 있음이 드러나 편입했다(§3.3).

엔진의 병렬 실행 표면을 전수 조사한 결과, **스레드 풀이 3벌 있고 그중 하나는
인스턴스가 0개**였다. 같은 작업(fan-out 후 배리어)을 세 곳이 각자 구현하고
있으며, 자체 풀의 동기화 프리미티브(`CountingSemaphore`)는 도입 이래 한 번도
수정된 적이 없다.

이관 대상은 **fork-join 경로뿐**이고 장기 블로킹 스레드는 그대로 둔다. 후자를
옮기면 워커가 영구 점유되어 풀이 굶기 때문이다 — 이 경계가 첫째 판정이다.

둘째 판정은 순서다. **기반 정비(S0.5)와 `AnimationJob` 이관(S6)을 갈라 놓았다**
— S2′~S3.5가 분해 자체를 바꾸므로, 먼저 옮기면 곧 버려질 모양을 이식하게 된다
(§3.3). 따라서 자체 풀은 S6이 끝나야 은퇴한다.

원칙 셋을 먼저 박는다.

1. **옮기는 것은 "짧고 끝나는 일"뿐이다.** 블로킹 루프·I/O 대기·값 반환
   비동기는 남긴다. Unity도 Job System과 비동기 로딩 스레드를 분리해 둔다.
2. **교체의 명분은 처리량이다.** 꼬리 지연이 아니다(§2.3 — 이 기계에서 측정
   불가). 정확성도 아니다(§2.2 — 자체 풀의 배리어는 정직했다).
3. **인터페이스를 먼저 세우고 백엔드를 갈아 끼운다.** `WorkerPool`이 이미
   그 모양이라 첫 소비자로 쓴다.

---

## 1. 지금 무엇이 있는가 — 실측 (2026-09-15)

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

### 3.4 ★ 앞선 판단 정정 — 잡끼리 겹치는 쓰기가 있다

이 조사 도중 "현행 fan-out은 잡끼리 공유 자료를 안 만지므로 접근 등록·충돌
검출 같은 계약이 잡을 것이 없다"고 판단했으나, **틀렸다.** 근거로 삼은 것은
`AnimationJob.cpp:170`의 X7 주석("worker는 Animator 소유 staging만 쓴다")
하나였는데, PHASE 13 §1.2가 공유 애셋 경로의 레이스를 이미 기록해 두었다.

| | 내용 | 위치 |
|---|---|---|
| R1 | 공유 애셋 동시 쓰기. `Skeleton`·`Animation`이 `DataSystem::Models` 캐시로 인스턴스 간 공유되는데 워커 8스레드가 `Animation::curKey`와 `Bone::m_global/localTransform`에 동시 기록. **같은 모델 2체면 레이스** | `AnimationJob.cpp:368,419,377-378,422-423` |
| R2 | `UpdateBlendBone`이 `nextanimation->m_nodeAnimations[boneName]`를 `find` 없이 호출 → 채널이 없으면 **워커 스레드에서 공유 애셋 `std::map`의 구조를 변경** | `AnimationJob.cpp:366,651` |
| R6 | 워커 스레드에서 C# 키프레임 이벤트 발화 | `AnimationJob.cpp:156,280` |

X7 주석은 **Scene packed storage와 부착 오브젝트 Transform에 대해서만** 참이고,
`DataSystem::Models` 캐시를 경유하는 공유 애셋에는 해당하지 않는다. 주석의
적용 범위를 그 문장이 다루지 않는 자료에까지 넓혀 읽은 것이 오류였다.

함의: 스케줄러를 교체해도 R1·R2는 그대로 남는다. **enkiTS는 이 결함을 고치지
않는다** — 고치는 것은 PHASE 13 S2′(공유 애셋에서 인스턴스 상태 분리)다.
순서가 또 한 번 PHASE 13을 가리킨다.

### 3.5 보류 — DX12/Vulkan 커맨드 풀

§1.2의 두 풀(§3.5)은 구조상 통합 대상이지만, RHI 워커가 스레드 친화적 자원
(커맨드 할당자·디스크립터 힙)을 워커 인덱스로 소유할 가능성이 있다. enkiTS의
`threadnum_`은 0..`GetNumTaskThreads()-1`로 안정적이므로 매핑은 가능하나,
**소유 구조를 읽기 전에는 착수하지 않는다.** S5에서 판정한다.

---

## 4. 단계 — PHASE 13 어디에 붙는가

단계 번호는 `AnimationSchedulerPlan.md` §3을 따른다. 이 문서는 항목별 근거만 적는다.

### PHASE 13 S0.5 — 태스크 스케줄러 기반 정비 (2일, P0, 즉시 착수 가능)

**① `AssetLoadJob` 삭제**
소비자 0. `AssetJob.h`/`.cpp` 제거, vcxproj·filters 항목 제거.
게이트: 전체 솔루션 빌드 exit 0 (mtime이 아니라 **종료 코드로** 판정).

**② enkiTS 도입 + `WorkerPool` 백엔드 교체**
- `vcpkg.json`에 `enkits` 추가(포트 존재 확인 완료, zlib 라이선스)
- `Enqueue`/`NotifyAllAndWait` 시그니처를 유지한 채 내부만 교체
- 소비자(`DataSystem.cpp:1678`·`:1702`)는 수정하지 않는다

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
- **게이트는 `run-all` 세트에 들어가야 존재한다.** 개별 스크립트로만 있으면
  없는 것과 같다.
- **빌드 성공은 exit code로만 판정한다.** mtime은 LTCG 실패도 갱신한다.

---

## 6. 미해결 · 미측정

| 항목 | 상태 |
|---|---|
| PHASE 13과의 최종 경계 | §3.3으로 갈랐고 PHASE 13 계획서·대시보드에 S0.5로 반영 완료(2026-09-15) |
| R1·R2 공유 애셋 레이스 | 스케줄러 교체로 안 고쳐진다. PHASE 13 S2′ 소관 |
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
