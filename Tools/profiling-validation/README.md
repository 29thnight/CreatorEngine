# 프레임 프로파일러 라이브 검증 (PHASE 14)

설계 문서: [ProfilingCapturePlan.md](../../docs/plans/ProfilingCapturePlan.md)
코어 계약 게이트: [../regression/verify-profile-core.ps1](../regression/verify-profile-core.ps1)

## 이 게이트가 맡는 자리

검사는 둘로 갈려 있다.

| | 무엇을 | 어떻게 |
|---|---|---|
| `Tools/regression/verify-profile-core.ps1` | **수집 코어의 계약** | 엔진을 안 띄운다. `EngineDiagnostics` 만 `cl` 로 직접 컴파일해 초 단위로 돈다 |
| 이 디렉터리 | **살아 있는 엔진에서 그것이 실제로 서는가** | 에디터를 무인 기동해 콘솔 명령의 JSON 응답과 종료 로그로 판정한다 |

경계가 분명하다 — **표의 숫자가 맞는지는 코어가 판정하고, 이 게이트는 그 숫자가
나올 자리에 실제로 무엇이 흘렀는지만 본다.** 화면이 숫자를 만들지 않으므로 둘을
나눌 수 있다.

판정 근거는 화면이 아니라 둘이다: **종료 코드**와 **응답 JSON**.

## 실행

```bash
pwsh -NoProfile -File .\Tools\profiling-validation\Invoke-ProfilingValidation.ps1 -Action Stats
```

| `-Action` | 자극 | 무는 것 |
|---|---|---|
| `Stats` (기본) | 기본 씬 · `profile.record` · 예열 | 보존 프레임 · 이벤트 · 등록 마커가 0 이 아닌지, 불균형 스코프 0, 청크 풀 미고갈, `[GameThread]`·`[RenderThread]`·`[PresentationThread]` 가 **등록만 되고 안 찍는 상태가 아닌지**, `[Worker 1]` 등록 |
| `Workers` | fixture 씬으로 갈아 끼워 애니메이션 잡 | 워커 레인 등록, **이벤트를 찍은 워커가 넷 이상**, 그 이벤트에 `AnimationJob` 이 있는지, 씬 교체의 `SceneActivated` 가 길이 없는 사건으로 남았는지 |
| `Window` | 프로파일러 창을 열고 돌린 뒤 닫는다 | `ProfilerWindow`·`ProfilerTimeline` **마커가 캡처에 나타나는지**(창이 자기 창을 증언한다), 창이 프레젠테이션 스레드에 붙는지, **창을 닫아도 녹화가 계속되는지** |
| `Gpu` | 라이브 렌더 | GPU 수집 장부 전체 — 제출마다 갈린 기록 · 뒤집힌 조각 0 · `busy <= queueSpan` · 분할 패스의 raw 조각 보존 · clock calibration 재표본 · CPU 축 정렬 · `[GPU Graphics]` 레인 귀속 · 제 프레임 칸 · 제출 번호 · 트랙 순서 · 종료 때 `abandoned`·`foreign` 0 |
| `Build` | — | `Debug\|x64` 빌드만 한다 |

### ★ 네 축 모두 자극에 `profile.record` 를 명시한다 (2026-09-22)

부팅과 함께 기록을 열던 줄을 걷었다. Record 는 **수집 스위치**이고, 켜지 않으면
캡처가 비어 있다 — 그러면 이 게이트들은 **빈 캡처를 성공으로 읽는다.**

`Workers` 는 그것을 **`scene.switch` 앞**에 둬야 한다. 뒤에 두면 `SceneActivated`
가 기록 밖에서 일어나 §7.3 트랙 1(길이 없는 사건)이 빈 채로 통과한다.

### PowerShell 5.1 로 돌리지 말 것

저장소의 스크립트와 엔진 로그가 UTF-8 인데 5.1 은 시스템 코드페이지로 읽어
한글이 깨지고 정규식 판정이 어긋난다. 스크립트에 `#Requires -Version 7.0` 이
걸려 있다.

### fixture 가 없으면 통과시키지 않는다

`Workers` 는 `Tools/regression/fixtures/profiling-workers/` 를 쓴다. 추적 밖
fixture 를 가진 게이트는 이 기계에서만 돌고 clean checkout 에서는 조용히 빈다.
그래서 없으면 실패로 끝낸다.

## 콘솔 명령

에디터 안에서 직접 부를 수도 있다. 넷 다 교란이 없다 — 프레임 경계를 스스로
넘기지 않는다.

- `profile.record` / `profile.pause` — 수집 스위치. 끄는 것이 Pause 다
- `profile.stats` — 스레드별 수집량 · 예산 소진 · 불균형 스코프 · 청크 풀
- `profile.frame` — 캡처를 얼려 공개한다. 최근 몇 칸의 프레임과, 캡처 전체를
  훑어 따로 내는 **길이 없는 사건** 목록

콘솔에 `clear` 는 없다 — 보존분을 버리는 것은 프로파일러 창의 `Clear` 버튼뿐
이다(`capture_service::clear()`). 무인 기동으로는 자극할 수 없어서 그 계약은
코어 게이트가 문다(`clear-open/` · `clear-new/` 검사와 변이
`clear-without-seal` · `scope-ignores-generation`).

