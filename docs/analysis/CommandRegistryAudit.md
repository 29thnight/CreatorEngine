# 명령 registry 감사 기준

> 기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스 정적 조사. 이 문서의 재작성에서 빌드·실행·성능 측정은 하지 않았다.

## 양방향으로 검사할 것

현재 registry는 descriptor schema와 accepted handler 이름을 연결한다. canonical/alias collision, 빈 summary, seed 누락을 Problems와 selftest가 드러내도록 구성되어 있다. 반대 방향인 “seed는 있는데 등록문이 없음”도 검사해야 한다. help와 registry가 함께 빠지는 경우 두 출력만 비교하면 누락을 못 본다.

현재 ConsoleCommandSystem은 등록 성공·실패 이름을 나누어 descriptor를 만든다. 따라서 seed 파일의 항목 수, C++ handler 함수 수, 실제 등록 명령 수와 alias 포함 이름 수는 모두 다르다. [CommandSurfaceTable](CommandSurfaceTable.md)에 정적 등록 지도를 두고 runtime discovery를 정본으로 사용한다.

## Undo, cost, capability

EditorOperation은 GUI와 transaction/Undo/selection 의미를 맞춰야 하는 분류다. 실제 SceneObjectCommands가 EditorObjectOperations로 위임하는 create/delete/rename/transform/parent/component/undo 경로를 확인할 수 있다. descriptor의 undoable flag 하나만으로 모든 실패·Play 모드·prefab override의 정확성이 증명되지는 않는다.

cost는 HTTP auto async 판단에 쓰이므로 무해한 장식이 아니다. Long을 Immediate로 잘못 표시하면 호출 timeout과 queue 관찰이 잘못될 수 있다. 반대로 query 전체를 Long으로 표시하면 불필요한 operation 왕복을 만든다. live/restart/terminates와 userCode도 실제 호출에 맞춰 검토한다.

## 은퇴의 판단

제품 API, fixture commandlet, C++ contract test를 구분한다. 계획이 닫혔다고 공유 저수준 test 함수를 곧바로 삭제하지 않으며, 현재 consumer와 실제 진입점을 확인한다. 역사적 [CommandSurfaceDisposition.tsv](CommandSurfaceDisposition.tsv)의 remove/api/commandlet은 현재 사용 가능 상태가 아니다. assets.generation처럼 나중에 재도입된 표면도 있다.

이번 감사는 수천 줄의 옛 명령 수·비용 추정·Undo 비율을 현재 수치로 반복하지 않는다. 명령별 실행 시간, GUI 동등성, queue pressure와 cancellation fixture는 새 실행 증거가 필요하다.

## 구현 근거

- [Engine/RuntimeHost/CommandCore/CommandRegistry.cpp](../../Engine/RuntimeHost/CommandCore/CommandRegistry.cpp)
- [Engine/RuntimeHost/CommandCore/CommandDescriptorSeeds.cpp](../../Engine/RuntimeHost/CommandCore/CommandDescriptorSeeds.cpp)
- [Editor/EngineEntry/ConsoleCommandSystem.cpp](../../Editor/EngineEntry/ConsoleCommandSystem.cpp)
- [Editor/EngineEntry/Commands/SceneObjectCommands.cpp](../../Editor/EngineEntry/Commands/SceneObjectCommands.cpp)
- [Engine/CommandService/CommandService.cpp](../../Engine/CommandService/CommandService.cpp)
