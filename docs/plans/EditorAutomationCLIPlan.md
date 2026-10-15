# Editor 자동화: 명령·서비스·Commandlet 경계

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 등록 경로를 2026-10-15에 대조했다. 이 개정에서는 빌드·실행·화면 검증을 하지 않았다. 과거 결과와 현재 구현의 존재를 구분한다.

## 실행 표면과 소유권

Editor의 명령 정본은 CommandRegistry descriptor와 명시적 domain registrar다. ConsoleCommandSystem이 인자를 큐에 넣고 게임 스레드 Pump에서 실제 제품 동작을 호출한다. UI thread의 mutation도 값과 generation handle을 캡처해 EnqueueEditorMutation으로 owner에 넘긴다. worker/queue가 Scene·Entity·Component raw pointer나 GC root를 임의로 보유하는 구조를 만들지 않는다.

GUI와 CLI가 같은 EditorObjectOperations를 쓰면 Undo와 사전조건을 공유할 수 있다. 그러나 모든 읽기/검사/저장 명령이 Undo 가능하다는 뜻은 아니다. descriptor의 class, roles, liveness, cost, userCode, undoable과 resultBearing을 확인한다. 실제 등록과 descriptor seed가 둘 다 필요하며 목록에 이름만 추가하는 것으로 handler가 생기지 않는다.

## 입력 모드

- `--exec`, `--script`, `--console`은 일반 배치/콘솔 입력이다
- `--exec-args <명령> [인자]... [--]`는 이미 분리된 인자를 전달하므로 공백 경로를 문자열 결합 후 재파싱할 필요가 없다
- `--command-service`는 local HTTP 서비스 활성화다. 기본 off이며 `--allow-user-code`는 서비스의 사용자 코드 실행을 별도로 허용한다
- `--commandlet` 또는 `--commandlet-script`는 전용 검증 프로세스 모드다. 일반 batch/console/HTTP와 혼합하면 mode conflict로 거부한다
- `--result-file`은 결과 JSONL을 기록한다. HTTP와 배치 봉투의 변환은 EditorCommandJson/ResultEnvelope를 공유한다

Commandlet registry와 live registry는 분리된다. texture reimport/pending probe 같은 검증 명령을 일반 서비스에서 호출할 수 있다고 안내하지 않는다. `EditorCommandlets::Run`은 host 초기화 후 게임 스레드에서 호출되는 별도 진입점이다. 일반 배치와 stdin에는 HTTP의 allow-user-code 스위치가 동일하게 적용되는 것은 아니다.

## HTTP 계약

CommandService는 loopback에만 bind하고 기본 port 0에서 OS가 고른 port를 쓴다. endpoint는 프로젝트 `Library/CommandService/endpoint.json`에 기록된다. 토큰은 인증 정보이므로 로그·문서·issue에 복사하지 않는다. `/health`를 포함해 Bearer 인증을 요구하고 Origin/Referer가 있는 요청은 거부한다. browser에서 localhost 호출이 가능하다는 이유로 이 검사를 없애지 않는다.

`GET /commands`와 `/commands/<name>`으로 descriptor를 확인한다. execute 요청에는 command와 문자열 args 배열 또는 지원되는 named parameters 객체를 사용하며 둘을 동시에 주면 거부한다. named schema의 미지 필드·형식·필수값을 검증한 후 structured arguments로 전달한다. 원문 command line으로 다시 합치지 않는다.

기본 queue 상한 64, 동시 연결 16, idle 30초, 요청 전체 수신 deadline 10초가 있다. 본문 1MiB, header 8KiB/64개 제한은 HTTP parser 계약이다. userCode 금지는 queue pressure보다 먼저 판정한다. queue full은 429이며 재시도 가능성과 이미 접수된 작업의 결과 불확실성을 구별해야 한다.

## 결과와 비동기 완료

