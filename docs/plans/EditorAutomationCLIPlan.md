# 라이브 커맨드 서비스 — 켜져 있는 에디터·플레이어에 명령을 주입한다 (PHASE 14.5 · LC 트랙)

작성: 2026-08-28. **전면 개정: 2026-09-04** — 배치 CLI 정식화 계획을 로컬 HTTP/JSON
라이브 서비스 계획으로 재수립하고, PHASE 6에서 떼어 PHASE 14.5로 옮긴다.

상태: **LC0 완료(2026-09-04). LC1~LC9는 구현·빌드·런타임 검증 전이다.**

LC0이 실측을 냈으므로 이 문서의 수치는 두 종류다 — **실측**(§2.1.1 · §7.1.1 · §13 LC0)과
**정적 추정**(나머지). 실측이 정적 추정을 정정한 자리는 그 자리에 명시했다. 앞선 판의
§2.1 표는 §2.1.1이 대체한다.

2026-09-04 2차 대조(정적)에서 §2의 수치·인용 좌표는 재확인됐고, 네 곳이 어긋나 고쳤다 —
직접 `SetExitCode` 수(§3.1), 배치 exit 표와 Player 현행 값의 충돌(§5.4),
LC0 inventory의 출처(§2.1 ★), `Engine/CommandService`의 빌드 단위 부재와
`Debug`/`Release` 두 구성뿐인 솔루션(§12.1 신설). 이 넷은 착수 전에 알아야 공수가
맞는 것들이라 본문에 반영했다.

파일 이름은 `EditorAutomationCLIPlan.md`로 유지한다 — 대시보드와 다른 계획 두 곳이 이
경로를 참조하고 있어 개명 이득이 링크 파손 비용보다 작다.

선행: PHASE 0의 `0-8`(`--exec`/`--script`/`--console`, command queue, frame-boundary
`Pump`) 완료. 이 계획은 그 구현을 폐기하지 않는다. 배치 프론트엔드는 그대로 두고 같은
실행 코어 위에 **서비스 프론트엔드**를 하나 더 얹는다.

관련: `ProfilingCapturePlan.md`(PHASE 14 — Development/Shipping 구성 구분과 계측 카운터를
공유한다) · `BuildPipelinePlan.md`(build/package 결과와 process exit) ·
`EngineLayerSeparationPlan.md`(Editor operation·Play mode 경계) ·
`SerializationPlan.md`(Player의 `runtime.text-parser calls=0` 게이트) ·
`EngineDistributionAndLauncherPlan.md`(`--project` Host 계약).

---

## 0. 한 줄 결론

지금의 CLI는 **배치 모델**이다. 프로세스를 켜고, 명령을 주고, 죽인다. 명령 하나를 더
보내려면 에디터를 다시 켜야 하고, 그 비용이 부팅 시간 전부다.

목표는 **서비스 모델**이다. 이미 켜져 있는 에디터에 로컬 HTTP로 붙어, JSON 한 덩이를
보내고, 같은 연결에서 결과를 받는다. 프로세스 수명이 명령 수명과 분리되면 다음이 따라온다.

1. 에디터를 껐다 켜지 않고 명령을 연속으로 보낸다.
2. 명령의 결과가 stdout 문자열이 아니라 호출자에게 값으로 돌아온다.
3. 실행 중인 Development Player에도 같은 계약으로 붙는다.
4. C# 어셈블리를 라이브 메모리에서 교체하고, 교체 결과를 같은 세션에서 확인한다.

이 계획이 닫아야 할 것은 "명령을 더 만드는 일"이 아니라 다음 다섯 계약이다.

| 계층 | 계약 | 없으면 벌어지는 일 |
|---|---|---|
| L0 | 결과·인자·descriptor 정본 | HTTP 200에 담을 값이 없다. 지금 결과는 `printf`로 stdout에만 있다 |
| L1 | 로컬 HTTP/JSON 전송과 인증 | 실행 표면이 무인증으로 열린다 |
| L2 | 프레임 예산 드레인·세션·취소 | 프레임당 1명령이라 명령 N개는 N프레임이 걸린다 |
| L3 | 라이브 코드 실행 등급 | "즉시 실행"이 임의 코드 실행과 같은 말이 된다 |
| L4 | Player Development 에이전트 | 서비스 코드가 Shipping 바이너리로 샌다 |

완료 모습:

```text
 에이전트 · CI · 사람 · 에디터 툴
              │  HTTP/1.1 keep-alive · 127.0.0.1:<port> · JSON · Bearer token
              ▼
      CommandService (수신 스레드)
      loopback bind · 토큰 · 본문 한계 · 감사 로그
              │  CommandInvocation(owned) + correlationId
              ▼
        service queue ─────┐
                           ├──► CommandExecutor ◄──── batch queue (--exec/--script/--console)
        프레임 경계 Pump이 ┘     descriptor · precondition · affinity · session
        예산만큼 드레인
              │
       ┌──────┴────────┐
       ▼               ▼
  즉시 완료 명령     장시간 명령
  CommandResult      operationId → 202 → 폴링/스트리밍
       │               │
       └──────┬────────┘
              ▼
        CommandResult (owned)
       ┌──────┼────────┬────────────┐
       ▼      ▼        ▼            ▼
  HTTP JSON  JSONL   human console  감사 로그
```

Editor와 Development Player는 **같은 서비스 계약**을 쓰고 **다른 registry**를 갖는다.
Player registry에는 에디터 저작 명령이 없다.

---

## 1. 범위와 비범위

### 1.1 범위

- 로컬 HTTP/1.1 + JSON 명령 서비스(에디터). loopback 전용, 토큰 인증, 감사 로그
- 실행 중인 에디터에 대한 실시간 명령 주입과 **초 이하 왕복** 지연 계약
- 프레임 예산 드레인 — 명령 N개가 N프레임을 기다리지 않게 한다
- 장시간 명령의 operation id·진행 스트림·취소
- `CommandInvocation` / `CommandDescriptor` / `CommandResult` / session 도입(L0)
- registry에서 생성되는 discovery(`GET /commands`) — 문서가 아니라 런타임이 정본
- 라이브 코드 실행 A/B 등급: 등록 명령 실행 · 로드된 어셈블리의 표식된 메서드 호출
- `script.reload`(collectible ALC 교체)를 서비스 계약으로 승격
- Development Player 에이전트와 Development/Shipping 구성 구분
- 11,968줄 `ConsoleCommandSystem.cpp`의 도메인별 buildable 분리
- 기존 배치 소비자 81개의 점진적 기계 판독 결과 전환

### 1.2 비범위

- 원격(비 loopback) 접속·인증서·TLS·다중 사용자 권한 모델
- MCP server/tool 노출 — §16에서 다시 판정한다(보류 근거가 바뀐다)
- 임의 C# 스니펫 컴파일 실행(L3-C) — 별도 플래그·별도 슬라이스로 분리하고 이번 완료
  조건에 넣지 않는다
- Editor command palette·새 GUI 명령 창
- 동적 command plugin/DLL discovery
- 새 외부 문법과 dotted ID 전면 개명
- 별도 `creator.exe`/UAT형 host 신설
- 렌더 창을 만들지 않는 진짜 headless Editor
- 모든 명령의 worker thread 실행 전환
- Shipping Player에 서비스를 남기는 어떤 형태의 타협도

`CoreWindow::SetUnattended(true)`는 대화상자를 억제할 뿐 headless가 아니다. 이번 계획은
그 사실을 숨기지 않는다. 서비스 모드에서도 에디터 창은 뜬다.

---

## 2. 현재 소스 기준선 (2026-09-04 정적 재실측)

### 2.1 규모 — 일주일 만에 77% 자랐다

| 항목 | 2026-08-28 | 2026-09-04 | 증가 |
|---|---:|---:|---:|
| `ConsoleCommandSystem.cpp` | 6,756줄 | **11,968줄** | +77% |
| registration call | 162 | **205** | +27% |
| 등록 이름(별칭 포함) | 176 | **219** | +24% |
| `Cmd_*` handler | 153 | **184** | +20% |
| `std::printf` call | 490 | **683** | +39% |
| CLI를 호출/파싱하는 추적 파일 | 40 | **81** | +103% |

이 표가 이 계획의 착수 근거다. 앞선 판 서문은 "새 명령을 계속 보태기 전에 계약을
세운다"였는데, 계약이 서기 전에 파일이 두 배 가까이 자랐다. 계약 없이 자라는 표면은
계약을 세우는 비용도 같이 키운다. `printf` 683회는 그대로 "결과가 값이 아니라 문자열로만
존재하는 자리" 683곳이다.

(집계 방법: `wc -l`, `reg(` 호출 grep, `reg({...})` 안의 문자열 리터럴 수, `Cmd_` 정의
grep, `std::printf` grep, `Tools/`·`scripts/`에서 `--exec|--script|Academy_4Q.exe` 참조
파일 수. 소스 스크래핑이라 오차가 있고, LC0에서 **런타임 표**로 다시 생성한다.)

★ **어느 표에서 다시 생성하는가.** LC3의 descriptor registry는 아직 없다 — LC0이 그것을
기다리면 착수가 세 슬라이스 뒤로 밀린다. 오늘의 표는
`ConsoleCmd::GetTable()`의 `unordered_map<std::string, ConsoleCommandHandler>`이고,
LC0은 **이 표를 그대로 덤프한다**. 다만 그 표의 두 성질을 알고 써야 한다.

- **별칭이 별도 entry다.** `reg({"quit","exit"}, &Cmd_quit)`는 항목 둘을 만든다. canonical
  대 alias 구분은 표에 없고, **handler 함수 포인터가 같다**는 사실로만 묶인다. 219개
  이름을 184개 handler로 그룹핑하면 alias 관계가 복원된다 — LC0 inventory는 그 그룹을
  낸다.
- **순회 순서가 비결정적이다.** `unordered_map`이므로 덤프는 반드시 이름으로 정렬한다.
  정렬 없이 낸 artifact는 실행마다 diff가 난다.

즉 LC0의 산출은 "descriptor snapshot"이 아니라 **이름↔handler 그룹 inventory**이고,
LC3이 descriptor를 세운 뒤 같은 artifact를 descriptor에서 재생성해 두 값이 일치하는지
대조한다. 그 대조가 LC3의 coverage 게이트에 이빨을 준다.

### 2.1.1 LC0 실측 결과 — 위 표를 대체한다 (2026-09-04)

`commands.dump`(런타임)와 `lc0-static-inventory.ps1`(정적)이 낸 값이다. LC0 계측 명령
3개를 뺀 착수 직전 값으로 적는다.

| 항목 | §2.1 정적 추정 | LC0 실측 | 비고 |
|---|---:|---:|---|
| 명령 그룹(= `reg()` 호출) | 184 (`Cmd_*` 정의 수) | **205** | 정적 추정이 틀렸다 — 아래 ★ |
| 등록 이름(별칭 포함) | 219 | **219** | 일치 |
| CLI 소비 파일 | 81 | **85** | 아래 ★★ |
| 소스를 긁는 소비자 | (미집계) | **10** | LC3 discovery가 대체 |
| 한국어 verdict 정규식 소비자 | (미집계) | **1** | `Invoke-Dx12Suite.ps1` |
| 직접 `SetExitCode`(Editor) | "두 곳" | **12** | §3.1에서 이미 정정 |
| raw line 재해석 지점 | (미집계) | **25** | LC2가 0으로 민다 |
| 사람이 읽는 verdict print | (미집계) | **87** | LC9 전까지 문안 고정 |
| help 안내 항목 | (미집계) | **143줄** | |
| help에 실린 명령 | (미집계) | **130 / 205** | 커버리지 **63%** |
| **help에만 있고 등록은 없는 이름** | (미집계) | **6** | 아래 ★★★ |

★ **"184 handler"는 그룹 수가 아니었다.** 그 수는 `Cmd_` 접두사를 가진 함수 정의를 센
것인데, 등록의 일부는 `Handle*` 같은 다른 이름의 함수를 가리킨다. 런타임 그룹은 205이고,
정적 `reg(` 호출 수와 정확히 일치한다. 두 방법이 같은 값에 닿는 것이 이 inventory의
교차 검증이다.

★★ **소비자 81은 죽은 이름으로 센 값이다.** §2.1의 집계는 `Academy_4Q.exe`를 패턴에
넣었는데 그 이름은 저장소에 **0건**이다 — 실행 파일은 `CreatorEditor.exe`로 개명됐다.
현행 이름을 넣으면 85다. (이 계획 본문과 `ConsoleCommandSystem.h`의 사용 예시도 아직
옛 이름을 쓴다. LC9의 문서 이관에 포함한다.)

★★★ **help가 없는 명령 6개를 안내한다.** `experiment.anim` · `experiment.bench` ·
`experiment.fbx` · `experiment.gltf` · `experiment.import` · `experiment.model` — 전부
등록 표에 없다. help를 보고 그대로 치면 "알 수 없는 명령"이 뜨고, **프로세스는 0으로
끝난다**(§3.1). §3.4의 drift가 추상적 우려가 아니라 6건의 실물이라는 뜻이다.

### 2.2 입력·실행 수명

| 항목 | 현재 소스 | 판정 |
|---|---|---|
| 실행 인자 | `ConsoleCommandSystem.cpp:614~640` | `--exec` 반복, `--script`, `--console`, `--heapcheck` |
| stdin | reader thread가 raw 한 줄을 queue에 넣기만 함 | parse는 GT `Execute()`에서 |
| queue | `ConsoleCommandSystem.h:103~104` | mutex + deque |
| 주입 표면 | `void Enqueue(std::string)` — **이미 스레드 안전** | 외부 스레드가 명령을 넣는 것은 오늘도 된다 |
| 결과 반환 | **없다** | 호출자에게 돌려줄 값이 없다. `printf`가 유일한 출구 |
| 실행 | `Pump()` — 프레임당 **정확히 한 명령** | wait/씬 로딩 중에는 0개 |
| App 연결 | `App.cpp:261~262` | `m_main->Update()` 직후 매 프레임 `Pump()` |
| 메시지 루프 | `CoreWindow.h:77~99` — `PeekMessage(PM_REMOVE)` 폴링 | 메시지가 없으면 곧장 프레임을 돈다 |
| 프레젠트 | `DX12DeviceResources.cpp:1391` — `Present(0, 0)` | vsync 대기 없음 |

★ **지연에 유리한 사실 하나와 불리한 사실 하나가 같은 곳에 있다.** 유리한 쪽 — 메시지
루프가 `PeekMessage` 폴링이고 present가 vsync를 기다리지 않으므로, 창이 비활성이어도
프레임은 계속 돈다. 즉 "에디터가 놀고 있어서 명령이 안 나간다"는 상황이 구조적으로는
없다. 불리한 쪽 — `Pump()`가 프레임당 한 명령이라 명령 10개는 최소 10프레임이고, 그
앞에 `IsSceneLoading()` 조기 반환과 `wait N` 프레임 보류가 있어 **큐가 임의로 길게 멈출
수 있다**. 서비스는 이 멈춤을 지연으로 숨기지 말고 상태로 응답해야 한다.

