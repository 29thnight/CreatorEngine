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
`capture_session` 하나에 `engine_frame_id`와 `submission_id`로 묶어 보존하는 **수집 코어**를
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

실행 표(`{marker_id, 함수 포인터}` 순회)로 자동화하는 길은 있으나, 그것은 씬 실행
구조 변경이고 `lifecycle 221사건` 순서 게이트를 지나간다. **이 계획은 그것을 기다리지
않는다** — 마커 표기를 호출부 형태에 독립으로 설계하면, 나중에 표가 생겨도

```cpp
for (auto& step : table) { ce::profile_scope _{ step.marker }; step.fn(*this, dt); }
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
  adapter를 붙이는 일이 재활용 폐기로 무의미해졌다. P1에 남는 것은 `engine_frame_id`
  단일 발행 지점 통합과 마커 표기 신설이다.
- **P1의 "Shipping compile-out"은 만드는 일이 아니라 무는 일이다** — `CE_DEVELOPMENT`가
  이미 전 프로젝트에 정의돼 있다(§5.2).
- **P2가 앞당겨진다.** 워커 계측이 PHASE 13의 전제이므로 sealed chunk handoff는
  "나중에 정확도를 올리는 일"이 아니라 **다른 페이즈를 막고 있는 일**이다.

### 0.5.15 2026-09-21 P2 동시성 경계 — 자극을 세우자마자 죽었다

**외부 감사가 정적으로 짚은 것을 자극으로 확인했다.** `publish_frame()` 과
`pause()` 가 등록된 **모든** 스트림의 현재 청크를 직접 봉인하는데, 이벤트를
적는 쪽은 그 잠금을 잡지 않는다. 설계는 "writer 는 자기 스트림만, 수집기는
봉인된 것만" 인데 구현이 그렇지 않았다.

**기존 검사가 이 경계를 한 번도 자극하지 않았다.** `test_multithread_stress`
는 워커를 전부 `join()` 한 **뒤에** 수집한다. 그래서 "적는 동안 거둔다" 가
한 번도 일어나지 않았고, 8 워커 검사가 내내 초록이었다.

**자극을 세우자 바로 죽었다.**

| 자극 | 결과 |
|---|---|
| 워커 4 × 20,000 스코프, 그동안 수집기가 `publish_frame` 반복 | **ACCESS_VIOLATION (0xC0000005)** |
| Debug · Release | 둘 다, 세 번 다 |

이벤트를 잃는 정도가 아니라 프로세스가 죽는다. `seal_current()` 가 `m_writer`
를 비우고 청크를 풀에 되돌리는 동안 주인이 그 포인터로 쓰고 있고, 되돌아간
청크가 다른 스레드에 다시 나가면 두 writer 가 한 저장소를 만진다.

**고친 것 셋.**

| 것 | 뜻 |
|---|---|
| `thread_stream::request_seal()` | 수집기는 **요청만** 올린다. 청크 포인터를 만지지 않는다 |
| `honor_seal_request()` | 주인이 `write()` 첫머리 — 청크를 만지기 전 — 에서 봉인한다 |
| `m_droppedEvents` 등 원자화 | 적는 것은 주인, 읽는 것은 수집기·요약이라 평범한 정수면 그 자체로 경합 |

`pause()` 도 같은 규칙이고, 봉인 응답을 **짧게 기다린 뒤 응답하지 않은 스트림을
센다**(`live_summary::pause_unacked_streams`). 잠든 워커는 다음에 깨어날 때
봉인하므로 그때까지의 꼬리가 그 캡처에 없을 수 있다.

**★ 변이가 단정보다 먼저 프로세스를 죽인다.** 그래서 하네스에 `AllowCrash` 를
두고 **충돌 자체를 붉음으로** 읽게 했다. 모든 변이에 열어 두면 "엉뚱한 데서
죽었다" 가 통과로 읽히므로, 적어 둔 변이에만 허용한다.

| 변이 | 잡은 방식 |
|---|---|
| `collector-seals-others`(남의 스트림을 직접 봉인) | **충돌**(exit -1073741819) |

**남은 것(이 조각에서 고치지 않았다).** 감사가 사본에서 재현한 넷이다.

1. **Pause 완료 보장** — 미응답을 세지만 어디에도 드러내지 않고 그대로 frozen
   으로 끝난다. 세는 것과 판정하는 것은 다르다.
2. **늦게 온 CPU 이벤트의 프레임 귀속** — ★ 이 수정이 만든 노출이다. writer 가
   자기 일정으로 봉인하게 되면서 CPU 이벤트도 늦게 도착할 수 있다. GPU 는
   `event.frame`(제출 프레임)으로 귀속하는 것이 맞지만(§5.4), 늦게 온 CPU
   구간은 **끝난 시각이 속한 프레임**으로 가야 한다 — `frame_record` 가 이미
   틱 경계를 들고 있다.
3. **Clear 세대 분리** — 세대가 없어 Clear 이전 청크가 재유입된다.
4. **Pause 시점의 열린 스코프** — `truncated_end` 로 남지 않고 누락된다.

정적으로만 짚은 셋(단일 collector · 불변 요약 · 종료 규약)은 그다음이다.

### 0.5.14 2026-09-21 P4 GPU 귀속 — 늦게 온 것을 제 프레임 칸으로 돌려보냈다

**먼저 쟀다. 제출→수집이 최대 50~64 ms 다** — 60 Hz 로 세 프레임이 넘는다.
`capture_ring` 은 이벤트를 **수집한 프레임**의 기록에 담으므로, 그대로 넣으면
GPU 일이 세 칸 뒤에 그려진다. "UI 보다 frame identity 가 먼저다"(§13.1)를
정면으로 어기는 자리라, 이 수 하나가 조각의 설계를 정했다.

**선 것 다섯.**

| 것 | 뜻 |
|---|---|
| `event_flags::gpu_span` · `event_chunk::late_ingest` | 늦게 오는 것임을 청크 단위로 가른다 |
| `thread_stream::write_span` | 이미 끝난 구간을 스코프 스택 없이 적는다 |
| `profiler_service::submit_gpu_span` | 전용 `[GPU Graphics]` 레인. **적는 쪽만** 봉인한다 |
| `capture_ring::place_late_span` | 제 프레임 칸을 찾아 넣고, 아직이면 기다리고, 없으면 **센다** |
| `intern_runtime_marker` | 런타임 이름을 **표가 소유한다**(정적 경로는 리터럴만 가리킨다) |

**경계는 P2 가 정해 둔 그대로다.** RenderEngine 은 프로파일러를 모른다 —
`EnhancedLiveGpuSpanSink` 로 함수를 받아 두고 부르기만 하고, 거는 쪽은
`EngineBootstrap` 이다. `thread_pool::worker_hooks` 와 같은 뒤집기다.

**★ 레인은 남이 봉인하면 안 된다.** `publish_frame` 은 등록된 모든 스트림의
청크를 봉인하는데, 그 루프를 도는 것은 게임 스레드이고 GPU 레인에 적는 것은
렌더 스레드다. 남이 봉인하면 두 스레드가 같은 청크 포인터를 만진다. 그래서
`stream_entry::self_sealed` 를 두고 그 루프가 이 레인을 건너뛴다.

**실측(Debug · 씬뷰·게임뷰 둘 활성 · 120 프레임 녹화).**

| 항목 | 값 |
|---|---|
| EngineDiagnostics 로 흘린 조각 | 900 ~ 1,206 |
| 캡처의 `[GPU Graphics]` 레인 | 5~6 칸 · 261~303 건 |
| **제 프레임 칸을 벗어난 구간** | **0** |

**변이 다섯이 각각 제 단정에서 붉어진다.** 코어 셋은 엔진을 띄우지 않고 돈다.

| 변이 | 잡은 단정 |
|---|---|
| `gpu-span-to-pending`(늦은 것을 수집한 프레임에) | `gpu-lane/` (코어) |
| `gpu-span-no-defer`(닫히기 전 것을 버림) | `gpu-deferred/placed` (코어) |
| `gpu-span-silent-drop`(버린 것을 안 셈) | `gpu-dropped/counted` (코어) |
| `sink-never-installed` | 흘린 조각 0 · 레인 없음 · 구간 0 (라이브) |
| `late-span-to-pending` | 제 프레임 칸을 벗어난 구간 **456/456** (라이브) |

마지막 둘이 라이브 축의 단정을 증명한다. 특히 skew 단정은 **임계값이 아니라
동치**다 — 이벤트가 담긴 칸과 이벤트가 들고 온 라벨이 같아야 한다.

**★ 남은 물음 하나(이 조각에서 고치지 않았다).** `publish_frame` 이 **다른
스레드의** 스트림을 봉인하는 것은 GPU 레인 말고도 워커 스트림 전부에 해당한다.
`thread_stream::seal_current` 가 `m_writer` 를 비우는 동안 그 워커가 `write()`
안에 있을 수 있다. GPU 레인은 `self_sealed` 로 비껴 두었지만 워커 쪽은 그대로다.
**아직 재지 않았다** — 코어 프로브에 "한 스레드가 계속 적는 동안 다른 스레드가
publish_frame 을 도는" 자극을 세워 잃거나 망가지는지 보는 것이 다음 실험이다.

### 0.5.13 2026-09-21 P4 clock calibration — 판정을 임계값이 아니라 인과에 걸었다

**§5.1 의 "CPU/GPU 상관: `GetClockCalibration`" 을 세웠다.** 그때까지 GPU
timestamp 는 큐 상대시간뿐이었고, "이 패스가 저 프레임의 어느 지점에서 돌았나"
를 물을 수단이 없었다. 두 시계가 다행히 같은 축을 쓴다 —
`profiler_service::now()` 도 `GetClockCalibration` 의 CPU 값도 QPC 원시 값이다.

**선 것 넷.**

| 것 | 뜻 |
|---|---|
| `ClockCalibration` | 한 순간에 함께 읽은 (GPU 틱, CPU 틱) 표본과 두 주파수 |
| `GpuTickToCpuTick` | 몫과 나머지를 따로 옮기는 정수 변환(넘침도 절삭도 없다) |
| `GpuFrameToken::cpuSubmitTick` | 제출을 연 순간의 CPU 시각 — 검산의 자 |
| `RefreshClockCalibrationIfStale` | 1 초 간격 재표본. 낡는 것을 자기 자리에서 안다 |

**★ 판정을 임계값이 아니라 인과에 걸었다.** 변환한 GPU 구간은 **CPU 가 제출을
연 뒤에 시작해서 수집하기 전에 끝나야** 한다. 하드웨어가 달라도 그대로 서는
관계이고, "몇 ms 안쪽" 같은 수를 고르면 그 수가 곧 거짓말의 여유가 된다.
장부는 `gpu.{clockValid, clockSamples, unalignedCollects, alignmentViolations,
minSubmitToBeginMs, minEndToCollectMs}`.

**실측(Debug · 씬뷰·게임뷰 둘 활성).**

| 항목 | 값 |
|---|---|
| GPU timestamp 주파수 | 1,000,000,000 Hz |
| CPU(QPC) 주파수 | 10,000,000 Hz |
| 제출 → 변환한 GPU 시작 (최소) | 2.67 ~ 3.90 ms |
| 변환한 GPU 끝 → 수집 (최소) | 0.65 ~ 2.19 ms |
| 정렬 위반 | **0 / 13~18** |

**★ 재표본은 장식이 아니다.** 표본 간 어긋남이 약 1 초에 **0.05 ms** 다(첫
구간은 예열을 품어 ~1.15 ms). 여유가 0.65~3.90 ms 이므로, 재표본이 없으면
십여 초 만에 변환한 GPU 구간이 제출보다 앞서게 되고 GPU 트랙이 제 프레임을
벗어난다. 그래서 "표본이 둘 이상인가" 를 따로 단정한다 — 이것도 임계값이
아니라 "다시 뜨는가" 다.

**변이 넷이 각각 제 단정에서 붉어진다.**

| 변이 | 잡은 단정 |
|---|---|
| `calibration-disabled` | 표본이 없다 — 통합 축이 꺼져 있다 |
| `convert-with-cpu-frequency`(GPU 주파수 자리에 CPU 주파수) | 정렬 위반 13/13 |
| `origin-shift-10ms`(원점을 10 ms 앞으로) | 정렬 위반 13/15 |
| `never-resample` | 표본 1 회 — 주기적 재표본이 돌지 않았다 |

세 번째가 이 게이트의 분해능을 말한다 — **한 프레임(16.7 ms)보다 작은 10 ms
어긋남을 잡는다.** 네 번째는 그 실행에서 정렬 위반이 0 이었다 — 짧은 실행에서는
죽은 재표본이 정렬 단정만으로는 드러나지 않는다.

### 0.5.12 2026-09-21 P4 raw 구간 보존 — 정상인 것을 결함으로 세고 있었다

**§3.3 의 "같은 이름의 slice 는 표시 단계에서 묶되 원본 interval 은 버리지
않는다" 와 §3.4 의 "GPU 합계는 단순 합만으로 정의하지 않는다" 를 세웠다.**
`Collect()` 가 이름으로 묶으며 원본 구간을 버리고 있었고, 그래서 GPU 타임라인을
그릴 수가 없었으며 조각이 서로 겹치는지도 물을 수 없었다.

**가른 것 셋.**

| 이름 | 뜻 |
|---|---|
| `PassSlice` | 조각 하나의 raw 구간 — 기록 순서 그대로, 묶기 **전**의 것 |
| queue span | 첫 timestamp 부터 마지막까지 — 그 제출이 큐를 잡고 있던 길이 |
| busy | 겹치지 않는 실행 구간의 합 — 실제로 일한 길이 |

이름 묶기는 `MergeSlices()` 로 떼어냈다. 표시용이라는 것을 이름이 말하게 했다.

**실측(Debug · 씬뷰·게임뷰 둘 활성).** 조각 36~39 개가 이름 26~29 개로 묶인다 —
분할 패스가 실제로 여러 조각으로 남아 있다는 뜻이고, 자가 검증의 출력이
`PostChain.BloomDown(x4)` 로 그것을 그대로 보여 준다. 길이는 늘
`이름합 ≥ queueSpan ≥ busy` 로 서고, queueSpan 과 busy 의 차(0.004~0.012 ms)가
제출 안의 공백이다.

**★ 게이트를 처음 걸자마자 "버린 조각 1" 로 붉어졌고, 그것이 결함이 아니었다.**
버림 판정을 `end <= begin` 으로 적어 **뒤집힌 것**과 **길이가 0 인 것**을 한데
묶었다. 선이 없는 `GizmoLine` 은 그릴 것이 없어 일찍 빠져나가므로 두 timestamp 가
같은 틱에 찍힌다 — 정상이다. 그것을 결함으로 세고, 게다가 그 조각을 **버려서**
타임라인에서 그 패스가 통째로 사라지게 하고 있었다. `end < begin` 만 버리고,
길이 0 은 조각으로 남기되 `zeroLengthSlices` 로 따로 센다. 조각 수가 38 에서
39 로 늘어난 것이 그 한 조각이 되살아난 자리다.

**★ 마지막 한 번만 읽는 게이트는 뷰를 고를 수 없다.** 같은 코드로 초록과 붉음이
번갈아 나왔다. `lastGpuSpan` 은 마지막으로 수집에 성공한 **한 제출**의 것이고,
씬뷰와 게임뷰 중 어느 쪽인지는 실행마다 갈린다. 한쪽에만 있는 패스(`GizmoLine`
은 씬뷰에만 있다)는 그래서 절반의 확률로만 검사를 받았다. 검산을 **수집하는
자리**로 옮겨 수로 누적했다 — `gpu.{droppedTotal, zeroLengthTotal,
spanViolations, sliceUnderflows}`. 판정은 마지막 값이 아니라 이 수에 건다. 길이
0 이 수집 12~17 회에 6~12 건으로 꾸준히 잡히므로 두 뷰가 모두 재어지고 있다.

**변이 셋이 각각 제 단정에서 붉어진다.**

| 변이 | 잡은 단정 |
|---|---|
| `zero-length-as-dropped`(`end <= begin` 복원) | 뒤집힌 조각 누적 7 |
| `queue-span-first-slice`(span 끝을 첫 조각에서) | busy > queueSpan 17/17 |
| `slice-count-from-merge`(조각 수를 이름 수로) | raw 조각 26 <= 이름 26 |

첫 변이는 누적 장부가 없었다면 잡히지 않았을 것이다 — 그 실행의 마지막 표본은
view 2 였고 거기에는 길이 0 인 조각이 없었다.

### 0.5.11 2026-09-21 P4 첫 조각 — GpuFrameToken. 83% → 0

**고친 것은 한 줄이다.** 질의 힙·리드백은 이미 슬롯별로 갈라져 있었고
**CPU 쪽 기록만 한 벌**이었다. `m_records` · `m_usedPasses` 를 슬롯별로 나누고,
수집 키를 "지금 기록 중인 슬롯" 에서 **표**로 바꿨다.

- `GpuFrameToken`(`RHI/IRHIGpuProfiler.h`) — `engineFrameId` · `submissionId` ·
  `fenceValue` · `renderViewId` · `ringSlot` · `queueId`. 엔진 프레임과 제출을 가른다(§3.4).
- `BeginFrame(engineFrameId, submissionId, renderViewId)` 가 **표를 돌려준다.** 링
  산술이 한 자리에만 있어야 부르는 쪽과 읽는 쪽이 갈라지지 않는다.
- `ResolveFrame(list, token)` · `Collect(token, ...)` — 둘 다 표를 받는다.
- `DisplaySlot` 이 그 표를 보관했다가 펜스 완료 때 그대로 넘긴다.

**★ 조용한 것을 시끄럽게 바꿨다.** 표가 낡았으면 `Collect` 는 그럴듯한 숫자를
내는 대신 **실패한다.** "숫자가 틀렸다" 보다 "링이 모자란다" 가 훨씬 고치기 쉬운
신호이고, 무엇보다 **센다.**

**실측.**

| | 수집 | 거절 | 패스 | 귀속 |
|---|---|---|---|---|
| 착수 전 | 12 | — (10 이 남의 것) | 26·29 로 오감 | 없음 |
| 착지 후 | 15 | **0** | 29 | frame 3411 · submission 16 · view 1 |

합계도 5.04 ms → 1.47 ms 로 내려갔다. 줄어든 것이 아니라 **두 뷰의 패스가
섞여 더해져 있던 것이 한 제출의 실시간으로 바뀜 것**이다.

**게이트.** `Invoke-ProfilingValidation.ps1 -Action Gpu` — 라이브를 예열하고 `dx12.live`
의 `gpu` 장부를 읽어 `mismatches == 0` · `collects > 0` · `passCount > 0` · `frame > 0`
을 물는다.

**★ 변이가 어느 절에 잡혔는지까지 못 박았다.** 가드가 둘이기 때문이다.

| 변이 | 결과 | 잡은 절 |
|---|---|---|
| `records-share-one-slot`(제출마다 슬롯 0) | 거절 10/14 · 붉음 | 신선도 검사 |
| `records-from-recording-slot`(기록을 남의 슬롯에서) | 거절 11/13 · 붉음 | 슬롯 소유 검산 |
| `stale-token-accepted`(신선도 검사 제거) | 통과 | — |
| 위 둘을 **함께** 걷음 | 통과 | — |

마지막 줄이 판정이다 — 첫 변이를 잡은 것은 신선도 검사였다. 그리고 신선도
검사를 **혼자** 걷을 때 통과하는 것은 눈멀어서가 아니라, 링 3 · 인플라이트 2 에서는
**그 절이 막을 조건이 일어나지 않기 때문**이다. 자극할 수 없는 절은 자를 열어야
서고, 여기서는 링을 좀히는 변이가 그 자를 열어 준다.

**아직 하지 않은 것.** raw pass interval · queue span 보존, clock calibration,
EngineDiagnostics 로의 귀속, GPU Timeline. 이번 조각은 **수치가 올바른 제출의
것이 되게 하는 것**까지다 — 그것이 서야 나머지가 의미를 갖는다.

### 0.5.10 2026-09-21 P4 착수 전 실측 — 수집의 83% 가 남의 제출을 읽고 있었다

**먼저 재야 한다.** §2.3 의 GPU 실측은 9-15 것이고, 그 뒤로 이 경로를 재는
게이트는 **하나도 없었다.** 패스별 GPU 시간은 렌더 디버그 창에만 있었고, 그
숫자가 올바른 제출의 것인지를 물을 수단은 없었다 — 눈으로는 틀린 숫자도
그럴듯하다.

**소스 확인(9-21). §3.3 이 적은 그대로였고, 더 좋아지지도 나빠지지도 않았다.**

| 항목 | 상태 |
|---|---|
| `DX12GpuProfiler::m_records` · `m_usedPasses` · `m_frameIndex` | 한 벌 — 링이 아니다 |
| 질의 힙·리드백 구역 | 이미 링 슬롯별로 갈라져 있다(`m_frameIndex * maxPasses * 2`) |
| `BeginProfilerFrame` | `EnhancedSceneRenderer.cpp` 의 **뷰마다** 도는 루프 안 |
| `Collect()` | token 을 받지 않고 호출 시점의 `m_frameIndex` 를 읽는다 |
| 소비 | `TickLive` 의 펜스 완료 루프(제출당 1회) — 죽어 있지 않다 |

즉 **GPU 쪽 자료는 이미 슬롯별로 갈라져 있고, CPU 쪽 기록만 한 벌이다.** 그것이
결함을 한 문장으로 적은 것이다.

**재는 자를 세웠다.** 제출할 때 그 제출이 쓴 링 슬롯을 `DisplaySlot` 에 적어
두고, 수집 직전에 `DX12GpuProfiler::CurrentRingSlot()` 과 맞대어 **어긋난 횟수를
센다.** 장부는 `dx12.live` 의 `gpu.{ms,collects,mismatches,passCount,passes}` 로 나온다.

**실측(Debug · 씬뷰·게임뷰 둘 활성 · `render.live.wait` 로 예열).**

| 표본 | 수집 | 어긋남 | 비율 |
|---|---|---|---|
| 예열 직후 | 2 | 2 | 100% |
| 중간 | 9 | 7 | 77.8% |
| 1500 프레임 | 12 | 10 | **83.3%** |

같은 실행에서 `passCount` 가 26 · 29 로 오갔다. 패스 구성이 바뀐 것이 아니라
**다른 뷰의 기록을 읽은 것**이다. 렌더 디버그 창이 지금까지 보여 준 패스별
숫자의 절대다수가 그런 것이고, 눈으로는 가릴 수 없었다.

**★ 결에 드러난 것 하나 더 — Player 가 빌드되지 않고 있었다.** 솔루션 전체를
빌드해 보니 `EngineBootstrap.h` 가 `ProfileScope.h` 를 include 하는데 `Player.vcxproj`
의 include 경로에 `Engine\EngineDiagnostics\` 가 없어 C1083 이었다. P2 에서 워커
훅을 거기 달면서 생긴 썩음이고(`f493c7eb`), 그 뒤로 누구도 솔루션을 통째로
빌드하지 않아 하루도 안 보였다. 경로를 더해 고쳤다 — `/t:Editor\CreatorEditor`
만 돌리는 습관이 이런 것을 덮는다.

### 0.5.9 2026-09-21 P3 착지 — 접는 일을 그리는 층에서 떼어냈다

**설계 판단 하나가 P3 의 모양을 정했다.** P3 가 만드는 것(Frame Overview ·
Timeline · Hierarchy/Flat)은 전부 "얼린 캡처를 어떻게 접는가" 이고, 접는 일에는
ImGui 가 한 줄도 필요 없다. 그리는 층에 두면 완료조건을 잴 수단이 화면뿐이
된다 — P1·P2 에서 두 번 겪은 자리다. 그래서 접는 일과 선택 상태를 코어에 세우고
창은 그것들의 reader 로 두었다. 완료조건 다섯 중 셋이 화면 없이 판정된다.

**선 것 셋.**

- `ProfileAggregate` — `aggregate_frames(capture, first, last)` 가
  `thread_summary`(레인 · root_ticks) · `hierarchy`(전위 순서) · `flat`(total
  내림차순)을 낸다. 각 줄은 total(inclusive) · self(exclusive) · calls · max 와
  잘린 구간 표시를 든다.
- `ProfileReader` — §6.4 가 말한 "reader 쪽 별도 상태". 얼린 캡처와 선택 범위를
  들고 집계를 캐시한다.
- `ImGuiHelper/Profiler*` — 창. 툴바 · Frame Overview · Hierarchy/Flat/Threads 표.
  **이 층에는 합계 산술이 없다.**

**★ 가장 틀리기 쉬운 자리는 트리 복원이었다.** 이벤트는 `end_scope` 에서
기록되므로 **끝난 순서**로 들어가고 자식이 부모보다 먼저 있다. 같은 스레드
안에서 (시작 tick, depth)로 세워야 전위 순회가 되고 그제서야 depth 스택으로
부모를 찾을 수 있다. 스레드가 바뀌면 스택을 비운다 — 깊이만 보면 남의 조상이
부모로 잡힌다. 표를 그릴 때 **루트를 depth 로 판별하지 않는 것**도 같은 결이다:
녹화가 도중에 시작되면 부모를 못 본 구간이 `depth > 0` 인 채로 루트가 되므로,
깊이로 거르면 그런 줄이 표에서 통째로 사라진다.

**완료조건은 정수 tick 으로 단정한다.** `timeline_total`(스레드별 depth 0 길이
합)과 `hierarchy_total`(루트 행 total 합)은 같은 것을 두 길로 더하므로 **정확히**
같아야 한다. 부동소수 ms 로 바꾼 뒤 비교하면 어긋나도 "허용 오차" 로 읽힌다.

**Live Follow 를 정의했다.** "새 캡처가 오면 선택을 최신으로 옮긴다". 끄면 보던
프레임을 지킨다 — 스파이크를 붙잡아 두는 것이 그 토글의 쓸모다. 다만 rolling
ring 이 그 프레임을 버렸으면 지킬 수가 없으므로 범위 안으로 자른다. 프레임
그래프에서 무언가를 고르는 순간 따라가기가 꺼진다. 그러지 않으면 다음 캡처가
올 때 선택이 최신으로 튀어 붙잡아 둔 프레임이 손에서 빠져나간다.

**완료조건 다섯의 판정 수단.**

| 완료조건 | 어디서 재나 |
|---|---|
| 10초 녹화 후 과거 프레임 선택 | 코어 프로브 `reader/past-calls` — 프레임마다 호출 수를 다르게 해 **값으로** 가린다 |
| pause 후 엔진이 돌아도 자료 불변 | 코어 프로브 `reader-frozen/` |
| Timeline 합계 ≡ Hierarchy inclusive | 코어 프로브 `aggregate/totals` (정수 동일) |
| 창을 닫아도 recording 유지 | `-Action Window` — 닫은 뒤 `state == recording` |
| Space 가 편집기 동작을 안 가로챔 | **단축키를 만들지 않았다**(아래) |

**★ "창이 열렸다" 와 "본문이 돌았다" 는 다르다.** 도크 탭으로 겹친 창은 선택돼야
본문이 돌고, 그러지 않으면 `editor.window ... open` 이 성공해도 `DrawProfilerHUD`
까지 오지 않는다. 그래서 창 본문에 마커를 걸고 **그 마커가 캡처에 나타나는지**로
판정한다 — 프로파일러가 자기 창을 증언한다. 실측은 프레젠테이션 스레드에 9건
(그린 프레임마다 하나)이다.

**Space 는 만들지 않는 것으로 지켰다.** §7.1 이 "Space 전역 단축키는 제거하거나
Profiler 창 focus 일 때만 받는다" 고 적은 것은 옛 코어 얘기이고, 지금 에디터에는
그런 단축키가 없다(`ImGuiKey_Space` 는 `EditorNavContract` 의 키 이름 표에만
있다). 새로 만들지 않는 것이 그 조건을 지키는 가장 싼 방법이다.

**이미 죽어 있던 지시 하나.** P3 할 일의 "기존 CPU Timeline renderer 이관" 은
대상이 없다 — 옛 `ProfilerWindow`(557줄)는 P1 에서 걷었고(§0.5.8), 자료 모델이
달라 그림을 그대로 옮길 수 없다는 것이 그때의 판단이었다. Frame Overview 는
새로 그렸다.

**타임라인을 같이 세웠다(§7.3).** 스레드마다 레인 하나, 구간마다 사각형 하나,
깊이는 아래로 쌓는다. 휠로 확대하고(커서 아래 tick 을 제자리에 둔다) 끌어서 이동한다.
그리는 층에는 **정렬도 집계도 없다** — 스팬은 코어가 이미 전위 순서로 세워 둔
것(`frame_aggregate::spans()`)이고 레인 경계도 코어가 적어 둔 것(`thread_summary::span_begin/end`)이다.
가로 시야(확대·이동)도 reader 가 든다 — "확대해도 구간 밖으로 나가지 않는다" 는 계약을
코어 프로브가 물고 있다. Timeline 을 **첫 탭**에 둔 것도 값이다: 도크 탭은 선택돼야
본문이 돌므로 뒤 탭에 두면 창을 열어도 한 번도 그려지지 않는다.

**녹화 제어를 CLI 에 내야 했다 — `profile.pause` · `profile.record`.** 타임라인은
**얼린 캡처가 있고 + 녹화 중** 일 때만 그려지고 마커도 남는다. `profile.frame` 은
읽는 명령이라 읽기 위해 얼렸으면 끝나기 전에 되돌려 놓으므로 그 상태를 만들 수
없었다. 같은 손길에 seed 표에 고아로 남아 있던 `profile.selftest` 을 걷었다 — P1 에서
은퇴시키면서 못 걷은 잔여물이고, 명령 표 공든에는 없어서 조용했다. 명령 133 → **135**.

**★ 그 자극을 넣자 결함 둘이 드러났다.**

첫째, **창이 얼린 것을 집는 관문이 상태를 보고 있었다.** `state == frozen 이고 손이
비었을 때` 집었는데, 얼린 순간은 보는 쪽이 한 번도 못 볼 수 있는 찰나다. 그러면
캡처가 있는데도 빈 안내문만 낸다. 관문을 **손에 든 것**으로 바꾸고, 그 규칙을
화면에서 `capture_reader::sync()` 로 옮겨 코어가 재게 했다 — 화면에 두면 갈아타는지를
물을 수단이 눈뿐이 된다. `sync` 는 Live Follow 의 약속도 온전히 지킨다: 껴 있으면
**새로** 얼린 것으로 갈아타고, 꺼 있으면 보던 것을 지킨다(예전에는 처음 하나만 집고
그 뒤로는 다시 안 보았다).

둘째, **pause·record 가 스코프 한가운데서 일어나면 짝이 어긋났다.** 상태 관문이
`begin_scope` 와 `end_scope` 양쪽에 걸려 있어서, 열고 나서 얼면 닫는 쪽이 얼어 버려
스택에 칸이 남고 **그 뒤의 모든 구간이 한 칸씩 깊어졌다.** 거꾸로, 얼린 채 연 구간의
닫는 쪽은 스택에서 남의 구간을 닫았다. 관문을 **여는 쪽에만** 두고, 그때도 짝을
예약하고 나가게(`thread_stream::skip_scope`) 고쳤다 — 깊이 상한을 넘겨 못 열었을 때와
정확히 같은 기제다. 툴바의 Pause 단추도 다른 스레드가 구간 안에 있을 때 눌리므로,
CLI 를 붙이기 전에도 있던 결함이다. **조용한 결함이었다** — 아무것도 실패하지
않고 깊이만 밀리므로, 불균형 계수기만 보는 단정으로는 잡힐 수 없었다.

**검사.** 코어 프로브 36 → **168 검사**, 변이 3 → **14**. P3 가 더한 열하나:
`aggregate-flatten`(부모를 안 찾고 전부 루트로) · `aggregate-self-as-total`(자식을 안 뻐) ·
`aggregate-unsorted`(끝난 순서 가정 노출) · `reader-follow-always`(따라가기를 꺼도 최신으로
점프) · `reader-stale-cache`(선택이 바뀜도 캐시 유지) · `timeline-view-unclamped`(시야를 구간
안으로 안 자름) · `timeline-view-kept`(선택이 바뀜어도 시야 유지) · `scope-skip-unpaired`
(여는 쪽만 건너뜀) · `scope-end-gated`(닫는 쪽에도 상태 관문) · `reader-sync-once`(한 번만
집음) · `reader-sync-always`(같은 것을 매번 다시 집음).

**★ `reader-sync-always` 는 처음엔 통과했다.** 단정을 따라가기를 **꺼 둔 채** 걸었는데,
그러면 "붙잡아 둔 것을 지킨다" 는 절이 먼저 막아 같은 것인지 가리는 절을 걷어도 그대로
통과한다. 켜 둔 채로 재야 변이가 잡혀다 — 두 층이 같은 절을 막으면 변이가 조용히
살아남는다.

**`-Action Window` 가 타임라인까지 자극한다.** 시나리오에 `profile.pause` → `profile.record`
를 **붙여** 넣었다. 사이에 `wait` 를 두면 얼린 순간을 창이 볼 수 있게 되어, 상태를
보고 집는 낡은 관문도 우연히 통과한다. 판정은 `ProfilerWindow` 와 `ProfilerTimeline` 을
**갈라 센다** — 타임라인은 얼린 캐프처가 없으면 한 줄짜리 안내문만 내고 빠져나가는데,
그래도 창은 열려 있고 `ProfilerWindow` 는 찍힌다. 실측은 프레젠테이션 스레드에 둘 다
최근 8 프레임에 7건이다. 변이 둘로 이빨을 확인했다: `draw_timeline()` 호출을 걷으면
timeline 0 · window 8, 앞에 다른 탭을 끼워 넣으면 timeline 0 · window 4 로 붉어졌다.

**아직 하지 않은 것.** §7.4 의 Min/P95/Frames 열은 미뤄 둔다. 그리는 트랙 순서
(§7.3)는 GPU 트랙이 서는 P4 에서 함께 정한다 — 지금은 코어가 정한 슬롯 오름차순 그대로다.

### 0.5.8 2026-09-20 P1+P2 착지 — 옛 코어를 걷었다

P1 과 P2 를 한 덩어리로 지었다. 재활용하지 않기로 한 이상(§0.5.3) 옛 소유권
모델로 먼저 짓고 나중에 갈아끼우는 것은 두 번 짓는 일이기 때문이다.

**걷은 것.** `Profiler.{h,cpp}`(781줄) · `ProfilerSelfTest.{h,cpp}`(660줄) ·
`ProfilerWindow.cpp`(557줄) · `profile.selftest` 명령 · `PROFILE_CPU_*` 매크로
일습. 저장소에 옛 심볼은 **0건**이다.

**선 것.** `ProfileMarker`(NTTP 정적 슬롯) · `ProfileEvent`/`ProfileThreadStream`
(sealed chunk handoff) · `ProfileCapture`(rolling ring · immutable reader) ·
`ProfileService`(스트림 소유 · recorder 상태머신 · 프레임 시계) ·
`ProfileScope`(RAII 표기).

**완료조건 — 마커 집합이 보존됐다.** 교체 직전 값을 그 자리에서 떠서 직후와
맞댔다(§10 P1b 가 요구한 동수 단정). 옛 코어 27 · 새 코어 26 인데, 차이는
**`CPU Frame` 하나뿐**이고 집합 대조에서 그 외에는 양쪽 모두 0 이다.
`CPU Frame` 은 옛 `Tick()` 이 프레임마다 자동으로 끼워 넣던 루트 이벤트이고,
새 코어는 프레임 시간을 `frame_record::tick_begin/tick_end` 가 직접 들고 있어
그 이벤트가 필요 없다. 즉 **호출부 39곳이 전부 이어졌다**.

**engine_frame_id 가 통합됐다.** `publish_frame(Time->GetFrameCount())` —
프로파일러가 자기 카운터를 따로 세지 않는다. §2.1 표의 정본 판정을 그대로 썼다.

**착지하며 잡은 결함 넷.** 전부 게이트가 먼저 잡았다.

1. `profile_scope` 가 전역 서비스에만 찍어, 인스턴스로 세운 서비스에는 아무것도
   들어가지 않았다 — 격리 설계가 반쪽이었다(코어 프로브가 잡았다).
2. `record()` 가 시작 프레임을 받지 않아 첫 스코프들이 "아직 모르는" 프레임에
   기록되고 닫을 때 붙는 라벨과 어긋났다(코어 프로브).
3. ★ **`#if defined(CE_SHIPPING)` 이 Development 에서도 참이었다.**
   `Directory.Build.targets` 는 두 구성 **모두**에서 이 매크로를 정의하고 값으로만
   가른다(`CE_SHIPPING=0;CE_DEVELOPMENT=1`). 그래서 Debug 에디터의 계측이 통째로
   빈 껍데기였고, "수집은 도는데 이벤트만 0" 이라는 모양으로 라이브 기준선이
   잡았다. `#if CE_SHIPPING` 으로 고쳤다.
