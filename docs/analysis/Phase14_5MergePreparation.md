# 명령 표면 통합 이후 지켜야 할 경계

> 기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스 정적 조사. 이 문서의 재작성에서 빌드·실행·성능 측정은 하지 않았다.

## 병합 절차 문서의 현재 용도

과거 특정 branch의 충돌 해결·명령 개수는 이 revision의 작업 지시가 아니다. 현재 유지할 경계는 Engine/RuntimeHost의 CommandCore, Editor의 등록·operation adapter, role-neutral CommandService, Development Player의 명시적 whitelist다.

새 domain 명령을 추가할 때 handler만 옮기지 말고 registrar entry와 descriptor seed를 함께 연결한다. alias 충돌은 실제 accepted 이름을 기준으로 기록한다. commandlet flag에 따라 일반 service registry와 검증 table이 달라지며 Player는 별도 등록을 요구한다.

## 통합 검증

명령 JSON/exit mapping, named parameter validation, GUI Undo/selection, service queue capacity와 shutdown callback 수명을 확인한다. interface를 늘렸다면 Editor와 Player adapter를 모두 확인해야 한다. Shipping의 compile-excluded source도 별도 구성으로 점검한다.

현재 master에서 이런 구조를 읽었으나 새 merge, build, runtime 검증은 하지 않았다. [CommandSurfaceImplementation](CommandSurfaceImplementation.md)과 [Phase14_5Closure](Phase14_5Closure.md)가 현행 검토 기준을 제공한다.

## 구현 근거

- [Engine/CommandService/CommandGateway.h](../../Engine/CommandService/CommandGateway.h)
- [Editor/EngineEntry/Commands/CommandRegistrar.h](../../Editor/EngineEntry/Commands/CommandRegistrar.h)
- [Editor/EngineEntry/ConsoleCommandSystem.cpp](../../Editor/EngineEntry/ConsoleCommandSystem.cpp)
- [Player/PlayerCommands.cpp](../../Player/PlayerCommands.cpp)
- [Player/Player.vcxproj](../../Player/Player.vcxproj)