### 2.3 서비스가 기댈 곳과 없는 것

| 필요한 것 | 현재 저장소 | 판정 |
|---|---|---|
| 소켓/HTTP | 1st-party 소스에 `WSAStartup`/`winsock`/`httplib`/`asio` **0건** | 전부 신설 |
| JSON | nlohmann은 PHASE 17 D4에서 은퇴, ryml은 저작 YAML backend | 신설하거나 ryml 재사용 판정 필요 |
| 관리 코드 라이브 교체 | `ClrHost::ReloadScripts()` + `ScriptLoadContext(isCollectible: true)` | **있다** |
| 리로드 시 값 보존 | `Cmd_script_reload`가 `PrepareForReload` → 교체 → `OnInitialized` 복원 | **있다** |
| 리로드 완료 관측 | `IsPreviousContextAlive()` / `script.status` | 있다(호출 스택 때문에 몇 프레임 뒤 확인) |
| Player 명령 표면 | `PlayerApp.cpp:166~175`의 `--smoke <frames>` 하나 | 신설 |
| Development/Shipping 구성 | `CE_SHIPPING`/`DEVELOPMENT_BUILD` 류 매크로 **0건** | 신설. PHASE 14가 같은 것을 필요로 한다(§11) |

★ **`Enqueue`가 이미 스레드 안전하다는 사실이 이 계획을 작아 보이게 만든다. 아니다.**
명령을 넣는 것은 오늘도 되지만, 넣은 사람이 결과를 받을 방법이 없다. HTTP 응답에 담을
값을 만드는 일(L0)이 전송 계층(L1)보다 크고 먼저다.

### 2.4 소비자가 문자열 형상에 의존한다

`Tools/dx12-validation/Invoke-Dx12Suite.ps1`은 명령 목록을 C++ 소스의 `"dx12.*"` 문자열
리터럴에서 뽑고, 판정을 `^[CLI] <name> (통과|실패|완료)` 한국어 정규식으로 읽는다. 그
스크립트 주석에는 help가 낡아 35개 중 26개만 실행한 사례와, registry 구현 형식이 바뀌어
35개를 0개로 읽은 사례가 이미 기록돼 있다. 서비스가 서면 이 소비자들은 정규식이 아니라
`GET /commands`와 JSON 결과를 읽는다.

---

## 3. 먼저 닫아야 할 정확성 결함

L0 없이 HTTP만 얹으면 아래 결함이 그대로 네트워크 표면으로 승격된다.

### 3.1 실패가 process success로 남을 수 있다

대부분의 selftest handler는 `bool passed`를 받은 뒤 `printf`/Debug log만 남긴다. session
결과 정본이 없어서, 명령이 "실패"를 출력한 뒤 `quit`하면 프로세스가 0으로 끝날 수 있다.
게다가 `Execute()`는 **unknown command도 `printf` 한 줄 뒤 그냥 return**한다 — 오타 하나가
조용히 exit 0이다.

직접 `SetExitCode`는 `ConsoleCommandSystem.cpp`에 **12곳**(값 `5` 6회 · `6` 6회)이고
`PlayerMain.cpp`에 6곳(값 `2`·`3`·`4`)이다. 두 실행체가 같은 숫자에 다른 뜻을 얹고 있고,
어느 handler가 쓰고 어느 handler가 안 쓰는지에 규칙이 없다. §5.4의 표는 이 둘을 하나의
사상으로 통일하는 것이며, LC1이 Editor를, LC8이 Player를 그 표로 옮긴다.

★ 다행히 이 remap은 소비자를 깨지 않는다 — `Tools/build.ps1`(Player smoke)과
`Tools/regression/run-all.ps1`은 모두 `-ne 0`만 본다. 특정 비-0 값에 의존하는 소비자는
2026-09-04 실측 기준 0건이다. 이 사실은 LC0 inventory가 다시 확인한다.

결정:

- 모든 명령은 정확히 하나의 terminal `CommandResult`를 만든다.
- session은 결과 severity를 누적하며 뒤의 성공/`quit`가 앞의 실패를 지우지 못한다.
- 배치 모드 기본은 continue + aggregate, `--fail-fast`만 조기 중단.
- 서비스 모드에서는 요청 하나가 세션 하나이고, 결과는 HTTP 상태와 body 양쪽에 실린다.
- 직접 `EngineBootstrap::SetExitCode`는 최종 session adapter 한 곳만 허용한다.

### 3.2 tokenizer 결과를 핸들러가 다시 버린다

`Split()`은 큰따옴표 구간을 하나의 token으로 만든다. 그러나 `object.parent`,
`object.duplicate`, `prefab.create` 등은 `parts.back()`과 `line.substr()/rfind()`로 raw
원문을 다시 역산한다. `object.parent "Big Boss Character" "Main Characters"`에서 복원된
child 이름에는 따옴표가 남는다.

★ **서비스에서는 이 결함이 다른 얼굴로 재발한다.** JSON이 `{"args":["Big Boss","Main
Characters"]}`로 이미 갈라 온 값을 라인 문법으로 다시 이어 붙였다가 다시 자르면, 그
왕복에서 같은 손실이 생긴다. 그래서 **서비스 입력은 라인 문자열을 거치지 않는다** —
JSON 배열이 곧장 `CommandInvocation`의 owned argument가 된다(§6).

결정:

- handler에는 raw `line`을 주지 않는다.
- 공백 포함 문자열의 배치 문법은 quote다. 서비스 문법은 JSON 배열이다.
- 기존의 "마지막 token을 둘째 이름, 앞의 raw remainder를 첫째 이름" 입력은 호환 adapter로
  한정하고 deprecation을 계측한다.
- Windows path의 `\`는 임의 escape로 소비하지 않는다. quote 안에서는 `\"`와 `\\`만
  처리하고 나머지 backslash는 보존한다.

### 3.3 같은 primitive가 같은 Editor operation을 뜻하지 않는다

- CLI `object.duplicate`와 GUI duplicate는 `Object::Instantiate`를 공유하지만 GUI 쪽에는
  Undo·선택·부모 보정이 더 있다.
- CLI `play`와 GUI Play 버튼은 `SetGameStart`를 공유하지만 UI 쪽은 `m_isGameMode`도 쓴다.

결정: "같은 low-level 함수"를 동등성 완료로 판정하지 않는다. mutating command마다
canonical Editor operation / 의도된 raw primitive / test-only probe 중 하나로 분류하고
descriptor에 표시한다(§9).

### 3.4 수동 help와 registry가 이미 갈라졌다

`PrintHelp()`는 하나의 긴 `printf` 문자열이고 registry는 별도 표다. 이름·alias·summary·
arguments·capability·help의 정본은 descriptor 한 곳이어야 한다. `GET /commands`가 그
snapshot을 그대로 낸다 — 에이전트가 무엇을 부를 수 있는지 문서를 읽지 않고 런타임에
묻는다.

---

## 4. 고정 결정

| 결정 | 채택 | 이유 |
|---|---|---|
| 전송 | **로컬 HTTP/1.1 + JSON** | 언어 중립. `curl`·PowerShell `Invoke-RestMethod`·에이전트가 추가 클라이언트 없이 소비한다 |
| bind | **127.0.0.1 / ::1 전용** | 서비스는 실행 표면이다. 비 loopback bind는 정적 게이트로 금지 |
| 인증 | **요청마다 Bearer token** | 같은 머신의 다른 프로세스도 loopback에 붙을 수 있다 |
| HTTP 구현 | **자체 최소 서브셋(Winsock2)**, 단 소켓 호출은 **플랫폼 이음매 뒤에** | 필요한 것은 loopback POST/GET·Content-Length·keep-alive뿐. 1st-party 소스의 소켓 의존이 0인 상태에서 포트를 새로 들이는 비용 > 구현 비용. 기각 근거는 §17. 이음매 근거는 §4.1 |
| JSON | **자체 codec** | 스키마가 작고 고정이다. ryml 재사용은 authoring 계측 경로(`AuthoringParsedDocument`)와 얽혀 Player의 `runtime.text-parser calls=0` 게이트를 흔든다 |
| 배치 CLI | **유지** | 소비자 81개와 CI. 서비스는 대체가 아니라 추가 프론트엔드 |
| canonical ID | dotted ID 유지 | 기존 scenario 호환 |
| 실행 지점 | **프레임 경계 `Pump()` 유지** | Scene/Editor state의 GT 규약 보존 |
| 드레인 | **예산 기반(시간·개수)** | 지연 계약. 단 배치 큐의 "프레임당 1개" 의미는 보존(§7.2) |
| 장시간 명령 | **operationId + 폴링/스트리밍** | 1초 계약을 거짓말로 만들지 않기 위해 |
| 결과 정본 | `CommandResult` | printf/Debug/process exit 삼중 판정 제거 |
| schema 정본 | `CommandDescriptor` | help/discovery drift 제거 |
| 기본 affinity | `GameThread` | 기존 handler의 thread-safety를 추측해 옮기지 않음 |
| 코드 실행 | **A/B 우선, C(스니펫 컴파일)는 opt-in 별도 슬라이스** | 임의 코드 실행은 승인·격리 모델이 먼저다 |
| Player | **Development 구성 전용 · 기본 off · 명시 플래그** | Shipping 표면 0 |
| headless | 비범위 | 현재 Editor 창·renderer 의존과 다른 문제 |
| MCP | **여전히 이 계획 밖** | 다만 보류 근거가 바뀐다 — §16 |

### 4.1 소켓은 이음매 뒤에 둔다 (2026-09-04 결정)

Winsock2 는 Windows 전용이다. 그것이 새 제약을 만드는가를 먼저 쟀다.

| 항목 | 실측 |
|---|---|
| 빌드 시스템 | `.sln`/`.vcxproj` 전용 — CMake 없음 |
| `Windows.h` 계열을 include 하는 1st-party 파일 | 32 |
| `HWND`/`HINSTANCE`/`WndProc` 를 쓰는 파일 | 29 |
| `#ifdef __linux__` 류 플랫폼 분기 | **0** |

엔진은 이미 Win32 에 묶여 있고 **이식을 시도한 흔적이 한 번도 없다**. 소켓이
포터블해져도 창·DX12·빌드 시스템이 따라오지 않는다. 그리고 이 저장소에는 이미
방침이 있다 — `EngineDistributionAndLauncherPlan.md` §147: "초기 릴리스에서
macOS/Linux 설치기를 함께 설계하지 않는다. **descriptor와 Host 계약만 플랫폼
중립으로 둔다.**" 구현은 Windows, 계약은 중립이다.

그래서 진짜 질문은 "Winsock2 가 Windows 전용인가"가 아니라 **"LC4 가 원래 중립이어도
될 곳까지 Windows 로 물들이는가"** 이고, 그것은 실재하는 위험이다. 이유 셋.

1. §12 가 `Engine/CommandService` 를 **Editor 와 Player 가 공유**하게 두었다.
   LC8 이 Development/Shipping 을 가를 때 이 모듈이 양쪽에 들어간다.
2. §12 의 규칙("CommandService 는 Editor 헤더를 include 하지 않는다")과 같은 규율이
   플랫폼 헤더에도 적용돼야 한다.
3. **`<winsock2.h>` 는 `<windows.h>` 보다 먼저 와야 한다.** 순서가 뒤집히면
   windows.h 가 끌고 오는 WinSock 1.1 선언과 충돌해 재정의 오류가 난다. 이것을
   공개 헤더에 두면 CommandService 를 include 하는 **모든 TU** 가 그 지뢰를 물려받는다.

**결정: Winsock2 를 쓰되 소켓 호출을 `.cpp` 한 곳에 가둔다.**

```text
Engine/CommandService/
  SocketPlatform.h          중립 타입(listen/accept/recv/send·오류 코드)
  SocketPlatform_Win32.cpp  <winsock2.h> 는 여기에만
  HttpRequest.*             파싱·헤더 한계          — 중립
  HttpListener.*            수신 루프·keep-alive     — 중립
  CommandService.*          라우팅·인증·operation 표 — 중립
  JsonValue.*               codec                    — 중립
```

이식 시 남는 델타는 `WSAStartup`/`WSACleanup` · `SOCKET` 대 `int` ·
`closesocket` 대 `close` · `WSAGetLastError` 대 `errno` 정도다(`WSAPoll` 은
`poll` 과 시그니처가 같다). **이음매 비용은 사실상 0 이고, 없을 때의 비용은
LC4 본체 전부가 Windows 맛이 되는 것**이다.

라이브러리(asio·cpp-httplib)로 가지 않는다. §17 이 이미 기각했고, 엔진이 어차피
Windows-only 인 지금 이식성은 그 결정을 뒤집을 근거가 못 된다. §17 이 적어 둔
되돌릴 조건은 그대로다 — **자체 파서의 결함을 §14.3 으로 통제하지 못하면** 재판정.

---

## 5. 서비스 계약 (L1)

### 5.1 수명과 발견

- 에디터는 `--command-service`(또는 설정)로 켜질 때만 listen한다. 기본은 off.
- 포트는 0으로 요청해 OS가 배정한다. 고정 포트를 강제하지 않는다(에디터 다중 실행).
- 배정 결과를 프로젝트 아래 `Library/CommandService/endpoint.json`에 쓴다.

```json
{
  "schemaVersion": 1,
  "pid": 24680,
  "port": 51837,
  "token": "<32바이트 난수의 base64url>",
  "host": "127.0.0.1",
  "project": "C:/.../Dynamic_CPP",
  "role": "editor",
  "startedUtc": "2026-09-04T04:12:33Z"
}
```

- 파일은 사용자 전용 ACL로 만든다. 토큰은 로그·결과·감사 기록 어디에도 남기지 않는다.
- 정상 종료 시 파일을 지운다. 시작 시 남아 있는 파일은 `pid`가 살아 있는지 확인하고
  죽었으면 회수한다 — 크래시 후 유령 endpoint가 클라이언트를 엉뚱한 곳으로 보내지 않게.

### 5.2 표면

| 메서드 · 경로 | 뜻 |
|---|---|
| `GET /health` | 살아 있는가. role·pid·frame·상태(idle/loading/playing) |
| `GET /commands` | registry snapshot. descriptor 전체 |
| `GET /commands/{id}` | 명령 하나의 descriptor |
| `POST /command` | 명령 하나 실행 |
| `POST /commands/batch` | 순서 있는 명령 묶음 실행(한 세션) |
| `GET /operations/{id}` | 장시간 명령의 현재 상태 |
| `POST /operations/{id}/cancel` | 취소 요청 |
| `GET /operations/{id}/stream` | 진행 이벤트 스트림 |

요청:

```json
{ "command": "object.parent",
  "args": ["Big Boss Character", "Main Characters"],
  "correlationId": "a41f",
  "timeoutMs": 2000,
  "mode": "auto" }
```

- `args`는 문자열 배열이다. 라인 문자열을 보내지 않는다.
- `mode`: `auto`(기본 — 짧으면 동기, 길면 202) · `sync` · `async`.
- `timeoutMs`를 넘기면 서버는 명령을 죽이지 않고 `202` + `operationId`로 승격한다.
  이미 시작한 GT 작업을 중간에 끊는 것이 더 위험하다.

