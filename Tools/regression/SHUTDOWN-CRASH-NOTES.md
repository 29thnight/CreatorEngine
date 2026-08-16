# 종료 시 크래시 · 덤프 누락 (해결)

두 층 모두 해결했다. 기록으로 남긴다.

## 층 1 — 덤프가 안 남는다

크래시 핸들러가 로그는 남기는데 `.dmp` 파일이 생기지 않았다.

**증거였던 것**

`x64\Debug\Log\Editor_20260731_102351.html`
```
10:24:21.508 CRASH - 미처리 예외로 프로세스 종료
             - ACCESS_VIOLATION @ 0x00007FF774C7C327
```
같은 시각 덤프 디렉터리에는 아무것도 없었고, 심볼 스택도 로그에 없었다.
로그에 CRASH 줄만 있고 스택도 덤프도 없다는 조합이 단서였다.

**원인 (넷)**

1. **순서.** `CoreWindow::WriteCrashDump`가 요약(`BuildCrashReport`)을 먼저 만들고
   덤프를 나중에 썼다. 요약은 dbghelp로 스택을 걷고 `std::string`을 늘려 가는데,
   힙이 손상됐거나 스택이 고갈된 프로세스에서는 바로 거기가 2차 크래시 지점이다.
2. **등록 시점.** 기록자 등록이 `App::Initialize` 중반에서만 불렸다. 크래시 후크는
   `Log::Initialize`가 훨씬 먼저 걸어 두므로, 부팅 구간 전체가 덤프 사각지대였다.
3. **댕글링 포인터 셋.** `CoreWindow::s_instance`, `EngineSettingInstance`, `Debug` —
   셋 다 종료 단계에서는 파괴된 인스턴스를 가리키는데 크래시 경로가 읽었다.
4. **조용한 실패.** 기록자 널·`CreateFile` 실패는 그냥 return이었고,
   `MiniDumpWriteDump` 실패 코드는 변수에 받아 놓고 버렸다.

**당시 한 일(3-2G 이전 구조)**

- `WriteMinidumpFile`을 떼어내 `.dmp`를 가장 먼저 쓴다. 요약(.txt)은 그 뒤다.
- 등록을 `EngineBootstrap::InitializeRuntime`의 `Log::Initialize` 직후로 옮겼다.
- `~CoreWindow`가 `s_instance`를 비운다. GitHash는 시작 시 사본을 떠 둔다.
  `Debug` 접근은 전부 `Log::IsAlive()`로 막았다.
- 실패 지점이 전부 `CrashNotify`로 stdout·디버거·로그에 남는다.
- 시작 로그에 `크래시 덤프 기록자 등록 완료` 줄이 남는다.
- 덤프 보존 5개(`kMaxRetainedDumpFiles`). 실측 59GB/25개가 쌓여 있었고,
  디스크가 차면 다음 크래시의 덤프가 실패한다.

## 층 2 — 종료 시 간헐 크래시

`ui_regression`이 프로브를 전부 통과하고도 종료 코드 `0xC0000374`(힙 손상)로
죽는 경우가 있었다. 간헐이라 단독 재실행으로는 잡히지 않았다(15회 무재현).

**어떻게 잡았나**

층 1을 고친 덕에 덤프가 남기 시작했고, 그 스택 두 건이 같은 곳을 가리켰다.

```
ShadowMapPass::CreateCommandListCascadeShadow → IRenderPass::PushQueue
  → concurrent_queue<ID3D11CommandList*>::push        (읽기 시도 0xFFFFFFFFFFFFFFFF)

RenderPassData::ClearRenderQueue
  → concurrent_vector<PrimitiveRenderProxy*>::clear   (같은 주소)
```

둘 다 **파괴된 동시성 컨테이너**를 만진 흔적이다. `Dx11Main::Finalize`를 보니
`isGameToRender`를 내려놓고 곧바로 `SceneManagers->Decommissioning()`으로 렌더
씬을 해체하고 있었다. 그 플래그는 루프 맨 위에서만 확인되므로, CB 스레드는
여전히 한 프레임 분량의 커맨드를 만드는 중이었다.

첫 프레임이 만들어지기 전에 종료를 걸면 이 창이 가장 넓어 거의 매번 재현됐다.

**한 일**

`isGameToRender = false` → `renderBarrier.Finalize()`(배리어에 걸린 스레드를 깨움)
→ CB/CE 종료 대기 → **그 다음에야** TagManager·CullingManager·SceneManager 해체.
게임 빌드(`GameMain::Finalize`)도 같은 순서였다.

**현재 구조(3-2G, 2026-08-16)**

`Core.Barrier`와 빈 CommandBuild 스레드는 삭제됐다. 메인 루프가 새 frame 발행을
끝낸 뒤 `PresentationThread`에 stop을 통지하고 join한다. 이어 전용 RenderThread의
bounded queue를 끝까지 drain하고 join한 다음에만 TagManager·SceneManager와 렌더
런타임을 해체한다. 종료 판정은 두 소비자 로그의 `pending 0 · balanced 1`이다.

| | 첫 프레임 전 종료 |
|---|---|
| 수정 전 | 6/6 · 5/5 · 5/6 크래시 (0xC0000005) |
| 수정 후 | 8/8 · 6/6 통과 |

**같이 고친 것**

- 표준 입력 리더를 `--console`에서만 띄운다. `--script`/`--exec`는 타이핑할
  사람이 없는데도 리더를 띄웠고, 그 스레드는 종료 시 `getline`에 갇힌 채
  detach됐다. `ExitProcess`가 CRT 내부(힙 락·iostream 버퍼)의 임의 지점에서
  그 스레드를 죽이므로, 이것도 종료 구간 힙 손상의 구조적 원인이다.
  회귀 세트가 전부 CLI 실행이라 이 경로에서만 나타났다.
- `--script`가 파일을 못 열면 크게 알리고 종료한다. 예전에는 로그에만 남기고
  명령 하나 없이 계속 돌아 하네스 타임아웃까지 매달렸다.
- 종료 추적에 `[9] 정적 소멸 완료`를 추가했다. `[8] atexit`는 CRT 정리가
  *시작*될 때 찍혀서, 그 뒤에 죽어도 정상 종료와 구분되지 않았다.

## 남은 수단

CLI:
- `crash.status` — 기록자 등록 여부·덤프 경로·무인 모드
- `crash.test av|abort|terminate|throw` — 일부러 죽여 덤프 경로를 검증
- `--heapcheck` — CRT 디버그 힙 전수 검사. 힙 손상은 망가뜨린 코드와 죽는
  지점이 멀어서, 켜면 망가뜨린 그 호출에서 멈춘다(대신 매우 느리다)

회귀(`run-all.ps1`에 포함):
- `verify-shutdown-order.ps1` — 첫 프레임 전 종료 6회
- `verify-crash-dump.ps1` — 크래시 3경로가 덤프와 심볼 스택을 남기는지

둘 다 수정을 되돌린 빌드에서 실패하는 것을 확인했다.

## 다시 이런 걸 쫓게 되면

1. `x64\Debug\Log\shutdown_trace.txt` — 세션별 마지막 줄이 어디까지 갔는지다.
   `[9]`가 없으면 정적 소멸 구간, `[9]`까지 있으면 힙·DLL 정리 구간이다.
2. `x64\Debug\Dump\*.txt` — 심볼 붙은 스택. 덤프는 5개만 보존된다.
3. 재현이 안 되면 창을 넓혀라. 종료 경합은 "종료를 최대한 이르게" 걸면 커진다.
