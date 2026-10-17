# PHASE 12 — 텍스처 sidecar 실패 진단 수정

작업: 2026-10-10 KST, 기반 HEAD `d3c85faf182e93842217dd46eee3d3aabf4afc13`.

## 문제와 변경

직전 실제 Debug Inspector 검증에서 정상 GUID가 있는 metadata를 잠그면 자동 저장 실패가
GUID 오류로 표시됐다. `SetTextureImportSettingsAndReimport`가 ParseFile 실패, 문서 구조,
GUID 실패를 하나의 조건으로 처리하면서 모든 실패 원인을 UUIDv4 오류로 덮어썼다.

파일을 읽거나 파싱하지 못하면 원래 진단을 유지하고, 문서가 map이 아니면 구조 오류를
표시한다. 파일에서 다시 GUID를 읽는 대신 이미 읽은 문서의 assetId/guid를 같은 우선순위로
검사한다. 실제 신원이 잘못됐을 때만 UUIDv4 오류를 표시한다. 이 변경은 마지막 비교 이후
동시 쓰기를 원자적으로 막는 변경이 아니다.

추가 회귀에서 잘못된 GUID 문자열이 생성자 예외를 일으키는 문제도 재현했다.
예외를 발생시키는 문자열 생성 대신 `Uuid::TryParse`로 검사해 정상적인 거부 결과와
UUIDv4 진단을 반환한다.

기존 자산 서비스 회귀에 잠금·삭제·손상 YAML·비-map·잘못된 GUID 거부와 디스크 bytes,
기존 generation 보존 검사를 추가했다. 파일 삭제 거부는 서비스 범위이며 실제 UI의
저장 대기/종료 중 삭제 수용으로 확대하지 않는다.

## 실행 검증

Debug Editor 라이브러리와 CreatorEditor runtime/launcher 빌드를 통과했다. 정리된 Physics,
RenderTests, HostImGuiPresentation Debug 라이브러리가 없어 초기 연결이 실패했으며
필요한 라이브러리를 재빌드한 뒤 최종 연결을 통과했다. Debug 최초 수용 당시 Release는
미검증이었으며 아래 Release 후속 실행에서 별도로 수용했다.

- 자산 서비스 회귀: Debug 짧은 경로·긴 경로 각각59 checks, sourceDrift0.
  [회귀 receipt](../../Build/TextureSidecarDiagnostics/lifecycle-result.json),
  [짧은 경로](../../Build/TextureImportLifecycle/run-Debug-short-1.log),
  [긴 경로](../../Build/TextureImportLifecycle/run-Debug-long-1.log).
- 실제 Debug UI: 읽어 둔 fixture metadata를 FileShare.None으로 잠그고 최대 크기를 키로
  0→2 편집했다. 자동 저장 오류가 `failed to open authoring document`로 표시됐으며 잠금
  해제 후 원래 bytes hash가 일치했다. Reload로 0을 읽고 오류를 해제한 다음 새 입력2의
  정상 자동 저장을 확인했다. 준비 명령과 파일 잠금은 제어된 fixture 설정이고 숫자 입력과
  Reload는 실제 UI 조작이다.
- 미저장 UnsavedT0Texture가 화면에 유지됐고 저장 씬·원본 PNG와 GUID를 보존했다.
  정상 Finalize와 DX12 pending task/batch/retirement 0/0/0도 확인했다.
  [UI receipt](../../Build/TextureSidecarDiagnostics/ui-result.json),
  [잠금 오류 화면](../../Build/TAUI-Debug-manual-bf22cb4e/locked-read-diagnostic.png),
  [Reload 복구](../../Build/TAUI-Debug-manual-bf22cb4e/reload-recovered.png),
  [새 편집](../../Build/TAUI-Debug-manual-bf22cb4e/fresh-edit.png),
  [종료 로그](../../Build/TAUI-Debug-manual-bf22cb4e/editor.out).
- 최종 Debug 소비자 회귀217 checks/sourceDrift0: ImageComponent/SpriteRenderer 자동4→2,
  미저장 씬·Undo·실패 보존·4 복구 통과.
  [소비자 receipt](../../Build/TextureSidecarDiagnostics/scene-result.json).

최종 UI runtime SHA-256:
`55AE5091C71C0B073E44241CB63DD52BBE802FC6F03FE15F130945054DDCC81C`.
실행 전후 동일하며 수정된 EditorAssetDatabase.cpp SHA-256:
`8E5F1EF3D9AA895BC27F0F457432DD9D0F8FF73E4771E2A997F22F08F5974427`.
HEAD는 기반 커밋이며 이 소스 수정은 작업 트리에 있다. Build 증거는 로컬 산출물이다.