응답(동기 성공):

```json
{ "schemaVersion": 1, "correlationId": "a41f", "sequence": 7,
  "command": "object.parent", "status": "succeeded", "code": "ok",
  "message": "parented", "data": {},
  "timing": { "queuedMs": 1.2, "waitedFrames": 1, "executedMs": 0.4 } }
```

`timing`은 장식이 아니라 지연 계약의 증거다. 클라이언트가 SLO 위반을 직접 관측할 수
있어야 "느린 것 같다"가 측정으로 바뀐다.

### 5.3 HTTP 상태 사상

| 상태 | 뜻 |
|---:|---|
| 200 | 명령이 실행됐다. 논리 성공/실패는 body의 `status`가 말한다 |
| 202 | 접수했고 `operationId`로 진행한다 |
| 400 | JSON·문법·인자 오류 (`InvalidArguments`) |
| 401 | 토큰 없음/불일치 |
| 403 | capability가 이 role에서 금지됨 (예: Player에 에디터 저작 명령) |
| 404 | 없는 명령 또는 없는 operation |
| 409 | precondition 불충족 — 씬 없음, 백엔드 없음, 로딩 중 |
| 413 | 본문 초과 |
| 429 | 큐 상한 초과 |
| 503 | 서비스가 드레인 중이거나 종료 중 |

★ **논리 실패를 500으로 내지 않는다.** selftest가 정직하게 "실패"를 판정한 것과 서버가
망가진 것은 다른 사건이고, 섞으면 클라이언트가 재시도해서는 안 될 것을 재시도한다.
`500`은 handler 밖의 내부 오류에만 쓴다.

### 5.4 배치 exit code (배치 모드 유지)

| exit | 의미 |
|---:|---|
| 0 | 모든 실행 명령 성공 |
| 2 | CLI 문법·unknown command·argument 오류 |
| 3 | precondition 불충족 |
| 4 | 명령 또는 selftest 판정 실패 |
| 5 | build/IO/내부 infrastructure 오류 |

OS exception/crash code는 덮어쓰지 않는다. session은 숫자 최대값이 아니라 명시한 severity
순서로 가장 심한 결과를 보존한다.

이 표는 **현행 값을 이어받지 않고 갈아엎는다.** 지금 Editor CLI는 `5`·`6`을, Player는
`2`·`3`·`4`를 각각 다른 뜻으로 쓴다(§3.1). 특히 Player의 현행 `2`(렌더러·ImGui 초기화
실패)는 새 표에서 `5`(infrastructure)로 간다. 이 이동이 안전한 근거는 §3.1에 있다 —
현재 어떤 소비자도 특정 비-0 값을 판정에 쓰지 않는다. LC0이 그 사실을 inventory로 고정한
뒤에만 LC1이 remap을 시작한다. 그 전에 값을 바꾸면 근거 없이 바꾸는 것이다.

---

## 6. 실행 코어 계약 (L0)

### 6.1 Invocation

```cpp
enum class CommandSource : uint8_t
{
    ExecArgument,
    ScriptFile,
    InteractiveConsole,
    HttpService,        // 신설
};

struct CommandInvocation
{
    uint64_t                     sequence{};
    CommandSource                source{};
    std::string                  commandId;
    std::vector<CommandArgument> arguments;
    std::string                  sourceName;     // script path · "--exec" · "stdin" · 클라이언트 라벨
    uint32_t                     sourceLine{};
    uint64_t                     sessionId{};    // 서비스 세션 또는 배치 세션
    std::string                  correlationId;  // 서비스 요청 대응
};
```

- queue를 건너는 값은 전부 owned value다.
- `string_view`, `Scene*`, `Entity*`, backend handle을 queue에 보관하지 않는다.
- raw line은 배치 frontend의 진단에만 남고 handler에 넘어가지 않는다.

### 6.2 Descriptor

```cpp
struct CommandDescriptor
{
    std::string_view                           name;
    std::span<const std::string_view>          aliases;
    std::string_view                           summary;
    CommandCategory                            category;
    std::span<const CommandArgumentDescriptor> arguments;
    CommandCapabilities                        capabilities;
    CommandExecutionAffinity                   affinity;
    CommandCostClass                           cost;     // 신설: Immediate / Frames / Long
    CommandRoleMask                            roles;    // 신설: Editor / Player / Both
    CommandHandler                             execute;
};
```

최소 capability: `RequiresEditor` · `RequiresScene` · `MutatesScene` · `MutatesAssets` ·
`UsesUndo` · `RequiresRenderer` · `RequiresDX12` / `RequiresVulkan` · `UnattendedSafe` ·
`MayBlock` · `TestOnly` · **`ServiceExposed`** · **`ExecutesUserCode`**.

★ 신설 셋의 이유. `cost`는 서비스가 `sync`/`202`를 **추측하지 않고** 결정하게 한다.
`roles`는 Player registry가 에디터 저작 명령을 실수로 물려받지 않게 한다.
`ExecutesUserCode`는 L3에서 승인 경계를 그을 유일한 표식이다.

capability는 권한 시스템이 아니다 — 실행 전 조건 검사, discovery 표시, 하네스 선별,
그리고 서비스 노출 판정을 위한 정적 사실이다.

### 6.3 Arguments

`ArgumentKind`는 현재 명령 표면만 담는다: `String`, `Bool`, `Int64`, `Double`, `Path`,
`AssetPath`, `EntityName`, `Enum`, `Remainder`(명령당 마지막 하나).

Entity/asset의 실제 해석은 parser가 아니라 GT precondition 단계가 한다. parser thread가
live Scene이나 DataSystem을 읽지 않는다. 이 규칙은 서비스에서 더 중요하다 — 수신 스레드는
GT가 아니다.

### 6.4 Result

```cpp
enum class CommandStatus : uint8_t
{
    Succeeded, InvalidArguments, PreconditionsFailed, Failed,
    Cancelled, TimedOut, InternalError,
    LegacyUnreported,   // 이행기 전용
};

struct CommandResult
{
    CommandStatus status{};
    std::string   code;       // "scene.not_found", "test.pixel_mismatch"
    std::string   message;
    CommandData   data;       // owned bool/int/double/string/array/object tree
};
```

`CommandData`에는 raw pointer, `Meta::Type*`, RHI native object를 넣지 않는다. 큰 바이너리·
PNG·dump는 inline base64가 아니라 artifact path와 digest로 참조한다.

```json
{"artifact":{"path":"...","kind":"png","sha256":"...","bytes":12345}}
```

---

## 7. 지연 계약 (L2)

### 7.1 예산 분해

목표는 "1초 미만"이 아니라 **p50 25ms / p95 60ms / p99 120ms(짧은 명령)** 이다. 1초는
상한이고, 상한을 목표로 잡으면 상한을 넘긴다.

★ **이 값은 2026-09-04 LC5 에서 한 번 조여졌다.** 원래 적혀 있던 50/150/300ms 는
착수 전 추정이었고, LC0 이 잰 바닥값은 p50 2.5~3.0ms 였다 — 목표가 바닥의 20배였다.
그 값을 게이트로 쓰면 **20배 퇴행이 초록으로 통과한다.** 지금 값은 LC5 의 HTTP 왕복
실측(p50 6.6ms · p95 8.1ms · p99 9.6ms)에 약 3배 여유를 얹은 것이다. 여유는 Debug
빌드와 느린 기계 몫이고, 그래도 원래 목표보다 2배 이상 엄하다.

| 구간 | 예산 | 근거 |
|---|---:|---|
| accept + 파싱 + 인증 | ≤ 2ms | loopback, 본문 수 KB |
| 큐 전달 | ≤ 1ms | 기존 mutex+deque |
| 프레임 경계 대기 | ≤ 2 프레임 | **실측 p50 2.6ms / p95 3.8ms** (아래) |
| 실행 | 명령별 | `cost=Immediate`만 동기 응답 대상. **실측 p50 0.29ms** |
| 직렬화 + 응답 | ≤ 2ms | |

#### 7.1.1 LC0 실측 (2026-09-04 · Debug · 창 비활성 · 4회 반복)

분모가 생겼다. `Tools/regression/lc0-measure.ps1`이 `--console` stdin으로 명령을 던지고
같은 파이프에서 결과를 받는 왕복을 200회 잰 값과, 엔진 안에서 `Pump()`가 직접 잰 값이다.
산출물은 `Artifacts/Tests/Editor/lc0/`의 `lc0_roundtrip.tsv`·`lc0_timing.tsv`.

| 측정 | p50 | p95 | p99 | max |
|---|---:|---:|---:|---:|
| **외부 왕복**(stdin→실행→stdout, 정지 상태) | 2.5–3.0ms | 3.4–4.0ms | 3.8–4.4ms | 5.0–5.7ms |
| 외부 왕복(재생 중) | 2.7–3.0ms | 3.3–3.7ms | 3.5–4.0ms | 4.0ms |
| 프레임 시간(전체 · 약 700프레임) | 2.5–2.7ms | 3.4–3.8ms | 3.8–4.3ms | 11–13ms |
| 큐 대기(`Enqueue`→dequeue) | 2.2ms | 3.2ms | 4.5ms | **2,371ms** |
| handler 실행 | 0.29ms | 0.42ms | 0.49ms | 9.6ms |
| 대기 프레임 수 | 1 | 1 | 1 | 62(=`wait 60`) |

범위로 적은 것은 4회 반복의 최소~최대다. 반복 간 변동이 작아 단일 실행 값을 그대로
쓰지 않았다 — 첫 측정은 같은 기계에서 빌드가 돌던 중이라 p95가 13.6ms로 나왔고, 그 값을
그대로 적었다면 예산이 기계 소음 위에 세워졌을 것이다.

읽어야 할 것 넷.

1. **오늘의 바닥값이 목표보다 20배 빠르다.** p50 ~2.8ms는 §7.1 목표(50ms)의 1/18이고,
   p99 ~4.2ms는 목표(300ms)의 1/70이다. 그래서 문제는 "달성 가능한가"가 아니라
   **"이 표를 SLO로 쓰면 20배 퇴행이 초록으로 통과한다"**이다. LC5의 게이트는
   50/150/300이 아니라 이 실측에 여유를 얹은 값(예: p50 10ms / p95 25ms / p99 50ms)이어야
   하고, §7.1 표의 목표치는 그때 갱신한다.
2. **지연은 거의 전부 프레임 경계 대기다.** handler 실행 p50이 0.29ms인데 왕복이 2.8ms다.
   줄일 것은 실행이 아니라 대기이고, 그것이 §7.2 드레인 예산이 겨냥하는 자리가 맞다.
3. **"명령 N개가 N프레임"은 주입률에 달렸다.** 하나씩 던지면 `waitedFrames`는 p99까지 1이다.
   N프레임 문제는 큐에 한꺼번에 쌓을 때만 생긴다 — 배치 시나리오와 `POST /commands/batch`가
   그것이다. 드레인 예산의 값어치는 단발 요청이 아니라 묶음에서 나온다.
4. **프레임 시간은 봉우리가 하나다.** 701프레임 중 666개가 2~4ms 구간이고 8ms를 넘는 것은
   1개다. 씬을 안 연 에디터가 창 비활성에서 ~380fps로 돈다는 뜻이고, §2.2의 "PeekMessage
   폴링 + `Present(0,0)`이라 창이 비활성이어도 프레임이 돈다"가 숫자로 확인됐다.
   **큰 씬을 연 상태의 분포는 아직 없다** — 이 값은 하한이지 대표값이 아니다.

★ **계측이 계측 대상을 흔든다는 것도 실측으로 드러났다.** 처음에는 프레임 상태 수집을
상시로 켜 뒀는데, 매 프레임 `GetForegroundWindow()` 시스템 호출이 붙어 프레임 시간 분포
자체가 달라졌다. 지금은 `cli.probe.timing reset`이 켤 때만 수집하고 기본은 off다. 꺼져
있을 때 드는 비용은 원자 변수 읽기 하나다. LC5가 서비스 계측을 상시화하려 할 때 같은
함정이 있다 — PHASE 14 카운터와 교차 확인하라는 §14.4는 이 이유로도 필요하다.

아직 못 잰 축 둘.

- **포커스 상태의 프레임 시간.** 하네스가 배경 프로세스에서 에디터를 띄우므로 창이 한 번도
  전경이 되지 않았다(`frames_with_window=701`, `focused=0` — 창 핸들은 있었고 전경만
  아니었다). artifact가 `# focused_unmeasured`로 그 사실을 스스로 적는다.
- **콘텐츠가 있는 씬의 프레임 시간.** 위 값은 빈 에디터의 하한이다.

### 7.2 드레인 정책

```text
Pump():
  Lifecycle::Trace::BeginFrame()          // 지금과 같은 자리, 조기 반환보다 앞
  if (waitFrames > 0) { --waitFrames; drainServiceControlOnly(); return; }
  if (SceneLoading)   { publishBlockedState("scene.loading"); return; }

  batch  : 최대 1개                        // 기존 의미 보존
  service: 예산이 남는 동안 반복
             · 시간 예산(기본 2ms) 또는 개수 상한(기본 8)
             · cost=Long 명령을 만나면 이번 프레임은 그것 하나만
```

★★ **LC0 실측이 이 의사코드의 전제 하나를 뒤집었다.**

위 3행 `if (SceneLoading) { publishBlockedState(...); return; }`는 "씬 로딩이 큐를 멈추는
주된 경로"라는 전제 위에 있다. 실측은 다르다. `scene.load` 하나가 큐를 **2,371ms**(반복 실행에서 2.2~2.4초) 막았는데,
같은 실행에서 `IsSceneLoading()`이 참인 프레임은 **0개**였다.

이유는 단순하다. `Cmd_scene_load`가 `SceneManagers->LoadScene()`을 **핸들러 안에서 동기로**
부른다. 그 2.2초 동안 `Pump()`는 아예 돌지 않으므로, 조기 반환 자리에 무엇을 적든 실행되지
않는다. `IsSceneLoading()` 경로는 **다른 곳이 비동기로 씬을 바꿀 때**의 자리이고, CLI가
스스로 부른 로드에는 걸리지 않는다.

따라서 LC5의 결정이 바뀐다.

- **blocked 상태 발행을 `Pump()`에 두지 않는다.** 가장 심한 멈춤은 정확히 `Pump()`가
  돌지 않는 구간이다. 거기서 상태를 갱신하는 설계는 필요할 때 침묵한다.
- 상태는 **명령 실행 전후로 원자 변수에 찍고**(실행 시작 시각·현재 명령 ID·큐 깊이),
  수신 스레드가 그것을 읽어 `GET /health`와 `202` 승격을 판정한다. 수신 스레드는 GT가
  아니므로 GT가 멈춰 있어도 응답할 수 있다 — §7.3의 "멈춤은 상태다"가 성립하는 유일한 배선이다.
- `mode=sync`의 `timeoutMs` 승격도 같은 이유로 수신 스레드가 판정해야 한다.

