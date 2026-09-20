# 프레임 프로파일러 수집·녹화·구간 분석 계획

작성: 2026-08-10
목표: 현재의 표시 중심 프로파일러를 **프레임 단위로 녹화하고, 멈춘 뒤 특정 구간을 재현 가능하게 분석하는 도구**로 승격한다.

> 이 문서는 구현 상태를 주장하는 문서가 아니라 착수 기준이다. 작성 시점에는 현재 소스의
> CPU/GPU 프로파일러, FrameProfiler UI, Resource Counter, DX12 라이브 인플라이트 경로를
> 정적으로 점검했다. 이 계획을 위한 빌드·실행·장시간 캡처 검증은 아직 수행하지 않았다.
>
> **갱신(2026-08-11): P0 완료.** 위 문단은 P0 착수 전의 상태다. P0에서 빌드·실행·검사를
> 실제로 돌렸고, 정적 점검이 놓쳤던 것 세 가지가 드러났다 — §10 P0의 "실측 결과" 절 참고.
> 요약: 계획이 P2로 미뤄둔 지혈 중 일부는 **P0의 전제**였다. 픽스처가 자기 뒤처리를 하는
> 순간 엔진이 망가지는 토대 위에는 검사 하네스를 세울 수 없기 때문이다.

---

## 0. 한 줄 결론

ImGui 타임라인을 먼저 확장하지 않는다. 엔진·렌더러·관리 런타임의 계측 결과를
`CaptureSession` 하나에 `EngineFrameId`와 `SubmissionId`로 묶어 보존하는 **수집 코어**를
먼저 만든다. 에디터는 이 불변 캡처를 읽기만 한다.

완료 모습은 다음과 같다.

1. 에디터에서 Record를 누르고 10초 이상 플레이한다.
2. 프레임 그래프에서 CPU/GPU/GC 스파이크를 찾는다.
3. 한 프레임 또는 여러 프레임을 선택한다.
4. CPU Timeline·GPU Queue·Hierarchy·Counters가 같은 선택 구간을 설명한다.
5. 캡처를 `.ceprof`로 저장하고 다시 열어 같은 결과를 얻는다.
6. 내부 캡처로 원인을 좁힌 뒤 다음 동일 조건을 PIX/ETW로 정밀 캡처할 수 있다.

---

## 0.5 2026-09-15 개정 — 무엇이 바뀌었나

이 개정은 착수 전 실측(9-14·9-15)과 두 가지 결정을 반영한다. **결정은 둘이다 —
현행 수집 코어를 재활용하지 않는다, 그리고 마커 매크로를 폐기한다.**

### 0.5.1 실측으로 뒤집힌 전제

| 전제(개정 전) | 실측(9-14·9-15) |
|---|---|
| "Shipping 구성 자체가 없어 compile-out을 검증할 대상이 없다" | **폐기.** PHASE 14.5 LC8이 이미 세웠다 — `Directory.Build.props`의 `EngineShipping` 스위치, `Directory.Build.targets:72-73`이 전 프로젝트에 `CE_SHIPPING`/`CE_DEVELOPMENT` 정의, 산출물 키 분리, 그리고 격리 게이트가 `run-all:299`에 편입돼 있다 |
| "profiler 검증 하네스가 산다" | **절반만.** `-Action SelfTest`는 초록이지만 `-Action Stats`는 9-06(521fa21a)부터 exit 1이다. 명령은 성공하고 판정기가 죽었다 — `reg.Legacy`→`reg.Result` 전환으로 사람이 읽는 `[profile.stats]` 블록이 사라졌는데 하네스가 그 리터럴을 `IndexOf`로 찾는다 |
| "예산 포화·드롭을 게이트 단정으로 쓸 수 있다" | **쓸 수 없다.** 편집 27 / 재생 34 이벤트(상한 1024의 3%), 이름 388B / 525B(상한 16384B). 계측 지점이 전부 시스템 단위 고정 지점이고 per-object 루프 안에 하나도 없어 **씬에 무엇을 얹든 상수**다. 어떤 변이로도 자극되지 않는 빈 단정이다 |

### 0.5.2 ★ 지금은 녹화가 아니다 — 4프레임 실시간이다

`PROFILER_INITIALIZE(5, 1024)`의 5 중 현재 프레임을 뺀 **넷만 읽힌다**(selftest 실측
`보존 프레임 [65, 69)`). UI의 `Recording / Paused`는 링을 덮어쓰다 멈추는 것이라,
**스파이크를 발견했을 때 그 프레임은 이미 덮여 있다.** §0의 완료 모습 1~3단계가
원리적으로 성립하지 않는다는 뜻이고, 그것이 이 계획의 존재 이유다.

### 0.5.3 재활용하지 않는다 — 구조적 근거

결함 8종(§3·P0 기록)은 잡다한 버그가 아니라 **원죄 하나의 파생**이다 — 수집기가
producer의 TLS를 직접 만진다(`Profiler.cpp:169`의 `pTLS->NumEvents = 0`). 그래서
스레드 은퇴 시 UAF가 나고, 표 순회에 락이 필요해지고, 락을 넓히면 `RegisterThread`가
같은 뮤텍스를 다시 잡아 교착이고, 수집 시점에 스팬을 정렬해야 해서 부등호 하나로
스레드가 통째로 사라졌다.

§6.2의 sealed chunk handoff는 이 다섯을 **고치는 것이 아니라 발생하지 않게** 한다.
writer가 자기 chunk를 봉인해 넘기면 collector가 남의 메모리를 만질 일이 없다.
고쳐서 도달할 수 없고 갈아야 도달하는 지점이므로, 현행 `CPUProfiler`는 재활용하지 않는다.

**경계 — 무엇을 버리고 무엇을 남기는가**

| 대상 | 처리 |
|---|---|
| `CPUProfiler` 클래스(4프레임 링·TLS 리셋·수집 시점 정렬·`FixedStack` 32·이름 `LinearAllocator`) | **버린다** |
| `ImGuiHelper/ProfilerWindow.cpp` 557줄 | **버린다** — §6.4가 "UI가 global vector를 직접 읽지 않는다" |
| `PROFILE_CPU_*` 매크로 정의 | **버린다** — §5.2 참조 |
| 마커 호출부 39곳 | **남긴다** — 표기 형태만 바뀐다(이름과 자리는 보존) |
| selftest 픽스처 11종 | **다시 겨눈다** — 검사의 존재 이유가 정확히 이것이다 |
| 결함 8종 기록 | **설계 입력으로 남긴다** |

selftest의 `cross-frame/preserve`는 지금 KNOWN-DEFECT인데, **새 설계에서는 PASS여야
한다**(§6.1의 `truncated` flag). 그 한 줄이 PASS로 바뀌는 것이 이 작업의 진척 정의다.

### 0.5.4 자동 계측은 이 계획의 전제가 아니다

"계측 호출부 0"을 목표로 두자는 논의가 있었고, 실측 결과 **지금 구조로는 상속형
자동 수집이 성립하지 않는다**:

- 시스템 7종은 전부 `Singleton<T>` **CRTP** 상속이라 타입마다 다르게 인스턴스화되어
  공통 포인터로 담을 수 없다. `CameraSystem`은 `final`에 상속이 없다.
- `SystemSchedule`은 **컴포넌트 저장소**지 실행표가 아니다. 클래스 주석이 못 박는다 —
  "무엇을 들고 있는가만 관리하고 언제 도는가는 건드리지 않는다".
- 계측 지점의 실제 모양은 `PROFILE_CPU_BEGIN("X"); X->Update(dt); PROFILE_CPU_END();`
  **하드코딩 나열**이다.

실행 표(`{MarkerId, 함수 포인터}` 순회)로 자동화하는 길은 있으나, 그것은 씬 실행
구조 변경이고 `lifecycle 221사건` 순서 게이트를 지나간다. **이 계획은 그것을 기다리지
않는다** — 마커 표기를 호출부 형태에 독립으로 설계하면, 나중에 표가 생겨도

```cpp
for (auto& step : table) { ce::ProfileScope _{ step.marker }; step.fn(*this, dt); }
```

한 줄로 흡수되고 코어 변경은 0이다. **자동 수집은 프로파일러의 요구가 아니라 씬 구조
정리의 결과로 둔다.**

> 참고 — 실행 표는 트랙 C3가 버린 델리게이트로의 회귀가 아니다. C3가 없앤 것은
> **컴포넌트 N개**의 가상 디스패치(암묵 구독, 등록 순서 = 프리팹 파일 순서 의존)이고,
> `Component`에는 지금 가상 `Update`/`LateUpdate`/`FixedUpdate`가 하나도 없다. 실행 표는
> **시스템 13개**의 함수 포인터 호출이며 목록도 순서도 소스에 고정된다. 입도와 결정자가
> 둘 다 다르다.

### 0.5.5 ★ PHASE 13(enkiTS)과의 순서 제약 — 새로 생긴 의존

`TaskSchedulerUnificationPlan.md`(9-15)가 PHASE 13 S0.5로 enkiTS 이관을 편입했다.
이것이 이 계획에 양방향 의존을 만든다.

**enkiTS가 이 계획에 주는 것**

- `threadnum_`이 `0..GetNumTaskThreads()-1`로 **안정 보장**된다. 현행 코어가 11비트
  `ThreadIndex` 슬롯을 스스로 관리하고 은퇴·재사용 규칙(히스토리 한 바퀴 뒤 재사용)을
  발명해야 했던 이유는 "스레드가 임의로 생기고 죽어서"인데, enkiTS 워커는 **고정 개수·
  고정 인덱스**다. §6.2의 thread stream 설계에서 슬롯 수명 문제의 상당 부분이 사라진다.
- 태스크 실행 지점이 소수로 모이므로, **자동 계측의 진짜 자리는 프레임 단계 표가 아니라
  태스크 스케줄러**다. 수가 많고 동적인 쪽이 태스크이고, 프레임 단계 13개는 고정이라
  한 줄씩 써도 부담이 없다.

