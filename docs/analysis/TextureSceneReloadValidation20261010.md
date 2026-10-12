# PHASE 12 — 실제 Editor 씬 재로드 검증

기준일: 2026-10-10 KST. 정본은 [TexturePipelinePlan](../plans/TexturePipelinePlan.md),
직전 자산 서비스 검사는 [임포트 수명 기록](TextureImportLifecycleValidation20261010.md)에 있다.

## 실행 경계

`verify-texture-scene-reload.ps1`은 현재 Debug/Release의 실제 `CreatorEditor.exe`와
`CreatorEditor.runtime.dll`을 사용한다. `--development-project`로 ignored Build 안의
새 프로젝트를 지정하고 실제 명령·프레임 경로에서 실행한다. 기존 프로젝트의 자산,
설정, 작업 공간 파일에 검증 fixture를 작성하지 않는다.

Inspector의 Reload Saved Scene이 사용하는 `scene.open_async`로 씬을 준비하고,
일반 프레임 진행 뒤 `scene.load.status`로 terminal state를 확인한다. 최초 fixture는
`scene.new` → `object.create` → `scene.save`로 만든다. 검사기는 저장된 fixture의
필수 bundle에 PNG 참조를 추가하고 문서 손상과 의존성 누락을 주입한다.
이는 제품 저작 UI와 같은 조작을 증명하는 검사가 아니다.

## 수용 계약

1. 필수 4×4 PNG가 포함된 저장 씬의 재로드가 Ready가 되고 SavedOnly 엔티티가 복원된다.
   이전 씬의 InitialUnsaved 엔티티는 새 씬에 남지 않는다. 제품 texture 조회로 4×4를 확인한다.
2. 교체 뒤 RetainedUnsaved 엔티티를 새로 만든다. 없는 씬 파일, 손상된 씬 문서,
   없는 필수 texture의 세 요청이 각각 Failed가 된다.
3. 각 실패 뒤 활성 씬을 별도 파일로 저장해 SavedOnly와 RetainedUnsaved 보존을 확인한다.
   `object.describe`의 entity handle과 sceneId도 최초 RetainedUnsaved 생성 결과와 같아야 한다.
4. 유효한 저장 씬으로 다시 요청하면 Ready가 되고 새로운 sceneId로 교체된다.
   저장되지 않은 RetainedUnsaved는 최종 씬에 남지 않는다.

정상 두 요청과 실패 세 요청을 구성마다 실행한다. 실패는 정확히 세 건의
`scene.load.status`에서 `scene.load_failed`로 발생해야 하며 나머지 명령은 성공해야 한다.
제품 명령 정책은 Failed에 종료 코드 4를 부여하므로 음성 검사 프로세스의 4는
예상 결과다. 검사기는 임의 오류/종료 코드를 성공으로 바꾸지 않는다.

## 결과와 재현

최종 결과는 `Build/TextureSceneReload/result.json`에 기록한다. 각 프로젝트의
`author/reload.jsonl`, stdout/stderr, 다섯 개 저장 관찰 파일과 시나리오를 보존한다.
Debug/Release 모두 정상 Ready 2건, Failed 3건, 실패 후 미저장 엔티티 및
entity handle/sceneId 보존 3건, 저장 관찰 파일 5개 비교를 통과했다.
총 재로드 요청 10건을 실행했다. 검증 대상 소스 5개의 `sourceDrift=0`을 확인했다.
두 실행의 종료 코드는 예상한 4이며 검사 스크립트는 성공으로 종료했다.
실행 프로젝트는 Debug `Build/TSR-Debug-9383a063`, Release `Build/TSR-Release-8e5d9c58`이다.
대시보드 전체 JavaScript 파싱·렌더·유한 진행률과 변경 파일 whitespace 검사는 통과했다.
구조 검사기의 기존 PHASE 4.85 quoted-key RTP 문제(문자열 140/항목 모양 14)는
이전 결과와 동일하게 남아 있다. 전체 구조 검사 통과로 표시하지 않는다.

```powershell
Tools/regression/verify-texture-scene-reload.ps1 -Configuration All
```

이 후속 작업은 제품 동작 변경이나 추가 빌드를 요구하지 않았다. 검사 시나리오를
추가했고 기존 unrelated 작업 트리 변경을 보존했다. commit/push는 수행하지 않았다.

## 잔여 범위

이번 texture는 작은 원본 PNG 의존성이다. 이후 cooked generation 게시와
ImageComponent 씬 재로드를 한 프로세스에서 연결한 결과는
[종단 검증 기록](TextureReimportSceneValidation20261010.md)에 별도로 유지한다.
Inspector Save and Reimport/확인창/취소/파일 선택의 실제 조작, GPU texture sampling,
Shipping GPU package, 신규 BC5/BC7·모델 전체 소비 및 품질/시간/메모리 측정은 남아 있다.
각 실패 유형을 모든 runtime publication 실패나 모든 GPU/장시간 조건으로 확대하지 않는다.

T0/T1a/T2는 진행 중이며 `earnedDays: 0`, T1b 대기, T3 중단을 유지한다.
