# 프레임 프로파일러 검증 (PHASE 14)

설계 문서: [ProfilingCapturePlan.md](../../docs/plans/ProfilingCapturePlan.md)

## 무엇을 재는가

P0의 `profile.selftest`는 **지금 무엇이 참인가**를 못박는 특성화 검사다.
"프로파일러가 좋은가"를 묻지 않는다 — P2에서 수집 코어를 갈아끼울 때
무엇이 깨졌는지 즉시 드러나게 하는 것이 목적이다.

그래서 판정이 셋으로 나뉜다.

| 표시 | 의미 |
|---|---|
| `PASS` | 지금 참이고, 앞으로도 참이어야 한다 |
| `KNOWN-DEFECT` | 지금 틀렸고, 이미 알고 있다. **실패로 세지 않는다** |
| `SKIP` | 이번 조건에서는 관측되지 않았다 |

`KNOWN-DEFECT`가 `PASS`로 바뀌는 것이 진척의 정의다. 실패 0이면
`PROFILE_SELFTEST_OK=true`가 로그에 찍히고, 스크립트는 그 마커로 판정한다.

## 실행

```bash
pwsh -NoProfile -File .\Tools\profiling-validation\Invoke-ProfilingValidation.ps1
```

`-Action Stats`는 프로파일러 자체 비용만 출력한다(프레임 경계를 넘지 않으므로
라이브 캡처를 건드리지 않는다). `-Action Build`는 Debug|x64 빌드만 한다.

**PowerShell 5.1로 돌리지 말 것.** 저장소의 스크립트와 엔진 로그가 UTF-8인데
5.1은 시스템 코드페이지로 읽어 한글이 깨지고 정규식 판정이 어긋난다.
스크립트에 `#Requires -Version 7.0`을 걸어 두었다.

## 콘솔 명령

에디터 안에서 직접 부를 수도 있다.

- `profile.selftest` — 특성화 검사. **프레임 경계를 스스로 넘으므로 라이브 캡처를 교란한다.** 성능을 재는 도중에 부르지 말 것
- `profile.stats` — 프로파일러 자체 비용과 용량 소진. 교란 없음

## 픽스처

| 픽스처 | 무엇을 고정하는가 |
|---|---|
| `nested/*` | 중첩 스코프의 깊이 증가, 구간 포함 관계, 파일·행 메타데이터 |
| `multithread/*` | 워커 스레드별 이벤트 귀속, 종료한 스레드의 슬롯 은퇴 |
| `cross-frame/preserve` | 프레임 경계를 넘는 스코프의 보존 — **현재 KNOWN-DEFECT** |
| `overflow/*` | 상한 초과분이 누락으로 **기록**되는가, 수집량이 상한을 넘지 않는가 |

### 픽스처가 워커 스레드를 쓰는 이유

`cross-frame`을 게임 스레드에서 하면 안 된다. `Tick()`은 스택 맨 위를 무조건
닫으므로, 열린 스코프가 있으면 `"CPU Frame"` 대신 그것을 닫는다. 그러면 게임
스레드의 스택이 프레임마다 한 칸씩 깊어져 `MAX_STACK_DEPTH`(32)에서 죽는다.

### 검사가 라이브 캡처를 교란하는 이유

`CPUProfiler::GetTLSUnsafe()`가 함수 지역 `static thread_local`이라 **모든
CPUProfiler 인스턴스가 스레드당 하나의 TLS를 공유한다.** 그래서 검사 전용
인스턴스를 따로 세울 수 없고, 전역 `gCPUProfiler`를 상대로 프레임 경계를
직접 넘길 수밖에 없다. P1에서 서비스가 스레드 스트림을 소유하면 풀린다.

## P0에서 함께 고친 것

검사 하네스를 세우려면 먼저 닫아야 했던 것들이다. 테스트가 자기 뒤처리를
하는 순간 엔진이 망가지는 토대 위에는 하네스를 못 세운다.

