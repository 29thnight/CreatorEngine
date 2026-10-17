# 저장 씬 재로드와 텍스처 의존성 검증

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 등록 경로를 2026-10-15에 대조했다. 이 개정에서는 빌드·실행·화면 검증을 하지 않았다. 과거 결과와 현재 구현의 존재를 구분한다.

## 현재 의미

씬 재로드는 여전히 제품 기능이지만 자동 텍스처 설정 편집의 필수 단계가 아니다. 현재 Inspector는 설정을 자동 저장하고 준비된 owner를 살아 있는 씬에 교체한다. 이 문서는 별도로 저장된 씬을 열 때 텍스처 의존성이 성공·실패하는 경계를 다룬다. 정상 편집 흐름은 [텍스처 정본](../plans/TexturePipelinePlan.md)을 따른다.

`scene.open_async <scene-path>`는 준비 요청을 접수하고 request-id를 반환한다. 접수 성공은 활성화 성공이 아니다. `scene.load.status <request-id>`에서 Ready/Failed/Stale/Cancelled/Superseded 등 terminal state를 확인해야 한다. 기존 씬은 새 씬과 필수 자산의 준비가 성공한 뒤에 교체되어야 한다.

## runner의 실제 동작

`verify-texture-scene-reload.ps1`은 실행 중 Editor가 있으면 중단한다. 새 `Build/TSR-*` 프로젝트에 PNG·shader·설정을 복사하고 실제 CreatorEditor를 `--development-project`, `--commandlet-script`, `--result-file`로 시작한다. author 단계는 scene.new, object.create, scene.save를 사용하지만 required bundle 텍스처 참조 및 손상 입력은 스크립트가 파일에 주입한다. 따라서 이 검사 전체를 사용자의 GUI 저작 흐름이라고 설명하면 안 된다.

검사 순서는 유효한 씬, 없는 씬, 손상된 문서, 없는 텍스처 의존성, 유효 씬 재복구다. 성공 뒤 SavedOnly가 복원되고 InitialUnsaved가 사라져야 하며, 음성 세 경우에서는 RetainedUnsaved가 남아야 한다. 각 결과의 저장 파일과 object 조회를 비교한다. 의도된 세 실패 때문에 프로세스 exit 4가 기대되며 단순 “비영 exit는 모든 검사 실패” 판정은 틀린다.

## 수명과 자료 해석

실패한 준비가 기존 scene/entity를 훼손하지 않는지와, 성공한 전환이 이전 미저장 내용을 버리는지는 서로 반대되는 단정이다. 텍스처 4×4 조회는 dependency 준비를 확인하지만 GPU 픽셀 비교는 아니다. 일정 프레임 wait는 runner의 재현 방식이며 임의 장비에 대한 wall-clock 완료 보장이 아니다.

과거 2026-10-10 실행은 `Build/TextureSceneReload` 계열 receipt에 기록되었고 현재 개정에서 재실행하지 않았다. 최신 소스는 command handler와 fixture 경로까지 확인했다. 수동 파일 대화상자·확인 모달 취소는 [이전 Inspector UX](TextureInspectorUiValidation20261010.md)의 역사적 범위이며 현재 자동 UI 수용과 혼합하지 않는다.

## 남은 수용

실제 큰 씬·다수 텍스처의 취소/연속 전환, stale 요청 ID, project 전환, shutdown과 겹친 준비, GPU 준비 실패 및 표시 결과는 별도 검사다. ImageComponent와 SpriteRenderer의 저장 경로 복원은 [종단 probe](TextureReimportSceneValidation20261010.md)가 더 직접적인 출발점이다.

## 구현 근거

- [Tools/regression/verify-texture-scene-reload.ps1](../../Tools/regression/verify-texture-scene-reload.ps1)
- [Editor/EngineEntry/Commands/SceneObjectCommands.cpp](../../Editor/EngineEntry/Commands/SceneObjectCommands.cpp)
- [Engine/SceneRuntime/SceneManager.cpp](../../Engine/SceneRuntime/SceneManager.cpp)
- [Editor/EngineGUIWindow/DrawYamlNodeEditor.cpp](../../Editor/EngineGUIWindow/DrawYamlNodeEditor.cpp)