★ **배치 큐의 "프레임당 1개"를 왜 지키는가.** `scripts/scene_churn_benchmark.txt` 같은
기존 시나리오는 프레임 수로 시간을 재고 `wait N`이 정확히 N프레임을 뜻한다는 전제 위에
있다. 서비스 지연을 위해 드레인을 바꾸면서 그 전제를 같이 바꾸면, 81개 소비자의 측정값이
조용히 이동한다. 두 큐를 나누는 것은 구현 편의가 아니라 **기존 측정 의미의 보존**이다.

`wait`는 서비스 세션에서 금지한다(400). 프레임 보류는 배치 시나리오의 문법이고, 동시
세션이 있는 서비스에서 전역 프레임 보류는 다른 세션의 지연이 된다.

### 7.3 멈춤은 지연이 아니라 상태다

씬 로딩·`wait`·재생 전환·긴 명령 실행 중에 들어온 요청은 조용히 기다리지 않는다.

- `mode=sync`면 예상 대기가 `timeoutMs`를 넘길 때 즉시 `202` + `operationId`.
- `GET /health`가 `state`, `blockedReason`, `queueDepth`, `oldestQueuedMs`를 낸다.
- 큐 깊이 상한을 넘으면 `429`. 무한 적재는 지연을 숨기는 가장 흔한 방법이다.

### 7.4 취소와 타임아웃

- `Cancelled`는 명령이 취소 지점을 가진 경우에만 온다. 취소 불가 명령은 descriptor가
  그 사실을 말하고 cancel 요청은 `409`.
- 서버는 클라이언트 연결이 끊겼다고 GT 작업을 죽이지 않는다. operation은 계속 진행하고
  결과는 `GET /operations/{id}`로 남는다.
- operation 결과는 완료 후 일정 시간(기본 5분) 또는 개수 상한까지 보관한다.

---

## 8. 보안 (서비스는 실행 표면이다)

이 절은 선택이 아니다. HTTP로 열리는 순간 `MutatesAssets`·`ExecutesUserCode` 명령이
프로세스 경계 밖에서 호출 가능해진다.

| 통제 | 규칙 |
|---|---|
| bind | `127.0.0.1`/`::1`만. `INADDR_ANY`·`0.0.0.0`·외부 NIC bind는 정적 게이트로 금지 |
| 인증 | 요청마다 `Authorization: Bearer <token>`. 상수 시간 비교. 실패는 401 + 실패 계수 |
| 토큰 | 프로세스마다 새로 생성(32바이트 CSPRNG). 로그·결과·감사에 남기지 않는다 |
| endpoint 파일 | 사용자 전용 ACL. 종료 시 삭제. 시작 시 죽은 pid 회수 |
| 브라우저 차단 | `Origin`/`Referer` 헤더가 있으면 거부. `Content-Type: application/json` 강제 |
| 크기 한계 | 본문 기본 1MiB, 헤더 8KiB, 헤더 개수 64, 요청 유휴 타임아웃 30초 |
| 연결 한계 | 동시 연결 상한, accept 백로그 상한 |
| 기본값 | 서비스는 **기본 off**. 켜는 것은 명시 플래그/설정 |
| Player | Development 구성에서만 컴파일·링크. Shipping 심볼 0 |
| 사용자 코드 | `ExecutesUserCode` 명령은 별도 플래그가 없으면 403 |
| 감사 | (시각, 클라이언트 포트, 명령, 인자 요약, 상태, 소요)를 파일에 남긴다. 토큰과 인자 원문은 남기지 않는다 |

★ **`Origin` 거부를 빼먹지 않는다.** 브라우저가 `localhost`로 요청을 보내는 것은
막히지 않는다. 토큰이 없으니 대부분 401로 끝나지만, 토큰 없이도 응답하는 경로가 하나라도
생기면 웹페이지가 로컬 에디터를 조작할 수 있다. `GET /health`조차 토큰을 요구한다.

---

## 9. Editor operation 동등성

mutating command를 다음 넷으로 전수 분류하고 descriptor에 표시한다.

| 분류 | 의미 | 예 |
|---|---|---|
| Shared Editor operation | GUI와 Undo/selection/transaction까지 같아야 함 | play/stop, 일반 duplicate, undo/redo |
| Shared engine service | GUI와 low-level 결과만 같으면 됨 | scene load/save, prefab save, script reload |
| Test/diagnostic probe | GUI 의미가 없고 관측/격리 검사용 | `dx12.*`, `vk.*`, `profile.*` |
| Raw fixture authoring | 회귀 fixture 생성용으로 Undo를 의도적으로 우회 | 일부 `*.authoring.probe` |

완료 판정 예: GUI Play와 서비스 `play`가 `IsGameStart`·edit/game Undo depth·selection을
같은 규약으로 전이 · GUI duplicate와 서비스 duplicate가 부모·선택·Undo를 동일하게 반영 ·
raw fixture command는 Undo 우회가 명시되고 일반 회귀와 분리.

★ 서비스가 이 분류를 더 급하게 만든다. 사람이 `--exec`로 한 번 부르던 것과, 에이전트가
초당 여러 번 부르는 것은 Undo 스택에 남기는 흔적이 다르다.

---

## 10. 라이브 코드 실행 (L3)

"코드를 라이브 메모리에서 즉시 실행한다"를 세 등급으로 나눈다. 등급을 나누지 않으면
`POST /command` 하나가 곧 임의 코드 실행 API가 된다.

| 등급 | 무엇 | 기반 | 이 계획 |
|---|---|---|---|
| **A** | 등록된 명령 실행 | 오늘도 있다 | LC4에서 서비스로 노출 |
| **B** | 로드된 어셈블리의 **표식된** 메서드 호출 | `ClrHost` + 리플렉션 | LC7 |
| **C** | 임의 C# 스니펫 컴파일·실행 | Roslyn 신설 | **범위 밖**. §10.3 |

### 10.1 A — 명령 실행

이미 프로세스 안에서 GT가 실행한다. 서비스가 더할 것은 결과 반환과 지연 계약뿐이다.

### 10.2 B — 표식된 메서드 호출과 어셈블리 교체

기반은 이미 있다. `ScriptLoadContext`는 `isCollectible: true`이고, `Cmd_script_reload`는
① 인스턴스 값을 챙기고 참조를 끊고 ② `ClrHost::ReloadScripts()`로 어셈블리를 교체하고
③ `OnInitialized()`로 인스턴스를 다시 만들어 값을 되돌린다. 이 왕복이 라이브 코드 교체다.

이 계획이 더하는 것:

- `script.reload`를 결과 있는 명령으로 승격 — 복원 수/전체, 실패 목록, 이전 컨텍스트
  잔존 여부를 `data`로 낸다. 지금은 `printf` 한 줄이고, 잔존 여부는 "몇 프레임 뒤
  `script.status`로 확인하라"는 주석으로만 존재한다.
- `script.invoke <type> <method> [args]` — **표식된** static 메서드만 호출한다.
  표식은 ScriptCore의 명시 attribute(예: `[EngineCallable]`)이고, 표식 없는 메서드는
  이름을 알아도 호출되지 않는다. 실행은 GT, 결과는 `CommandResult`.
- 리로드 실패 시 이전 어셈블리 상태를 보존하고 명확히 실패로 판정한다. 반쯤 교체된
  상태로 진행하지 않는다.

완료 모습: **에디터를 끄지 않고** C# 수정 → 외부 빌드 → `POST /command script.reload` →
`POST /command script.invoke ...` 왕복이 한 세션에서 닫히고, 그 왕복 시간이 실측으로
기록된다.

### 10.3 C — 스니펫 컴파일은 왜 지금이 아닌가

