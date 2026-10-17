# Scene IBL lookup의 참조 cache와 live 평가

> 기준: `493536e300e274cbf59989cdd62b20310c8ae64f`의 코드·호출 연결 정적 검토. 이 문서 갱신에서는 빌드·GPU 실행·성능 측정을 수행하지 않았다.

## 두 소비 경로

`SceneLookupCache::Prepare`는 reference/approximate cache 경로와 runtime fragment 평가 경로를 구별한다. `SceneLookupFrame::UsesRuntimeEvaluation()`이 true이면 legacy root에 놓인 Samples buffer는 단일 sentinel이며 전체 화면의 reference sample 배열이 아니다. live renderer는 runtime 평가를 명시적으로 선택한다.

reference 경로는 재질의 evaluated-pixel 입력 11장과 environment를 받아 정확한 이전 입력을 비교하고 바뀐 pixel을 적분한다. approximate cache는 바뀐 pixel을 split-sum으로 끝낸다. 시간이 지나면 reference 적분으로 수렴하는 temporal refinement가 아니다.

## 입력 수집과 재사용

frame identity는 viewport, frame/view/scene epoch, environment generation, recording/descriptor generation과 연결된다. `DeclareInputs`, `DeclareCaptureOutputs`, `DeclareBake`, `DeclareReady`가 capture → compute → shading 읽기 의존성을 만든다. GBuffer나 재질 parameter 하나만 같다는 이유로 lookup을 재사용하지 않는다.

runtime 입력은 irradiance/prefiltered/source/importance texture와 film sensitivity constant를 해당 recording에 bind한다. `PrincipledRuntimeIbl.slang`의 split-sum은 isotropic roughness와 F0/F90 항으로 반사를 근사하고 coat는 별도 normal/roughness를 사용한다. anisotropy, F82/박막의 각도별 세부, sheen lobe 모양은 축약된다.

## 게시와 수명

`TrackAcceptedSubmission`은 queue 접수에 대한 수명 증거를 저장하고 `PublishSubmitted`는 전체 owning graph의 성공 이후 cache 게시를 허용한다. upload prefix나 callback 기록만으로 새 cache를 accepted로 만들지 않는다. previous frame은 weak 연결이며 resource pool과 completion 추적이 재사용 자원 안전을 책임진다.

recording/descriptor/graph epoch가 맞지 않는 frame은 거부한다. 메모리 예산이나 자원 준비가 일시적으로 안 되는 경우 `PreparationDeferred()`를 구별한다. device 변경과 종료에는 `ShutdownAfterIdle`이 필요하다.

## 통계와 비용 읽기

`visible = baked + approximated + reused + rejected`가 통계 계약이다. `reserved0 == 1`은 runtime fragment 평가 프레임을 나타낸다. baked는 reference 적분 수이고 approximated와 합쳐 정확도 수용으로 해석하면 안 된다.

reference cold 비용과 메모리는 viewport 및 활성 lobe에 따라 커진다. live runtime 방식은 전 화면 bake record/readback 비용을 피하지만 Blender reference와 다른 근사를 사용한다. 예전 MAT-9 숫자는 frozen capture의 증거일 뿐 현재 live 모드의 품질·속도를 자동 증명하지 않는다. [비교 기록](../analysis/MAT9SplitSumLiveLookupDifference.md)을 함께 읽는다.

## 구현 근거

- [`Engine/RenderEngine/MaterialGraphSceneLookup.h`](../../Engine/RenderEngine/MaterialGraphSceneLookup.h)
- [`Engine/RenderEngine/MaterialGraphSceneLookup.cpp`](../../Engine/RenderEngine/MaterialGraphSceneLookup.cpp)
- [`Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp`](../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp)
- [`Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledRuntimeIbl.slang`](../../Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledRuntimeIbl.slang)
