# Scene Material 입력 밀봉과 coverage

> 기준: `493536e300e274cbf59989cdd62b20310c8ae64f`의 코드·호출 연결 정적 검토. 이 문서 갱신에서는 빌드·GPU 실행·성능 측정을 수행하지 않았다.

## producer에서 selected view까지

`SceneMaterialSource::Capture`는 producer 경계에서 Material의 graph instance, coverage, material slot을 불변 owner로 만든다. render thread는 mutable Material 객체를 따라가지 않고 이 snapshot을 소비한다. non-graph source의 null 반환은 graph로 자동 승격되는 신호가 아니다.

`SceneViewInput::Seal`은 현재 `frameId`, `sceneEpoch`, `viewId`, history revision, viewport와 camera snapshot을 받아 draw를 밀봉한다. 각 `SceneDrawInput`에는 temporal object/incarnation/instance ID, geometry key, model/mesh handle, material slot, queue, view depth, 현재/이전 geometry plan이 들어간다.

model과 material은 frame pin table이 exact generation을 소유하고 draw는 table index와 anchored borrow를 가진다. pointer 주소만 있거나 변경 가능한 mesh/palette를 보관하는 구조가 아니다. `MaterialOwner(draw)`로 같은 pin에서 owner를 회수한다.

## coverage와 물리 route

`ClassifySceneCoverage`는 enabled인 유효 coverage와 0..1 baseAlpha를 요구하고 Blended, Masked, Opaque를 구분한다. 이것은 [제품 route](MaterialGraphProduct.md)의 Deferred/Forward/Volume 선택과 별개다. 생성된 Surface Settings의 alphaMode, Material rendering mode와 packet의 fixed state가 서로 맞아야 한다.

한 view에서 같은 materialSlot을 공유하는 draw는 같은 instance와 같은 flags/cutoff/baseAlpha를 가져야 한다. `SelectReadyInput`은 이를 검증한 후 slot revision을 갱신한다. view/frame/extent mismatch, slot 0, 충돌 identity, surface/volume 없는 프로그램은 오류다.

## geometry와 예산

기본 SceneInputBudget은 draw/chunk 각각 4096, CPU payload 256 MiB, GPU payload 512 MiB, geometryMaxPixelError 1이다. mesh plan은 검증된 정적 topology를 재사용할 수 있으나 world/pose/camera는 새 view 기준으로 밀봉한다. 같은 model generation이라는 이유만으로 다른 pose나 카메라 batch를 재사용할 수 없다.

현재 및 이전 geometry는 temporal consumer가 일관된 identity로 사용할 수 있게 따로 pin된다. 폐기된 object incarnation 또는 geometry 변경을 단순히 이전 화면 index와 연결하지 않는다.

## 실패 경계와 확인 방법

Seal 실패는 불완전한 view를 게시하지 않는 경계다. 그 다음 program 준비는 [Scene generation](MaterialGraphSceneGeneration.md), graph 기록은 [Scene host](MaterialGraphSceneHost.md)의 책임이다. 입력 준비 성공을 native 제출 성공으로 표시하지 않는다.

정적 검토 시 producer capture → EnhancedDrawItem → Seal → SelectReadyInput → Prepare의 연결과 각 단계의 owner/table index를 추적한다. 테스트용 CPU point packet과 실제 SceneViewInput을 혼용하지 않는다. 이번 갱신에서는 해당 회귀를 실행하지 않았다.

## 구현 근거

- [`Engine/RenderEngine/MaterialGraphSceneInput.h`](../../Engine/RenderEngine/MaterialGraphSceneInput.h)
- [`Engine/RenderEngine/MaterialGraphSceneInput.cpp`](../../Engine/RenderEngine/MaterialGraphSceneInput.cpp)
- [`Engine/RenderEngine/MaterialGraphSceneCoverage.h`](../../Engine/RenderEngine/MaterialGraphSceneCoverage.h)
- [`Engine/RenderEngine/MaterialGraphInstancePins.h`](../../Engine/RenderEngine/MaterialGraphInstancePins.h)
- [`Engine/RenderEngine/MaterialGraphSceneHost.cpp`](../../Engine/RenderEngine/MaterialGraphSceneHost.cpp)