HTTP 200은 명령의 논리 성공을 보장하지 않는다. 실패·취소·timeout도 result status로 반환할 수 있다. unknown command는 404, invalid arguments는 400, precondition은 409, internal error는 500으로 매핑한다. 큐에 들어간 뒤 클라이언트 대기 timeout이 났다고 제품 작업이 취소되었다고 가정하지 않는다.

비동기 operation은 반환된 poll/stream 경로를 사용해 terminal 결과를 읽는다. domain 자체의 준비/완료 상태도 별도다. `scene.open_async`의 접수 뒤 `scene.load.status`를 확인하고, `profile.save`의 요청 뒤 `profile.save status`를 확인한다. `profile.deep.start/status/stop`의 session ID는 uint64를 보존하는 십진 문자열이며 정확한 session에 stop을 요청한다. CPU 준비 완료, 업로드 명령 기록, GPU fence 완료를 한 상태로 합치지 않는다.

## Editor 창 명령과 현재 UI 한계

`editor.windows`, `editor.menu`는 선언/배선을 감사한다. `editor.window <stable-id> open|close|focus`는 UI mailbox로 요청한다. `editor.workspace`는 save/load/reset/preset/named 작업을 요청하고 게시 상태로 확인한다. `editor.viewport`, `editor.theme`, `editor.panelcost`는 UI가 게시한 관측값을 읽는다. 명령 결과를 바로 읽을 수 있어도 다음 UI 프레임 적용이 필요한 작업은 접수와 적용을 구분한다.

하단 Cmd는 편집 가능한 문자열 필드만 있으며 Enter는 이 명령 큐를 실행하지 않는다. 현재 shell UI에 필드가 있다는 이유로 CLI 실행 표면과 동일하게 안내하지 않는다. 실행/이력/완성의 제품 연결은 남은 요구다.

## 회귀 기준

`verify-editor-command-surface.ps1`과 descriptor/서비스 검사는 실제 surface를 기준으로 유지한다. 음성 입력, 모드 충돌, auth 실패, queue full, stale ID, 취소와 정상 shutdown, 사용자 코드 permission, Shipping에서의 surface 부재를 각각 확인해야 한다. UI 드래그·clipboard·파일 대화상자를 command 성공으로 대체하지 않는다. 본 개정에서는 runner를 실행하거나 새 endpoint를 열지 않았다.

## 구현 근거

- [Editor/EngineEntry/ConsoleCommandSystem.cpp](../../Editor/EngineEntry/ConsoleCommandSystem.cpp)
- [Editor/EngineEntry/ConsoleCommandSystem.h](../../Editor/EngineEntry/ConsoleCommandSystem.h)
- [Editor/EngineEntry/EditorCommandServiceHost.cpp](../../Editor/EngineEntry/EditorCommandServiceHost.cpp)
- [Editor/EngineEntry/Commands/CommandRegistrar.h](../../Editor/EngineEntry/Commands/CommandRegistrar.h)
- [Editor/EngineEntry/Commandlets/EditorCommandlets.h](../../Editor/EngineEntry/Commandlets/EditorCommandlets.h)
- [Engine/RuntimeHost/CommandCore/CommandDescriptorSeeds.cpp](../../Engine/RuntimeHost/CommandCore/CommandDescriptorSeeds.cpp)
- [Engine/CommandService/CommandService.h](../../Engine/CommandService/CommandService.h)
- [Engine/CommandService/CommandService.cpp](../../Engine/CommandService/CommandService.cpp)
- [Editor/EngineEntry/Commands/SceneObjectCommands.cpp](../../Editor/EngineEntry/Commands/SceneObjectCommands.cpp)
- [Editor/EngineEntry/Commands/DiagnosticsCommands.cpp](../../Editor/EngineEntry/Commands/DiagnosticsCommands.cpp)
- [Tools/regression/verify-editor-command-surface.ps1](../../Tools/regression/verify-editor-command-surface.ps1)
