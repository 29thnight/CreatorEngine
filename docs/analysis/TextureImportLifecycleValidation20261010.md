# 텍스처 임포트 수명 검사: 서비스 단위의 관찰 범위

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 등록 경로를 2026-10-15에 대조했다. 이 개정에서는 빌드·실행·화면 검증을 하지 않았다. 과거 결과와 현재 구현의 존재를 구분한다.

## 이 검사가 확인하는 경계

`TextureImportLifecycleProbe`는 실제 EditorAssetDatabase, DataSystem, scheduler, sidecar writer, producer와 image cache를 연결한다. 표시 창을 조작하는 검사가 아니며 probe가 queued asset drain을 진행한다. UI 이벤트, scene activation, RHI 제출 및 최종 GPU 픽셀은 이 검사 밖이다. 전체 데이터 흐름은 [텍스처 정본](../plans/TexturePipelinePlan.md)을 따른다.

`verify-texture-import-lifecycle.ps1`은 Debug/Release/All과 Repetitions(1~20), SkipProjectReferences를 받는다. 기본은 All·1회다. 새 Build 디렉터리의 short/long 경로에 Tiny4.png를 놓고 source와 binary hash를 receipt에 기록한다. 스크립트는 probe를 빌드하므로 문서 검토용으로 실행해서는 안 된다. 의존성을 별도 최신 빌드하지 않았다면 SkipProjectReferences 결과는 신뢰할 수 없다.

## 유지되는 단정

검사 소스는 4×4 원본에 maxDimension=2를 저장하고 GUID·사용자 메타·sampler 필드 보존, 새 descriptor/image 2×2, 이전 owner의 4×4 유지 여부를 다룬다. 잘못된 enum의 저장 거부, 손상 원본의 cook 실패와 정상 세대 유지, 원본 복구, 연속 recipe, image 예산 0 이후 이전 세대의 정확한 backing 재획득, cook과 rename 경합이 포함된다. 통과 조건은 단순 exit 0뿐 아니라 `TEXTURE_IMPORT_LIFECYCLE_OK`와 최소 검사 수다.

이 수명 검사의 핵심은 논리 퇴거 이후 owner가 가진 이미지가 즉시 파괴되지 않는다는 점과, owner 해제 후에도 description의 원래 artifact로 재수화할 수 있다는 점이다. “메모리 예산 0”은 모든 프로세스 메모리가 0이 된다는 뜻이 아니다.

## 현재 구현에서 바뀐 해석

과거 기록은 임의 GUID 세대 경로의 길이와 Windows 공유 위반 수정을 다뤘다. 현재는 `Library/TextureAssetGenerations/<content-key>`를 재사용하고 staging rename으로 게시한다. 오래된 경로 설명을 새 generation 규칙으로 사용하지 않는다. producer는 원본 캡처와 검증을 수행하며 Editor는 최신 revision 및 변경 전후 stamp를 확인한다. 파일 rename을 허용하는 읽기 공유 모드만으로 stale 게시가 막히는 것은 아니다.

## 과거 증거와 재검증

2026-10-10 문서는 Debug/Release×short/long×5회, 36개 단정씩의 결과를 보고했다. 그 receipt 위치는 `Build/TextureImportLifecycle/result.json`이며 Build 산출물은 이 소스 checkout에 포함된 보장된 증거가 아니다. 새 실행이나 720개 단정의 재확인을 이 개정에서 수행하지 않았다.

후속 수용은 [씬 재로드](TextureSceneReloadValidation20261010.md), [실제 컴포넌트 종단](TextureReimportSceneValidation20261010.md), [자동 Inspector](TextureAutomaticInspectorUiValidation20261010.md)로 나눈다. 장시간 외부 writer, 큰 배열/cube, 여러 프로세스가 같은 cache를 갱신하는 경합, 실제 압축 품질과 GPU 소비는 이 작은 fixture에서 추론할 수 없다.

## 구현 근거

- [Tools/regression/verify-texture-import-lifecycle.ps1](../../Tools/regression/verify-texture-import-lifecycle.ps1)
- [Tools/regression/texture_import_lifecycle_probe.cpp](../../Tools/regression/texture_import_lifecycle_probe.cpp)
- [Tools/regression/TextureImportLifecycleProbe.vcxproj](../../Tools/regression/TextureImportLifecycleProbe.vcxproj)
- [Editor/EngineEntry/EditorAssetDatabase.cpp](../../Editor/EngineEntry/EditorAssetDatabase.cpp)
- [Engine/RenderEngine/Experiment/Cooked/TextureCookProducer.cpp](../../Engine/RenderEngine/Experiment/Cooked/TextureCookProducer.cpp)
