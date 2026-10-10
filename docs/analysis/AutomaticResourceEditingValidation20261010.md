# 에디터 리소스 편집 자동 처리 — 2026-10-10

사용자 요청은 편집 감지 후 필요한 처리를 자동 실행하고, 재질 그래프 Apply·Save 및
프리팹 Apply까지 자동화하는 것이다. 이 기록은 소스 구현과 실제 실행 수용을 구분한다.
기존 dirty worktree의 텍스처 임포트 구현 위에 후속 변경을 적용했으며 전체 변경을
새 작업의 성과로 계산하지 않는다. 커밋/게시를 수행하지 않았다.

## 현재 구현

| 영역 | 감지와 자동 처리 | 보존/실패 정책 |
|---|---|---|
| 텍스처 임포트 설정 | Inspector 편집 → 400ms 공통 저장 대기열 → 비동기 cook → 게시 | GUID/원본 보존, 패널 숨김과 선택 변경에 독립 |
| 일반 메타 설정 | Inspector 편집 → 자동 저장 → watcher의 typed 재임포트/소비 갱신 | 기존 GUID 유지; 모델은 importSettings와 기존 ModelImporter 옵션을 편집하고 정체성 필드는 유지 |
| 열린 씬 텍스처 | 준비된 description + CodecImage → Image/Sprite/Sheet/Decal/재질 owner 교체 | 씬 재로드 없음, UI rect·재질 값·논리 참조와 Undo 보존, 이전 프레임 pin 유지 |
| 모델 | 원본/메타 watcher → worker 재임포트 → 현재 MeshRenderer/Animator 세대 반영 | 모델/메시 ID와 재질 편집·애니메이션 시간 보존, 누락 메시 거부 |
| 재질 셰이더 | shader source/include/shadermeta watcher → worker 컴파일 → 새 그래프/코드 프로그램 게시 | 준비/검증 성공분만 게시; 구세대/컴파일 실패/계약 비호환은 기존 값 유지 |
| 재질 그래프 | 편집 안정화·컴파일 성공 → 자동 Apply → Save | 창이 닫혀도 진행, 저장 후 Undo/Redo 유지, 디스크 충돌 거부 |
| 프리팹 | 편집 씬 snapshot 변화 → 자동 Save → 기존 UpdateInstances | 편집 씬 제외, 기존 override 적용 정책 및 Undo 유지 |
| C# 게임 스크립트 | Assets/Script의 수정·추가·삭제·이름 변경 → 안정화 → 비동기 빌드 → 기존 리로드·재연결 | 중복 내용 감지, 빌드 중 재편집 재빌드, 컴파일 실패 시 이전 코드/인스턴스 유지 |
| 렌더 프로필 | Inspector 편집 → 공통 저장 대기열 → 원자 파일 게시 | 런타임 즉시 반영은 기존 UpdateProfileEditMode; 디스크 충돌 거부 |

공통 대기 저장은 종료 전에 배출한다. 프리팹과 재질 그래프도 종료 시 마지막 편집을
처리하며, 재질/셰이더 컴파일 worker는 관련 서비스 해체 전에 정리한다. 임포트 실패는
기존 정상 자산을 유지하고 로그/Inspector 상태에 보고한다. 기존 명시적 API와 재시도
동작은 호환을 위해 유지한다. 새 자산을 다른 오브젝트에 할당하는 선택과 씬 전체 저장은
리소스 편집 자동화와 별개인 기존 사용자 작업이다.

주요 소스: `EditorAssetDatabase.cpp`, `App.cpp`, `DrawYamlNodeEditor.cpp`,
`InspectorWindow.cpp`, `MaterialGraphWindow.cpp`, `PrefabEditor.cpp`,
`Material.cpp`, `ShaderMetadataRuntime.cpp`, `LXMaterialRuntime.cpp`,
`MeshRenderer.cpp`, `ImageComponent.cpp`, `DecalComponent.cpp`,
`SpriteSheetComponent.cpp`, `PrefabUtility.cpp`.

## 검증

- 수정 경로의 `git diff --check`: 통과. 관련 없는 기존 workspace 파일의 변경은 보존.
- `verify-texture-import-editor-source.py`: 5 tests 통과. 정적 계약 검사이며 runtime 증거가 아니다.
- Debug 실제 Editor host 빌드: 통과 (`Editor/CreatorEditor.vcxproj`, `Build/automatic-resource-host-debug-accepted.log`).
- 실제 host 자동 텍스처/프리팹 검증: 통과 (281 assertions; 폴링 조건 검사 포함). `Build/TextureReimportScene/result.json`, fixture `Build/TRS-Debug-e7ec2f8c`.
- 실제 host 자동 재질/모델/셰이더 검증: 통과 (107 assertions; 폴링 조건 검사 포함). `Build/Obj/MaterialProductProbe/AutomaticEditor-Debug-c2b41c9cfbeb/result.json`.

