# 명령과 GUI의 의미 동등성 체크리스트

> 기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스 정적 조사. 이 문서의 재작성에서 빌드·실행·성능 측정은 하지 않았다.

## 준비

동일 revision의 Development Editor, 격리한 복제 프로젝트와 고정 scene을 사용한다. 실제 registry에서 명령과 named input 지원을 확인한다. editor_operation/undoable은 검증 대상을 고르는 metadata이며 GUI가 같은 함수를 호출한다는 사실만으로 결과를 통과 처리하지 않는다.

## 확인할 사용자 흐름

- create/duplicate/delete: hierarchy, 선택 대상, 이름 충돌, Undo 후 identity·component 복원
- rename/parent/move/transform: scene dirty, prefab override, Undo/Redo 및 부모 변환 영향
- component add/remove/property: 필수 component 거부, invalid type/value 실패, 이전 값 보존
- select/navigation: 빈 선택과 다중 선택의 규약, 대상 삭제 뒤 stale selection 정리
- Play/Stop: Edit/Game Undo 분리, scene snapshot 복원, 연속 요청과 준비 중 전환
- save/load/prefab: product service와 authoring operation을 구분하고 저장 실패가 성공 메시지로 덮이지 않는지 확인

각 항목은 GUI 조작과 해당 command를 별도 초기 상태에서 수행한 뒤 result status/code, Undo 깊이, selection, scene dirty와 저장된 결과를 비교한다. raw fixture는 의도적으로 Undo를 우회할 수 있으므로 일반 편집과 같은 기대를 강제하지 않는다.

## 기록 방식

입력, 전/후 상태, 실패 시 무변경 여부, screenshot/log와 재현 revision을 남긴다. “호출처가 있다”는 정적 사실, “창에서 조작했다”는 실행, “의미가 같다”는 판정을 구분한다. 이번 문서는 실제 operation 위임을 읽어 체크리스트를 갱신한 것으로 수동 GUI 검증은 미실행이다.

## 구현 근거

- [Editor/EngineEntry/Commands/SceneObjectCommands.cpp](../../Editor/EngineEntry/Commands/SceneObjectCommands.cpp)
- [Engine/RuntimeHost/CommandCore/CommandDescriptorSeeds.cpp](../../Engine/RuntimeHost/CommandCore/CommandDescriptorSeeds.cpp)
- [Editor/EngineEntry/ConsoleCommandSystem.cpp](../../Editor/EngineEntry/ConsoleCommandSystem.cpp)