## 은퇴한 것 — `profile.selftest` 와 옛 `CPUProfiler`

P0 에는 `profile.selftest` 라는 특성화 검사가 있었다. 판정이 셋으로 나뉘고
(`PASS` · `KNOWN-DEFECT` · `SKIP`), `PROFILE_SELFTEST_OK=true` 마커로 스크립트가
판정했다. **P1+P2 에서 수집 코어를 통째로 갈아 끼우며 함께 은퇴했다.**

그 검사의 구조적 한계가 은퇴 사유다 — `CPUProfiler::GetTLSUnsafe()` 가 함수 지역
`static thread_local` 이라 **모든 인스턴스가 스레드당 하나의 TLS 를 공유했다.**
검사 전용 인스턴스를 세울 수 없어 전역 싱글톤의 프레임 경계를 직접 넘겨야 했고,
그래서 검사 직후에 stats 를 재면 예산이 100% 포화로 보였다. 지금은 코어가
프로세스 밖에서(`verify-profile-core.ps1`) 자기 계약을 증명하므로 그 타협이
필요 없다.

옛 이름은 소스에 **0 건**이다(2026-09-22 실측) — `CPUProfiler` · `PROFILE_CPU_BEGIN`
· `PROFILE_CPU_END` · `PROFILER_INITIALIZE` · `PROFILE_SELFTEST_OK`. 이 문서와
계획서 안에만 남아 있다.

`cross-frame/preserve` 는 당시 유일한 `KNOWN-DEFECT` 였고, **지금은 PASS 다** —
코어 프로브의 `cross-frame/` 네 절이 전부 초록이고 변이 `close-open-scopes` 가
그 자리에서 붉어진다(§14).

## P0 에서 함께 고친 것 — 역사

> ⚠ 아래 표는 **은퇴한 `CPUProfiler` 의 결함 이력**이다. 그 코드는 이제 없다.
> 남겨 두는 이유는 같은 종류의 결함이 새 코어에서도 가능하기 때문이고, 그래서
> 오른쪽 열의 계약들이 지금 `verify-profile-core.ps1` 의 검사로 옮겨 가 있다.

| 문제 | 증상 | 조치 |
|---|---|---|
| 이벤트 상한 초과 | `assert` 만 하고 그대로 기록 → NDEBUG 에서 벡터 밖에 씀 | 세고 버린다(`overflow/` 가 지금 문다) |
| 이름 예산 초과 | 같은 모양으로 힙 밖에 씀 | 대체 이름 + 누락 계수 |
| 스레드 은퇴 경로 없음 | 등록한 스레드가 죽으면 이후 모든 수집이 UAF | 은퇴·슬롯 재사용 |
| 수집이 락 없이 스레드 표 순회 | 등록이 겹치면 재할당으로 이터레이터 무효 | 수집 구간에 잠금 |
| 스팬 그룹핑 부등호 반대 | **그 프레임에 이벤트가 없는 스레드를 만나면 그보다 인덱스가 큰 스레드가 통째로 타임라인에서 사라짐** | 부등호 정정 |
| 스택 불균형이 영구 누적 | 일시정지 경계에서 깊이가 어긋난 채 남아 TLS 의 다음 멤버를 덮어씀 | 복구 + 불균형 계수 |
| 일시정지 플래그가 non-atomic | 게임 스레드가 쓰고 워커가 읽는 데이터 레이스(UB) | 원자 변수 |
| 슬롯 즉시 재사용 | 히스토리에 남은 옛 주인의 이벤트가 새 주인 이름으로 오귀속 | 한 바퀴 뒤에만 재사용 |

### 픽스처가 워커 스레드를 썼던 이유

`cross-frame` 을 게임 스레드에서 하면 안 됐다. 수집이 스택 맨 위를 무조건
닫으므로, 열린 스코프가 있으면 프레임 스코프 대신 그것을 닫았다. 그러면 게임
스레드의 스택이 프레임마다 한 칸씩 깊어져 최대 깊이에서 죽는다. 지금도 같은
이유로 `Workers` 축이 **실제 워커**를 자극한다 — 게임 스레드 하나로는 워커
귀속을 증명할 수 없다.

## 기준선 — 읽는 법

> ★ **여기에 절대 수를 적지 않는다.** 기준선 숫자는 남이 계측을 얹기만 해도
> 흔들리고, 그러면 통과인 축이 붉은 것처럼 보이거나 그 반대가 된다. 그래서 이
> 게이트의 단정은 전부 **"무엇을 찍었는가"** 지 "몇 개를 찍었는가" 가 아니다.
>
> 예외는 둘뿐이고, 그것도 하한이다 — 이벤트를 찍은 워커 **넷 이상**(전부를
> 요구하지 않는다. 잡을 어느 워커가 집는지는 스케줄러 사정이다), 그리고 0 이
> 아니어야 하는 값들(보존 프레임 · 마커 · 청크 · 조각).

측정값이 필요하면 게이트를 돌려 그 자리에서 읽는다. 문서에 적어 둔 수는
적는 순간부터 낡는다.
