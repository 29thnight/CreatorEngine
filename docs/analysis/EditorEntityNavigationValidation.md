# 엔티티 선택 이력·편집 잠금·아이콘 신원

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 등록 경로를 2026-10-15에 대조했다. 이 개정에서는 빌드·실행·화면 검증을 하지 않았다. 과거 결과와 현재 구현의 존재를 구분한다.

## 선택 이력

SelectionHistory는 scene ID와 EntityHandle을 최대 128개 보관한다. scene이 바뀌면 이력을 비우고 같은 선택을 연속 추가하지 않는다. 뒤로 이동한 뒤 새 선택을 관찰하면 forward 구간을 자른다. Peek/Move는 살아 있는 handle만 찾으므로 삭제된 entity 또는 재사용된 slot이 다른 선택으로 복원되지 않아야 한다.

Inspector의 뒤로/앞으로는 NavigateSelection을 호출하고 자산 선택 메타 상태를 비운다. 이력 이동은 UI navigation이며 authoring Undo transaction이 아니다. 이력을 scene 파일에 저장하는 기능으로 안내하지 않는다.

## 편집 잠금

Entity의 m_editorLocked는 저작용 상태이며 runtime simulation을 멈추는 값이 아니다. EditorObjectOperations가 ancestor와 필요한 경우 descendant 잠금을 검사한다. Inspector는 부모에 잠긴 경우 부모부터, 잠긴 자식을 포함하면 그 자식부터 해제하도록 안내한다. 잠금 버튼이 보이는 것만으로 모든 시스템의 mutation을 전역 차단하는 보안 경계가 생기는 것은 아니다. 실제 authoring operation, gizmo, hierarchy의 guard를 유지해야 한다.

SetEditLocked와 SetIcon은 generation-checked reference를 사용해 Undo command를 만든다. 이름·transform·부착·reparent 같은 다른 operation도 각자의 잠금 사전조건을 검사한다. Play simulation이 사용하는 entity enabled나 physics layer와 혼동하지 않는다.

## 아이콘 preset

m_editorIcon에는 파일 경로 대신 stable preset ID를 저장한다. 현재 아홉 값은 기본 빈 ID, game-manager, camera, light, audio, player, trigger, script, prefab이다. 알 수 없는 ID는 표시 시 기본 아이콘으로 fallback한다. Inspector와 Hierarchy는 EditorAssetPresentation의 같은 mapping을 읽는다. 현재 그림은 [Fluent Emoji](EditorFluentEmojiValidation.md)이며 초기 @icons 그림을 현재 배포 자원으로 설명하지 않는다.

저장/재로드, duplicate, Undo/Redo에서 아이콘·잠금 값이 유지되는지와 선택 이력이 세션 한정으로 동작하는지는 별도로 검사한다. 기존 Artifacts 이미지와 과거 성공 기록은 최신 binary의 수용이 아니다. `verify-editor-entity-authoring.ps1`은 실제 operation 회귀의 출발점이며 이번 개정에서는 실행하지 않았다.

## 구현 근거

- [Editor/EngineEntry/EditorSelectionHistory.h](../../Editor/EngineEntry/EditorSelectionHistory.h)
- [Editor/EngineEntry/EditorEntityIcons.h](../../Editor/EngineEntry/EditorEntityIcons.h)
- [Editor/EngineEntry/EditorObjectOperations.cpp](../../Editor/EngineEntry/EditorObjectOperations.cpp)
- [Editor/EngineGUIWindow/InspectorWindow.cpp](../../Editor/EngineGUIWindow/InspectorWindow.cpp)
- [Engine/SceneRuntime/Entity.h](../../Engine/SceneRuntime/Entity.h)
- [Tools/regression/verify-editor-entity-authoring.ps1](../../Tools/regression/verify-editor-entity-authoring.ps1)
