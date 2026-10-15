# 리소스 자동 편집: 감지·준비·게시의 실제 소유자

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 등록 경로를 2026-10-15에 대조했다. 이 개정에서는 빌드·실행·화면 검증을 하지 않았다. 과거 결과와 현재 구현의 존재를 구분한다.

## 공통 경로

자동 처리는 Inspector 함수 안의 파일 저장으로 끝나지 않는다. `App`의 정상 루프는 queued asset 변경을 drain한 뒤 `EditorAssetDatabase::TickAutomaticChanges`, prefab 자동 저장, EditorMain 갱신, material session 갱신, 명령 pump와 frame 게시를 진행한다. watcher와 worker는 입력 및 준비 결과를 만들고 살아 있는 씬의 참조 교체는 owner 경로에서 수행한다.

일반 meta, typed texture 설정, render profile 파일은 EditorAssetDatabase의 sidecar/path별 자동 저장 대기열을 사용한다. 400ms 안정화와 최초 디스크 바이트 비교를 적용하며 충돌·쓰기 실패는 오류로 남긴다. UI 패널의 수명과 대기열의 수명은 다르다. render profile의 메모리 즉시 반영과 파일 저장 성공도 분리해 관찰해야 한다.

## 리소스별 차이

- 텍스처: typed 설정 저장 → background cook → immutable generation → queued publication → description 및 image 준비 → 알려진 씬 소비자 owner 교체다. 자세한 계약은 [텍스처 정본](../plans/TexturePipelinePlan.md)에 있다
- 모델: watcher가 안정화된 원본/메타 변경을 처리하고 모델 준비 작업을 요청한다. 준비된 모델 세대를 현재 MeshRenderer/Animator에 반영한다. 없는 메시 또는 비호환 세대를 기존 참조에 강제로 덮어쓰지 않는다
- 셰이더: source/include/shadermeta 변경에서 worker 컴파일을 수행하고 검증된 프로그램만 게시한다. 오류 로그의 “retained prior materials”는 실패한 새 결과 대신 이전 material을 유지하는 경로다
- 재질 그래프: session revision과 document ID에 맞는 preview 준비가 끝나고 편집이 0.6초 안정되어야 자동 Apply 다음 Save를 시도한다. active preview/입력 중에는 보류하며 같은 revision의 반복 실패를 무한 재시도하지 않는다. 현재는 Scene 선택이 없어도 자산을 만들고 열 수 있다
- prefab: 편집 씬 ID로 대상을 다시 찾고 authored entity tree snapshot을 비교한다. 200ms polling, 600ms 안정화 후 GUID를 보존해 저장하고 cache-owned 정의로 `UpdateInstances(accepted, editScene)`를 호출한다. 편집 씬은 instance 적용에서 제외한다. 외부 디스크 변경 및 저장 실패는 편집 내용을 보존하며 로그를 낸다
- C#: `Assets/Script` 변경을 내용 snapshot과 650ms 안정화로 감지한다. 비동기 외부 빌드 성공 후 reload와 재연결을 수행한다. 빌드 중 원본이 바뀌면 후속 빌드가 필요하고 Play 상태에서는 생성/재시도 또는 적용이 보류·거부될 수 있다

## 종료와 실패

`App::Finalize`는 파일 대기 저장과 prefab 마지막 snapshot을 먼저 처리하고 CLI를 종료한다. material shutdown은 preview job과 마지막 revision을 처리한 뒤 자동 저장을 시도하고 session을 해제한다. 그 뒤 EditorMain과 runtime이 정리된다. “종료 flush가 호출됨”은 모든 파일 저장 성공의 증명이 아니며 오류 로그와 최종 파일을 따로 확인해야 한다.

자동 자산 갱신은 씬 전체 자동 저장 기능이 아니다. 미저장 엔티티를 유지한 채 자원 owner만 바뀔 수 있다. Undo/Redo를 검증하려면 단순 화면 일치 대신 실제 편집 명령·stable handle·저장된 참조를 관찰해야 한다. 각 subsystem의 안정화 시간은 서로 다르므로 모든 자동 작업을 하나의 400ms 완료 보장으로 설명하지 않는다.

## 검증 경계

`verify-automatic-material-editor.ps1`, `verify-automatic-script-editor.ps1`, texture 종단 probe는 서로 다른 수용 범위를 가진다. 2026-10-10 기록의 당시 실행 결과를 현재 HEAD에서 재실행한 결과로 전용하지 않는다. 현재 코드와 연결은 확인했지만 다중 편집자 충돌, 강제 종료, 장시간 재편집, 모든 shader/model 호환성의 수용은 별도 실행이 필요하다.

## 구현 근거

- [Editor/EngineEntry/App.cpp](../../Editor/EngineEntry/App.cpp)
- [Editor/EngineEntry/EditorAssetDatabase.cpp](../../Editor/EngineEntry/EditorAssetDatabase.cpp)
- [Editor/EngineEntry/PrefabEditor.cpp](../../Editor/EngineEntry/PrefabEditor.cpp)
- [Editor/EngineEntry/EditorScriptAuthoring.cpp](../../Editor/EngineEntry/EditorScriptAuthoring.cpp)
- [Editor/EngineGUIWindow/MaterialGraphWindow.cpp](../../Editor/EngineGUIWindow/MaterialGraphWindow.cpp)
- [Tools/regression/verify-automatic-material-editor.ps1](../../Tools/regression/verify-automatic-material-editor.ps1)
- [Tools/regression/verify-automatic-script-editor.ps1](../../Tools/regression/verify-automatic-script-editor.ps1)
