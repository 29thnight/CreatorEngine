# 텍스처 cook 통합 검증의 계층과 증거

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 등록 경로를 2026-10-15에 대조했다. 이 개정에서는 빌드·실행·화면 검증을 하지 않았다. 과거 결과와 현재 구현의 존재를 구분한다.

## 통합 지점

현재 텍스처 파이프라인은 source settings parser, DirectXTex cooker, CECT strict reader, asset manifest/resolver, Editor generation publisher와 런타임 image cache로 나뉜다. [정본](../plans/TexturePipelinePlan.md)에 동작 계약을 모았으며 이 문서는 어느 검사가 무엇을 입증하는지를 설명한다. 과거 [PR #166](https://github.com/29thnight/CreatorEngine/pull/166)은 구현 이력의 출처이지 최신 HEAD 수용증이 아니다.

`ExperimentTextureCookSelfTest`는 artifact encode/decode와 손상 거부 등의 CPU 계약을, `ExperimentTexturePipelineSelfTest`는 설정·cook 정책의 조합을 다룬다. `TextureImportLifecycleProbe`가 Editor writer/worker/publication까지 연결하고 `texture_reimport_scene_probe`가 실제 씬 소비자를 연결한다. 같은 단어 “texture test”로 이 네 경계를 합치면 안 된다.

## 결과 판정 방법

- 파싱: 알 수 없는 enum, duplicate settings, 범위 밖 수치가 기존 설정을 부분 변경하지 않는지 확인한다
- cook: authored mip 보존과 GenerateFull 꼬리 생성, linear/sRGB 해석, normal/alpha/HDR 허용 조합을 입력별로 대조한다
- artifact: header/table, pitch, payload 범위, 정확한 길이를 strict reader가 검증하는지 확인한다
- 임포트: 원본 및 GUID 보존, sidecar 저장과 cook 실패의 분리, 최신 요청만 게시하는지 확인한다
- 런타임: descriptor와 image의 별도 캐시, 정확한 backing 재획득 및 owner pin을 확인한다
- 패키지/GPU: 생성한 artifact가 실제 패키지에 포함되고 각 backend에서 올바른 픽셀을 내는지 별도 실행한다

소스에서 경로가 연결되어 있다는 사실은 마지막 두 항목의 실측을 대신하지 않는다. 특히 synthentic BC/normal test 통과와 사용자의 전체 콘텐츠 압축 품질, import 시간 및 memory peak는 서로 다른 결과다.

## 과거 수치의 취급

기존 2026-10-09~10 문서에 적힌 MSVC 빌드, RTX 4070 Ti 실행, 개별 selftest 수치와 로컬 Build 로그는 그 시점의 기록으로 Git 이력에 남는다. 이 개정은 그 숫자를 새 실행으로 재게시하지 않는다. 저장소에 남은 JSON·이미지는 수정하지 않으며, binary/source hash와 입력이 일치하는 경우에만 해당 시점 증거로 읽는다.

## 재현의 출발점

서비스 경계는 [임포트 수명](TextureImportLifecycleValidation20261010.md), async scene activation은 [씬 재로드](TextureSceneReloadValidation20261010.md), 이미지·스프라이트·Undo 연결은 [종단 소비](TextureReimportSceneValidation20261010.md)를 사용한다. Windows 전용 runner를 Linux에서 소스 검사만 해놓고 “통과”라고 보고하지 않는다. 남은 범위는 전체 backend/포맷·모델 sampling·Shipping GPU·대규모 품질 및 성능이다.

## 구현 근거

- [Editor/RenderTests/ExperimentParity/ExperimentTextureCookSelfTest.cpp](../../Editor/RenderTests/ExperimentParity/ExperimentTextureCookSelfTest.cpp)
- [Editor/RenderTests/ExperimentParity/ExperimentTexturePipelineSelfTest.cpp](../../Editor/RenderTests/ExperimentParity/ExperimentTexturePipelineSelfTest.cpp)
- [Engine/RenderEngine/Experiment/Cooked/CookedTexture.cpp](../../Engine/RenderEngine/Experiment/Cooked/CookedTexture.cpp)
- [Engine/RenderEngine/Experiment/Cooked/TextureCooker.cpp](../../Engine/RenderEngine/Experiment/Cooked/TextureCooker.cpp)
- [Tools/regression/verify-texture-import-lifecycle.ps1](../../Tools/regression/verify-texture-import-lifecycle.ps1)
- [Tools/regression/verify-texture-reimport-scene.ps1](../../Tools/regression/verify-texture-reimport-scene.ps1)