4. `profile_event` 가 `thread_slot` 을 들고 있지 않아, 이벤트가 청크를 떠나
   프레임 벡터로 옮겨지는 순간 스레드 귀속을 잃었다. 귀속을 이벤트에 실었다 —
   옛 코어가 수집 시점에 스팬을 정렬해야 했던 이유가 이것이고, 그 정렬의 부등호
   하나가 스레드를 통째로 사라지게 했다.

**검사 표면이 바뀌었다.** `profile.selftest` 와 `Invoke-ProfilingValidation
-Action SelfTest` 는 은퇴했다. 옛 selftest 는 전역 싱글톤을 공유하는 구조 때문에
라이브 캡처의 프레임 경계를 직접 넘겨야 했고, 그 교란이 stats 를 못 믿게 만들었다.
새 코어는 서비스를 인스턴스로 세울 수 있어 **엔진을 띄우지 않고** 검사한다 —
`Tools/regression/verify-profile-core.ps1` 이 Debug·Release 각각 36 검사를 초
단위로 돌리고 변이 셋으로 이빨을 증명한다. `-Action Stats` 는 라이브 기준선
하나로 남았고, 이름 예산 단정은 **대상이 사라져서** 뺐다.

**P3 로 넘긴 것.** `ProfilerWindow` 는 지금 요약과 녹화 제어만 낸다(빈 창을 두면
계측이 살아 있는지 에디터에서 볼 수단이 P3 까지 사라진다). 타임라인·Hierarchy·
프레임 선택이 P3 의 몫이다.

