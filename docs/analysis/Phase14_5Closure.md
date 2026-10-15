# Phase 14.5 명령 표면 수용 경계

> 기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스 정적 조사. 이 문서의 재작성에서 빌드·실행·성능 측정은 하지 않았다.

## 현재 구조로 확인한 것

CommandCore의 schema/parser/result/session, Editor domain registrar, 별도 CommandService gateway 및 Player whitelist가 존재한다. help/TSV/HTTP discovery는 descriptor를 공유한다. batch exit는 worst status를 유지하고 HTTP는 논리 실패와 서버 내부 오류를 구분한다.

이는 소스 연결의 확인이다. 모든 명령의 runtime·GUI 동등성이나 현재 branch의 종결 판정을 새로 내린 것은 아니다. 오래된 command count 또는 회귀 결과를 현재 master에 붙이지 않는다.

## 종결에 필요한 두 종류의 증거

자동 검증은 canonical/alias 충돌, seed↔registration 양방향 일치, tokenizer/structured input, named schema, JSON result와 exit code, role/Shipping, service auth·queue·timeout을 다뤄야 한다. GUI 검증은 공통 operation을 통해 Undo/Redo, selection, Play/Stop, prefab override와 실패 시 무변경을 확인해야 한다.

HTTP 200은 body succeeded와 다르다. operation cancel은 현재 409이며 진행 중 작업을 멈추지 않는다. 이런 제한을 숨긴 gate는 종결 근거가 될 수 없다.

## 후속 문서

[명령 구조](CommandSurfaceImplementation.md), [registry 감사](CommandRegistryAudit.md), [GUI 체크리스트](Phase14_5ManualGuiChecklist.md)가 현행 계약이다. 과거 병합 전 성공 기록은 역사적 맥락이며 이 재작성에서 빌드·실행하지 않았다.

## 구현 근거

- [Engine/RuntimeHost/CommandCore/CommandRegistry.cpp](../../Engine/RuntimeHost/CommandCore/CommandRegistry.cpp)
- [Engine/RuntimeHost/CommandCore/CommandSession.cpp](../../Engine/RuntimeHost/CommandCore/CommandSession.cpp)
- [Editor/EngineEntry/Commands/SceneObjectCommands.cpp](../../Editor/EngineEntry/Commands/SceneObjectCommands.cpp)
- [Engine/CommandService/CommandService.cpp](../../Engine/CommandService/CommandService.cpp)
- [Player/PlayerCommands.cpp](../../Player/PlayerCommands.cpp)