**이 계획이 enkiTS에 지우는 조건**

- ★ **지금 애니메이션은 이미 워커 8스레드에서 도는데(`AnimationJob.cpp:39`의
  `ThreadPool(8)`), 프로파일러는 `[GameThread]` 하나만 본다**(실측: 스레드 슬롯 1개).
  즉 **PHASE 13이 최적화하려는 바로 그 비용이 현재 캡처에 전혀 잡히지 않는다.**
- 그런데 현행 코어에 워커 계측을 그냥 붙이면 안 된다. §3.1의 원소 단위 경합이 그대로
  열려 있고, **지금 안 터지는 유일한 이유가 writer가 `[GameThread]` 하나뿐이기
  때문**이다. 워커가 마커를 찍는 순간 계약 부재가 현실이 된다.
- 따라서 **워커 계측은 §6.2 sealed chunk handoff 이후에만 추가한다.** 그 전에는
  enkiTS 이관(S0.5)을 진행하되 **워커에 마커를 걸지 않는다.**

**PHASE 13이 이 계획의 소비자다.** `AnimationSchedulerPlan.md` §4의 완료 기준 —
"100체 씬에서 버짓 상한 초과 프레임 1% 미만 · 예측 비용 대 실측 오차 15% 이내 ·
HUD에서 인스턴스별 강등 등급·비용·사유 관측" — 은 프로파일러 없이 판정할 수 없다.
13-7(버짓 HUD)과 14-3(ProfilerWindow 신설)의 파일 접점 경고가 여기서 실체를 갖는다.

### 0.5.6 개정으로 바뀐 실행 계획

- **P1의 "기존 `PROFILE_CPU_*` 매크로를 adapter로 연결" 항목은 폐기한다.** 옛 코어에
  adapter를 붙이는 일이 재활용 폐기로 무의미해졌다. P1에 남는 것은 `EngineFrameId`
  단일 발행 지점 통합과 마커 표기 신설이다.
- **P1의 "Shipping compile-out"은 만드는 일이 아니라 무는 일이다** — `CE_DEVELOPMENT`가
  이미 전 프로젝트에 정의돼 있다(§5.2).
- **P2가 앞당겨진다.** 워커 계측이 PHASE 13의 전제이므로 sealed chunk handoff는
  "나중에 정확도를 올리는 일"이 아니라 **다른 페이즈를 막고 있는 일**이다.

### 0.5.7 2026-09-20 정찰 최신화 — 전제 다섯이 또 바뀌었다

닷새 만에 다시 쟀다. 착수 전 정찰은 한 번 적고 끝나는 것이 아니다.

**① 회귀 세트가 폐지됐다(9-16).** `Tools/regression/run-all.ps1` 이 지워졌고, 정책은
"세션마다 변경이 닿는 검사를 모아 같은 바이너리·전제끼리 묶어 집중해서 돌린다" 로 바뀌었다.
**그러므로 이 계획서의 "run-all 편입" 완료 조건은 폐기한다.** 대신 새 규칙이 요구하는 것은
*찾을 수 있게 적는 것*이다 — "그 영역의 계획서 절이나 README 표에서 이름으로 찾을 수 있게".
⚠ 그런데 `Invoke-ProfilingValidation.ps1` 은 `Tools/regression/README.md` 표에 **없다**.
편입 대상이 사라졌을 뿐 **안 모인다는 사실은 그대로**다(§10 P1b 참조).

**② enkiTS 가 들어왔다 — 그것도 S0.5 보다 멀리.** `9d588bdd`·`c2396f4f` 가 `thread_pool` 과
`job_scheduler` 를 엔진 수명으로 소유하게 하고 **DataSystem·썸네일·AnimationJob·Foliage·AI** 를
그룹별 완료 경계로 통일했다. 구 풀은 제거됐다. 워커 수는
`enki::GetNumHardwareThreads()`(`ThreadPool.cpp:113`)다.

이것이 §0.5.5 의 예측을 둘 다 현실로 만들었다.

- **좋은 쪽**: 워커가 공용 스케줄러 하나로 모여 `threadnum_` 안정 보장이 실재한다 —
  §6.2 의 슬롯 키로 그대로 쓴다. 현행이 발명해야 했던 11비트 슬롯 은퇴·재사용 규칙이 불필요해진다.
- ★ **나쁜 쪽**: 계측 사각지대가 **넓어졌다**. 전에는 애니메이션 워커뿐이었지만 이제
  자산 적재·썸네일·Foliage·AI 까지 그 위에서 돈다. 등록 스레드는 여전히
  `[GameThread]` **하나**뿐이다(`EditorMain.cpp:82`). 엔진이 병렬로 옮겨 간 일이
  전부 캡처 밖이다.

**③ 계측 장치가 계속 따로 생긴다 — 파편이 셋이 됐다.**

| 장치 | 생긴 때 | 재는 것 | 읽는 법 |
|---|---|---|---|
| `profile.*`(이 계획) | PHASE 14 P0 | 게임 스레드 CPU 이벤트 | `profile.stats`·`profile.frame` |
| `EditorPanelCost` | 9-14 (W7-0) | 패널별 draw 비용 | `editor.panelcost` |
| `AnimationDiagnostics` | 9-20 (PHASE 13 S1) | 애니메이션 단계·워커 | `measure-animation-baseline.ps1` |

`AnimationDiagnostics.h` 머리 주석이 이 계획이 §5.4 에서 풀려던 문제를 그대로 적고 있다 —
"Worker durations overlap owner wait and each other; never add them to frame time."
**도메인이 자기 손으로 프레임 모델을 세우고 있다.** 늦을수록 파편이 는다.

**④ 상수 표기가 바뀌었다.** `6fd9d248` 이 UPPER_SNAKE 24종을 층별 표기로 옮겼다 —
`MAX_STACK_DEPTH` → `kMaxStackDepth`, `EVENT_BUFFER_SIZE` → `kEventBufferSize`.
새 코어의 이름도 `docs/design/CodingConventions.md` 를 따른다.

**⑤ W7-5 가 착지했다 — 이 계획의 임시 계측이 잡은 것이다.**
`ContentsBrowserWindow.cpp:849` 가 관문을 `BeginDragDropTarget()` 블록 **안으로** 옮겼고
프리팹 경로는 `m_prefabDirectory` 멤버로 캐시됐다. PHASE 14 임시 계측(`SceneStructureLockWait`)
과 신설 `profile.frame` 이 아니었으면 이 병목은 "스캔 0 이니 W7-1 로 끝났다" 로 남았을 것이다.
[[scan-count-zero-but-still-expensive]] 참조.

**그대로인 것.** P1 신설 심볼 5종 전부 0건. 등록 스레드 1개. §3.1 소유권 위반 생존.
임시 계측 생존(App.cpp 3곳·EditorMain.cpp 11곳·`profile.frame`) — 새 코어로 갈 때 정리한다.

**⑥ 두 축을 오늘 다시 돌렸다 — 초록인데 기준선 숫자가 움직였다.** `-Action SelfTest` 종료 코드 0 ·
`PROFILE_SELFTEST_OK=true`, `-Action Stats` 종료 코드 0 · 판정 통과(9-15 에 JSON 판정으로 고친 뒤
처음 재확인). 그런데 **이벤트/프레임이 38 · 이름 573B** 다 — §2.1 이 적은 편집 **27** · 388B 가 아니다.
원인은 회귀가 아니라 **내가 9-15 에 넣은 임시 계측 14곳**이 그대로 살아 있기 때문이다. 즉 이 축은
남의 계측이 드나들 때마다 값이 바뀐다. 그래서 §10 P1b 의 완료조건에서 **절대 숫자를 걷어냈다** —
단정의 모양은 "27 인가" 가 아니라 **"교체 직전 그 자리에서 뜬 값과 교체 직후가 같은가"** 여야 한다
([[mutation-proves-gate-has-teeth]]: 이빨은 A/B 동수 단정에 있지 절대 숫자에 있지 않다).
문서에 박은 숫자가 하루 만에 거짓이 되는 것을 실행으로 잡았다 — 안 돌렸으면 틀린 기준선을 커밋했다.

**`cli_registry.golden.tsv` 는 이미 최신이었다.** `verify-cli-registry-golden.ps1` 종료 코드 0 ·
골든 133 = 현재 133 · "한 글자도 다르지 않다". `profile.frame`(golden 91행)은 9-17 갱신본에
이미 들어가 있었다 — "갱신이 필요하다" 고 적었던 이 계획의 빚은 실재하지 않았다.

---

---

## 1. 범위와 비범위

### 1.1 이번 계획의 범위

- C++ CPU scope와 스레드 이벤트 녹화
- C# 스크립트 marker와 관리 GC counter 녹화
- DX12 패스별 GPU timestamp의 프레임 정확성 보장
- Draw/Batch/VRAM/업로드 링/디스크립터/리소스/GC counter 수집
- 고정 메모리 예산의 rolling capture
- Record/Pause/Clear, 프레임 선택, Timeline, Hierarchy, Save/Load
- 캡처 overflow·누락·프로파일러 자체 비용의 가시화
- 선택 조건을 이용한 다음 프레임 PIX/ETW 캡처 연결

### 1.2 1차 범위에서 제외

- 모든 C++/C# 함수를 자동 계측하는 Deep Profiling
- 과거 내부 캡처를 PIX GPU Capture로 소급 변환
- 원격 장치 스트리밍 프로파일러
- 네트워크 프로토콜과 다중 사용자 공유
- 매 할당의 네이티브 call stack 수집
- GPU 파이프라인 통계 query와 shader instruction 수준 분석
- 단일 프레임의 pass/draw/dispatch event tree, 중간 render target 미리보기와 draw 단위
  격리 replay — 이 기능은 `RenderFrameDebuggerPlan.md`가 소유한다.
- Flame Graph, 비교 분석, 회귀 대시보드의 완성형 UX