**워커 등록이 섰다 — P2 완료조건의 마지막 항목.** enkiTS 의
`TaskSchedulerConfig::profilerCallbacks.threadStart/threadStop` 을 물어 워커가
태어나는 자리에서 등록한다. 이 콜백은 `userData` 없는 함수 포인터라 정적
트램펄린이 필요했고, `thread_pool::worker_hooks` 로 받아 둔다 — 유틸리티 층은
관측 도구를 모르고 받아 둔 함수를 부르기만 한다. 배선은 `EngineBootstrap` 이
한다.

**순서가 전부였다.** 훅 등록과 `profiler().initialize()` 가
`get_job_scheduler().start()` **보다 먼저**여야 한다. 처음에는 늦게 걸었고 결과는
**워커 0 개**였다 — 훅은 워커가 태어날 때 한 번만 불리므로, 이미 태어난 뒤에
거는 것은 아무 일도 하지 않는다. 그래서 `InitializeRuntime` 의 맨 앞으로 옮겼다.
등록만으로는 아무것도 안 보인다는 것도 같이 겪었다: 워커 10 개가 표에 뜨는데
이벤트는 26 그대로였다.

**애니메이션 워커의 시간이 캡처에 나타난다.** `AnimationJob::Update` 의 잡 람다
첫 줄에 스코프를 걸었다. `Test1.creator`(애니메이터 있는 씬)로 전환해 재니
`[Worker 8]` 에 `AnimationJob` 이 귀속되고 프레임 이벤트가 26 → 27(최대 30)로
늘었다. PHASE 13 §4 의 "예측 비용 대 실측 오차 15% 이내"를 판정할 수단이 이것이다.

