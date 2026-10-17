# 종료 계획의 검증 명령을 유지·은퇴하는 기준

> 기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스 정적 조사. 이 문서의 재작성에서 빌드·실행·성능 측정은 하지 않았다.

## 계획 종료와 API 제거는 다른 판단

현재 commandlet flag는 process-scoped verification table을 선택한다. 계획 문서의 체크박스가 완료되어도 제품 회귀를 지키는 handler나 test entry가 자동 제거되는 구조는 아니다. 공통 contract test, commandlet dispatch와 외부 harness의 소비를 각각 조사해야 한다.

반대로 소스에 test 함수가 남아 있다고 live command로 광고하면 안 된다. 현재 GetTable의 registrar 호출과 CommandDescriptorSeeds, EditorCommandlets::Run을 함께 확인해야 한다. runtime registry가 최종 discovery이며 Shipping Player에 Editor 검증 surface를 약속하지 않는다.

## 보존 정책

역사적 [CommandSurfaceDisposition.tsv](CommandSurfaceDisposition.tsv)는 당시 remove/api/commandlet 판단을 남기는 기록이다. 현재 소스가 다시 등록한 항목까지 제거된 것으로 간주하지 않는다. 새 은퇴 작업은 대체 product operation, 유지할 regression owner, 제거할 launcher/harness와 성공·실패 판정 경로를 명시해야 한다.

이 문서는 과거 “닫힌 계획”의 잔여 숫자를 재검증하지 않는다. 현재 계약은 [CommandSurfaceImplementation](CommandSurfaceImplementation.md), 현재 등록 경로는 [CommandSurfaceTable](CommandSurfaceTable.md)로 승계한다.

## 구현 근거

- [Editor/EngineEntry/ConsoleCommandSystem.cpp](../../Editor/EngineEntry/ConsoleCommandSystem.cpp)
- [Editor/EngineEntry/Commandlets/EditorCommandlets.cpp](../../Editor/EngineEntry/Commandlets/EditorCommandlets.cpp)
- [Engine/RuntimeHost/CommandCore/CommandDescriptorSeeds.cpp](../../Engine/RuntimeHost/CommandCore/CommandDescriptorSeeds.cpp)
- [Player/Player.vcxproj](../../Player/Player.vcxproj)
