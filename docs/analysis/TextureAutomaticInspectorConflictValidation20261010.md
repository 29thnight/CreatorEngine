# 자동 저장 중 외부 디스크 변경의 충돌 계약

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 등록 경로를 2026-10-15에 대조했다. 이 개정에서는 빌드·실행·화면 검증을 하지 않았다. 과거 결과와 현재 구현의 존재를 구분한다.

## 충돌은 어디에서 결정되는가

`QueueAutomaticSave`는 path를 정규화해 첫 예약 시 디스크 문자열을 expectedDisk로 보관한다. 같은 파일의 후속 UI 편집은 writer와 changedAt만 바꾼다. 400ms 뒤 TickAutomaticSaves가 ready 작업을 대기열에서 꺼내고 현재 디스크 문자열과 비교한다. 다르면 pending edit를 쓰지 않고 “The source changed on disk; pending edits were not written.” 오류를 기록한다.

이것은 pending 구간의 낙관적 충돌 검출이다. 모든 외부 writer와의 파일시스템 transaction lock이나 임의 시각의 compare-and-swap 보장은 아니다. 비교 이후 실제 write 사이의 제3자 변경까지 모두 직렬화한다고 주장하지 않는다.

## UI 복구 경로

`DrawTextureImportSettingsEditor`는 AutomaticSaveError를 매번 조회한다. 충돌 시 `Reload Disk Settings`로 sidecar를 다시 파싱하고 settings/observed/baseline을 맞춘 뒤 오류를 지운다. 이 동작은 새 자동 저장을 예약하지 않으므로 다른 프로세스의 값을 읽은 직후 다시 덮어쓰지 않는다. 읽기·파싱이 실패하면 saveError를 표시한다.

사용자가 이후 값을 새로 편집하면 그 시점의 디스크를 기준으로 새 예약이 시작된다. `Retry Import`는 현재 유효한 draft를 typed save/reimport API에 전달하는 별도 의도적 경로다. draft가 남아 있다는 것과 디스크에 저장되었다는 것을 혼동하지 않아야 한다.

## 종료 경합

FlushAutomaticSaves는 debounce를 기다리지 않고 같은 디스크 비교를 수행한다. 따라서 정상 종료라고 외부 변경을 무조건 덮어써서는 안 된다. `AUTOSAVE_FLUSH`는 배출 시도 age, `AUTOSAVE_CONFLICT`는 거부를 관찰하는 로그다. 파일 최종 바이트와 오류 상태 없이 로그 한 줄만으로 성공을 판정하지 않는다.

## 이전 관찰 및 현재 수용 한계

2026-10-10의 conflict UI 기록은 Release pending 구간에 외부 meta를 바꾸고, UI 오류→disk reload→다시 편집, 그리고 정상 종료 충돌을 구분했다. 당시 집계는 `Build/TextureAutomaticConflictUi/result.json`, fixture는 `TAUI-Release-conflict-64d24b9d` 및 `TAUI-Release-conflictshutdown-3daac159`였다. 본 개정은 해당 local Build 자료를 재생성하거나 현재 HEAD에서 재실행하지 않았다.

현재 source에서 conflict guard, UI 오류 표시, read-only reload와 shutdown 재사용을 확인했다. 다중 프로세스 고빈도 write, 원자 rename과 권한/잠금 실패, 읽을 수 없는 파일과 빈 파일의 구별, 장시간 반복 경합은 별도 실패 주입이 필요하다. 정상 자동 흐름과 수명은 [텍스처 정본](../plans/TexturePipelinePlan.md), 관찰 helper는 [Release 입력 기록](TextureAutomaticInspectorReleaseValidation20261010.md)을 따른다.

## 구현 근거

- [Editor/EngineEntry/EditorAssetDatabase.cpp](../../Editor/EngineEntry/EditorAssetDatabase.cpp)
- [Editor/EngineGUIWindow/DrawYamlNodeEditor.cpp](../../Editor/EngineGUIWindow/DrawYamlNodeEditor.cpp)
- [Tools/regression/texture_auto_ui_pending_probe.inl](../../Tools/regression/texture_auto_ui_pending_probe.inl)
- [Editor/EngineEntry/App.cpp](../../Editor/EngineEntry/App.cpp)