**★ 자극이 없으면 워커 칸은 빈 채로 초록이다.** 기본 씬에는 애니메이터가 없어
잡이 0 개이고, 그때 캡처의 스레드는 `[GameThread]` 하나뿐이다 — 등록된 워커는
이벤트가 없으면 프레임에 나오지 않는다. `-Action Stats` 의 라이브 기준선이 바로
그 상태라, **이 게이트는 워커 계측이 죽어도 초록이다.** 워커 축을 재려면 씬 전환이
앞에 있어야 한다([[green-soak-never-stimulated]] 와 같은 결).

**훅이 주는 것은 이름뿐이다.** 등록되지 않은 스레드가 마커를 찍으면
`current_stream()` 이 그 자리에서 `Thread N` 이라는 이름으로 등록한다. 그래서
훅을 끊는 변이는 "워커가 사라진다" 가 아니라 "이름이 `[Worker 3]` 에서
`Thread 5` 로 바뀐다" 로 나타난다. 훅의 값은 **귀속이 아니라 라벨과 등록 시점**
이다 — 이것을 "워커가 안 잡힌다" 의 증거로 쓰면 안 된다.

**RenderThread 도 섰다 — 간선을 늘리지 않고.** 전용 렌더 스레드는
`EnhancedSceneRenderer.cpp` 의 `std::thread` 인데, 거기에서 프로파일러를 직접
부르면 **RenderEngine → EngineDiagnostics 간선이 새로 생긴다**(이 모듈이 참조하는
엔진 라이브러리는 `Utility_Framework` 하나뿐이다). PHASE 4 가 간선을 154 → 101 로
줄인 방향과 반대이므로 `thread_pool` 과 같은 역전을 썼다 — 렌더러는
`RenderThreadHooks`(시작·종료·프레임 begin/end 네 개의 함수 포인터)를 받아 두고
부르기만 하고, 꽂는 일은 `EngineBootstrap` 이 한다.

