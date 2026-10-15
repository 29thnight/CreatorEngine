# 자동 텍스처 Inspector의 입력·상태 표시 계약

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 등록 경로를 2026-10-15에 대조했다. 이 개정에서는 빌드·실행·화면 검증을 하지 않았다. 과거 결과와 현재 구현의 존재를 구분한다.

## 현재 입력 배선

Inspector는 source별 draft에 설정·관찰값·parse/save 오류를 보관한다. Color Space, Compression, Mip Policy, Maximum Dimension, Normal Map, Alpha Coverage, Alpha Cutoff, Compression Quality를 편집한다. Maximum Dimension 입력은 0~16384로 clamp하며 0은 원본 크기를 뜻한다. sampler wrap/filter는 각 참조에 있다는 안내를 별도로 표시한다.

typed validator를 통과하고 관찰값과 달라졌을 때만 `QueueAutomaticTextureSettings`를 호출한다. 정상 경로는 Enter·Save·씬 reload가 필요 없다. 무효 BC5/normal 조합은 오류를 표시하고 저장되지 않는다. Retry Import도 valid 조건을 통과해야 실행되므로 보이는 버튼의 스타일과 실제 요청 허용을 구별한다.

## 수명과 상태를 관찰하는 법

선택 변경이나 Inspector 닫힘은 EditorAssetDatabase의 대기열을 취소하지 않는다. 400ms 안정화 후 sidecar가 저장되고 worker cook·frame-boundary publication·image 준비 뒤 씬 owner가 교체된다. “ready” 문자열만으로 모든 GPU 작업이 끝났다고 보지 않는다. 디스크 최종 값, import 상태, 미저장 엔티티, 이전 세대 유지 여부를 별도로 읽는다.

손상 PNG에서 Retry를 눌러도 정상 owner를 실패 결과로 대체하지 않아야 한다. 원본 복구는 watcher를 통해 다시 준비된다. 자동 resource 갱신은 씬을 저장하는 동작이 아니므로 실행 중 남아 있던 미저장 엔티티가 재시작 후 없다는 사실은 정상일 수 있다.

## 이전 Debug 관찰의 범위

2026-10-10의 `Build/TAUI-Debug-534117b3` 기록은 숫자 키 입력, 다른 자산 선택, 닫힌 Inspector 상태의 외부 meta 변경, 손상 PNG 및 Retry, 원본 복구, BC5 무효 조합을 다뤘다. 실제 입력 후 화면과 sidecar를 관찰했지만 별도 GPU pixel oracle이나 현재 component owner ID를 독립 조회한 검사는 아니었다. 당시 선택 변경이 정확히 400ms 대기 안에서 이루어졌다고 보장하지도 않았다.

본 개정은 그 작은 Debug UI 결과를 최신 HEAD에서 다시 실행하지 않았다. pending-close/종료의 제어된 관찰은 [Release 기록](TextureAutomaticInspectorReleaseValidation20261010.md), 외부 편집 충돌은 [충돌 기록](TextureAutomaticInspectorConflictValidation20261010.md)을 따른다. 전체 구현 정본은 [텍스처 파이프라인](../plans/TexturePipelinePlan.md)이다.

## 구현 근거

- [Editor/EngineGUIWindow/DrawYamlNodeEditor.cpp](../../Editor/EngineGUIWindow/DrawYamlNodeEditor.cpp)
- [Editor/EngineGUIWindow/InspectorWindow.cpp](../../Editor/EngineGUIWindow/InspectorWindow.cpp)
- [Editor/EngineEntry/EditorAssetDatabase.cpp](../../Editor/EngineEntry/EditorAssetDatabase.cpp)
- [Engine/RenderEngine/Experiment/Cooked/TextureImportSettings.cpp](../../Engine/RenderEngine/Experiment/Cooked/TextureImportSettings.cpp)