`verify-texture-reimport-scene.ps1`은 독립 프로젝트에서 실제 sidecar watcher와
공통 설정 대기열을 검사한다. 4→2px 자동 owner 교체, 미저장 UI 크기와 Undo/Redo,
이전 이미지 소유, cook 실패/씬 실패 보존 및 복구에 더해 프리팹 자동 Apply와
Undo/Redo를 검사한다. `verify-automatic-material-editor.ps1`은 별도 복제 프로젝트에서
재질 그래프 자동 Apply/Save, 숨긴 창의 처리, 저장 후 Undo/Redo, 모델 메타 변경 후
현재 renderer 세대 갱신, include 변경/원복 후 그래프 세대 갱신을 검사한다.

실행 검증 중 프리팹 자동 저장의 Windows 파일 잠금 문제를 재현했다. 충돌 검사에
사용한 읽기 핸들을 닫기 전에 원자 교체를 시도해 GetLastError=5로 거부됐다.
읽기 핸들을 닫고 게시하도록 수정했다. 또한 디스크 캐시의 PrefabNode 시퀀스를
단일 Entity 루트로 정규화한 뒤 UpdateInstances에 전달하고, 자동 Apply의 예외를 보고한다.
임시 추적 코드와 서비스 접근 변경은 제거했다.

두 실행 receipt의 Debug runtime DLL SHA-256은 같다:
`4F776088C80F6B3275536526AB374736FA95FC64EC441EC5017334406A8DB22F`.
템플릿의 오래된 씬 직렬화 대신 현재 native Scene/Model 생성 경로로 재질 fixture를
구성했다. 이전 실행 파일의 결과와 실패한 시도는 최종 통과 증거에 포함하지 않는다.

실행으로 확인한 텍스처 소비자는 ImageComponent/SpriteRenderer다. SpriteSheet/Decal/재질
텍스처 교체, Animator 시간 보존, 코드 전용 셰이더, 렌더 프로필 및 모델 이외의 일반
메타 자동 저장 경로는 소스와 빌드로 확인했으며 개별 실행 수용은 수행하지 않았다.

## C# 자동 빌드·재연결 후속

`EditorAssetDatabase`의 watcher 이벤트를 `EditorScriptAuthoring::NotifySourceChanged`에
연결했다. 기존 프로세스 빌드 경로를 공유하며, 편집 안정화 후 현재 프로젝트
`Assets/Script/**/*.cs`를 다시 빌드한다. 파일 내용이 같은 중복 알림은 무시한다.
빌드 완료 후 소스 목록/내용을 다시 비교하고, 빌드 도중 바뀐 경우 재빌드한다.
성공 시 기존 `PrepareForReload → ReloadScripts → RestoreAfterReload` 경로로
로드된 씬의 기존 스크립트 컴포넌트를 재연결하며 직렬화 필드 값을 보존한다.
Play 중에도 프레임 경계의 기존 리로드 경로를 사용한다. 실패는 로그와
`script.creation` 상태로 보고하고, 프로젝트 전환/종료 시 소유한 빌드 작업을 정리한다.

- Debug host 빌드: 통과 (`Build/automatic-script-debug-verified.log`).
- 실행 검증: `Tools/regression/verify-automatic-script-editor.ps1 -Configuration Debug` 통과.
- Receipt: `Build/AutomaticScripts-Debug-60ad2d9b/result.json` (80 assertions; 상태 검사 포함).
- Runtime DLL SHA-256: `B286C2E2BCBB5626DFC7DEF2120CFB18940FA2D5D7E5D9D52723317702559FD0`.
- 파일 생성·수정·이름 변경·보조 타입 삭제·임시 파일 원자 교체를 실제 watcher로 확인했다.
- 두 컴포넌트의 직렬화 필드 값 보존, 컴파일 오류 시 이전 코드/인스턴스 정체성 유지,
  오류 수정 후 자동 복구, Play 중 재연결 및 활성 인스턴스 수 2 유지를 확인했다.
- 검증은 `script.create`/`script.reload`를 호출하지 않고 파일 저장으로 빌드를 유발했다.
  테스트가 사용하는 배포 스크립트 파일은 종료 후 원본 백업으로 복원했다.

현재 프로젝트의 게임 스크립트에 대한 처리다. 엔진의 ScriptCore/제너레이터 자체 변경은
기존 엔진 빌드 범위다. 삭제/이름 변경으로 컴포넌트 클래스가 없어지면 기존 리로드의
타입 복원 오류로 보고한다. 임의의 비직렬화 런타임 상태 전체 보존을 의미하지 않는다.
Release/Shipping, 여러 프로젝트 전환과 빌드 중 반복 편집의 강제 교란 시험,
배포판 빌드 도구 경로는 이번 Debug 실행 수용과 구분한다.

## 수용 한계

Release/Shipping, 모든 이미지 포맷·모델·소비자, 직접 마우스/키보드 입력의 전 조합,
GPU 픽셀 비교, 성능/장시간 수용을 이 변경의 정적 검사로 통과 처리하지 않는다.
파일 watcher는 안정화 시간을 사용하며, 무효한 편집이나 호환되지 않는 셰이더 계약은
자동 게시하지 않는다. PHASE 12의 earnedDays와 기존 잔여 품질 게이트는 유지한다.