**훅이 begin/end 로 갈리는 자리에는 얇은 표면을 냈다.** `ProfileScope.h` 에
`profile_scope_begin/end` 를 더했다. 여기서 `profiler()` 를 직접 부르면
CE_SHIPPING 약속이 그 파일 밖으로 새기 때문이다(§0.5.8 결함 3 이 그 약속을 한 번
깨뜨렸다). 짝은 받는 쪽에서 다시 묶는다 — 렌더러는 `RenderThreadFrameScope` 라는
RAII 로 감싸 `TickLive` 가 예외로 빠져나가도 닫히게 했다.

**PresentationThread 는 등록만 되어 있었다.** 워커와 똑같은 모양으로 표에는 뜨는데
이벤트가 0 이었다. `PresentFrame` 을 스코프로 감쌌다 — 잠금 대기까지 함께 재는
자리라야 게임 스레드의 파괴 구간과 겹쳐 멈춘 시간이 보인다.

**실측(애니메이터 있는 씬, 245 프레임).** 스레드 11, 마커 41, 불균형 0, 누락 0.

| 스레드 | 찍은 이벤트 |
|---|---|
| `[GameThread]` | 6,322 |
| `[PresentationThread]` | 241 |
| `[Worker 1..8]` | 21~39 (합 241) |
| `[RenderThread]` | 1 |

