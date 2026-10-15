# Commandlet 은퇴 기록을 현재 코드와 연결하는 법

> 기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스 정적 조사. 이 문서의 재작성에서 빌드·실행·성능 측정은 하지 않았다.

## 현재 분리

제품 명령은 live service/help registry, process-scoped 검증은 commandlet table로 분리된다. seed의 commandlet flag가 등록 destination을 결정하며 EditorCommandlets::Run에는 세 material migration/resolve/script 특수 검사가 남아 있다. “검사 코드 있음”과 “CLI에서 호출 가능”은 별개다.

과거 은퇴 때 얻은 원칙은 유지한다. synthetic fixture를 제품 API처럼 숨기지 않고, 일반 편집은 공통 EditorObjectOperations를 통해 Undo/selection 규약을 따른다. test 입력 asset이나 corpus를 명시해 실제 프로젝트에서 임의 파일을 고르는 fallback을 피한다.

## 과거 목록의 한계

[CommandSurfaceDisposition.tsv](CommandSurfaceDisposition.tsv)는 역사적 분류로 보존한다. remove는 영구 금지 목록이 아니며 api도 현재 등록 보증이 아니다. 현재 assets.generation은 다시 seed/registrar에 있는 commandlet이므로 옛 remove row를 근거로 호출 불가라고 답하면 틀린다.

정확한 현재 명령은 [정적 등록 지도](CommandSurfaceTable.md)에서 source를 찾고 실제 실행체 discovery로 확인한다. 은퇴 후보는 source handler, registrar, descriptor, regression consumer, GUI product operation까지 함께 추적한다. 새 실행 검증 없이 이전 101/108 등의 개수를 현재 상태로 재사용하지 않는다.

## 구현 근거

- [Editor/EngineEntry/ConsoleCommandSystem.cpp](../../Editor/EngineEntry/ConsoleCommandSystem.cpp)
- [Editor/EngineEntry/Commandlets/EditorCommandlets.cpp](../../Editor/EngineEntry/Commandlets/EditorCommandlets.cpp)
- [Editor/EngineEntry/Commands/AssetAuthoringCommands.cpp](../../Editor/EngineEntry/Commands/AssetAuthoringCommands.cpp)
- [Engine/RuntimeHost/CommandCore/CommandDescriptorSeeds.cpp](../../Engine/RuntimeHost/CommandCore/CommandDescriptorSeeds.cpp)