Deep Profiling은 기본 녹화와 분리한다. 모든 호출을 자동 계측하면 관측 대상의 실행 특성을
바꾸기 쉽다. 1차 도구는 정적 marker ID를 사용하는 낮은 오버헤드 계측을 기준으로 한다.

---

## 2. 현재 소스에서 확인한 기반

> 이 절은 **2026-09-15 실측으로 전면 갱신했다.** 초판(8-11)의 근거 줄은 그 뒤의
> 이관으로 대부분 무효가 됐다 — 수집 코어는 `ImGuiHelper`에서 `Engine/EngineDiagnostics`
> 로 옮겼고(P1a), `EnhancedSceneRendererLive.cpp`는 소멸했으며, `EnhancedRenderGraph.cpp`
> 는 `Render/Graph/`로 이동했다. 낡은 줄 번호를 남기면 다음 사람이 그것을 근거로 읽는다.

### 2.1 CPU 계측

| 항목 | 현재 근거(9-15 실측) | 판정 |
|---|---|---|
| 초기화 | `Editor/EngineEntry/EditorMain.cpp:80` `PROFILER_INITIALIZE(5, 1024)` | historySize 5 · 프레임당 최대 1,024 이벤트 |
| **읽히는 과거** | selftest 실측 `보존 프레임 [65, 69)` | ★ **4프레임**. 5 중 현재 프레임을 뺀 넷만 읽힌다 |
| 프레임 경계 | `EditorMain.cpp:279`(부팅 마감) · `:602`(메인 루프) | 게임 프레임 말미에 `Tick()` |
| 이벤트 | `Engine/EngineDiagnostics/Profiler.cpp` | QPC 기반 Begin/End와 TLS stack |
| **등록 스레드** | `EditorMain.cpp:81` `PROFILE_REGISTER_THREAD("[GameThread]")` | ★ **1개뿐.** P0(8-11) 때의 3개(Game·CB·CE)에서 줄었다 |
| 보관 | `Profiler.h` | 프레임별 event vector와 이름 `LinearAllocator`(16,384B) |
| 일시정지 | `Profiler.h` · `ImGuiHelper/ProfilerWindow.cpp` | queued pause 상태 존재 |

**계측 밀도 실측 — 예산은 문제가 아니다.**

| | 이벤트 | 이름 바이트 | 드롭 | 스레드 |
|---|---|---|---|---|
| 편집 모드 | **27** / 1024 (2.6%) | 388 / 16384 | 0 | 1 |
| 재생 모드 | **34** / 1024 (3.3%) | 525 / 16384 | 0 | 1 |

둘 다 `peak == last`로 **모든 프레임에서 동일**했다. 계측 지점은 정적으로 39곳이고
(`Scene.cpp` 22 · `SceneManager.cpp` 15 · `EditorMain.cpp` 2) **전부 시스템 단위 고정
지점**이다 — `PROFILE_CPU_SCOPE`의 실사용은 0이고 per-object 루프 안에 박힌 마커도 0이다.
그래서 씬에 무엇을 얹든 이벤트 수는 상수이고, 재생을 걸어도 27→34(딱 7개, 재생 경로
계측분)에 그친다.

함의 둘.

1. **예산 포화·드롭은 게이트 단정으로 쓸 수 없다**(§11.4 참조). 1024에 닿으려면 계측
   지점이 30배 늘어야 한다.