★ **`[RenderThread]` 가 1 인 것은 계측이 아니라 Debug 의 렌더 소비가 그렇기
때문이다.** 종료 장부가 `publish 245 / consume 4 / latest-wins 241 / overflow 241`
이라고 적는다 — 발행 245 중 **4 개만 소비**되고 나머지는 접힌다. 그 4 개 중
녹화 시작 뒤의 것이 1 개다.

**`profile.stats` 가 스레드마다 몇 건을 찍었는지 낸다(`capturedEvents`).**
`profile.frame` 은 최근 8 프레임만 내므로 드물게 도는 스레드는 그 창에 영영 안
걸린다 — 실제로 RenderThread 를 붙이고도 프레임 표에서 못 찾아 헛짚었다. 이 계수는
얼린 캡처 전체를 보므로 창과 무관하다. 캡처가 없을 때의 0 과 갈리도록
`captureFrozen` 을 함께 낸다.

**게이트가 이빨을 얻었다.** 축이 둘이다 — 자극이 다르기 때문에 나눴다.

- `-Action Stats` — 기본 씬의 기준선. `[GameThread]` · `[PresentationThread]` ·
  `[RenderThread]` 의 **건수**를 단정한다.
- `-Action Workers` — fixture 씬으로 애니메이션 잡을 돌려 워커를 잰다. 이벤트를
  찍은 워커가 넷 이상인지, 워커 스레드에 `AnimationJob` 이라는 **이름**이
  나타나는지를 단정한다(건수만 보면 "무언가 찍었다" 까지다).

변이 넷으로 증명했다. 렌더 스레드 훅을 통째로 끊으면 "`[RenderThread]` 가 스레드
목록에 없다", **등록은 두고 프레임 훅만** 끊으면 "`[RenderThread]` 가 등록만 되고
이벤트를 하나도 안 찍었다"(수명 훅과 구간 훅이 따로 잡힌다), `PresentFrame`
스코프를 걷으면 같은 문장이 `[PresentationThread]` 로, `AnimationJob` 스코프를
걷으면 "이벤트를 찍은 워커가 0 개뿐이다" 와 "워커 스레드에 `AnimationJob` 이
하나도 없다" 가 **둘 다** 붉는다.

**워커 fixture 를 저장소가 소유하게 됐다.**
`Tools/regression/fixtures/profiling-workers/ProfilingWorkerFixture.creator`.
전면 `*.creator` 무시에 대한 명시적 예외다 — fixture 가 추적 밖이면 그 게이트는
한 기계에서만 돌고 clean checkout 에서는 조용히 빈다.

★ **자산이 한 톨도 필요 없었다.** 씬에 든 것은 빈 `Animator` 컴포넌트 여덟 개뿐
(10 KB, 모델·텍스처·클립 참조 0)인데 워커마다 100 건 넘게 찍힌다. 계측이 잡
람다의 **첫 줄**에 있고 그 잡은 스켈레톤이 없으면 바로 돌아 나오기 때문이다 —
"워커가 돌았는가" 를 재는 데에는 모델이 필요 없다. 모델이 딸린 씬을 추적시키는
쪽으로 갔다면 수십 MB 를 싣고도 같은 것만 쟀을 것이다. 제품 저작 경로
(`scene.new` · `scene.populate` · `component.add` · `scene.save`)로 만들었고 손으로
쓰지 않았다.

