# 종료 시 크래시 · 덤프 누락

두 층으로 나뉜다. 층 1은 해결했고, 층 2가 남아 있다.

## 층 1 — 덤프가 안 남는다 (해결)

크래시 핸들러가 로그는 남기는데 `.dmp` 파일이 생기지 않았다.

**증거였던 것**

`x64\Debug\Log\Editor_20260731_102351.html`
```
10:24:21.508 CRASH - 미처리 예외로 프로세스 종료
             - ACCESS_VIOLATION (잘못된 메모리 접근 - 널/댕글링 포인터 의심)
             @ 0x00007FF774C7C327
```
같은 시각 덤프 디렉터리에는 아무것도 없었고, 심볼 스택도 로그에 없었다.
로그에 CRASH 줄만 있고 스택도 덤프도 없다는 조합이 곧 단서였다 —
기록자가 아예 없었다면 스택도 없지만, 기록자가 있고 그 안에서 죽었어도 같은 모양이 된다.

**원인 (넷)**

1. **순서.** `CoreWindow::WriteCrashDump`가 요약(`BuildCrashReport`)을 먼저 만들고
   덤프를 나중에 썼다. 요약은 dbghelp로 스택을 걷고 `std::string`을 늘려 가는데,
   힙이 손상됐거나 스택이 고갈된 프로세스에서는 바로 거기가 2차 크래시 지점이다.
   요약을 만들다 죽으면 덤프까지 통째로 잃었다.
2. **등록 시점.** 기록자 등록(`Log::SetCrashDumpWriter`)이 `CoreWindow::SetDumpType`
   안에 있고, 그게 `App::Initialize` 중반에서만 불렸다. 크래시 후크는
   `Log::Initialize`가 훨씬 먼저 걸어 두므로, 디바이스 생성·셰이더 컴파일·리소스
   로드 구간 전체가 덤프 사각지대였다.
3. **댕글링 포인터 셋.** `CoreWindow::s_instance`는 `App::Initialize`의 지역 객체를
   가리키는데 소멸 시 정리되지 않았고, 크래시 경로가 읽는 `EngineSettingInstance`와
   `Debug`도 종료 단계에서는 파괴된 인스턴스를 가리킨다. 종료 구간 크래시는
   덤프를 쓰기도 전에 핸들러 안에서 다시 죽을 수 있었다.
4. **조용한 실패.** 기록자가 널이면 아무 말 없이 return했고, `CreateFile` 실패도
   그냥 return이었으며, `MiniDumpWriteDump` 실패 코드는 변수에 받아 놓고 버렸다.

**한 일**

- `WriteMinidumpFile`을 떼어내 `.dmp`를 **가장 먼저** 쓴다. 요약(.txt)은 그 뒤다.
- 기록자 등록을 `EngineBootstrap::InitializeRuntime`의 `Log::Initialize` 직후로 옮겼다.
  `App.cpp` / `GameApp.cpp`의 늦은 호출은 제거했다.
- `~CoreWindow`가 `s_instance`를 비운다. GitHash는 시작 시 `g_crashGitHash`로 복사해
  두고 크래시 경로는 사본만 읽는다. `Debug` 접근은 전부 `Log::IsAlive()`로 막았다.
- 모든 실패 지점이 `CrashNotify`로 stdout·디버거·로그에 동시에 남는다.
  0바이트 덤프는 지운다 — '덤프는 있는데 안 열린다'가 더 나쁘다.
- 시작 로그에 `크래시 덤프 기록자 등록 완료` 줄이 남는다. 이번 실행이 덤프를 남길 수
  있는 상태인지 세션 로그 첫머리만 봐도 알 수 있다.
- 덤프 보존 5개(`kMaxRetainedDumpFiles`). 전체 메모리 덤프는 하나가 2~3GB라
  정리 없이 두면 디스크가 차고, 그러면 다음 크래시의 덤프가 실패한다.
  실측으로 59GB(25개)가 쌓여 있었다.

**검증 수단 (새로 생김)**

CLI:
- `crash.status` — 기록자 등록 여부·덤프 경로·무인 모드
- `crash.test av|abort|terminate|throw` — 일부러 죽여 덤프 경로를 검증한다

회귀:
- `Tools/regression/verify-crash-dump.ps1` (run-all.ps1에 포함)
  세 경로 각각에 대해 `.dmp` 생성·크기·`.txt` 요약·심볼 붙은 스택·기록자 등록
  로그를 단정한다. 확인 후 만든 덤프는 지운다.

덤프 코드는 크래시가 나야만 실행되므로 평소엔 아무도 확인하지 않는다.
그래서 조용히 망가져 있었다. 이제는 매 회귀 실행마다 확인된다.

## 층 2 — 종료 시 간헐 크래시 (남음)

`ui_regression` 실행이 프로브를 전부 통과하고도 종료 코드
`0xC0000374`(힙 손상)로 죽는 경우가 있다. 간헐이며 단독 4회 재실행,
이어진 run-all 재실행에서는 재현되지 않았다.

- DX12 코드(PHASE 3-3/3-4)는 이 경로에서 실행되지 않으므로 그 변경과 무관하다.
- `run-all.ps1`이 종료 코드를 판정에 넣기 때문에 이 간헐 실패가 회귀 세트를
  붉게 만든다. 숨기지 말 것 — 고칠 것.
- 층 1을 고쳤으므로 **다음에 재현되면 덤프가 남는다.** 그게 이 층의 출발점이다.

**재현 시도**

```
x64\Debug\Academy_4Q.exe --script Tools\regression\ui_regression.txt
```
종료 코드 확인. 시나리오는 모델 배치 + 캔버스/텍스트/이미지 생성 +
재생·정지 4회. 크래시는 마지막 quit 이후 종료 구간에서 난다.

**이미 있는 단서**

`EngineBootstrap.h`의 `ShutdownTrace`가 `x64\Debug\Log\shutdown_trace.txt`에
종료 단계를 append한다. 마지막 줄이 곧 어디까지 갔는지다. 정상 종료는
`[8] atexit 도달`까지 찍힌다.

한 가지 알려진 크래시 지점(2026-07-31 19:06 덤프)은 종료가 아니라 커맨드 빌드
스레드였다: `RenderPassData::ClearRenderQueue`(RenderPassData.cpp:237)에서
`concurrent_vector::clear` 중 AV. 같은 원인일 수도 있으니 대조할 것.

## 하지 말 것

RectTransform/Canvas 레이아웃 코드와 RHI/DX12 코드는 건드리지 않는다 —
별개의 진행 중 작업이다.