2. **이벤트 수 자체가 유일하게 자극 가능한 축이다.** 마커 하나가 새 코어로 안 이어지면
   27이 26이 된다 — 그리고 그것이 마침 P1 완료조건("기존 호출부 대량 수정 없이 새 코어로
   이벤트가 들어감")을 직접 재는 값이다.

**★ 가장 큰 사각지대 — 워커가 통째로 안 보인다.**
`AnimationJob.cpp:39`가 `ThreadPool(8)`로 애니메이션 워커 8개를 띄우고 프레임마다
`Enqueue`→`NotifyAllAndWait`로 돌리는데, 그 워커 중 **어느 것도 프로파일러에 등록되지
않는다**(등록 스레드 1개). RenderThread·PresentationThread에도 마커가 0이다. 즉
**오늘의 CPU 캡처는 애니메이션과 렌더 경로를 볼 수 없다** — PHASE 13이 최적화하려는
비용이 정확히 그 안에 있다(§0.5.5).

### 2.2 기존 FrameProfiler UI

`ImGuiHelper/ProfilerWindow.cpp`(557줄)에는 이미 다음 조작이 있다.

- 스레드별 중첩 bar · 검색 필터 · threshold pause
- Ctrl+wheel 확대 · 우클릭 이동 · double-click 구간 확대
- drag 구간 시간 측정 · event tooltip의 frame/file/line 표시

★ **`Recording / Paused` 문구가 있지만 녹화가 아니다.** 4칸 링을 계속 덮어쓰다
Space로 멈추면 그 순간 링에 남아 있던 4프레임만 보게 된다. 구간을 보존하는 장치가
없으므로 §0의 완료 모습 1~3단계("Record 후 10초 플레이 → 스파이크 발견 → 그 프레임
선택")가 **원리적으로 성립하지 않는다** — 발견했을 때 그 프레임은 이미 덮여 있다.

drag 구간도 화면에 길이만 그린다. 선택 결과를 보존하거나 Hierarchy 계산의 입력으로
쓰지 않으므로 분석 모델이 아니라 일회성 자다.

이 파일은 §0.5.3에 따라 버린다(§6.4의 immutable reader로 대체).

### 2.3 DX12 GPU 계측

| 항목 | 현재 근거(9-15 실측) | 판정 |
|---|---|---|
| timestamp heap/readback | `Engine/RenderEngine/RHI/DX12/DX12GpuProfiler.{h,cpp}` | 프레임 링 크기의 query 저장소 존재 |
| 패스 경계 | `Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp` | 그래프가 실행 패스를 자동으로 감쌈 |
| 프레임 진입 | `Render/Scene/EnhancedSceneRenderer.cpp:3588` `dx12.BeginProfilerFrame(frameCounter++)` | ★ **뷰마다 불린다**(§3.3) |
| 수집 | `DX12GpuProfiler::Collect(std::vector<PassTiming>&, std::string&)` | ★ **프레임 token을 받지 않는다** |
| 저장소 | `DX12GpuProfiler.h`의 `m_records` · `m_frameIndex` | ★ **한 벌뿐**(링이 아니다) |
| 표시 | `Editor/EngineGUIWindow/EnhancedRenderDebugWindow.cpp` | 마지막 CPU/GPU 합계와 패스별 표 |

그래프 경계에서 자동으로 감싸는 선택은 유지한다 — 패스 작성자가 marker를 빼먹지 않기
때문이고, 이것이 §0.5.4가 말한 "자동 계측이 성립하는 자리"의 기존 사례다. 다만 현재
결과는 raw timestamp가 아니라 마지막 완료분의 duration 목록으로 축약되고, 뷰 구분이 없다.

### 2.4 Counter 기반

`EditorGUIWindow/ResourceCounterWindow`는 다음 값을 이미 읽는다.

- DataSystem 모델·재질·텍스처·UI 리소스·retained asset
- RenderScene proxy·UI proxy·animator·palette·render pass data
- VRAM usage/budget · 엔진 리소스 census
- CoreCLR Gen0/1/2 횟수, heap, fragmentation, GC pause percentage

DX12 쪽에도 `DX12UploadRing::Stats`(allocations·bytes·overflows·peak frame bytes)와
`DX12DescriptorRing::Stats`(allocations·descriptors·overflows·peak frame descriptors)가 있다.

현재 값은 UI가 0.5초마다 직접 polling한다. 캡처용으로는 각 owner가 프레임 경계에 값
스냅샷을 발행하고 수집 코어가 `EngineFrameId`에 붙여야 한다.

---

## 3. 녹화 기능 전에 닫아야 할 정확성 문제

### 3.1 CPU producer를 프레임 말미에 직접 초기화한다

`CPUProfiler::Tick()`은 등록된 각 TLS의 event buffer를 읽은 뒤 `NumEvents = 0`으로
되돌린다. 워커가 동시에 Begin/End를 쓰는 동안 안전하게 buffer 소유권을 넘기는 계약이
없다.

필요한 변경:

- 스레드별 writer 전용 chunk를 둔다.
- collector는 writer가 닫아 넘긴 chunk만 읽는다.
- frame 경계와 scope 경계를 동일시하지 않는다.
- scope가 프레임을 넘어도 stack과 시작 timestamp를 잃지 않는다.
- overflow 시 assertion만 내지 않고 `droppedEvents`를 캡처에 기록한다.

### 3.2 종료한 스레드의 TLS 주소 수명

`ThreadData`가 `const TLS*`를 보관한다. 등록 스레드가 종료되면 주소를 계속 읽을 수 있다.
현재 장수하는 CB/CE 스레드만으로는 잘 드러나지 않지만 워커 풀이 재구성되거나 임시 스레드가
들어오면 성립하지 않는다.

필요한 변경:

- `ThreadStream` 소유권을 profiler service가 가진다.
- TLS는 소유 객체의 handle만 보관한다.
- 스레드 종료를 `ThreadEnd` 이벤트로 남기고, 미소비 chunk 회수 후 stream을 은퇴한다.

### 3.3 GPU frame record와 query slot이 분리돼 있다

`DX12GpuProfiler`의 query heap/readback은 frame ring 크기지만 `m_records`,
`m_usedPasses`, `m_frameIndex`는 한 벌이다. `Collect()`도 수집할 프레임 token을 받지 않는다.
반면 라이브 렌더러는 합산 인플라이트를 2개까지 허용하고, 멀티카메라에서는 한 엔진 틱에
제출이 둘 이상일 수 있다.

필요한 변경:

```cpp
struct GpuFrameToken
{
    uint64_t engineFrameId;
    uint64_t submissionId;
    uint64_t fenceValue;
    uint64_t renderViewId;
    uint32_t ringSlot;
    uint8_t  queueId;
};
```

- query record도 `ringSlot`별로 따로 둔다.
- DisplaySlot/pendingQueue가 해당 `GpuFrameToken`을 보관한다.
- `Collect(const GpuFrameToken&)`만 허용한다.
- 수집 결과에 pass별 begin/end raw timestamp를 유지한다.
- 같은 이름의 slice는 표시 단계에서 묶되 원본 interval은 버리지 않는다.

### 3.4 엔진 프레임과 렌더 제출은 1:1이 아니다

씬뷰와 게임뷰가 함께 있으면 한 `EngineFrameId`에 여러 GPU 제출이 생긴다. 따라서 다음 두
식별자를 구분한다.

- `EngineFrameId`: 게임 업데이트 경계. 프레임 그래프와 CPU Hierarchy의 기준.
- `SubmissionId`: GPU queue에 실제 제출한 단위. 카메라·뷰·queue를 식별.

GPU 합계는 모든 패스 duration의 단순 합만으로 정의하지 않는다.

- queue span: 첫 timestamp부터 마지막 timestamp까지
- busy interval: 겹치지 않는 실행 구간의 합
- pass duration: 개별 marker interval
- multi-queue overlap: queue별로 따로 표시

### 3.5 profiler가 에디터 모듈에 있다

`ImGuiHelper/Profiler.h`는 CPU 수집 구조와 UI 선언을 함께 가지고 `d3d12.h`까지 include한다.
이 상태에서 Player 녹화나 원격 프로파일링을 만들면 코어가 에디터에 계속 의존한다.

결정:

- 수집·보관·직렬화는 새 코어 프로젝트 `EngineDiagnostics`로 분리한다.
- ImGui는 `EngineGUIWindow`의 reader일 뿐이다.
- DX12 타입은 `RenderEngine/RHI/DX12` adapter 밖으로 노출하지 않는다.
- 기존 `ImGuiHelper/Profiler.*`는 전환 기간 adapter로만 남겼다가 제거한다.

---

## 4. 목표 계층과 의존 방향

```text
[Editor]
EngineGUIWindow/ProfilerWindow
        │ CaptureReader / immutable snapshot
        ▼
[Core diagnostics]
EngineDiagnostics
  MarkerRegistry · ThreadStream · FrameAssembler
  CaptureSession · CounterRegistry · CaptureFile
        ▲                    ▲
        │                    │
RenderEngine DX12       ScriptBinder/CoreCLR
GPU timestamp provider  managed marker/counters
```

의존 규칙:

1. `EngineDiagnostics`는 ImGui, D3D11, D3D12, Scene, DataSystem을 include하지 않는다.
2. renderer와 managed host가 provider 인터페이스를 구현해 값을 밀어 넣는다.
3. 에디터는 profiler 내부 mutable container를 직접 순회하지 않는다.
4. UI가 닫혀 있어도 녹화는 가능해야 한다.
5. Player/Development 빌드도 에디터 없이 `.ceprof`를 생성할 수 있어야 한다.

권장 파일 구조:

```text
EngineDiagnostics/
  ProfilerTypes.h
  ProfilerService.h/.cpp
  MarkerRegistry.h/.cpp
  ThreadEventStream.h/.cpp
  FrameAssembler.h/.cpp
  CaptureSession.h/.cpp
  CaptureFile.h/.cpp
  CounterRegistry.h/.cpp

RenderEngine/RHI/DX12/
  DX12GpuProfiler.h/.cpp          # 기존 구현을 token 기반으로 교체

ScriptBinder/
  ProfilerBridge.h/.cpp

ScriptCore/Diagnostics/
  ProfilerMarker.cs
  ProfilerCounter.cs

EngineGUIWindow/
  ProfilerWindow.h/.cpp
  ProfilerTimelineView.h/.cpp
  ProfilerHierarchyView.h/.cpp
  ProfilerModuleView.h/.cpp
```

---

## 5. 공통 데이터 모델

### 5.1 Clock

- CPU 원본 clock: QPC
- 저장 단위: session 시작을 0으로 한 nanosecond `uint64_t`
- session header에 QPC frequency와 시작 UTC를 기록
- GPU clock: queue timestamp frequency
- CPU/GPU 상관: `ID3D12CommandQueue::GetClockCalibration`
- calibration sample은 session 시작, 장시간 캡처의 주기 지점, device reset 후 다시 기록

### 5.2 Marker

> **2026-09-15 개정 — 매크로를 폐기한다.** 현행 `PROFILE_CPU_BEGIN`/`PROFILE_CPU_END`
> 매크로 쌍을 남기지 않는다. 호출부 39곳의 **이름과 자리는 보존**하되 표기를 바꾼다.

```cpp
using MarkerId = uint32_t;

struct MarkerDesc
{
    MarkerId    id;
    CategoryId  category;
    StringId    name;
    StringId    file;
    uint32_t    line;
    MarkerFlags flags;
};
```

이벤트마다 문자열을 복사하지 않는다. 정적 marker는 최초 등록 뒤 정수 ID만 writer에 쓴다.
동적 이름이 필요한 경우 별도 dynamic string table과 rate limit을 둔다.

#### 매크로를 버리는 이유 — 둘 다 구조적 이득이다

**① 짝 불균형이 문법적으로 불가능해진다.** 현행 결함 6은 `EndEvent`가 건너뛰어지면
(`if (m_Paused) return;`이 Pop 앞에 있다) 깊이가 어긋난 채 **영구 누적**되고, 32를 넘으면
`FixedStack::Push`가 TLS의 다음 멤버를 덮어쓰는 것이었다. RAII 스코프는 이 결함을
고치는 것이 아니라 **작성할 수 없게** 만든다. 열고 닫는 두 문장이 하나가 되면 한쪽만
실행되는 경로가 존재하지 않는다.

**② 이름 예산이라는 개념 자체가 사라진다.** 지금은 수집기가 프레임마다
`frame.Allocator.String(event.pName)`으로 이름을 복사하고, 그래서 16,384B 예산과
`DroppedNames` 계수가 필요했다. 이 저장소는 **C++23**이므로 NTTP 문자열 리터럴로
마커 ID를 컴파일 타임 상수로 확정할 수 있다 — 등록은 정적 초기화에서 한 번,
hot path에는 정수만 흐른다. 예산도 누락 계수도 필요 없어진다.

#### 표기

```cpp
// 정적 마커 — 이름은 컴파일 타임 상수, 등록은 1회
ce::ProfileScope _{ ce::Marker<"AnimatorSystem">() };

// 보조 — 이름을 생략하면 std::source_location이 함수·파일·행을 채운다
ce::ProfileScope _{};

ce::ProfileCounter(ce::Marker<"DrawCalls">(), drawCount);
ce::ProfileInstant(ce::Marker<"SceneLoaded">());
```

`source_location` 판은 **보조**다. 지금 `Scene::Update` 한 함수 안에 계측 지점이
13개 있어 함수 이름만으로는 구분되지 않는다 — 이름을 명시하는 쪽이 기본이다.

#### ★ 호출부 형태에 독립이어야 한다

마커 ID가 컴파일 타임 상수이고 스코프가 RAII이면, 호출부가 39곳이든 1곳이든 코어는
동일하다. 나중에 씬 실행 표가 도입되면

```cpp
for (auto& step : table) { ce::ProfileScope _{ step.marker }; step.fn(*this, dt); }
```

한 줄로 흡수되고 코어 변경은 0이다. **이 독립성이 설계 요구사항이다** — 프로파일러가
씬 실행 구조 리팩터를 인질로 잡지 않게 한다(§0.5.4).

#### Build 정책 — 만들 것이 아니라 물 것이다

> **2026-09-15 갱신.** 초판은 "저장소에 그런 매크로가 0건이므로 먼저 도착하는 쪽이
> 만든다"고 적었다. **PHASE 14.5 LC8이 먼저 도착했고 이미 서 있다.**

- `Directory.Build.targets:72-73`이 **전 프로젝트에** 정의한다 —
  `EngineShipping=true`면 `CE_SHIPPING=1;CE_DEVELOPMENT=0`, 아니면 `CE_SHIPPING=0;CE_DEVELOPMENT=1`
- 스위치는 `Directory.Build.props`의 `EngineShipping`이고 **솔루션 구성을 늘리지 않는다**
  (`msbuild ... /p:EngineShipping=true`)
- 산출물 키가 갈린다 — `Bin\x64-Release-Shipping\Player\`
- 격리 게이트 `verify-player-shipping-isolation.ps1`이 **`run-all:299`에 편입돼 있다**
- 현재 소비자는 `Player/PlayerCommandService.cpp` 하나뿐이다

따라서 이 계획이 할 일은 **`CE_DEVELOPMENT`를 무는 것**이다. 마커 타입을
`CE_DEVELOPMENT == 0`에서 빈 타입으로 두고(`if constexpr`로 본문 소거), 격리 게이트에
"Shipping 바이너리에 프로파일러 심볼 0" 단정을 얹으면 완료조건이 닫힌다.
**새 매크로를 만들지 않는다** — 두 벌이 되면 격리 게이트가 둘로 갈린다.

포함된 빌드에서도 runtime category mask로 비활성화할 수 있어야 한다.

### 5.3 Event

```cpp
enum class ProfileEventType : uint8_t
{
    ScopeBegin,
    ScopeEnd,
    Instant,
    Counter,
    ThreadBegin,
    ThreadEnd,
    FrameBegin,
    FrameEnd,
    DroppedEvents
};

struct ProfileEvent
{
    uint64_t timestampNs;
    uint64_t sequence;
    MarkerId marker;
    ThreadId thread;
    ProfileEventType type;
    uint64_t payload;
};
```

writer는 시간순 append만 한다. parent/depth/self time은 capture finalize 또는 UI 분석 시 계산한다.
이렇게 해야 프레임 경계를 넘는 scope와 중간 녹화 시작을 다룰 수 있다.

### 5.4 Frame

```cpp
struct CapturedFrame
{
    uint64_t engineFrameId;
    uint64_t beginNs;
    uint64_t endNs;

    EventRange cpuEvents;
    GpuSubmissionRange gpuSubmissions;
    CounterRange counters;

    uint32_t droppedEvents;
    FrameFlags flags;
};
```

프레임 첫/끝 marker는 profiler 자체가 main loop 한 곳에서 발행한다. 하위 시스템이
`PROFILE_FRAME()`을 임의로 호출하지 못하게 한다.

### 5.5 Counter

초기 category와 counter:

| Module | Counter |
|---|---|
| CPU | frame ms, update ms, render-submit CPU ms, thread count |
| GPU | queue span ms, pass ms, submissions, in-flight skips |
| Rendering | draw, batch, triangle/vertex 가능 시, decal, visible proxy, light |
| Memory | process committed/resident, VRAM usage/budget |
| DX12 | upload bytes/overflow, descriptors/overflow, transient usage |
| Managed | GC Gen0/1/2 delta, heap, fragmentation, pause, allocated bytes 가능 시 |
| Assets | model/material/texture/retained counts |
| Physics | bodies, active bodies, contacts, simulation ms 가능 시 |
| Audio | active voices/channels, update ms 가능 시 |

Counter는 소유 스레드가 값을 발행한다. UI가 DataSystem/RenderScene container에 직접 lock을
잡고 capture하지 않는다.

---

## 6. 수집 파이프라인

### 6.1 Recorder 상태 머신

```text
Stopped ──Record──> Recording ──Pause/Trigger──> Frozen
   ▲                    │                         │
   └──────Clear─────────┴────────Record──────────┘
```

- `Stopped`: marker API는 runtime mask에 따라 즉시 return
- `Recording`: rolling ring에 계속 기록
- `Frozen`: producer 기록을 멈추고 immutable capture를 reader에 공개
- `Clear`: capture와 selection만 비우고 marker registry는 재사용

Pause는 다음 engine frame 경계에서 확정한다. 중간 scope는 `truncated` flag로 닫아 분석기가
잘못된 self time을 만들지 않게 한다.

### 6.2 Thread stream

초기 구현은 복잡한 lock-free queue보다 규약이 명확한 chunk handoff를 우선한다.

- thread마다 writer 전용 고정 크기 chunk
- chunk가 차거나 frame publish 지점에 도달하면 sealed queue로 넘김
- collector만 sealed chunk를 소비
- hot path에서 heap allocation 금지
- free chunk가 없으면 drop count 증가, blocking 금지
- thread별 sequence로 동일 timestamp 순서 보존

기본 chunk 크기와 pool 수는 selftest 측정으로 결정한다. 상수부터 크게 잡아 문제를 숨기지
않고, overflow가 UI와 로그에 보이게 한다.

### 6.3 Rolling capture

초기 기본값:

- 600 engine frames
- 전체 메모리 예산 128 MiB
- 둘 중 먼저 닿으면 가장 오래된 완결 프레임부터 제거
- 현재 쓰는 프레임과 미완 scope가 참조하는 string/marker는 제거하지 않음
- UI에 `used / budget`, retained frames, dropped events 표시

프레임 수는 60 FPS에서 약 10초라는 UX 기본값일 뿐이다. 실제 보존 길이는 이벤트 밀도와
메모리 예산에 의해 달라질 수 있으므로 둘을 함께 표시한다.

### 6.4 Immutable reader

현재처럼 UI가 global profiler vector를 직접 읽지 않는다.

- recording 중에는 경량 `LiveSummary`만 double-buffer로 공개
- pause 시 `shared_ptr<const CaptureSession>`을 원자적으로 교체
- UI selection과 정렬은 reader 쪽 별도 상태
- 저장 작업도 immutable capture를 읽으므로 recorder를 오래 잠그지 않음

---

## 7. 에디터 UX

### 7.1 Toolbar

- Record / Pause
- Clear
- Live Follow
- 캡처 프레임·메모리 사용량
- Category/Module 선택
- Save / Load
- Trigger 설정
- `Capture next matching frame in PIX`

Space 전역 단축키는 제거하거나 Profiler 창 focus일 때만 받는다. 편집기 viewport 입력과
충돌하지 않아야 한다.

### 7.2 Frame Overview

최상단에 모든 분석의 entry point인 프레임 그래프를 둔다.

- CPU frame ms
- GPU queue span ms
- target frame budget 선: 16.67/33.33ms
- GC/alloc marker
- draw/batch 또는 선택 counter overlay
- 클릭: 단일 프레임 선택
- Shift+click/drag: frame range 선택
- 현재 recording head와 ring eviction 경계 표시

### 7.3 Timeline

트랙 순서:

1. Frame boundaries와 instant events
2. CPU main/game thread
3. command-build/command-execute/worker threads
4. managed/script thread
5. GPU Graphics queue
6. GPU Compute/Copy queue가 생기면 별도 트랙

기존 확대·이동·검색·tooltip 코드는 재사용하되 data source를 `CaptureReader`로 교체한다.
GPU bar는 `SubmissionId`, view/camera, fence, pass name을 tooltip에 표시한다.

### 7.4 Hierarchy

선택한 frame range에 대해 marker별로 집계한다.

| 열 | 의미 |
|---|---|
| Total | 모든 호출 inclusive time 합 |
| Self | 자식 scope를 제외한 시간 |
| Calls | 호출 수 |
| Avg | 호출 평균 |
| Min/Max | 호출 최소/최대 |
| P95 | 긴 꼬리 확인 |
| Frames | marker가 나타난 프레임 수 |
| GC/Counter | 해당 marker에 연결된 값이 있을 때 |

보기 모드:

- Hierarchy: parent-child call tree
- Flat: marker 이름으로 전부 합침
- Calls: 개별 호출 목록

MVP는 frame range 선택을 기준으로 한다. 임의 sub-frame 시간 범위를 지속 선택해 집계하는
기능은 CPU/GPU clock 정렬이 검증된 뒤 추가한다.

### 7.5 Module panels

- CPU Usage
- GPU / Rendering
- Memory / Resources
- Managed GC
- Physics
- Audio

처음부터 빈 module을 다 만들지 않는다. provider와 검증 데이터가 생긴 순서대로 공개한다.

---

## 8. `.ceprof` 캡처 파일

### 8.1 요구사항

- 저장 후 같은 프레임 수·marker 수·통계를 재현
- 빌드가 달라도 읽되 schema 비호환은 명확히 거절
- 부분 파일과 손상을 검출
- 대형 캡처를 frame index로 lazy load 가능
- 에디터가 없는 Development Player에서도 생성 가능

### 8.2 초기 형식

```text
Header
  magic = "CEPROF"
  formatVersion
  engineVersion / buildId / git commit if available
  QPC frequency / session start UTC
  platform / process / command line

Chunk table
  StringTable
  MarkerTable
  ThreadTable
  CalibrationTable
  FrameIndex
  CpuEvents chunks
  GpuEvents chunks
  Counter chunks
  Diagnostics chunk
```

각 chunk는 type, version, byte size, CRC를 가진다. 1차는 무압축으로 정확성을 검증하고,
파일 크기 실측 뒤 LZ4/Zstd 같은 chunk 압축을 별도 슬라이스로 결정한다.

저장 실패는 기존 frozen capture를 파괴하지 않는다. 임시 파일에 완성한 뒤 최종 이름으로
교체한다.

---

## 9. PIX·RenderDoc·ETW의 역할

내부 프로파일러는 긴 구간의 탐색 도구이고 외부 도구는 한 재현 지점의 정밀 분석 도구다.

| 도구 | 담당 |
|---|---|
| Creator Profiler | 여러 프레임 추세, CPU/GPU/GC 상관, 스파이크 조건 발견 |
| Creator Frame Debugger | 선택한 한 submission의 pass/draw 의미, batch 구성과 중간 출력 재현 |
| PIX | 선택 조건의 다음 GPU 프레임, queue/pass/resource/shader 정밀 분석 |
| RenderDoc | 렌더 상태·리소스·draw 재생 검증 |
| ETW/WPA | OS scheduling, context switch, I/O, thread wait 분석 |

과거 `.ceprof`에는 GPU command stream과 전체 resource state가 없으므로 PIX 캡처로 변환할 수
없다. 대신 다음 trigger를 제공한다.

내부 Frame Debugger도 GPU command stream 전체를 보존하는 외부 replay 도구가 아니다. profiler가
문제 frame/view를 찾으면 `RenderFrameDebuggerPlan.md`의 `.ceframe` metadata/preview로 엔진 의미를
확인하고, 같은 조건의 다음 frame을 PIX/RenderDoc으로 연결한다.

- marker duration이 임계값 초과
- CPU/GPU frame budget 초과
- 특정 marker 첫 등장
- 다음 N번째 프레임

조건이 맞으면 **다음 재현 프레임**의 PIX capture를 요청한다. PIX marker는 지원되는
WinPixEventRuntime 경로로만 넣고 raw Begin/End 주입은 하지 않는다.

---

## 10. 실행 계획

각 단계는 독립 커밋이고, 다음 단계는 앞 단계의 selftest와 smoke가 통과한 뒤 착수한다.

### P0 — 현재 동작 기준선과 profiler selftest 표면

할 일:

- 현재 `CreatorEngine.sln` Debug|x64 빌드 기준선 기록
- FrameProfiler 열기/닫기, pause/resume, 5프레임 표시 smoke
- `profile.selftest` console command 추가
- nested scope, multithread, cross-frame, overflow test fixture 정의
- profiler 자체 CPU 시간·event count를 로그에 출력

예상 변경:

- `EngineEntry/ConsoleCommandSystem.cpp`
- 신규 `EngineDiagnostics` 프로젝트 뼈대 또는 임시 테스트 진입점
- `Tools/profiling-validation/Invoke-ProfilingValidation.ps1`

완료 조건:

- 기존 UI를 켜지 않아도 selftest 실행 가능
- 성공 로그에 고정 marker `PROFILE_SELFTEST_OK=true`
- 실패 시 frame/thread/marker/예상값이 로그에 남음
- 현행 GPU live smoke와 DX12 validation 결과를 회귀시키지 않음

#### P0 실측 결과 (2026-08-11, 완료)

통과 10 / 실패 0 / 기지 결함 1. 산출물은 `profile.selftest`·`profile.stats` 콘솔 명령과
`Tools/profiling-validation/`이다. 판정 근거는 종료 코드와 `PROFILE_SELFTEST_OK=true` 마커.

기준선(캐릭터·파티클 없는 기본 씬):

| 항목 | 실측 |
|---|---|
| Debug\|x64 전체 빌드 | 0 오류 · 0 경고 |
| `Tick()` 비용 | 평균 25~32us · 최대 78us |
| 이벤트/프레임 | 26 / 상한 1024 (3%) |
| 이름 바이트/프레임 | 405 / 상한 16384 (2%) |
| 등록 스레드 | 당시 3 (`[GameThread]`·`[CB-Thread]`·`[CE-Thread]`), 3-2G 이후 현재 1 (`[GameThread]`) |

**정적 점검이 놓친 것 세 가지.**

1. **§3.1이 "assert만 낸다"고 적은 것은 절반이다.** `Tick()`의
   `PROFILER_CHECK(newIndex < frame.Events.size())`는 assert 뒤에 **그대로 기록을 이어간다.**
   NDEBUG에서는 assert가 사라지므로 상한을 넘긴 순간 `Events` 벡터 밖에 쓴다.
   `LinearAllocator::Allocate`도 같은 모양이다. 즉 overflow는 "누락되는" 문제가 아니라
   **Release 힙 손상**이었다. 픽스처를 실행하려면 먼저 닫아야 했다.

2. **§3.2의 TLS 수명 문제는 등록 해제 API가 없어서 회피가 불가능하다.** 스레드가 등록 후
   종료하면 `ThreadData::pTLS`가 죽은 `thread_local`을 가리키고, **이후 모든 `Tick()`이**
   그 저장소를 읽는다. 멀티스레드 픽스처는 정의상 스레드를 만들고 접으므로, 이것 없이는
   검사가 에디터를 UAF 상태로 남긴 채 끝난다. `UnregisterThread()`를 P0에서 신설했다.

3. **아무도 몰랐던 결함 — 스팬 그룹핑의 부등호가 반대다.**
   `while (threadIndex < events[Begin].ThreadIndex) Begin++`는, 그 프레임에 이벤트가
   **하나도 없는** 스레드를 만나면 남은 이벤트 **전부**가 조건을 만족해 커서를 끝까지 민다.
   결과적으로 **그보다 인덱스가 큰 스레드가 통째로 사라진다.** 기존 FrameProfiler 타임라인도
   같은 이유로 스레드를 조용히 누락해 왔다 — 한 스레드가 쉰 프레임에서 그 뒤 스레드가 빈다.
   `multithread/capture`가 0/3으로 실패해 드러났고, 원인은 워커(인덱스 3~5)가 아니라
   그 앞의 CB/CE 중 하나가 쉰 것이었다. **§13-1("UI보다 frame identity가 먼저다")의 실례다.**

**설계 제약 두 가지(P1·P2 착수 전에 알고 있어야 한다).**

- `CPUProfiler::GetTLSUnsafe()`가 함수 지역 `static thread_local`이라 **모든 CPUProfiler
  인스턴스가 스레드당 TLS 하나를 공유한다.** 검사 전용 인스턴스를 세울 수 없어, selftest는
  전역 `gCPUProfiler`의 프레임 경계를 직접 넘긴다 — 그래서 **라이브 캡처를 교란한다.**
  §3.2의 "`ThreadStream` 소유권을 profiler service가 가진다"가 이것을 푼다.
- **프레임을 넘는 스코프는 게임 스레드에서 재현하면 안 된다.** `Tick()`은 스택 맨 위를
  무조건 닫으므로, 열린 스코프가 있으면 `"CPU Frame"` 대신 그것을 닫는다. 게임 스레드의
  스택이 프레임마다 한 칸씩 깊어져 `kMaxStackDepth`(32)에서 죽는다. 워커에서만 관측할 것.

**P0이 이미 닫은 몫 / P2가 받는 몫.** P2는 아래를 다시 하지 않는다.

| P0이 닫은 것(최소 지혈) | P2가 받는 것(정식 구조) |
|---|---|
| 드롭 계수화(`DroppedEvents`·`DroppedNames`) | 캡처 diagnostics로 승격, UI 노출 |
| 널 슬롯 스킵 + `UnregisterThread` | `ThreadStream` 소유권 역전(서비스가 소유) |
| 수집 구간 `m_ThreadDataLock`(**표만**) | writer 전용 chunk의 sealed handoff |
| 스팬 그룹핑 부등호 정정 | (해당 없음 — 닫힘) |
| `EventStack` 은퇴·재등록 시 복구 + 불균형 계수 | `truncated` 플래그로 승격(§6.1) |
| `m_Paused`·`m_QueuedPaused` 원자화 | (해당 없음 — 닫힘) |
| 슬롯 재사용을 히스토리 한 바퀴 뒤로 유예 | 스트림 은퇴 시 미소비 chunk 회수(§3.2) |

**★ P0의 락이 지키는 것은 스레드 표 하나뿐이다.** `Tick()`이 `pTLS->EventBuffer[i]`를
읽는 동안 그 워커가 `BeginEvent`에서 `resize`를 돌리면 옛 버퍼가 해제된다 —
**원소 단위 경합은 그대로 남아 있다.** 3-2G에서 3자 배리어와 CB 스레드를 제거하면서
`PresentationThread`의 프로파일 매크로와 등록도 함께 제거했다. 현재 캡처가 안전한
이유는 `[GameThread]` 하나만 writer이기 때문이지 계약이 고쳐졌기 때문이 아니다.
PresentationThread/RenderThread 계측은 §3.1의 sealed chunk handoff 뒤에만 추가한다.

collector가 producer TLS의 `NumEvents`를 0으로 되돌리는 구조 자체도 **그대로 남아 있다.**
P2의 성공 판정은 `cross-frame/preserve`가 `KNOWN-DEFECT`에서 `PASS`로 바뀌는 것이다.

### P1 — EngineDiagnostics 코어와 공통 frame clock

#### P1a — 수집 코어 물리 이관 (2026-08-24, 완료)

§3.5의 소유권 문제를 먼저 닫았다. E축(EngineLayerSeparationPlan) E6의 마지막
판정(Player→ImGuiHelper 참조 제거)이 이것에 걸려 있었고, 실측하니 P0 정비를
거친 `Profiler.h`는 이미 ImGui include 0의 거의 순수한 수집기라 재설계 없이
물리 이동으로 실현됐다.

- ✅ `EngineDiagnostics.vcxproj` 신설(StaticLibrary, 경계 층 1 — Utility와
  동급, **ProjectReference 0의 완전 독립 라이브러리**). `Profiler.{h,cpp}`·
  `ProfilerSelfTest.{h,cpp}` 4파일을 ImGuiHelper에서 git mv.
- ✅ 코어/UI 경계 확정: `Profiler.h`의 `DrawProfilerHUD()` 선언을
  `ImGuiHelper/ProfilerHUD.h`(신설)로 분리 — 코어는 표시를 모르고, ImGui는
  reader다(§3.5 결정의 이행). 소비자 `ProfilerWindow.cpp`(구현)와
  `MenuBarWindow.cpp`(호출)가 새 헤더를 문다.
- ⚠ **죽은 `<d3d12.h>` include가 세 파일의 Windows 의존을 몰래 먹여
  살리고 있었다.** 제거하자 `Profiler.cpp`(QPC·GetThreadDescription)·
  `ProfilerSelfTest.cpp`(GetCurrentThreadId)·`ProfilerWindow.cpp`
  (LARGE_INTEGER·ARRAYSIZE)가 차례로 붉어졌다 — 각자 `<Windows.h>` 명시로
  정리. 헤더의 `ARRAYSIZE`는 템플릿 파라미터 `N`으로 바꿔 Windows 의존
  자체를 걷었다. Debug 유니티는 청크 병합이 이 전이를 가려 초록이었다 —
  비유니티 레그가 잡았다(검증 순서가 유니티만 돌면 놓치는 종류).
- ✅ 재배선: ScriptBinder는 ImGuiHelper include 경로를 EngineDiagnostics로
  교체(계측 소비 2건이 전부였다 — 죽은 문 닫기), ImGuiHelper·Academy_4Q는
  경로+참조 추가, **Player는 ImGuiHelper 참조를 제거하고 EngineDiagnostics
  참조로 교체**. Player의 ImGuiHelper include 경로는 유지 — PlayerMain의
  `"imgui.h"`가 대소문자 무시로 `ImGui.h` 래퍼(IMGUI_DEFINE_MATH_OPERATORS)
  로 해석되는 현 컴파일 결과를 보존한다(헤더 온리라 링크와 무관).
- ✅ P1 할 일의 "ImGui/D3D12 include 없는지 include boundary 검사"가 래칫으로
  성립: 층 1 등록으로 ImGuiHelper(2)·에디터 층 헤더를 무는 순간 상향 간선.
  음성 테스트 — `ImGui.h` include 주입 시 정확히 그 간선으로 붉어짐을 확인.
- ✅ 검증: Release 비유니티·Debug 유니티 4레그 오류 0, 래칫 0/0, 회귀 세트
  전체 통과(lifecycle 221 동일), `Invoke-ProfilingValidation` 통과
  (PROFILE_SELFTEST_OK=true — 이동한 코어의 실행 실증). **P1 완료 조건 중
  "UI를 링크하지 않는 Player에서도 코어가 빌드됨"이 닫혔다.**

> **2026-09-15 개정.** 아래 할 일·완료 조건을 재작성했다. 재활용 폐기(§0.5.3)와
> 매크로 폐기(§5.2)로 **항목 하나가 무의미해졌고**, Shipping 항목은 **대상이 이미
> 서 있음이 확인됐다**(§0.5.1).

#### P1b — 새 코어와 공통 frame clock (개정)

할 일:

- `ProfilerService` — 스트림을 서비스가 소유한다. 현행의 함수 지역
  `static thread_local`(모든 인스턴스가 스레드당 TLS 하나를 공유) 제약이 여기서 풀린다
- `MarkerRegistry` — 컴파일 타임 마커 ID 등록(§5.2). **매크로를 만들지 않는다**
- `ce::ProfileScope` RAII — 짝 불균형을 문법적으로 불가능하게
- **단일 `EngineFrameId` 발행 지점 통합.** 새로 만드는 일이 아니라 지금 서로 모르고
  도는 세 카운터를 묶는 일이다:

  | 후보 | 성질 | 판정 |
  |---|---|---|
  | `TimeSystem::m_frameCount`(atomic uint32) | Editor·Player 공통, 편집·재생 모두 증가, **`FixedTick` 호출부 0**이라 루프당 1회(9-15 재확인) | **정본** |
  | `Runtime::TickSimulationFrame` | 편집 모드는 앞에서 early return | 부적합 |
  | `LiveState::publishedFrameId`(atomic uint64) | RT 파이프라인 제출 단위 | `SubmissionId` 축 후보 |

- 호출부 39곳의 표기를 새 RAII로 교체(**이름과 자리는 보존**)
- `CE_DEVELOPMENT`를 물어 compile-out(§5.2 — 새 매크로 신설 금지)
- ImGui/D3D12 include 없는지 include boundary 검사 유지

~~기존 `PROFILE_CPU_*` 매크로를 새 API adapter로 연결~~ — **폐기.** 옛 코어에 adapter를
붙이는 일이 재활용 폐기로 의미를 잃었다. 매크로는 정의째 없어진다.

완료 조건:

- **이벤트 수가 보존된다 — 절대 숫자가 아니라 교체 전후 동수**. 이것이 "기존 호출부 대량
  수정 없이 새 코어로 이벤트가 들어감"을 재는 값이고, 마커 하나가 안 이어지면 하나 줄어든다.
  ★ **절대 숫자를 완료조건에 박으면 안 된다**(2026-09-20 실측으로 확인): §2.1 의 편집 27 /
  재생 34 는 임시 계측을 걷은 값이고, 9-15 에 넣은 임시 계측 14곳(`App.cpp` 3 · `EditorMain.cpp` 11)이
  붙어 있는 지금은 **편집 38 · 이름 573B** 다. 즉 이 축은 남의 계측이 드나들 때마다 움직인다 —
  단정은 같은 소스 상태에서 **교체 직전 값을 그 자리에서 떠서 교체 직후와 맞대는** 모양이어야 한다
- 동일 marker가 여러 스레드에서 하나의 안정된 ID 사용
- `CE_SHIPPING=1` 빌드에서 프로파일러 심볼 0 — `verify-player-shipping-isolation.ps1`에
  단정 추가(그 게이트는 이미 `run-all:299`에 있다)
- UI를 링크하지 않는 `Player`에서도 코어가 빌드됨 *(P1a에서 이미 닫힘)*
- ★ **프로파일링 검사를 찾을 수 있게 적는다.** 회귀 세트는 9-16 에 폐지됐다 — 편입할 곳이
  없다. 대신 `Tools/regression/README.md` 표와 이 계획서 §11 에 `Invoke-ProfilingValidation.ps1`
  을 이름으로 올린다. 지금 README 표에 없어서, 세트가 있던 시절과 똑같이 **안 모인다**.
  Stats 축이 9-06 부터 8일·115커밋 동안 죽어 있던 것을 아무도 몰랐던 이유가 그것이다

#### P1b 착수 전 선행 — 죽은 게이트 축 복구

P1 착수 **전에** 기준선을 세운다. 갈아엎은 뒤에는 "원래 34였다"를 증명할 자리가 없다.

- `Invoke-ProfilingValidation -Action Stats`가 JSON을 읽게 고친다(텍스트 복구가 아니다 —
  `reg.Result` 규약이 정본이고 이 저장소에 `ConvertFrom-Json` 게이트 13종의 선례가 있다)
- 단정은 **이벤트 수 두 개**(편집·재생)로 좁힌다. 씬을 추적되는 fixture로 고정한다.
  수는 그때 그 자리에서 뜬다 — 임시 계측이 드나들면 값이 움직이므로 박아 두지 않는다
  (2026-09-20: 임시 계측 14곳이 붙은 채로 편집 38 · 이름 573B · 누락 0 · 불균형 0 · 슬롯 1)
- 드롭·포화(`totalDroppedEvents`·`peakFrameEvents < eventCapacity`)는 **값만 기록하고
  이빨 없음을 주석에 남긴다** — §2.1이 보였듯 어떤 변이로도 자극되지 않는다
- 변이로 증명한다: 마커 하나를 빼서 값이 하나 줄어 붉어지는지, 그리고 그 변이가 실제로
  단정 자리를 지나는지까지

### P2 — CPU thread stream과 rolling capture

> **2026-09-15 우선순위 승격.** P2는 "나중에 정확도를 올리는 일"이 아니라 **다른 페이즈를
> 막고 있는 일**이다. PHASE 13이 애니메이션 워커 8스레드를 최적화하는데(§0.5.5),
> 그 워커에 마커를 걸려면 sealed chunk handoff가 먼저 있어야 한다 — 지금 코어에 워커
> 계측을 붙이면 §3.1의 원소 단위 경합이 그대로 열린다. **지금 안 터지는 유일한 이유가
> writer가 `[GameThread]` 하나뿐이기 때문**이다.

할 일:

- owner 수명이 명확한 `ThreadStream`
- sealed chunk handoff — writer가 자기 chunk를 봉인해 넘기고 collector는 남의 메모리를
  만지지 않는다. 현행 결함 8종 중 다섯이 **고쳐지는 것이 아니라 발생하지 않게** 된다
- cross-frame scope와 mid-capture scope 처리 — `truncated` flag(§6.1).
  **selftest의 `cross-frame/preserve`가 KNOWN-DEFECT에서 PASS로 바뀌는 것이 판정이다**
- 600프레임/128MiB rolling ring
- drop/overflow 진단
- `CaptureSession` freeze
- ★ **워커 스레드 등록** — 애니메이션 워커 8개, RenderThread, PresentationThread.
  enkiTS 이관(PHASE 13 S0.5) 이후라면 `threadnum_`(0..`GetNumTaskThreads()-1` 보장)을
  슬롯 키로 쓴다 — 현행이 발명해야 했던 11비트 슬롯 은퇴·재사용 규칙이 불필요해진다

완료 조건:

- 8개 이상 writer thread stress에서 충돌·손상 없음
- 프레임 경계를 넘는 scope의 duration이 정확함(`cross-frame/preserve` PASS)
- thread 생성/종료 후 dangling TLS 접근 없음
- pool 고갈 시 정지하지 않고 dropped count가 정확히 증가
- ASan 가능 구성 또는 동등한 메모리 검증에서 오류 없음
- ★ **애니메이션 워커의 시간이 캡처에 나타난다** — PHASE 13 §4의 완료 기준("예측 비용
  대 실측 오차 15% 이내")을 판정할 수단이 이것이다

### P3 — CPU 중심 에디터 녹화 MVP

할 일:

- 새 `EngineGUIWindow/ProfilerWindow`
- Record/Pause/Clear/Live Follow
- Frame Overview
- 기존 CPU Timeline renderer 이관
- 단일/다중 frame selection
- Hierarchy/Flat의 Total/Self/Calls/Avg/Max
- capture memory/dropped event 표시

완료 조건:

- 10초 녹화 후 과거 프레임 선택 가능
- pause 후 engine이 계속 실행돼도 선택 데이터가 변하지 않음
- Timeline 합계와 Hierarchy inclusive time이 허용 오차 내 일치
- 창을 닫아도 recording 상태가 유지되거나 명시적으로 설정한 정책대로 동작
- profiler 창 focus 밖에서 Space가 편집기 동작을 가로채지 않음

### P4 — GPU frame token과 CPU/GPU clock 정렬

할 일:

- `DX12GpuProfiler` record를 frame ring별로 분리
- `GpuFrameToken` 도입
- DisplaySlot/pendingQueue에 token 연결
- `Collect(token)`으로 API 교체
- raw pass interval과 queue span 보존
- clock calibration과 `SubmissionId`, view 정보 기록
- GPU Timeline 추가

검증 장면:

- 가벼운/무거운 GPU 패스를 프레임마다 번갈아 실행
- 인플라이트 0/1/2 각각 측정
- 씬뷰 단독, 게임뷰 단독, 두 뷰 동시
- 리사이즈와 pipeline rebuild 직전·직후

완료 조건:

- heavy/light 패턴이 올바른 `EngineFrameId`와 `SubmissionId`에 교대로 매핑
- 2-in-flight와 멀티카메라에서 이전/최신 query record 혼동 없음
- fence 미완료 slot을 읽지 않음
- GPU query overflow와 collect 실패가 캡처 diagnostics에 남음
- D3D12 debug layer/DRED 메시지 0건

### P5 — Counter provider와 관리 marker

할 일:

- `CounterRegistry`와 category/module mask
- Resource Counter의 데이터를 owner-published counter로 이동
- Upload/Descriptor/VRAM/Draw/Batch counter 연결
- `ScriptCore.Diagnostics.ProfilerMarker`와 native bridge
- GC 수치를 frame counter로 연결
- provider별 수집 비용 측정

완료 조건:

- UI polling이 엔진 container mutex를 직접 잡지 않음
- GC 발생 프레임과 CPU script marker가 같은 frame에 보임
- upload/descriptor overflow가 발생한 정확한 frame 식별
- 비활성 module이 불필요한 비싼 census를 호출하지 않음

### P6 — 저장·불러오기

할 일:

- `.ceprof` header/chunk/index 구현
- frozen capture 비동기 저장
- 파일 열기와 lazy frame access
- schema version과 CRC 오류 UI
- 캡처 metadata 표시

완료 조건:

- 600프레임 round-trip 후 frame/marker/thread/event/counter 수 동일
- 선택한 대표 marker의 Total/Self/Calls 동일
- 중간 절단 파일과 CRC 오류를 crash 없이 거절
- 저장 중 새 recording을 시작해도 저장 대상이 변하지 않음

### P7 — 자동 trigger와 외부 캡처 연결

할 일:

- CPU/GPU/GC/marker 조건 trigger
- 조건 전후 프레임 보존(pre-trigger/post-trigger)
- 기존 DX12 validation/PIX launch 흐름과 연결
- 선택적 ETW TraceLogging provider/export
- 외부 캡처 경로와 `.ceprof` metadata를 상호 참조

완료 조건:

- `GPU frame > threshold` 재현에서 내부 캡처가 자동 freeze
- 요청한 다음 GPU 프레임을 지원되는 PIX 경로로 capture
- PIX 사용 불가 환경에서는 내부 캡처만 보존하고 원인을 명확히 표시
- profiler marker 추가가 기존 PIX command stream을 손상하지 않음

---

## 11. 검증 매트릭스

### 11.1 CPU 정확성

| 사례 | 기대 |
|---|---|
| 3단 nested scope | parent/depth/Total/Self 정확 |
| sibling scope | 순서와 self time 정확 |
| frame을 넘는 scope | 시작·끝 보존, 관련 frame 표시 |
| recording 중간 시작 | 열린 scope를 truncated로 표시 |
| pause 중간 요청 | 다음 frame 경계에서 freeze |
| thread 생성/종료 | ThreadBegin/End와 stream 안전 회수 |
| chunk pool 고갈 | deadlock 없이 dropped count 증가 |
| 같은 timestamp | thread sequence로 안정 순서 |

### 11.2 GPU 정확성

| 사례 | 기대 |
|---|---|
| 1 submission | pass raw interval과 queue span 일치 |
| 2 in-flight | token별 query record 분리 |
| 두 카메라 | 같은 EngineFrameId, 서로 다른 SubmissionId/view |
| pass slice | raw slice 보존, UI aggregate 가능 |
| resize/rebuild | 이전 pipeline token 완료 후 안전 폐기 |
| query overflow | 일부 누락을 숨기지 않고 diagnostics 표시 |

### 11.3 파일

- empty capture
- 1 frame
- 600 frames
- dynamic thread/marker가 많은 캡처
- 손상 header/chunk/index
- 이전 schema version
- Unicode marker와 파일 경로

### 11.4 오버헤드

다음 네 모드를 같은 장면·해상도·프레임 수로 비교한다.

1. profiler compile-out (`CE_SHIPPING=1`)
2. compiled but stopped
3. CPU marker recording
4. CPU+GPU+all counters recording

기록값:

- median/P95/P99 CPU frame · GPU frame
- profiler service 자체 CPU 시간
- events/sec와 bytes/sec
- dropped events · peak capture memory

허용 예산은 P0 실측 후 확정한다. 숫자를 먼저 정해 통과시키기 위해 데이터를 줄이지 않는다.

> ★ **2026-09-15 경고 — 예산 소진을 게이트 단정으로 쓰지 말 것.** §2.1 실측이
> 편집 27 / 재생 34 이벤트(상한 1024의 3%), 이름 388B / 525B(상한 16384B)를 보였고
> 둘 다 `peak == last`였다. 계측 지점이 전부 시스템 단위 고정 지점이라 **씬에 무엇을
> 얹어도 상수**이고, 1024에 닿으려면 계측 지점이 30배 늘어야 한다. 드롭·포화 단정은
> 어떤 변이로도 자극되지 않는 **빈 단정**이다 — 달더라도 값만 기록하고 이빨이 없음을
> 명시한다. 이 절이 재야 할 것은 예산이 아니라 **네 모드의 프레임 시간 차이**다.
>
> 그리고 이 절은 **P2 이후에야 의미가 생긴다.** 지금은 워커 8스레드가 계측 밖이라
> 모드 3·4가 재는 것이 반쪽이다(§2.1).

---

## 12. 실패 정책과 관측 가능성

프로파일러가 게임을 멈추거나 원래 오류를 가려서는 안 된다.

- writer buffer 부족: drop하고 counter 증가
- GPU query 부족: 해당 pass를 누락 표시, frame 전체를 정상으로 가장하지 않음
- 파일 저장 실패: frozen capture 유지
- reader schema 오류: 파일을 거절하고 이유 표시
- marker Begin/End 불균형: thread와 marker stack을 diagnostics에 기록
- clock calibration 실패: CPU/GPU 통합 축을 비활성화하고 queue 상대시간은 유지
- profiler 내부 예외: recording을 안전 정지하고 엔진 실행은 유지

에디터의 Diagnostics 패널에 최소 다음을 항상 표시한다.

- recording state
- retained frame range
- capture memory/budget
- CPU dropped events
- GPU dropped queries/collect failures
- malformed scopes
- active thread streams
- clock calibration 상태
- profiler 자체 frame cost

---

## 13. 구현 중 지켜야 할 결정

1. **UI보다 frame identity가 먼저다.** 잘못 매핑된 예쁜 그래프는 없는 것보다 위험하다.
2. **원본 interval을 버리지 않는다.** 합계·정렬·병합은 reader의 일이다.
3. **문자열은 marker 등록 시 한 번만 처리한다.** hot path에서 복사·해시·allocation 금지.
4. **producer를 collector가 직접 초기화하지 않는다.** sealed ownership만 넘긴다.
5. **멀티카메라를 기본 조건으로 본다.** EngineFrame과 GPU submission을 1:1로 가정하지 않는다.
6. **Resource Counter를 없애지 않는다.** provider가 준비되는 동안 기존 창은 비교 기준으로 유지한다.
7. **Deep Profile은 별도 모드다.** 기본 marker capture의 성능 계약을 깨지 않는다.
8. **PIX/RenderDoc은 대체재가 아니라 후속 정밀 도구다.** 내부 profiler가 여러 프레임에서 조건을 찾는다.
9. **Player를 함께 검증한다.** 수집 코어는 에디터에 종속되지 않는다.
10. **각 단계에 재현 명령과 성공 marker를 남긴다.** 화면만 보고 완료 판정하지 않는다.

---

## 14. 최종 완료 조건

다음 전부가 성립해야 계획 완료다.

- [ ] Editor/Development에서 Record/Pause/Clear 가능
- [ ] 최소 600프레임 또는 정한 메모리 예산만큼 rolling capture — **4프레임 링 폐기**(§2.2)
- [ ] 멀티스레드 CPU Timeline과 선택 구간 Hierarchy
- [ ] ★ **애니메이션 워커 8스레드·RenderThread·PresentationThread가 캡처에 나타남**
      (§2.1 — 지금은 `[GameThread]` 하나뿐이라 렌더·애니메이션 경로가 통째로 안 보인다)
- [ ] 멀티카메라·2-in-flight에서도 정확한 GPU frame/submission 매핑
- [ ] CPU/GPU/Rendering/Memory/GC counter가 같은 EngineFrameId에 정렬
- [ ] overflow·누락·malformed scope·profiler overhead 표시
- [ ] `.ceprof` 저장/불러오기 round-trip 검증
- [ ] profiler UI가 닫혀도 Development Player capture 가능
- [ ] `CE_SHIPPING=1`에서 프로파일러 심볼 0 — `verify-player-shipping-isolation.ps1` 단정
- [ ] CreatorEngine.sln의 Academy_4Q + Player 빌드 통과
- [ ] CPU/GPU selftest와 장시간 stress 통과
- [ ] ★ **selftest `cross-frame/preserve`가 KNOWN-DEFECT에서 PASS로** — 이 한 줄이
      수집 코어 교체의 진척 정의다
- [ ] ★ **프로파일링 검사가 README 표와 §11 에 이름으로 올라 있다** — 회귀 세트는
      폐지됐고(9-16), 찾을 수 없는 검사는 다음 세션에 모이지 않는다
- [ ] DX12 debug layer/DRED 회귀 없음
- [ ] 선택 조건에서 지원되는 PIX 다음 프레임 캡처 가능

**매크로 잔존 0**(§5.2) — `PROFILE_CPU_BEGIN`·`PROFILE_CPU_END`·`PROFILER_INITIALIZE`
등 옛 매크로가 정의·사용 모두에서 사라져야 한다. 소스 전수 검사로 판정하고, 주석·이력은
제외한다.

이 조건을 닫은 뒤에 Flame Graph, 두 캡처 비교, 원격 플레이어 연결, 자동 성능 회귀 게이트를
후속 계획으로 분리한다.

---

## 15. 참고 기준

- Unity Profiler 운용 원칙: 개발 빌드/플레이 모드/에디터 프로파일링 분리, Deep Profiling의
  높은 오버헤드, Live 갱신 중지 후 분석 권장
  <https://docs.unity3d.com/2022.2/Documentation/Manual/profiler-profiling-applications.html>
- Unity `ProfilerMarker`: marker handle 사용과 비개발 빌드 compile-out
  <https://docs.unity3d.com/cn/6000.0/ScriptReference/Unity.Profiling.ProfilerMarker.html>
- D3D12 CPU/GPU clock calibration
  <https://learn.microsoft.com/windows/win32/api/d3d12/nf-d3d12-id3d12commandqueue-getclockcalibration>
- Windows TraceLogging/ETW
  <https://learn.microsoft.com/windows/win32/tracelogging/trace-logging-reference>
- CreatorEngine 기존 DX12 검증 진입점
  `Tools/dx12-validation/Invoke-DX12Validation.ps1`
- 스케줄러 이관과 순서 제약: `docs/plans/TaskSchedulerUnificationPlan.md`(PHASE 13 S0.5·S6)
- 이 계획의 소비자: `docs/plans/AnimationSchedulerPlan.md` §4 완료 기준(버짓·강등 관측)