⚠ 이 fixture 로는 애니메이션 **결과**(포즈·블렌드·스키닝)를 검증할 수 없다.
그것은 `verify-animation-*.ps1` 의 몫이고, 여기서 재는 것은 계측의 생사뿐이다.

**★ `consume 4` 는 Debug 가 느려서가 아니었다 — 예열을 안 기다린 것이다.**
`render.live.wait` 를 앞에 두고 재니 그 한 줄이 **22.8 초**를 쓴다. 라이브 첫
프레임의 GBuffer ShaderMeta 반영(slang reflect)이고, 그동안 게임 프레임은 61 →
3418 로 가며 발행된 것이 전부 `latest-wins` 로 접힌다. **예열이 끝나면 소비는
15 ms 마다 일어난다.** 워밍업 프레임 수를 늘리는 것으로는 영영 나아지지 않는
축이었다 — `wait N` 은 게임 스레드 프레임 수라 렌더가 한 프레임에 얼마를 쓰는지와
무관하게 지나간다(PHASE 4 가 `render.live.wait` 를 만든 이유가 바로 이것이고,
그 주석이 같은 말을 적어 두고 있었다).

그래서 Stats 축은 `render.live.wait` 를 **앞뒤로** 둔다. 앞의 하나가 예열을
통과시키고, 뒤의 하나가 측정 구간 안에서 라이브 프레임이 최소 한 번 끝나는 것을
보장한다. 게임 스레드를 세우지 않으므로(`WaitForResult` 로 판정만 미룬다) 다른
축의 값이 왜곡되지 않는다. 그렇게 재면 `[RenderThread]` 가 50 건이고, 게이트
전체가 28 초다.

**아직 하지 않은 것.** `AnimationJob` 외의 잡 구간에는 마커가 없다. 어디를 잴지는
타임라인을 보고 정하는 P3 의 몫이다.

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
**임시 계측은 걷었다**(같은 날 — 아래 ⑥ 참조). `App.cpp` 3곳과 `EditorMain.cpp` 9곳을 되돌렸고
(`unique_lock` → `lock_guard` 포함), `EditorMain.cpp` 의 제품 계측 `GameLogic`·`EndOfFrame` 2곳은 남겼다.
`profile.frame` 명령은 남긴다 — 임시 계측이 아니라 보존 프레임을 밖에서 읽는 창구이고, 남은 27개
이벤트에도 그대로 쓴다. 지금 기준선은 다시 **편집 27 · 388B** 다(걷은 뒤 재빌드해 확인).

**⑥ 두 축을 오늘 다시 돌렸다 — 초록인데 기준선 숫자가 움직였다.** `-Action SelfTest` 종료 코드 0 ·
`PROFILE_SELFTEST_OK=true`, `-Action Stats` 종료 코드 0 · 판정 통과(9-15 에 JSON 판정으로 고친 뒤
처음 재확인). 그런데 **이벤트/프레임이 38 · 이름 573B** 다 — §2.1 이 적은 편집 **27** · 388B 가 아니다.
원인은 회귀가 아니라 **내가 9-15 에 넣은 임시 계측**이 그대로 살아 있기 때문이다. 즉 이 축은
남의 계측이 드나들 때마다 값이 바뀐다. 그래서 §10 P1b 의 완료조건에서 **절대 숫자를 걷어냈다** —
단정의 모양은 "27 인가" 가 아니라 **"교체 직전 그 자리에서 뜬 값과 교체 직후가 같은가"** 여야 한다
([[mutation-proves-gate-has-teeth]]: 이빨은 A/B 동수 단정에 있지 절대 숫자에 있지 않다).
문서에 박은 숫자가 하루 만에 거짓이 되는 것을 실행으로 잡았다 — 안 돌렸으면 틀린 기준선을 커밋했다.

**A/B/A 왕복으로 닫았다(같은 날).** 임시 계측을 주석 처리하고 Debug 재빌드해 다시 재니 **25 · 367B** 로,
9-15 의 27 · 388B 와도 어긋났다. 원인은 **내가 걷을 대상을 잘못 셌기 때문**이다 — `EditorMain.cpp` 의
`PROFILE_CPU_BEGIN` 11곳 중 **2곳(`GameLogic` · `EndOfFrame`)은 9-15 이전부터 있던 제품 계측**이고,
내가 새로 넣은 것은 12곳(`App.cpp` 3 · `EditorMain.cpp` 9)이다. 그 둘만 되살려 다시 빌드하니
**27 · 388B** 로 한 바이트까지 9-15 와 일치했고, 임시 계측을 전부 복원하니 다시 **38 · 573B** 였다.
→ **회귀는 없다.** 그리고 12곳을 넣었는데 38−27=11 만 늘었다 — 안 도는 한 곳은 `profile.frame` 으로
지목했다: **`TickSimulationFrame`**(`EditorMain.cpp:665`)이다. 그 계측은 `IsPlayCommitted()` 가 거짓일 때의
`return` **뒤**에 있어 편집 모드에서는 람다가 먼저 빠져나간다. 보존 프레임 [59,63) 네 개가 전부 이벤트 38 ·
빠진 것 그 하나였다(§2.1 의 engine_frame_id 후보 표가 "편집 모드는 early return 이라 안 돈다" 고 적은 것이
계측 축에서 그대로 재현됐다). 이 왕복이 없었으면 25 를 보고 "계측 2곳이 사라졌다" 는 유령을 쫓았을 것이다
([[gate-measures-stale-binary]]: 걷은 뒤에는 반드시 재빌드하고, 되돌린 뒤에도 재빌드한다).

**`profile.frame` 을 밖에서 부르는 절차**(라이브 서버 없이): `wait 60` · `profile.frame` · `quit` 를
CRLF 스크립트로 적고 `--commandlet-script` 로 넘기되, **출력은 `Start-Process -RedirectStandardOutput`**
으로 받는다 — GUI 앱이라 셸 파이프로는 0 바이트다. `wait` 없이 부르면 보존 범위가 [1,2) 뿐이라
부팅 첫 프레임(557ms)만 잡히고, 그 프레임에는 `CLIPump`(그 안에서 명령이 도는 중이라 아직 안 닫혔다)와
`PublishRenderFrame`(아직 오지 않았다)이 빠져 있어 **관측 시점이 결과를 바꾼다**
([[stimulus-changes-the-layout-it-probes]]).

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
스냅샷을 발행하고 수집 코어가 `engine_frame_id`에 붙여야 한다.

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

- `thread_stream` 소유권을 profiler service가 가진다.
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

씬뷰와 게임뷰가 함께 있으면 한 `engine_frame_id`에 여러 GPU 제출이 생긴다. 따라서 다음 두
식별자를 구분한다.

- `engine_frame_id`: 게임 업데이트 경계. 프레임 그래프와 CPU Hierarchy의 기준.
- `submission_id`: GPU queue에 실제 제출한 단위. 카메라·뷰·queue를 식별.

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
  marker_registry · thread_stream · FrameAssembler
  capture_session · CounterRegistry · CaptureFile
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
  profiler_service.h/.cpp
  marker_registry.h/.cpp
  ThreadEventStream.h/.cpp
  FrameAssembler.h/.cpp
  capture_session.h/.cpp
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
using marker_id = uint32_t;

