# 명령 실행·발견·HTTP의 현재 구조

> 기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스 정적 조사. 이 문서의 재작성에서 빌드·실행·성능 측정은 하지 않았다.

## 등록과 실행

Editor는 ConsoleCommandSystem의 `GetTable`에서 8개 domain registrar를 호출하고 추가 scene 진단 명령을 등록한다. 각 등록은 accepted canonical/alias를 handler table에 넣고 `CommandDescriptorSeeds`의 summary/usage/cost/role/class/liveness/named input/undoable 정보를 descriptor에 연결한다. 중복으로 거부된 이름도 registry Problems에 기록한다.

seed가 commandlet인 항목은 별도 CommandletTable/CommandRegistry::Commandlets로 간다. 일반 API discovery와 process-scoped 검증을 혼합하지 않는다. `EditorCommandlets::Run`의 material migration/resolve/script 검사는 명시적 특수 dispatch도 갖는다. C++ selftest 함수가 존재하는 것만으로 호출 명령이 존재하지 않는다.

## 결과와 owner

CLI line은 tokenizer를 거치고 structured argument 호출은 문자열 재조합 없이 전달한다. handler는 CommandResult를 반환한다. SceneAccess는 live state를 빌리는 경로와 독립 owned state 경로를 나누며, socket thread가 직접 scene mutation을 실행하지 않는다. EditorObjectOperations를 쓰는 create/delete/rename/parent/transform/component/undo 경로는 GUI와 공통 operation을 사용한다. 모든 probe가 Undo를 기록하는 것은 아니다.

CommandSession은 batch의 가장 심한 status를 유지한다. 성공 0, 인자 오류 2, 선행조건 3, 실패/취소/timeout 4, 내부 오류 5가 exit mapping이다. 뒤의 성공이 앞선 실패를 지우지 않는다. crash fixture는 의도적으로 exception boundary를 빠져나가는 특수 등록이다.

## HTTP 서비스

서비스는 기본 꺼져 있고 host의 명시적 시작을 통해 켠다. local loopback·Bearer token·요청 크기/기한·동시 연결/queue 상한을 갖는다. Origin/Referer 요청은 거부하며 /health도 인증 예외가 아니다. endpoint 파일에 인증 정보가 있으므로 공유 로그·문서에 token을 복사하지 않는다.

GET /health, /warmup, /commands, /commands/{id}; POST /command; /operations/{id} 조회·stream·cancel 경로가 있다. auto mode에서 Long descriptor는 async operation으로 승격된다. HTTP 200이 논리 성공을 보장하지 않으므로 body status/code를 읽는다. timeout은 이미 시작한 GT 작업을 강제 중단하지 않는다. 현재 operation cancel은 요청을 기록하지만 취소 지점이 없어 409와 실행 계속을 반환한다.

user-code capability는 서비스 시작과 별도 허용이다. allowUserCode 기본 false이고 실행 dispatcher도 해당 descriptor에서만 CLR user-code scope를 연다. 비용·liveness 표시는 handler 의미의 선언이며 실제 지연 측정을 대신하지 않는다.

## Player와 Shipping

Development Player는 독립 kPlayerCommands whitelist와 seed role을 둘 다 통과해야 registry에 들어간다. Editor 명령이 Both seed를 가졌다는 사실만으로 자동 Player handler가 생기지 않는다. Shipping은 CLI/service 구현을 compile-exclude하며 local 입력을 Development 전용 오류로 거부한다.

현재 표면은 실행체의 help/commands.list/GET /commands가 정본이다. [CommandSurfaceTable](CommandSurfaceTable.md)은 정적 등록 위치를 찾아가는 보조표이며 [CommandSurfaceDisposition.tsv](CommandSurfaceDisposition.tsv)는 과거 은퇴 결정 기록으로만 보존한다.

## 구현 근거

- [Editor/EngineEntry/ConsoleCommandSystem.cpp](../../Editor/EngineEntry/ConsoleCommandSystem.cpp)
- [Editor/EngineEntry/Commands/CommandRegistrar.h](../../Editor/EngineEntry/Commands/CommandRegistrar.h)
- [Engine/RuntimeHost/CommandCore/CommandDescriptorSeeds.cpp](../../Engine/RuntimeHost/CommandCore/CommandDescriptorSeeds.cpp)
- [Engine/RuntimeHost/CommandCore/CommandRegistry.cpp](../../Engine/RuntimeHost/CommandCore/CommandRegistry.cpp)
- [Engine/RuntimeHost/CommandCore/CommandSession.cpp](../../Engine/RuntimeHost/CommandCore/CommandSession.cpp)
- [Engine/CommandService/CommandService.cpp](../../Engine/CommandService/CommandService.cpp)
- [Editor/EngineEntry/EditorCommandServiceHost.cpp](../../Editor/EngineEntry/EditorCommandServiceHost.cpp)
- [Player/PlayerCommands.cpp](../../Player/PlayerCommands.cpp)
- [Player/Player.vcxproj](../../Player/Player.vcxproj)