## 수용 경계

이번 진단 수정·한정 서비스 거부 검사는 기존 12-0-D의 후속 검증이며 추가 완료 가중치는
배정하지 않는다. PHASE12는 13.25/20 = 66.25%, 표시66%를 유지한다.
읽기 실패 문구는 OS 잠금과 삭제를 개별 오류 코드로 설명하지 않는다. 저장 대기 중
삭제/손상 외부 설정, 최종 비교 이후 동시 쓰기, 반복/장시간, 다중 창/배율,
Debug 종료 충돌과 전체 소비자/GPU/품질은 잔여다.

## Release 후속 검증

동일 수정 소스로 Release Editor 라이브러리를 빌드했다. 초기 참조 빌드 생략 상태에서
정리된 생성 헤더와 의존 라이브러리가 없어 실패했으며 Utility/EngineDiagnostics/GCCE/
SceneRuntime/RenderEngine/Physics/RenderTests/HostImGuiPresentation/CommandService를
복구했다. 최종 Editor 라이브러리 빌드는 통과했다.

Release 자산 서비스 회귀는 짧은·긴 경로 각59 checks, sourceDrift0로 통과했다.
[Release 회귀 receipt](../../Build/TextureSidecarDiagnostics/lifecycle-release-result.json),
[짧은 경로](../../Build/TextureImportLifecycle/run-Release-short-1.log),
[긴 경로](../../Build/TextureImportLifecycle/run-Release-long-1.log).
이는 서비스 거부·보존 검증이며 UI 저장 대기/종료 중 삭제 경합 수용은 아니다.

Release 회귀 실행 파일과 Editor의 병렬 LTCG 연결에서 같은 SceneRuntime 중간 ASM 파일을
생성해 공유 위반이 발생했다. 회귀 연결을 완료한 뒤 Editor 연결을 직렬로 재시도해 통과했다.
최종 CreatorEditor runtime/launcher 및 배포 산출물 생성이 완료됐다. 기본 Release 설정을
유지했으며 이번 후속 검증에서 제품 소스는 추가 변경하지 않았다.

실제 Release Inspector에서 읽어 둔 metadata를 FileShare.None으로 잠그고 숫자2를 입력했다.
자동 저장 실패가 `failed to open authoring document`로 표시됐고 잠금 해제 후 baseline
bytes hash가 일치했다. 실제 Reload로 디스크0을 읽고 오류를 해제한 다음 새 숫자2 입력의
정상 자동 저장을 확인했다. 미저장 객체·저장 씬·PNG·GUID를 유지하고 정상 Finalize 및
DX12 pending0/0/0으로 종료했다. 숫자/Reload는 실제 UI 입력이며 잠금은 제어된 실패 주입이다.

- [Release UI receipt](../../Build/TextureSidecarDiagnostics/ui-release-result.json)
- [잠금 오류 표시](../../Build/TAUI-Release-manual-e0e973ba/locked-read-diagnostic.png)
- [재읽기 복구](../../Build/TAUI-Release-manual-e0e973ba/reload-recovered.png)
- [새 편집 자동 저장](../../Build/TAUI-Release-manual-e0e973ba/fresh-edit.png)
- [정상 종료 로그](../../Build/TAUI-Release-manual-e0e973ba/editor.out)

최종 Release 소비자 회귀270 checks/sourceDrift0도 통과했다. ImageComponent/SpriteRenderer
자동4→2, 미저장 씬·Undo, cook/reload 실패 보존 및4 복구를 확인했다.
[Release 소비자 receipt](../../Build/TextureSidecarDiagnostics/scene-release-result.json).
UI와 소비자 회귀 runtime SHA-256은 모두
`F148DF4861C033FE889DFDB27E9C7AADA31BCE9B246EB49E6A3C5F287C80F7B0`이며 실행 전후 동일했다.
서비스 및 소비자 회귀의 소스 manifest도 최종 확인에서 drift0이었다.

이번 Release 수정 재검증은 기존12-0-D의 후속 수용이다. 별도 가중치를 더하지 않고
66.25% (표시66%)를 유지한다. 저장 대기 중 삭제/손상·최종 비교 이후 동시 쓰기·
반복/장시간·다중 창/배율·Debug 종료 충돌 및 전체 소비자/GPU/품질은 잔여다.