struct marker_desc
{
    marker_id    id;
    category_id  category;
    string_id    name;
    string_id    file;
    uint32_t    line;
    marker_flags flags;
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
ce::profile_scope _{ ce::marker<"AnimatorSystem">() };

// 보조 — 이름을 생략하면 std::source_location이 함수·파일·행을 채운다
ce::profile_scope _{};

ce::profile_counter(ce::marker<"DrawCalls">(), drawCount);
ce::profile_instant(ce::marker<"SceneLoaded">());
```

> **2026-09-20 표기 확정 — 층 B(snake_case).** 초판은 `ce::ProfileScope`·`ce::Marker<>` 로
> 적었으나, 9-20 에 확정된 `CodingConventions.md` 가 층 B 를 snake_case 로 못 박았고
> `thread_pool`·`job_scheduler` 가 "엔진 공용 실행 유틸리티" 로 층 B 확정된 선례가 있다.
> 프로파일러도 같은 성격의 공용 인프라이고 계측 API 는 코드 전역에 박히므로, 선례와
> 어긋나면 눈에 띈다. `ce::` 안에 타입이 들어가는 것은 컨벤션 §5.4 에 예외로 적었다 —
> `profile_scope` 는 컨테이너가 아니라 **자유 함수를 쓸 수 없는 자리의 자유 함수**이고
> (여는 일과 닫는 일이 한 문장이어야 한다) 그것을 표현하는 수단이 소멸자뿐이기 때문이다.

> **NTTP 실증(2026-09-20).** MSVC v145 `/std:c++23preview` 에서 `fixed_string` NTTP 가
> W4 경고 0 으로 서고, 이름마다 `marker_slot<Name>` 정적 슬롯이 갈리며 같은 이름은 같은
> 슬롯을 얻는 것을 확인했다. 등록은 `static inline const marker_id id = intern(...)` 의
> 동적 초기화 1회이고, registry 는 함수 지역 static 으로 들고 있어 TU 간 초기화 순서에
> 의존하지 않는다.

`source_location` 판은 **보조**다. 지금 `Scene::Update` 한 함수 안에 계측 지점이
13개 있어 함수 이름만으로는 구분되지 않는다 — 이름을 명시하는 쪽이 기본이다.

#### ★ 호출부 형태에 독립이어야 한다

마커 ID가 컴파일 타임 상수이고 스코프가 RAII이면, 호출부가 39곳이든 1곳이든 코어는
동일하다. 나중에 씬 실행 표가 도입되면

```cpp
for (auto& step : table) { ce::profile_scope _{ step.marker }; step.fn(*this, dt); }
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
    marker_id marker;
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

- recording 중에는 경량 `live_summary`만 double-buffer로 공개
- pause 시 `shared_ptr<const capture_session>`을 원자적으로 교체
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
GPU bar는 `submission_id`, view/camera, fence, pass name을 tooltip에 표시한다.

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
  §3.2의 "`thread_stream` 소유권을 profiler service가 가진다"가 이것을 푼다.
- **프레임을 넘는 스코프는 게임 스레드에서 재현하면 안 된다.** `Tick()`은 스택 맨 위를
  무조건 닫으므로, 열린 스코프가 있으면 `"CPU Frame"` 대신 그것을 닫는다. 게임 스레드의
  스택이 프레임마다 한 칸씩 깊어져 `kMaxStackDepth`(32)에서 죽는다. 워커에서만 관측할 것.

**P0이 이미 닫은 몫 / P2가 받는 몫.** P2는 아래를 다시 하지 않는다.

| P0이 닫은 것(최소 지혈) | P2가 받는 것(정식 구조) |
|---|---|
| 드롭 계수화(`DroppedEvents`·`DroppedNames`) | 캡처 diagnostics로 승격, UI 노출 |
| 널 슬롯 스킵 + `UnregisterThread` | `thread_stream` 소유권 역전(서비스가 소유) |
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

- `profiler_service` — 스트림을 서비스가 소유한다. 현행의 함수 지역
  `static thread_local`(모든 인스턴스가 스레드당 TLS 하나를 공유) 제약이 여기서 풀린다
- `marker_registry` — 컴파일 타임 마커 ID 등록(§5.2). **매크로를 만들지 않는다**
- `ce::profile_scope` RAII — 짝 불균형을 문법적으로 불가능하게
- **단일 `engine_frame_id` 발행 지점 통합.** 새로 만드는 일이 아니라 지금 서로 모르고
  도는 세 카운터를 묶는 일이다:

  | 후보 | 성질 | 판정 |
  |---|---|---|
  | `TimeSystem::m_frameCount`(atomic uint32) | Editor·Player 공통, 편집·재생 모두 증가, **`FixedTick` 호출부 0**이라 루프당 1회(9-15 재확인) | **정본** |
  | `Runtime::TickSimulationFrame` | 편집 모드는 앞에서 early return | 부적합 |
  | `LiveState::publishedFrameId`(atomic uint64) | RT 파이프라인 제출 단위 | `submission_id` 축 후보 |

- 호출부 39곳의 표기를 새 RAII로 교체(**이름과 자리는 보존**)
- `CE_DEVELOPMENT`를 물어 compile-out(§5.2 — 새 매크로 신설 금지)
- ImGui/D3D12 include 없는지 include boundary 검사 유지

~~기존 `PROFILE_CPU_*` 매크로를 새 API adapter로 연결~~ — **폐기.** 옛 코어에 adapter를
붙이는 일이 재활용 폐기로 의미를 잃었다. 매크로는 정의째 없어진다.

완료 조건:

- **이벤트 수가 보존된다 — 절대 숫자가 아니라 교체 전후 동수**. 이것이 "기존 호출부 대량
  수정 없이 새 코어로 이벤트가 들어감"을 재는 값이고, 마커 하나가 안 이어지면 하나 줄어든다.
  ★ **절대 숫자를 완료조건에 박으면 안 된다**(2026-09-20 실측으로 확인): §2.1 의 편집 27 /
  재생 34 는 임시 계측을 걷은 값이고, 9-15 에 넣은 임시 계측 12곳(`App.cpp` 3 · `EditorMain.cpp` 9)이
  붙어 있는 동안은 **편집 38 · 이름 573B** 였다(걷은 지금은 27 · 388B 로 복귀). 즉 이 축은
  남의 계측이 드나들 때마다 움직인다 —
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
  (2026-09-20 현재: 임시 계측을 걷은 뒤 편집 27 · 이름 388B · 누락 0 · 불균형 0 · 슬롯 1 —
  붙어 있던 동안은 38 · 573B 였다)
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

- owner 수명이 명확한 `thread_stream`
- sealed chunk handoff — writer가 자기 chunk를 봉인해 넘기고 collector는 남의 메모리를
  만지지 않는다. 현행 결함 8종 중 다섯이 **고쳐지는 것이 아니라 발생하지 않게** 된다
- cross-frame scope와 mid-capture scope 처리 — `truncated` flag(§6.1).
  **selftest의 `cross-frame/preserve`가 KNOWN-DEFECT에서 PASS로 바뀌는 것이 판정이다**
- 600프레임/128MiB rolling ring
- drop/overflow 진단
- `capture_session` freeze
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
- clock calibration과 `submission_id`, view 정보 기록
- GPU Timeline 추가

검증 장면:

- 가벼운/무거운 GPU 패스를 프레임마다 번갈아 실행
- 인플라이트 0/1/2 각각 측정
- 씬뷰 단독, 게임뷰 단독, 두 뷰 동시
- 리사이즈와 pipeline rebuild 직전·직후

완료 조건:

- heavy/light 패턴이 올바른 `engine_frame_id`와 `submission_id`에 교대로 매핑
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
| 두 카메라 | 같은 engine_frame_id, 서로 다른 submission_id/view |
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
- [x] ★ **애니메이션 워커 8스레드·RenderThread·PresentationThread가 캡처에 나타남**
      (§0.5.8 — 셋 다 이벤트가 귀속된다. 회귀 감시는 아직 `[GameThread]`·
      `[PresentationThread]` 두 축에만 걸려 있다)
- [ ] 멀티카메라·2-in-flight에서도 정확한 GPU frame/submission 매핑
- [ ] CPU/GPU/Rendering/Memory/GC counter가 같은 engine_frame_id에 정렬
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