| 문제 | 증상 | 조치 |
|---|---|---|
| 이벤트 상한 초과 | `assert`만 하고 그대로 기록 → NDEBUG에서 `Events` 벡터 밖에 씀 | 세고 버린다(`DroppedEvents`) |
| 이름 예산 초과 | 같은 모양으로 힙 밖에 씀 | `Allocate`가 `nullptr` 반환, 대체 이름 + `DroppedNames` |
| 스레드 은퇴 경로 없음 | 등록한 스레드가 죽으면 `ThreadData::pTLS`가 죽은 TLS를 가리켜 이후 모든 `Tick()`이 UAF | `UnregisterThread()` 신설, `Tick()`이 널 슬롯 건너뜀, 슬롯 재사용 |
| `Tick()`이 락 없이 스레드 표 순회 | 등록이 겹치면 재할당으로 이터레이터 무효 | 수집 구간에 `m_ThreadDataLock` |
| 스팬 그룹핑 부등호 반대 | **그 프레임에 이벤트가 없는 스레드를 만나면 그보다 인덱스가 큰 스레드가 통째로 타임라인에서 사라짐** | 부등호 정정 |
| `EventStack` 불균형이 영구 누적 | 일시정지 경계에서 `EndEvent`가 건너뛰어지면 깊이가 어긋난 채 남고, 32를 넘으면 `FixedStack`이 TLS의 다음 멤버를 덮어씀 | 은퇴·재등록 시 복구 + 불균형 계수(`profile.stats`) |
| `m_Paused`가 non-atomic | 게임 스레드가 쓰고 워커가 읽는 데이터 레이스(UB) | `std::atomic<bool>` |
| 슬롯 즉시 재사용 | 히스토리에 남은 옛 주인의 이벤트가 새 주인 이름으로 표시(오귀속) | 히스토리 한 바퀴 뒤에만 재사용 |

### 아직 안 닫힌 것 — P2의 몫

**`m_ThreadDataLock`은 스레드 표 하나만 지킨다.** `Tick()`이 `pTLS->EventBuffer[i]`를
읽는 동안 그 워커가 `BeginEvent`에서 `resize`를 돌리면 옛 버퍼가 해제된다.
3-2G 이후 3자 렌더 배리어와 CB 스레드는 사라졌다. 현재는 `[GameThread]`만
프로파일러에 등록하고, 독립적으로 도는 `PresentationThread`에서는 프로파일 매크로를
호출하지 않는다. 따라서 현행 캡처가 안전한 것은 writer가 하나라서이지 원소 수집 계약이
고쳐져서가 아니다. **PresentationThread나 RenderThread를 등록하기 전에** P2의 sealed
chunk handoff를 먼저 구현해야 한다.

마지막 항목은 3-2G 이전 검사가 잡았다. `multithread/capture`가 0/3으로 실패했고,
원인이 워커(인덱스 3~5)가 아니라 그 앞의 CB/CE 중 하나가 쉰 프레임이었다.
기존 타임라인 UI도 같은 이유로 스레드를 조용히 누락해 왔다.

## 판정 기준

- 종료 코드 0
- `PROFILE_SELFTEST_OK=true` 출력
- `미처리 예외` 미출력

## 기준선 (2026-08-11 실측 · 3-2G 이전 역사값)

- `CreatorEngine.sln` Debug|x64: 0 오류 · 0 경고
- `Tick()` 비용: 평균 25~32us · 최대 78us (일반 프레임, 캐릭터 없는 기본 씬)
- 이벤트/프레임: 26 / 상한 1024 (3%)
- 이름 바이트/프레임: 405 / 상한 16384 (2%)
- 당시 등록 스레드: `[GameThread]`, `[CB-Thread]`, `[CE-Thread]` 3개
- 현재 등록 스레드(2026-08-16, 3-2G): `[GameThread]` 1개

일반 프레임의 여유는 크지만 이 수치는 **캐릭터·파티클이 없는 기본 씬** 기준이다.
`PROFILE_CPU_BEGIN` 호출부가 현재 41곳(`Scene.cpp` 14 · `SceneManager.cpp` 17 ·
`EditorMain.cpp` 7 · `EditorRenderer.cpp` 3)뿐이라는 점도 함께 봐야 한다 —
계측을 늘리면 이 여유는 빠르게 줄어든다.