먼저 범위를 분명히 한다. **컴파일 대상은 관리(C#) 레이어뿐이며 네이티브 엔진은 영향받지
않는다** — 산출 어셈블리는 `ScriptLoadContext`에 얹히고, C++ 변경은 여전히 재빌드와 프로세스
재시작을 요구한다. **다만 스크립트 레이어는 권한 경계가 아니다** — `ClrHost`가 넘기는
`ScriptApiTable`은 함수 포인터 182개짜리이고 그중에는 씬을 바꾸는 것들이 있으며, .NET은
스니펫이 `DllImport`로 임의 DLL을 여는 것도 막지 않는다. collectible ALC가 제한하는 것은
어셈블리의 **수명**이지 그 코드가 닿을 수 있는 **범위**가 아니다.

그래서 보류 근거는 "엔진을 다시 컴파일할까 봐"가 아니다. Roslyn을 관리 측에 들이면 그 184개
표면에 대한 임의 코드 실행이 HTTP 한 번으로 가능해진다는 것이고, 그 전에 먼저 있어야 하는
것이 있다.

1. `ExecutesUserCode` capability와 별도 플래그가 서고 감사에 남는다.
2. 컴파일 실패·예외가 에디터를 죽이지 않는다는 증거(격리·예외 경계).
3. 생성된 어셈블리의 수명 — collectible ALC에 얹고 회수되는지, 누수되는지.
4. Roslyn 의존이 관리 측 배포 크기·부팅 시간에 주는 비용 실측.

넷이 서기 전의 C는 "빠른 승리"처럼 보이지만 되돌리기 어려운 표면이다. 별도 계획으로
판정한다. 그 전까지 B가 실질적으로 같은 일을 한다 — 코드를 파일에 쓰고, 빌드하고,
리로드하고, 부른다. B도 같은 API 표를 쓰지만 **호출 가능한 진입점이 명시 attribute로
열거된다**는 점이 다르다.

---

## 11. Player Development 에이전트 (L4)

### 11.1 지금 없는 것

Player에는 `--smoke <frames>` 하나뿐이고, 저장소 전체에 Development/Shipping을 가르는
매크로가 없다. PHASE 14(프로파일러)도 같은 구분을 필요로 한다(`CE_PROFILE_ENABLED=0`
compile-out, "Development Player 캡처"). **구분을 두 번 만들지 않는다** — 먼저 도착하는
쪽이 만들고 다른 쪽이 소비한다. 14.5가 먼저면 14.5가 만든다.

### 11.2 계약

- Player는 같은 HTTP/JSON 계약을 쓰고 **다른 registry**를 갖는다. `roles`에
  `Player`가 없는 명령은 애초에 등록되지 않는다(런타임 거부가 아니라 부재).
- 켜는 방법: `--command-service` 명시 플래그. 기본 off. Shipping은 플래그가 있어도 없다.
- Player 에이전트는 authoring 문서 경로(`AuthoringParsedDocument`)를 쓰지 않는다.
  전용 JSON codec을 쓴다 — `runtime.text-parser calls=0` 게이트(`Tools/build.ps1`가
  Player smoke 출력에서 검사한다)를 흔들지 않기 위해서다.
- Shipping 빌드에서 서비스 심볼·소켓 심볼이 0임을 패키징 게이트에서 확인한다.

### 11.3 무엇을 할 수 있게 되는가

실행 중인 Development Player에 붙어 씬/오브젝트 상태를 읽고, 런타임 명령을 넣고, 값이
게임을 재시작하지 않고 반영되는 것을 확인한다. 재현이 어려운 상태(특정 웨이브, 특정 UI
상태)에 도달한 뒤 그 상태 위에서 명령을 시험할 수 있다는 것이 이 계층의 실제 값어치다.

Player에서의 코드 라이브 교체(B 등급)는 이번 범위에 넣지 않는다 — Editor에서 먼저 닫고,
Player ALC 수명은 별도로 판정한다.

---

## 12. 물리 파일 구조와 의존 방향

```text
Editor/EngineEntry/CommandCore/          -- role 중립 실행 코어
  CommandTypes.h
  CommandParser.h/.cpp
  CommandRegistry.h/.cpp
  CommandExecutor.h/.cpp
  CommandFormatters.h/.cpp

Engine/CommandService/                   -- 신설 · Editor와 Player가 공유
  HttpListener.h/.cpp                    -- Winsock loopback 최소 서브셋
  HttpRequest.h/.cpp
  JsonValue.h/.cpp                       -- 자체 codec
  CommandService.h/.cpp                  -- 라우팅·인증·operation 표
  ServiceEndpointFile.h/.cpp

Editor/EngineEntry/Commands/             -- 도메인별 registration + 얇은 adapter
  CoreCommands.cpp
  SceneObjectCommands.cpp
  AssetAuthoringCommands.cpp
  ScriptUiAnimatorCommands.cpp
  DiagnosticsCommands.cpp
  RenderTestCommands.cpp
  BuildCommands.cpp

Player/PlayerCommands.cpp                -- Player registry(축소)

Editor/EngineEntry/ConsoleCommandSystem.h/.cpp
  -- 호환 facade: command-line/stdin/script 수명, Enqueue, Pump, Shutdown
```

규칙:

- 파일 분리는 기능 변경과 한 덩어리로 하지 않는다.
- `Engine/CommandService`는 Editor 헤더를 include하지 않는다. registry를 주입받는다.
  이 방향이 지켜져야 Player가 같은 코드를 쓸 수 있다.
- 실제 renderer test·asset writer·build orchestrator를 command 폴더로 옮기지 않는다.
- include는 각 TU가 직접 소유한다. 기존 unity build의 전이 include에 기대지 않는다.
- domain 하나를 옮길 때마다 Editor unity/non-unity build를 둘 다 통과한다.
- Shipping Player project에 `CommandService`가 등록되지 않는다.

`ConsoleCommandSystem.cpp` 11,968줄을 한 번에 잘라 빌드를 마지막에 보는 방식은 금지한다.

### 12.1 빌드 시스템 배치 — 위 경로는 아직 프로젝트가 아니다

위 트리는 디렉터리 배치이고, 이 저장소의 빌드 단위는 **vcxproj**다. 두 사실을 먼저 적는다.

- `Engine/` 아래의 빌드 단위는 `Utility_Framework` · `RenderEngine` · `Physics` ·
  `SceneRuntime` · `EngineDiagnostics` 다섯 static lib이다. **`CommandService`라는
  프로젝트는 없다.** 파일을 만든다고 빌드에 들어가지 않는다.
- 솔루션 구성은 `Debug|x64` · `Release|x64` **둘뿐**이다. `Shipping`은 물론
  `Development`도 없다. §11의 "Development/Shipping 구성 구분"은 매크로 하나가 아니라
  **13개 프로젝트에 구성을 추가하는 솔루션 전역 변경**이다.

결정:

| 항목 | 결정 | 이유 |
|---|---|---|
| `Engine/CommandService` 빌드 단위 | **새 static lib vcxproj를 만든다** | Editor와 Player가 링크를 각자 결정해야 한다. 기존 lib에 얹으면 Player가 Shipping에서 그 lib 전체를 뺄 수 없다 |
| Editor 링크 | 항상 링크. 서비스 on/off는 런타임 플래그 | 에디터에는 Shipping 개념이 없다 |
| Player 링크 | **구성 조건부 `ProjectReference`** | 심볼 0을 링크 단계에서 보장한다. `#ifdef`로 본문만 비우면 lib은 여전히 들어온다 |
| 구성 추가 시점 | **LC8**. LC4는 Editor만 | 13개 vcxproj와 `Tools/build.ps1`을 건드리는 일을 전송 계층 신설과 같은 슬라이스에 넣지 않는다 |

★ **`Tools/build.ps1`이 구성 이름을 문자열로 안다.** 구성을 추가하면서 그 스크립트와 CI
호출부를 같이 옮기지 않으면 Player smoke가 빈 출력으로 통과한다 — false-green이 게이트
자체에서 난다. LC8의 완료 기준에 "새 구성으로 `Tools/build.ps1`이 Player smoke를 실제로
돌리고 `runtime.text-parser calls=0`를 읽는다"를 포함한다.

---

## 13. 실행 슬라이스

총 추정 **26 인일**. 명령·소비자 수는 LC0에서 런타임 표 덤프로 다시 생성하고 공수를
갱신한다. (앞선 판의 18일은 배치 CLI 정식화만이었다. 증가분 8일이 서비스·라이브 실행·
Player다.)

### LC0 — 기준선·지연 실측·canary (P0 · 1.5일)

- `commands.dump` 를 추가해 런타임 표(`GetTable()`)를 이름 정렬·handler 그룹으로 덤프하고
  name/alias/handler/help coverage inventory를 artifact로 고정한다(§2.1의 ★).
- raw `line` 재해석 handler, 직접 `SetExitCode`, verdict print 지점을 센다.
- **에디터 프레임 시간 분포를 실측한다** — 포커스/비포커스/씬 로딩/재생 중. §7.1의 분모.
- **현행 왕복 지연의 바닥값을 잰다** — stdin `Enqueue` → 실행 → `printf`까지.
- 씬 로딩·`wait`로 큐가 멈추는 시간 분포를 잰다.
- CLI 소비 파일 81개와 정규식·소스 스크래핑 목록을 고정한다.
- unknown command와 논리 실패가 exit 0으로 남는 false-green canary를 만든다.
- quoted empty/string/path/name과 잘못 닫힌 quote parser golden을 만든다.

완료 기준: 거동 변경 0 · 지연 예산의 분모가 추정이 아니라 실측 · canary가 현 결함에 실제
발화 · 명령/소비자 수가 상세 목록으로 고정.

**상태: 완료 (2026-09-04).** 구현·빌드·런타임 검증까지 마쳤다.

| 완료 기준 | 증거 |
|---|---|
| 거동 변경 0 | 기존 명령 205개 무수정. 추가는 계측 명령 3개(`commands.dump`·`cli.probe.timing`·`cli.echo.args`)와 `Pump`/`Enqueue`의 타임스탬프뿐. `PrintHelp`는 `printf`→`fputs`로 바뀌었고 출력 바이트는 같다(서식 지정자 0개) |
| 지연 분모 실측 | §7.1.1. `lc0_roundtrip.tsv`(외부 200회) + `lc0_timing.tsv`(내부 361프레임/240명령) |
| canary 발화 | `verify-cli-exit-contract.ps1` — 4/4 케이스가 `false-green`으로 판정됨(실패 문안을 출력하고 exit 0) |
| 명령·소비자 고정 | §2.1.1. `lc0_command_inventory.tsv`(205 그룹/219 이름) + `lc0_static_inventory.tsv`(소비자 85 · 파일별 목록 포함) |
| parser golden | `cli_parser_golden.expected` — 16 케이스 119줄. escape 미지원·미닫힘 quote 묵인·토큰 중간 quote 제거를 바이트로 고정 |

산출물과 도구:

```text
Editor/EngineEntry/CommandBaseline.h/.cpp        계측 코어(Editor·Engine 의존 0)
Tools/regression/lc0-measure.ps1                 왕복 지연 하네스(--console stdin)
Tools/regression/lc0-static-inventory.ps1        정적 inventory
Tools/regression/verify-cli-exit-contract.ps1    false-green canary(래칫)
Tools/regression/verify-cli-parser-golden.ps1    tokenizer 골든(래칫)
Artifacts/Tests/Editor/lc0/*.tsv                 산출 artifact 4종
```

뒤 슬라이스로 넘긴 것 둘.

- **포커스 상태 프레임 시간** — 배경 프로세스에서 띄운 에디터가 전경이 되지 않아 못 쟀다.
  artifact가 `# focused_unmeasured`로 스스로 적는다(§7.1.1).
- **`IsSceneLoading()` 경로의 큐 정지 분포** — 실측에서 그 플래그가 참인 프레임이 0이었다.
  CLI가 부르는 씬 로드는 handler 안에서 동기로 멈추기 때문이다. 이 발견은 §7.2에 반영했고,
  분포를 재려면 비동기 로드 경로를 따로 태워야 한다.

LC0의 두 래칫은 `Tools/regression/run-all.ps1`에 들어갔다. 둘 다 오늘 초록이고, LC1/LC2가
거동을 바꾸는 순간 붉어진다.

### LC1 — `CommandResult`·session·exit spine (P0 · 2.5일)

- `CommandStatus`, stable code, owned data, session severity 도입.
- unknown/parse/precondition/handler/internal failure를 exit mapping에 연결.
- `help/quit/wait`, `scene.load`, `game.pak`, 대표 selftest 하나를 result-bearing으로 이행.
- legacy void handler adapter는 `LegacyUnreported`를 반환하고 신규 명령에는 금지.
- 기본 continue와 `--fail-fast` 검증.

완료 기준: 실패→`quit`가 비-0 · 앞 실패 뒤 성공도 비-0 · 전부 성공은 0 · crash code
비가림 · 직접 exit write가 session adapter 외 0으로 줄어드는 ratchet 가동.

**이 슬라이스 없이는 HTTP 응답 본문이 존재하지 않는다.** 순서를 앞당길 수 없다.

**상태: 완료 (2026-09-04).**

| 완료 기준 | 증거 |
|---|---|
| 실패→`quit` 가 비-0 | canary `failure-then-quit` = exit 3. `quit` 은 성공을 내지만 session 이 가장 심한 결과를 보존한다 |
| 전부 성공은 0 | `verify-cli-exit-spine.ps1` `all-success` = 0. 이행 명령과 legacy 명령을 섞어 돌린다 |
| unknown/parse/precondition 사상 | `command.unknown`→2 · `wait.not_a_number`/`wait.negative`→2 · `scene.not_found`→3 |
| crash code 비가림 | `g_exitCode` 는 `EngineBootstrap::Run` 이 **정상 반환할 때만** 읽힌다. 미처리 예외는 그 지점에 닿지 않는다. 실측 증거: 회귀 세트의 해상도 스위프가 명령 전부 성공(session 0) 뒤 크래시했고 프로세스 종료 코드는 `0x0000087D` 였다 — session 값이 크래시 코드를 덮지 않았다 |
| ratchet 가동 | `cli_exit_spine.ratchet.json` 상한 16(=18−2). 늘면 붉어지고 줄면 상한을 낮춘다 |
| 기본 continue · `--fail-fast` | 같은 시나리오를 두 모드로 돌려 **표식 명령의 실행 여부**로 판정한다 — 종료 코드만으로는 큐를 버렸는지 구분되지 않는다 |

이행한 명령 6 개: `help` · `quit`/`exit` · `wait` · `scene.load`/`scene.switch` ·
`game.pak` · `inputmap.authoring.probe`. 나머지 199 개는 `LegacyUnreported` 다.

★ **`LegacyUnreported` 는 성공이 아니라 "모른다"다.** severity 순위에서는 성공보다
아래지만 exit code 는 0 이다. 둘을 갈라 둔 이유 — 순위를 성공과 같게 두면 미이행
명령이 성공으로 집계되어 남은 이행 대상을 셀 수가 없고, exit 을 비-0 으로 두면
아직 결과를 안 내는 명령 199 개가 전부 실패로 뒤집혀 회귀 세트가 통째로 붉어진다.
`SeverityRank` 와 `ExitCodeFor` 를 별개 함수로 둔 것이 이 구분을 가능하게 한다.

★ **이행하며 드러난 결함 셋.** 계획이 예상하지 못한 것들이라 적어 둔다.

1. **`wait` 가 인자 오류를 조용히 삼켰다.** `std::atoi` 는 실패를 0 으로 돌려주고
   `std::max(0, ...)` 가 음수를 0 으로 바꾼다. 그래서 `wait abc` 와 `wait -5` 가
   대기 없이 지나가고도 성공이었다 — **프레임 수로 시간을 재는 시나리오가 시간을
   재지 않고 통과할 수 있었다**는 뜻이다. §14.2 의 "조용한 0 변환 금지"가 겨냥한
   자리이고, LC1 이 `InvalidArguments` 로 닫았다.
2. **selftest 실패가 infrastructure 오류로 보고되고 있었다.**
   `inputmap.authoring.probe` 의 왕복 불일치가 `SetExitCode(5)` 였다. 5 는 §5.4 에서
   build/IO 오류다. 검사가 정직하게 판정한 실패와 디스크가 죽은 것을 같은 숫자로
   알리면 자동화가 재시도해서는 안 될 것을 재시도한다. `Failed`(4)로 바로잡았다.
3. **핸들러 예외가 프로세스를 죽였다.** `Execute` 에 예외 경계가 없어서, 무인
   실행 중 예외 하나가 남은 시나리오를 통째로 날리고 어디까지 갔는지도 남기지
   않았다. 이제 `InternalError`(5)로 바뀌고 세션은 계속 돈다.

★★ **LC1 이 스스로 만든 회귀 둘. 검토와 회귀 세트가 각각 하나씩 잡았다.**

이 슬라이스는 "실패가 종료 코드에 닿게 한다"가 목표인데, 착수 직후의 구현이 그
목표를 두 곳에서 정확히 반대로 뒤집었다. 적어 두는 이유는 이행 중인 다음
슬라이스(LC6·LC8)가 같은 모양의 함정을 만나기 때문이다.

1. **session 이 legacy 의 직접 exit 쓰기를 덮어썼다.** session 이 명령마다
   exit code 를 쓰기 시작하자, 아직 이행되지 않은 핸들러가 직접 쓴 값이 **같은
   프레임 안에서** 0 으로 덮였다. `material.corpus.probe` 를 인자 없이 부르면
   핸들러가 6 을 쓰는데 프로세스는 0 으로 끝났다 — LC1 이전보다 나빠진 상태다.
   해당 명령 7 개(`terrain.authoring.probe` · `foliage.authoring.probe` ·
   `animator.scene.probe` · `inputmap.corpus.probe` · `asset.guid.rename.probe` ·
   `material.corpus.probe` · `prefab.corpus.digest`)가 전부 조용해질 뻔했다.

   고친 방법: legacy adapter 가 핸들러 전후의 exit code 를 관측해 §5.4 표로
   되읽고(`LegacyDirectExit`) 원래 값을 복원한다. 최종 값을 정하는 곳은 여전히
   session 하나다.

   ★ **정적 래칫이 이것을 못 잡는다.** `verify-cli-exit-spine.ps1` 의 래칫은
   직접 쓰기의 **수**를 세지 그 값이 살아남는지 보지 않는다. 수를 세는 게이트와
   효과를 보는 게이트는 다른 물건이다 — 동적 케이스(`legacy-direct-exit`)를
   따로 넣었고, 래칫이 0 에 닿을 때까지 유지한다.

2. **예외 경계가 `crash.test` 를 무력화했다.** 3 번 결함을 고치려고 `Execute` 에
   try/catch 를 두자, **일부러 죽는 것이 일인 명령**이 죽지 않게 됐다. 크래시
   덤프 경로 회귀가 프로세스 종료를 기다리다 타임아웃 났다. 그 검사는 크래시가
   나야만 도는 것이라, 무력화되면 덤프 경로 전체가 조용한 사각지대가 된다.

   고친 방법: registry entry 에 `letExceptionsEscape` 를 두고 `crash.test` 만
   경계를 통과시킨다. 좋은 기본값에도 예외가 있어야 한다.

이 둘 모두 **exit code 를 지키는 게이트가 아니라 다른 검사**가 잡았다는 점을 적어
둔다. LC0 canary 는 넷 다 초록이었고 exit spine 게이트도 초록이었다.

★ **LC0 의 정적·런타임 대조가 곧바로 일을 했다.** `regResult(`/`regEscaping(` 이
생기자 정적 집계가 201, 런타임이 208 로 갈라졌다 — 소스 스크래핑이 구현 형식
변화에 깨지는 §2.4 의 결함과 같은 모양이고, 이번에는 **대조가 있어서 같은 날
드러났다**. 집계 패턴을 세 형식으로 넓혀 208=208 로 맞췄고, 이행 진행도
(`registrations_result_bearing` 6 / `registrations_legacy` 202)를 지표로 추가했다.
LC9 의 완료 조건 하나가 legacy 0 이므로 이 수가 그 눈금이다.

★ **이행 중 출력이 겹친다(의도).** 이행한 명령이 실패하면 핸들러 자신의 한국어
문안과 `PublishResult` 의 기계용 줄이 둘 다 나온다. 지금은 그대로 둔다 — 사람이
읽는 문안을 정규식으로 읽는 소비자가 아직 살아 있고(§2.1.1: 1 건), LC9 가 그것을
JSON 으로 옮긴 뒤에 핸들러의 printf 를 정리한다. 이행 전 명령 199 개는 출력이
전혀 늘지 않는다(`LegacyUnreported` 는 조용히 지나간다).

### LC2 — 소유형 invocation·raw 재해석 제거 (P0 · 2일)

- lexical parser와 source location 분리. 배치 라인 문법은 여기서만 산다.
- JSON `args` 배열이 라인 문자열을 거치지 않고 곧장 owned argument가 되는 경로 확보.
- `object.parent`, `object.duplicate`, `prefab.create`부터 `parts + raw line` 혼합 제거.
- Windows path, 빈 string, quote/escape, UTF-8 이름을 golden으로 고정.
- legacy ambiguous form 사용량 계측.

완료 기준: migrated handler의 raw line 접근 0 · quoted 두 이름이 실제 scene operation을
통과 · 기존 command file 소비 경로 입력 호환 · 같은 인자를 라인/JSON 두 경로로 넣었을 때
동일 invocation · parser mutation canary 전부 발화.

**상태: 완료 (2026-09-04).**

| 완료 기준 | 증거 |
|---|---|
| raw line 접근 0 | `raw_line_reinterpretation` **25 → 0**. `ctx.line` 별칭 22 개 전부 제거. 남은 `ctx.line` 은 `cli.echo.args` 의 `line_len` 진단 1 건뿐이고, 그것은 파서를 되비추는 프로브라 원문 길이가 산출물이다 |
| quoted 두 이름이 실제 조작 통과 | `object.create "Big Boss Character"` → `object.parent "Big Boss Character" "Main Characters"` 뒤 `scene.hierarchycheck` 가 **오브젝트 3 · 최상위 1**. 판정을 "못 찾음 메시지가 없다"로 두지 않았다 — 그것은 명령이 아예 안 돌아도 참이다 |
| 입력 호환 | 시나리오 파일의 따옴표는 전부 주석 안이고 `\"` 는 0 건, 명령 줄의 홀수 따옴표도 0 건. 전체 회귀 세트로 확인 |
| 두 경로 동일 invocation | 라인 `--script` 와 구조화 `--exec-args` 가 **같은 `arg[]` 3 줄**을 낸다 |
| mutation canary 발화 | 골든 diff 가 정확히 두 곳 — `"say \"hi\""` 가 `say \hi\` → `say "hi"`, 닫히지 않은 따옴표가 조용한 토큰 → `parse.unclosed_quote`(exit 2). 나머지 14 케이스 불변 |

★ **문법을 한 곳에 모으는 것이 목적이 아니라 수단이다.** 예전 `Split()` 은 이미
따옴표를 옳게 잘랐다 — 결함은 tokenizer 가 아니라 **핸들러가 그 결과를 버리고
원문을 다시 자른 것**이었다. 그래서 형상만 보는 골든으로는 잡히지 않았고,
`object.parent "A B" "C D"` 는 자식 이름에 따옴표가 남은 채 "이름을 못 찾음"
한 줄로 끝났다. 따옴표 문법이 사실상 동작하지 않는다는 것이 오래 보이지 않은
이유가 그 한 줄이다.

★ **`--exec-args` — 오늘 존재하는 구조화 입력 경로.** OS 가 이미 갈라 준 argv 를
라인으로 이어 붙이지 않고 그대로 owned argument 로 쓴다. LC4 의 수신 스레드가
같은 문(`EnqueueStructured`)을 쓴다. 종결자 `--` 를 둔 이유는 실측이다 — 처음에는
남은 argv 를 전부 먹게 했더니 **뒤에 `--exec quit` 을 붙일 수 없어** 무인 실행이
종료하지 못하고 하네스 타임아웃까지 살아 있었다.

★ **바꾼 것 하나를 명시한다: 따옴표 없는 여러 토큰에서 공백 연속이 접힌다.**
`TrimLine(line.substr(cmd.size()))` 14 곳을 `JoinFrom(parts, 1)` 로 옮기면서,
따옴표 없이 여러 토큰으로 준 값의 공백 **연속**이 하나로 접힌다.

검토가 이것을 이름·경로가 아닌 곳에서도 짚었다 — `object.property` 와 `script.set`
은 이름이 아니라 **값**을 받고, 그 값에 공백 연속이 들어갈 수 있다. 다만 옛 코드도
그 자리에서 안전하지 않았다: `rest.substr(parts[1].size())` 로 **토큰 길이만큼**
원문을 잘랐기 때문에, 이름을 따옴표로 감싸면 원문이 두 글자 더 길어져 값이
어긋났다. 즉 옛 방식은 "따옴표를 쓰면 값이 깨지고", 새 방식은 "따옴표를 안 쓰면
공백이 접힌다"이다.

결정: §3.2 가 이미 정한 대로 **공백이 값의 일부이면 따옴표로 감싼다.** 따옴표 안은
한 토큰이라 내부 공백이 손대지 않은 채로 간다(게이트의 `payload-spaces` 가 그것을
못박는다). 따옴표 없는 여러 토큰은 오류가 아니라 legacy 형식이고, `JoinFrom` 과
`SplitTrailingName` 이 **그 사용을 센다**(`LegacyJoinUseCount`). LC9 가 그 수로
제거 시점을 정한다. 진짜 자유 형식 payload 가 필요해지면 `Remainder`
argument kind(§6.3)를 세워야 하고 join 으로 다루면 안 된다.

★ **같은 규약을 쓰는 명령은 같은 함수를 부른다.** 처음에는 계획이 이름을 댄 셋만
`SplitTrailingName` 으로 옮기고 나머지 여섯(`object.rename` · `script.add` ·
`component.add` · `prefab.update` · `animator.state` · `animator.param`)은 자기
`rfind` 를 그대로 뒀다. 기능은 같았지만 **legacy 사용 계수가 그 여섯을 못 본다** —
LC9 가 "아무도 안 쓴다"를 근거로 규약을 뗄 때 근거가 여섯 개만큼 비어 있게 된다.
아홉 곳 전부 같은 함수를 부르게 바꿨다. `animator.state` 처럼 뒤 토큰이 둘인
명령을 위해 `trailingCount` 를 받는다.

★ **`experiment.catalog` 는 원문 재해석의 최악 형태였다.** `ctx.line.find("mount")`
로 잘랐기 때문에 **경로 안에 "mount" 가 있으면 엉뚱한 곳을 잘랐다**
(`experiment.catalog mount D:/mount/x`). 토큰을 이어 붙이는 방식에는 그 함정이 없다.

### LC3 — Descriptor registry·discovery (P0 · 2.5일)

- descriptor와 argument/capability/cost/role metadata 도입.
- `help`, `help <command>`, `commands.list`, `commands.describe`를 registry에서 생성.
- alias 중복·canonical 중복·help 누락을 초기화/selftest 실패로.
- `Invoke-Dx12Suite`가 소스를 긁지 않고 명령 목록을 얻는 discovery 결과 제공.

완료 기준: 등록 명령 help/description coverage 100% · 수동 `PrintHelp` 목록 0 · 이름 중복
canary 발화 · registry snapshot 순서 deterministic · descriptor 없이 새 handler 등록 불가 ·
모든 명령이 `cost`와 `roles`를 갖는다.

**상태: 완료 (2026-09-04).**

| 완료 기준 | 증거 |
|---|---|
| help coverage 100% | **211 / 211**(LC0 실측은 130/205 = 63%). 게이트가 **양방향**을 본다 — 등록→help(커버리지)와 help→등록(고아). 한 방향만 보면 LC0 이 찾은 결함 둘 중 하나를 놓친다 |
| help 고아 0 | `experiment.anim`·`bench`·`fbx`·`gltf`·`import`·`model` 6 개가 사라졌다. 등록에 없는 이름은 schema 에도 없다 |
| 수동 `PrintHelp` 목록 0 | 143 줄짜리 손글씨 문자열이 사라지고 `RenderHelp(registry)` 가 대신한다 |
| 이름 중복 canary | `commands.selftest` 가 중복·요약 누락·descriptor 부재를 **exit 4** 로 낸다. 예전에는 `printf` 한 줄로 지나가고 그 뒤 조용히 한쪽이 먹혔다 |
| snapshot deterministic | 두 실행의 `commands.list` 출력이 바이트 동일(212 행) |
| descriptor 없이 등록 불가 | seed 가 없으면 `summary` 가 비고 `CommandRegistry::Add` 가 거부한다 |
| `cost`·`roles` 전수 | 211 개 전부. cost 분포 — Immediate 31 · Frames 153 · Long 24. roles 는 전부 `Editor`(오늘 Player 에 여는 명령이 없다. LC8 이 고른다) |
| `Invoke-Dx12Suite` 이관 | C++ 소스 스크래핑을 끊고 `commands.list` 를 읽는다. 소스를 데이터로 읽는 소비자 7 → **6** |

★ **게이트가 착수 직후 자기 자신을 잡았다.** `commands.list`·`commands.describe`·
`commands.selftest` 를 등록하자마자 `commands.selftest` 가 **문제 3 건**을 냈다 —
그 셋에 seed 가 없었기 때문이다. "descriptor 없이 새 handler 등록 불가"가 문서의
약속이 아니라 실제로 도는 규칙이라는 것을 첫 세 명령이 증명했다.

★ **요약 78 개는 지어낸 것이 아니다.** 131 개는 현행 help 문자열에서 그대로
가져왔고(그것이 오늘의 정본 문서다), 나머지 77 개는 핸들러의 주석과 `printf`
문안을 읽고 적었다. help 에 한 번도 실린 적 없던 것들이고, 그래서 커버리지가
63% 였다. 자동 추출로 채우고 검토하지 않는 편이 빨랐겠지만, 그렇게 만든 문서는
**틀린 문서**가 되고 이 슬라이스가 없애려던 것이 정확히 그것이다.

★ **`cost` 는 기본값을 주지 않았다.** seed 표에 없으면 등록이 거부된다. 기본값을
두면 208 개가 전부 `Immediate` 로 서고, LC5 의 서비스가 그 값을 보고 동기 응답을
고른다 — 틀린 값이 있는 편이 값이 없는 편보다 나쁘다. 판단이 애매할 때는 비싼
쪽(`Frames`)으로 적었다. 틀리는 방향이 안전하기 때문이다: `Long` 을 `Immediate`
로 적으면 지연 계약이 깨지지만, 반대는 응답이 202 로 한 번 더 도는 것뿐이다.

★★ **검토가 잡은 것: 중복 검사가 죽은 코드였다.**

`commands.selftest` 는 "이름 중복을 실패로 낸다"고 적어 두었는데, 실제로는 **발화할
수 없었다.** 이름 충돌은 조회 표(`unordered_map::emplace`)에서 일어나고 진 이름은
descriptor 에 애초에 담기지 않는다 — `CommandRegistry::Add` 안의 충돌 검사는 볼
것이 없는 검사였다. 검사가 없는 것보다 **있다고 믿는 검사가 없는 것**이 나쁘고,
이 슬라이스가 없애려던 것이 정확히 그 모양이다(help 가 그랬듯이).

진 이름을 `RecordRejectedName` 으로 registry 에 따로 넘겨 고쳤다. 그리고 §14.7 대로
**변이로 이빨을 확인했다** — `scene.new` 를 일부러 두 번 등록하니 `commands.selftest`
가 `exit 4` 와 `이름 중복으로 등록되지 못함: scene.new` 를 냈다. 고치기 전에는 같은
변이가 `printf` 한 줄만 남기고 exit 0 이었다.

같은 검토에서 셋을 더 고쳤다.

- **별칭 충돌이 canonical 충돌과 다르게 처리됐다.** 문제만 적고 descriptor 를 그대로
  저장해서, 충돌한 별칭으로는 영영 닿을 수 없는 descriptor 가 표에 남았다. 이름만
  바꾼 "조용히 한쪽이 먹힘"이라 canonical 과 같게 버리도록 맞췄다.
- **seed 표 정렬이 아무것도 지키지 않았다.** 이진 탐색이 전제하는 불변식인데 표는
  손으로 유지한다. 행 하나를 엉뚱한 자리에 넣으면 그 근처 이름들이 조용히 "요약 없음"
  으로 등록을 거부당한다 — seed 가 분명히 있는데 없다고 하는 상태다. `static_assert`
  로 컴파일 타임에 못박았다(비용 0).
- **`HelpText()` 의 magic static 이 순서에 기대고 있었다.** registry 를 채우는 것은
  `GetTable()` 이고, 오늘은 모든 호출 경로가 그것을 먼저 지나서 맞다. 그러나 앞으로
  `--help` 조기 처리나 LC4 의 수신 스레드가 먼저 부르면 help 가 "0개"로 **영구 동결**
  된다(magic static 은 되돌릴 수 없다). `HelpText()` 가 직접 `GetTable()` 을 부르게 했다.

★ **인자 스키마(§6.2 의 `arguments`)는 넣지 않았다.** 211 개의 인자 타입·필수성을
한 슬라이스에서 정확히 적는 것은 사실상 지어내기가 된다. 대신 `usage` 문자열
(`<필수> [선택]`)까지를 정본으로 세우고, 구조화된 argument 스키마는 domain 을
옮기는 LC6 에서 그 domain 을 아는 사람이 채운다. 비워 두되 비어 있다는 사실이
`CommandDescriptor` 주석에 적혀 있다.

### LC4 — 로컬 HTTP/JSON 서비스 (P0 · 3일) ★ 신규 핵심

- `Engine/CommandService` 신설: Winsock loopback listener, HTTP/1.1 최소 서브셋
  (Content-Length·keep-alive·헤더 한계), 자체 JSON codec.
- endpoint 파일 발행/회수, 토큰 생성·검증, §8 통제 전부.
- `GET /health`, `GET /commands`, `GET /commands/{id}`, `POST /command` 동기 경로.
- 서비스 수명을 에디터 시작/종료에 연결하고 크래시 후 유령 endpoint를 회수.
- 감사 로그.

완료 기준: 켜져 있는 에디터에 `curl`로 명령을 보내 결과 JSON을 받는다 · 비 loopback bind
정적 게이트 통과 · 토큰 없는 요청 전부 401(`/health` 포함) · `Origin` 있는 요청 거부 ·
본문 초과 413 · 서비스 off가 기본이고 off일 때 소켓 0 · 배치 경로 회귀 0.

**상태: 완료 (2026-09-04).**

| 완료 기준 | 증거 |
|---|---|
| `curl` 왕복 | `curl -H "Authorization: Bearer …" -d '{"command":"object.create","args":["Big Boss Character"]}'` → 결과 JSON. 에디터가 그 이름 그대로 오브젝트를 만들었다 |
| bind 정적 게이트 | `s_addr` 대입 중 `INADDR_LOOPBACK` 아닌 것 0건 |
| 토큰 없는 요청 401 | `/health` 포함 예외 없음 |
| `Origin`/`Referer` 거부 | 403 |
| 본문 초과 413 | 1MiB+64B → 413, 프로세스 생존 |
| 기본 off | `--command-service` 없으면 endpoint 파일도 소켓도 없다 |
| 배치 회귀 0 | 전체 회귀 세트가 LC3 기준선과 동일 |

★ **구조화 인자가 왕복 손실 없이 닿는다.** JSON `args` 배열이 `EnqueueStructured`
(LC2)로 곧장 들어가 라인 문법을 한 번도 거치지 않는다. `"Big Boss Character"` 가
공백째 그대로 씬에 도착하는 것을 게이트가 확인한다.

★ **게이트 자체가 두 번 스스로 걸렸다.** ① 유령 endpoint 검사가 미리 만든 파일을
먼저 읽어 포트 1 로 접속했다(존재가 아니라 **우리 pid** 를 기다려야 한다).
② 에디터 stdout 을 파이프로 받고 읽지 않아 버퍼가 차서 종료가 막혔다 — 증상은
"endpoint 파일이 남았다"였는데 원인은 파이프였다.

★★ **보안 검토가 CRITICAL 하나와 HIGH 하나를 잡았다.**

1. **`RestrictToCurrentUser` 가 아무것도 제한하지 않았다.**
   `SetFileAttributesW(FILE_ATTRIBUTE_NORMAL)` 을 부르고 주석에 "상속을 끊고
   소유자 권한만 남긴다"고 적어 뒀다. 그 함수는 읽기전용·숨김 같은 **속성 비트**만
   건드리고 ACL 은 손도 대지 않는다. 즉 평문 토큰이 든 `endpoint.json` 이 부모
   디렉터리에서 상속한 권한 그대로 놓여 있었고, 같은 머신의 다른 계정이 읽으면
   실행 표면의 자물쇠가 통째로 넘어간다. **주석이 사실이 아닌 채로 통과할 뻔했다.**

   프로세스 토큰의 SID 로 명시적 DACL 을 만들고 `SE_DACL_PROTECTED` 로 상속을
   끊었다. 권한을 **먼저** 세우고 그 핸들로 쓴다(쓴 뒤 고치면 그 사이에 노출된
   파일이 존재한다). 권한을 못 세우면 **쓰지 않고 서비스도 뜨지 않는다** — 토큰이
   유일한 자물쇠라, 보호할 수 없는데 적어 두는 것보다 안 여는 편이 낫다.

   변이로 이빨을 확인했다: 실제 구현은 `Protected=true · 허용 ACE 1개`,
   `icacls /inheritance:e` 로 상속을 되살리면 `Protected=false · 허용 ACE 4개` —
   게이트가 잡는다. **그 4개가 처음 구현이 남기던 상태다.**

2. **인증 이전 단계의 서비스 정지가 가능했다.** 수신 루프가 연결을 직접 처리해서,
   아무 로컬 프로세스나 연결만 열고 아무것도 안 보내면 유일한 스레드를 잡았다.
   유휴 타임아웃은 `recv` 마다 새로 30초를 주므로 29초마다 1바이트씩 흘리면
   **영원히** 산다. 토큰도 필요 없다. 덤으로 `maxConnections` 검사는 동시 연결이
   1 을 넘을 수 없어 **죽은 코드**였다.

   연결을 작업 스레드로 넘기고, 유휴 타임아웃과 별개로 **요청 절대 기한**(10초)을
   두었다. 게이트가 미완성 요청을 붙여 둔 채 정상 요청을 보내 확인한다 — **6ms**.

   함께 고친 둘: 작업 스레드에 예외 경계가 없어 요청 하나가 `std::terminate` 로
   에디터를 죽일 수 있었고, 종료 시 `recv` 로 막힌 스레드를 그냥 기다리면 종료가
   최대 30초 늦어졌다(`shutdown` 으로 깨운다).

★★ **게이트의 사각지대도 함께 메웠다.** 검토가 "지워도 초록"이라고 짚은 것들이다 —
`Transfer-Encoding` 거부(요청 밀수 방어 전체가 이 한 줄에 걸려 있었다) · 헤더 개수
상한 · endpoint 파일 DACL · slowloris. 그리고 bind 정적 검사가 **이름만 막고 있었다**:
`INADDR_ANY` 는 그냥 0 이라 `s_addr = 0` 으로 쓰면 정규식을 조용히 통과하고 모든
인터페이스에 열린다. 이제 주소 대입 자체를 보고 `INADDR_LOOPBACK` 이 아닌 것을 잡는다.

이번 슬라이스에 넣지 않은 것: `/operations` 폴링·취소·스트리밍과 프레임 예산 드레인은
LC5 다. 지금은 `timeoutMs` 를 넘기면 **실행은 계속되고 응답만 먼저** 돌아간다
(§5.2 — 이미 시작한 GT 작업을 끊는 것이 더 위험하다).

### LC5 — 프레임 예산 드레인·세션·operation·스트리밍 (P0 · 2.5일) ★ 신규

- §7.2 드레인 정책 구현. 배치 큐 1/frame 의미 보존.
- 세션 모델 — 서비스 세션에서 `wait` 금지, 세션별 severity 누적.
- `cost=Long` 명령의 operationId·폴링·취소·진행 스트림.
- blocked 상태 노출(`state`, `blockedReason`, `queueDepth`, `oldestQueuedMs`), 429 상한.
- `timing` 필드와 지연 SLO 회귀 게이트.

완료 기준: 짧은 명령 100회 연속 왕복의 p50/p95/p99가 §7.1 목표 안 · 씬 로딩 중 요청이
지연이 아니라 상태로 응답 · 장시간 명령이 동기 응답을 막지 않음 · 드레인 예산을 0으로
바꾸면 SLO 게이트가 붉어짐 · 기존 프레임 기반 벤치마크 수치 불변.

**상태: 완료 (2026-09-04).**

| 완료 기준 | 증거 |
|---|---|
| 100회 왕복이 SLO 안 | p50 **6.6ms** · p95 **8.1ms** · p99 **9.6ms** (상한 25/60/120) |
| 멈춤이 상태로 | `GET /health` 가 `state`·`blockedReason`·`queueDepth`(서비스)·`batchQueueDepth`·`oldestQueuedMs` 를 낸다. GT 가 막혀 있어도 답한다(LC4 에서 원자 변수만 읽게 배선). 실행 중이 아닌 정지(`scene.loading`·`batch.wait:N`)도 `blocked` 로 낸다 |
| 긴 명령이 동기를 막지 않음 | `game.pak`(cost=Long)이 `mode=auto` 에서 **202 + operationId** 로 승격되고, 그 동안 짧은 명령이 **16ms** 에 답한다 |
| 예산 0 → SLO 붉어짐 | 변이 실측 **p50 1508ms** 대 정상 6.6ms |
| 프레임 벤치마크 불변 | 배치 큐는 프레임당 1개 유지. 전체 회귀 세트가 LC4 기준선과 동일 |

★ **SLO 를 목표가 아니라 실측 위에 세웠다.** §7.1 에 적혀 있던 50/150/300ms 는 착수 전
추정이고 바닥값의 20배였다 — 그대로 게이트로 쓰면 20배 퇴행이 초록으로 통과한다.
25/60/120 으로 조이고, 그 근거(무엇을 재서 그렇게 정했는지)를 §7.1 과 게이트 머리말
양쪽에 적었다.

★★ **LC4 가 남긴 결함 하나를 여기서 닫았다: 서비스 실패가 배치 판정을 뒤집었다.**

LC4 직후에는 배치와 서비스가 **같은 session** 을 썼다. 그래서 HTTP 로 부른
`scene.load` 하나가 실패하면 `CommandSession::Batch()` 에 누적되고, 그 값이 프로세스
종료 코드가 됐다 — **에디터가 exit 3 으로 끝났다.** 배치 시나리오는 아무 잘못이 없는데
그 판정이 뒤집힌다. LC1 이 세운 "session 은 가장 심한 결과를 보존한다"가 옳게 동작한
결과라 더 잡기 어려웠다: 규약은 지켜졌고 **적용 범위가 틀렸다.**

이제 서비스 명령은 배치 session 에 누적하지 않는다. 서비스 요청의 판정은 HTTP 응답으로
간다. 같은 이유로 `--fail-fast` 의 큐 비우기도 배치 큐만 비운다 — 배치의 판단이 결과를
기다리는 HTTP 요청을 조용히 죽이면 안 된다.

★ **두 큐를 나눈 것은 구현 편의가 아니다.** 배치 큐는 프레임당 정확히 하나를 유지한다
(§7.2). `wait N` 이 정확히 N 프레임이라는 전제 위에 `scene_churn_benchmark` 류가 서
있고, 서비스 지연을 위해 드레인을 바꾸면서 그 전제를 같이 바꾸면 81 개 소비자의
측정값이 조용히 이동한다.

★ **`wait` 는 서비스 세션에서 금지한다.** 전역 프레임 보류는 자기 요청만 늦추는 것이
아니라 **다른 요청 전부**의 지연이 된다. 400 `service.wait_forbidden`.

★ **취소는 시늉하지 않는다.** 오늘 취소 지점을 가진 명령이 하나도 없어서 cancel 은
**409** 다(§7.4). 요청은 기록해 두되 실행은 계속된다. 끊는 시늉을 하고 실제로는 계속
도는 것이 가장 나쁘다 — 호출자는 끝났다고 믿고 그 위에서 다음 판단을 한다.

★ **스트림은 수명 전이까지다.** `GET /operations/{id}/stream` 이 queued→running→
completed 를 흘린다. **명령 단위 진행률은 생산자가 없다** — 그것을 내려면 명령이 진행을
만들어야 하는데 오늘 그런 명령이 0 개다. 없는 것을 있는 척하지 않고, 생산자는 domain 을
옮기는 LC6 이후가 채운다.

★ **게이트가 스스로 두 번 걸렸다.** ① 지역 변수 `$samples` 가 매개변수 `$Samples` 를
덮었다 — PowerShell 변수는 대소문자를 가리지 않는다. 증상("List 를 Int32 로 못 바꾼다")
이 원인에서 멀어 한참 헤맸다. ② 변이 검사를 429 검사 **뒤에** 뒀더니 큐가 이미 차 있어
요청이 느려지는 대신 즉시 429 로 거절됐고, p50 4ms 가 나와 "이빨이 없다"는 잘못된
판정이 났다. **재는 것이 무엇인지에 맞는 상태에서 재야 한다.**

#### LC5 검토에서 나온 결함 5 건과 그 처리

첫 구현이 게이트를 전부 통과한 뒤 동시성 검토를 돌렸고, **게이트가 못 보던 결함
다섯 건**이 나왔다. 전부 고쳤고, 셋은 게이트를 늘려 변이로 이빨을 확인했다.

| # | 결함 | 왜 게이트가 못 봤나 | 처리 |
|---|---|---|---|
| 1 | 스트림 핸들러가 종료 신호를 안 본다 → **종료가 최대 60초 지연** | 게이트는 스트림을 열고 **끝까지 읽은 뒤** 종료했다. 열어 둔 채 내리는 경로를 아무도 안 밟았다 | 루프에 `m_stopping` 확인 추가. `stream_shutdown` 이벤트로 끝낸다 |
| 2 | `--command-service` 만 준 실행은 **registry 가 빈 채** 수신 스레드를 띄운다 → vector read/write 경합 | 게이트의 첫 요청이 `wait` 라, 그 한 번이 표를 채워 냉시작 상태를 지웠다 | 서비스를 열기 전에 `EnsureRegistryPopulated()`. 정렬 캐시까지 GT 에서 만든다 |
| 3 | 같은 원인으로 **냉시작 첫 Long 명령이 동기로** 돈다 | 위와 같다 | 위와 같은 수정. 게이트에 냉시작 인스턴스를 따로 띄우는 검사 추가 |
| 4 | `/health` 가 **배치 큐**를 낸다 → 서비스 큐가 상한까지 차도 `queueDepth 0` | 게이트가 `/health` 의 값을 **읽기만 하고 단정하지 않았다** | `ServiceStatus` 가 두 큐를 따로 낸다. `queueDepth`(서비스) + `batchQueueDepth` |
| 5 | `ExecuteAsync` 가 `OperationTable*` 를 **생포인터로** 람다에 담는다 | 오늘 호출자가 magic static 하나뿐이라 안 터진다 | `shared_ptr` + `weak_ptr` 캡처. 서비스가 먼저 죽으면 결과를 버린다 |

추가로 429 상한이 **TOCTOU** 였다(깊이를 읽고, 나중에 따로 넣는다). 동시 요청이 전부
검사를 통과한 뒤 차례로 들어와 상한을 넘길 수 있었다. 이제 상한 확인과 적재가 **같은
락 안**이고, 서비스 쪽 사전 검사는 "확실히 찰 요청에 operation 기록을 만들지 않는"
빠른 길로만 남겼다.

★★ **다섯 건 중 넷의 공통 원인이 하나다: 게이트가 자기가 만든 상태에서만 쟀다.**

①은 스트림을 끝까지 읽은 뒤에, ②③은 표가 이미 채워진 뒤에, ④는 배치 큐가 비어 있는
줄도 모르고. 검사가 **자기에게 편한 상태를 만들어 놓고** 그 안에서만 물으면, 통과는
"결함이 없다"가 아니라 "그 상태에서는 안 보인다"는 뜻이다. LC5 의 원래 게이트가
9/9 초록이었는데도 CRITICAL 두 건이 살아 있던 이유가 이것이다.

새 검사 셋은 전부 **변이로 이빨을 확인했다**(§14.7). 수정을 되돌려 다시 빌드한 결과:

| 검사 | 정상 | 변이(수정 제거) |
|---|---|---|
| `cold-long-async` | HTTP **202** | HTTP **500** — 표가 빈 채 열려 첫 요청이 내부 오류 |
| `health-service-queue` | `queueDepth` **64** | **0** — 64 개가 줄 서 있는데 한가한 서버로 보인다 |
| `shutdown-with-stream` | **2.5s** | **25s 초과** — 상한에 걸려 종료를 못 봤다 |

변이 ①의 결과가 예측과 달랐다는 점을 적어 둔다. 검토는 "동기로 돌아 5초 뒤 `timed_out`"
을 예상했는데 실제로는 **500** 이 났다. 어느 쪽이든 붉어지지만, **예측한 실패 모드와
실제 실패 모드가 다르면 실측을 적는다** — 예측을 적으면 다음 사람이 틀린 것을 믿는다.

### LC6 — 도메인 분리·Editor operation 의미 정렬 (P1 · 4일)

- §12의 domain 단위로 한 파일씩 이동·빌드한다.
- 이동하는 handler를 result-bearing으로 함께 바꾸되 domain 알고리즘은 수정하지 않는다.
- mutating command를 §9 넷으로 전수 표기한다.
- play/duplicate/undo/selection의 GUI 대 서비스 characterization을 닫는다.

완료 기준: `ConsoleCommandSystem.cpp`가 facade/Pump wiring 중심 · domain 하나당
unity/non-unity build · GUI/서비스 의미 차이 미분류 0 · Player 링크 변화 0.

LC3 이후 domain 단위로 LC4/LC5와 병행 가능하다. 같은 handler를 한쪽이 옮기고 다른 쪽이
formatter까지 바꾸는 동시 작업만 금지한다.

### LC7 — 라이브 코드 실행 A/B (P1 · 2.5일) ★ 신규

- `script.reload`를 결과 있는 명령으로 승격 — 복원 수/실패 목록/이전 컨텍스트 잔존.
- `script.invoke` — 표식된 static 메서드만, GT 실행, 결과 구조화.
- 표식 attribute를 ScriptCore에 정의하고 표식 없는 메서드 호출 시도를 거부로 판정.
- 리로드 실패 시 이전 상태 보존 회귀.
- 수정 → 빌드 → reload → invoke 왕복 시간을 실측해 기록.

완료 기준: 에디터 재시작 0회로 C# 변경이 반영됨 · 표식 없는 메서드 호출 0 · 리로드 실패가
반쯤 교체된 상태를 남기지 않음 · `ExecutesUserCode` 없는 경로로는 B에 도달 불가.

### LC8 — Player Development 에이전트 (P1 · 3일) ★ 신규

- Development/Shipping 구성 구분 도입(PHASE 14와 공유 · §11.1 · 배치는 §12.1).
  13개 vcxproj와 `Tools/build.ps1`·CI 호출부를 같은 슬라이스에서 옮긴다.
- Player registry(축소)와 `roles` 기반 등록.
- `--command-service` 플래그, 기본 off.
- Player 전용 JSON codec 경로 — authoring 계측 경로 미사용.
- Shipping 링크 0 · `runtime.text-parser calls=0` 유지 증명.

완료 기준: 실행 중인 Development Player에 붙어 상태를 읽고 런타임 명령을 반영 ·
Shipping 바이너리에 서비스·소켓 심볼 0 · Player smoke의 text-parser 카운터 불변 ·
`roles`에 Player가 없는 명령은 Player registry에 부재 · **새 구성으로 `Tools/build.ps1`이
Player smoke를 실제로 돌리고 `runtime.text-parser calls=0`를 읽는다**(구성 이름만 바꾸고
스크립트를 두면 빈 출력이 통과한다) · Player의 현행 exit `2`/`3`/`4`가 §5.4 표로 이관됨.

### LC9 — 소비자 이관·legacy 제거·최종 ratchet (P1 · 2.5일)

- JSONL 결과 모드(`--result-format jsonl`)와 서비스 JSON이 같은 schema v1을 공유.
- 소비자를 domain별로 이관: `Invoke-Dx12Suite` → `profiling-validation` → `featuretest`
  → `regression` 개별 → `run-all.ps1` → 루트 `scripts/*.txt`와 문서 예제.
- 이관된 consumer는 text fallback을 유지하지 않는다. 이중 parser는 새 drift를 만든다.
- `LegacyUnreported`, raw reconstruction 호환, 수동 verdict print를 사용량 0 확인 후 제거.
- 정적 게이트 추가: 소스 스크래핑 재도입 금지, 비 loopback bind 금지, 무인증 경로 금지,
  Shipping 서비스 심볼 금지.

완료 기준: void command handler 0 · terminal result 없는 command 0 · help drift 0 ·
direct exit write 1곳 · 소비자의 한국어 verdict regex 0 · §18 전부 통과.

### 의존 순서

```text
LC0 → LC1 → LC2 → LC3 → LC4 → LC5 ─┬→ LC7 ─┐
                     └→ LC6(병행) ─┼→ LC8 ─┼→ LC9
                                    ┘       ┘
```

---

## 14. 검증 매트릭스

### 14.1 정적 게이트

- registry canonical/alias 중복 0
- descriptor 없는 command 0 · `cost`/`roles` 미기입 0
- argument/help coverage 100%
- handler raw line access 최종 0
- `EngineBootstrap::SetExitCode` 직접 호출은 session adapter 한 곳
- command enumeration을 위한 C++ source scraping 0
- **비 loopback bind 0** (`INADDR_ANY`·`0.0.0.0`·외부 주소 리터럴)
- **토큰 검증을 거치지 않는 라우트 0**
- Shipping Player project의 `CommandService`·소켓 심볼 0
- Editor command 파일의 Shipping Player 등록 0

### 14.2 parser·result unit/selftest

| 시나리오 | 판정 |
|---|---|
| `""` | 빈 argument 1개 |
| `"Main Camera"` | quote 제거, 한 token |
| `object.parent "Big Boss" "Main Characters"` | 이름 둘 정확 보존 |
| 같은 인자를 JSON `args`로 | 라인 경로와 동일 invocation |
| Windows path | backslash 무손실 |
| 닫히지 않은 quote | `InvalidArguments`, handler 미호출 |
| 숫자 overflow/garbage | 조용한 0 변환 금지 |
| unknown command | 배치 exit 2 · 서비스 404 |
| precondition failure | 배치 exit 3 · 서비스 409 |
| selftest false | 배치 exit 4 · 서비스 200 + `status:"failed"` |
| 실패 뒤 성공·quit | session 비-0 유지 |

### 14.3 서비스·보안 게이트

| 시나리오 | 판정 |
|---|---|
| 토큰 없음 / 틀린 토큰 | 401. `/health` 포함 예외 없음 |
| `Origin` 헤더 있음 | 거부 |
| 본문 1MiB 초과 | 413, 연결 정리 |
| 헤더 폭탄·불완전 요청·유휴 연결 | 타임아웃·거절, 프로세스 영향 0 |
| 비 loopback 접속 시도 | 연결 불가(bind 자체가 loopback) |
| 큐 상한 초과 | 429 |
| 서비스 off | listen 소켓 0, endpoint 파일 없음 |
| 크래시 후 재시작 | 유령 endpoint 회수, 새 토큰 |
| 잘못된 JSON·타입 불일치 | 400, handler 미호출 |

### 14.4 지연 게이트

- 짧은 명령 100회 연속: p50/p95/p99가 §7.1 목표 안
- 씬 로딩 중 요청: `blockedReason` 응답까지의 시간이 SLO 안
- 장시간 명령 실행 중 짧은 명령: 동기 응답이 막히지 않음
- 서비스 드레인이 프레임 시간에 더하는 비용이 예산 상한 안(PHASE 14 카운터와 교차 확인)

### 14.5 build gate

- VS18/v145 Editor Debug/Release unity build
- Editor non-unity build — domain split의 전이 include 누락 검출
- Player Development / Shipping 양쪽 build
- Shipping 패키징 산출물의 심볼 검사

### 14.6 runtime gate

- `--exec` 반복 순서와 process exit · `--script` 주석/빈 줄/source line · `--console` 종료 회수
- missing script unattended 즉시 실패 · `wait N` 정확한 frame delay · 씬 로딩 중 보류
- play/stop/undo/selection GUI 대조
- quoted object duplicate/parent/prefab authoring
- DX12/Vulkan 대표 selftest success/failure와 artifact
- 서비스/배치 logical result diff 0 · JSONL stdout noise 0
- `script.reload` → `script.invoke` 왕복이 에디터 재시작 없이 닫힘
- 실행 중 Development Player 접속·명령·상태 조회
- 전체 `Tools/regression/run-all.ps1`와 DX12 suite

### 14.7 변이(mutation) 확인

새 게이트가 처음부터 초록이면 이빨을 확인한다.

- result success를 강제로 failure로 바꾸면 process exit가 붉어짐
- descriptor에서 command 하나를 빼면 discovery/help coverage 붉어짐
- parser quote close 처리를 제거하면 golden 붉어짐
- **토큰 비교를 항상 true로 바꾸면 보안 게이트 붉어짐**
- **bind 주소를 `0.0.0.0`으로 바꾸면 정적 게이트 붉어짐**
- **드레인 예산을 0으로 만들면 지연 게이트 붉어짐**
- **Shipping에 서비스 심볼을 남기면 패키징 게이트 붉어짐**

---

## 15. 위험과 롤백 경계

| 위험 | 대응/롤백 |
|---|---|
| 로컬 HTTP가 무인증 실행 표면이 됨 | 토큰 필수·loopback bind·`Origin` 거부·기본 off·감사 로그. §14.3이 게이트 |
| 자체 HTTP 파서의 결함 | 표면을 최소 서브셋으로 고정, 한계·불완전 요청 회귀, 실패 시 연결만 죽고 프로세스는 산다 |
| 서비스 드레인이 프레임 시간을 늘림 | 시간·개수 예산 상한, PHASE 14 카운터로 상시 관측, 예산은 설정값 |
| 드레인 변경이 기존 벤치마크 의미를 바꿈 | 배치 큐 1/frame 보존, 프레임 기반 수치 불변을 게이트로 |
| 임의 코드 실행으로 미끄러짐 | L3-C 범위 밖 명시, `ExecutesUserCode` capability와 별도 플래그 |
| 219개 명령 signature 일괄 변경 | legacy adapter로 domain별 이행, 한 번에 전환 금지 |
| human 로그를 바꿔 기존 regex 파손 | 소비자 이관 전 human verdict 유지 |
| domain 분리 후 unity build 숨은 include 드러남 | 하나씩 이동, unity/non-unity 둘 다 build |
| exit code 강화로 기존 false-green script가 실패 | LC0 canary·소비자 inventory로 원인을 노출, 성공으로 되돌리지 않음 |
| Player 서비스가 Shipping으로 샘 | 구성 분리 + 심볼 게이트 + 패키징 검사 |
| Player JSON codec이 text-parser 게이트를 흔듦 | authoring 경로 미사용, Player smoke 카운터 불변 확인 |
| operation 표가 메모리를 잠식 | 보관 기한·개수 상한, 초과 시 오래된 것부터 폐기 |
| GT 밖으로 옮긴 명령의 data race | affinity 기본 GT, snapshot/worker 안전 증명 전 이동 금지 |
| 11,968줄 대규모 move와 동시 작업 충돌 | handler domain별 작은 patch |

각 슬라이스는 이전 배치 모드와 command file을 유지하므로 rollback은 domain adapter·registry
entry·서비스 on/off 단위로 가능해야 한다. LC9 전에는 legacy adapter를 제거하지 않는다.

---

## 16. MCP — 보류 근거가 바뀐다

앞선 판은 "local command contract가 안정되지 않아서" MCP를 보류했다. 이 계획이 서면 그
이유는 사라지고 다른 이유가 남는다: **HTTP/JSON 계약이 서면 MCP는 얇은 어댑터**가 되고,
얇은 어댑터는 별도 계획에서 며칠이면 된다. 지금 섞으면 전송 계약과 tool schema 계약을
동시에 흔들 뿐이다.

MCP를 다시 판정할 조건:

1. 모든 외부 노출 command가 descriptor/result-bearing이다.
2. schema v1과 exit/HTTP 상태 사상이 전체 regression에서 안정됐다.
3. capability·`cost`·`roles`가 전수 분류됐다.
4. long-running command의 timeout/cancel/partial result 수명이 정의됐다.
5. 승인 모델(어떤 mutation을 어떤 표식 없이 허용할지)이 정해졌다.
6. HTTP/JSON으로 해결되지 않는 실제 요구가 남아 있다.

그 전까지 금지: command 하나를 MCP tool 하나로 기계적 노출 · descriptor에서 곧바로
`tools/list` 생성 · MCP를 이유로 command ID/argument 선변경 · MCP transport 의존을 command
core에 추가. 대시보드에 MCP task를 등록하지 않는다.

---

## 17. 기각한 대안

| 대안 | 기각 근거 |
|---|---|
| 명명 파이프·공유 메모리 | 빠르지만 클라이언트마다 전용 코드가 필요하다. `curl` 한 줄로 붙는 것이 이 계획의 값어치의 절반이다 |
| WebSocket 우선 | 요청/응답이 지배적이고 스트리밍은 장시간 명령에만 필요하다. HTTP + 진행 스트림으로 충분하고 표면이 작다 |
| cpp-httplib 등 HTTP 라이브러리 도입 | 필요한 것은 loopback POST/GET·Content-Length·keep-alive뿐이다. 저장소는 의존을 실측으로 걷어온 이력(boost 80 포트, 미사용 포트 10개)이 있고, 서버 라이브러리는 우리가 쓰지 않을 표면(TLS·라우터·멀티파트·압축)을 함께 들여온다. **다만 자체 구현의 파싱 결함은 실재하는 위험이다** — LC4가 표면을 최소로 고정하고 §14.3으로 검증하지 못하면 이 결정을 되돌린다 |
| ryml을 JSON codec으로 재사용 | Editor만 보면 합리적이다. 그러나 Player 경로가 authoring 계측(`AuthoringParsedDocument`)과 얽혀 `runtime.text-parser calls=0` 게이트의 의미를 흐린다. 두 role이 같은 codec을 쓰는 편이 계약이 하나다 |
| 기존 `printf` 출력을 서비스가 캡처해 반환 | stdout 파싱을 네트워크 계층으로 승격하는 것이다. 지금 소비자들이 겪는 문제를 그대로 물려받는다 |
| 명령마다 스레드 실행으로 지연 단축 | Scene/Editor state의 GT 규약을 깬다. 지연은 드레인 예산으로 줄인다 |
| 배치 CLI 폐기 | 소비자 81개와 CI가 그 위에 있다. 서비스는 추가지 대체가 아니다 |

---

## 18. 최종 완료 조건

- [ ] 기존 `--exec`/`--script`/`--console` 입력과 dotted ID가 호환된다.
- [ ] 등록 command/alias 전부 descriptor·argument·help·`cost`·`roles`를 가진다.
- [ ] 모든 command가 정확히 하나의 terminal `CommandResult`를 만든다.
- [ ] 논리 실패가 뒤의 `quit`와 무관하게 배치 process 비-0으로 끝난다.
- [ ] 켜져 있는 에디터에 로컬 HTTP로 붙어 명령을 보내고 결과 JSON을 받는다.
- [ ] 짧은 명령 왕복이 §7.1 SLO를 만족하고, 그 수치가 실측으로 기록된다.
- [ ] 명령 N개가 N프레임을 기다리지 않으며, 기존 프레임 기반 벤치마크 수치는 불변이다.
- [ ] 멈춤(씬 로딩·긴 명령)이 지연이 아니라 상태로 응답된다.
- [ ] 토큰 없는 요청이 어떤 경로로도 통과하지 않고, bind는 loopback뿐이다.
- [ ] 서비스는 기본 off이고, off일 때 listen 소켓과 endpoint 파일이 없다.
- [ ] 에디터 재시작 없이 C# 변경 → reload → invoke 왕복이 한 세션에서 닫힌다.
- [ ] 표식 없는 관리 메서드는 호출되지 않는다.
- [ ] 실행 중인 Development Player에 붙어 상태를 읽고 런타임 명령을 반영한다.
- [ ] Shipping Player에 서비스·소켓 심볼이 0이고 `runtime.text-parser calls=0`이 유지된다.
- [ ] 배치 JSONL과 서비스 JSON이 같은 schema v1을 공유하고 logical result가 같다.
- [ ] 자동화 consumer가 source/help/한국어 verdict 문자열을 파싱하지 않는다.
- [ ] GUI와 서비스 mutating operation의 공유/의도적 차이가 전수 분류되고 gate로 고정된다.
- [ ] `ConsoleCommandSystem.cpp`가 facade/Pump wiring 중심이며 domain handler가 분리된다.
- [ ] Editor unity/non-unity Debug/Release와 Player Development/Shipping이 통과한다.
- [ ] MCP 구현·transport·tool schema가 0이고, 임의 스니펫 컴파일 실행이 0이다.

여기까지 닫혀야 "명령이 많다"가 아니라 **켜져 있는 엔진에 붙어 초 이하로 일을 시킬 수
있는 서비스**라고 판정한다.

---

## 19. 문서 관계와 상태 규약

- 이 트랙은 2026-09-04에 **PHASE 6에서 분리되어 PHASE 14.5**가 됐다. PHASE 6은 데드 코드
  제거·정적 분석·리소스 회귀라는 원래 범위로 돌아간다.
- 분리 이유는 성격이 다르기 때문이다. PHASE 6은 "더러워지지 않는 구조"이고, 이 트랙은
  **새 제품 표면**(서비스·라이브 실행·Player 제어)이다. 품질 상시화 항목 옆에 두면
  둘 중 하나가 항상 미뤄진다. 실제로 미뤄진 쪽은 이 트랙이었고, 그 사이 대상 파일이
  일주일에 77% 자랐다(§2.1).
- 14.5라는 자리는 PHASE 14(프로파일러)와 Development/Shipping 구성·계측 카운터를 공유하기
  때문이다. 둘 중 먼저 도착하는 쪽이 구성 구분을 만들고 다른 쪽이 소비한다(§11.1).
- 대시보드 PHASE 0 `0-8`은 최초 자동화 레이어의 완료 기록으로 유지한다.
- 계획 작성만으로 LC0을 완료 처리하지 않는다.
- 문서 정적 검사와 실제 build/runtime 검증을 구분한다.
- 구현 슬라이스가 끝날 때만 이 문서와 대시보드의 상태·실측 수치·gate 결과를 함께 갱신한다.
- §2.1의 수치는 2026-09-04 정적 집계다. LC0이 런타임 표 덤프로 다시 생성하면 그 값으로
  대체한다.
