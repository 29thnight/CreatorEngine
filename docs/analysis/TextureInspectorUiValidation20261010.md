# 수동 텍스처 Inspector 흐름의 호환성 기록

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 등록 경로를 2026-10-15에 대조했다. 이 개정에서는 빌드·실행·화면 검증을 하지 않았다. 과거 결과와 현재 구현의 존재를 구분한다.

## 이전 UI와 현재 UI의 관계

이 경로는 2026-10-10 초기의 Save and Reimport 및 Reload Saved Scene UI 검증 문서를 찾는 독자를 위한 호환 요약이다. 그 버튼 순서를 현재 사용 절차로 안내하지 않는다. 현재 `DrawTextureImportSettingsEditor`는 유효한 편집을 자동 저장 대기열에 넣고 `Retry Import`, 충돌 시 `Reload Disk Settings`를 제공한다. “Scene references update without reloading the scene” 안내가 현재 본문에 있다.

일부 status fallback 문자열에 Save and Reimport라는 옛 표현이 남아 있어도 실제 버튼의 존재를 뜻하지 않는다. UI를 기술할 때는 그리는 함수와 입력 handler를 함께 읽어야 한다.

## 보존할 검증 의미

이전 수동 흐름은 저장 전 draft와 디스크 값의 차이, 사용자 확인 전 미저장 씬을 버리지 않는 동작, 파일 선택 취소 및 실제 마우스 입력을 확인하려는 목적이었다. 이 요구 중 “현재 값을 디스크와 구분한다”는 자동 대기열/오류 표시로 이어졌고 “미저장 씬 보호”는 reload를 하지 않는 자동 owner 교체로 바뀌었다. scene.open_async 자체의 실패 보존은 [별도 씬 검사](TextureSceneReloadValidation20261010.md)로 유지한다.

2026-10-10의 `Build/TUI-Debug-1479b598/Project`는 이전 UI를 실행한 격리 프로젝트다. 그 화면·DLL hash·클릭 기록은 당시 UX의 증거이며 지금 자동 UI의 실행 수용으로 재사용할 수 없다. 원본 기록은 Git 이력에서 확인할 수 있고 외부 Build 산출물이 이 checkout에 있다고 가정하지 않는다.

## 현재 검사를 할 때

유효한 설정 변경 후 400ms 안정화, 패널 닫힘 후 저장, 잘못된 BC5/normal 조합의 거부, cook 실패 시 이전 owner 유지, 디스크 충돌 복구를 확인한다. 현재 Debug UI의 범위는 [자동 Inspector](TextureAutomaticInspectorUiValidation20261010.md), Release 입력 전달은 [Release 기록](TextureAutomaticInspectorReleaseValidation20261010.md), 충돌 처리는 [충돌 기록](TextureAutomaticInspectorConflictValidation20261010.md)에 구분했다. 문서 개정 자체는 클릭이나 GPU 결과의 재검증이 아니다.

## 구현 근거

- [Editor/EngineGUIWindow/DrawYamlNodeEditor.cpp](../../Editor/EngineGUIWindow/DrawYamlNodeEditor.cpp)
- [Editor/EngineEntry/EditorAssetDatabase.cpp](../../Editor/EngineEntry/EditorAssetDatabase.cpp)
- [Editor/EngineEntry/Commands/SceneObjectCommands.cpp](../../Editor/EngineEntry/Commands/SceneObjectCommands.cpp)
