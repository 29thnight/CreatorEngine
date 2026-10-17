# Scene SSS의 capture, reflection, diffusion

> 기준: `493536e300e274cbf59989cdd62b20310c8ae64f`의 코드·호출 연결 정적 검토. 이 문서 갱신에서는 빌드·GPU 실행·성능 측정을 수행하지 않았다.

## 설치 경로와 목적

SSS feature가 켜진 surface는 Forward transport가 필요하다. Scene compiler는 `LXSceneSubsurfacePS`와 runtime effects entry를 포함하고 `SceneHost`는 reference resources 또는 live runtime effects 경로를 준비한다. local BRDF의 diffuse 색만 바꾸는 기능이 아니다.

`SceneSubsurfaceFrame`은 7개 capture input, metal/dielectric reflection buffer와 filtered irradiance를 소유한다. `DeclareInputs/CaptureOutputs → DeclareReflection/DeclareFilter → GraphReflection/GraphIrradiance` 순서로 graph 의존성을 노출한다. live tile 경로의 `SceneRuntimeEffects`는 전 화면 reference 적분과 구분해야 한다.

## 공간 의미

기본 profile은 normalized diffusion dipole이며 Cycles Random Walk가 아니다. 공용 physical profile은 Radius×Scale을 world-unit mean free path로 해석한다. Scene artist 입력은 `BuildSceneSubsurfaceProfile`에서 1/(4π) scale 변환을 거친다. 이 변환을 공용 numeric golden 전체에 중복 적용하면 안 된다.

filter는 visible position의 world distance와 projected surface area를 이용한다. 같은 owner mask의 표본만 섞고 다른 draw/가려진 pixel을 배제한다. zero/local channel과 유효 이웃이 없는 경우 원래 source를 유지한다. 이것이 depth와 normal을 무시한 화면 blur와 다른 이유다.

## composition과 수명

reflection은 metal/dielectric를 나눠 special surface 조합에 전달한다. 현재 alpha layer는 ordered Blended stream의 자기 capture를 사용하고 뒤쪽 이미지를 지우지 않는다. opaque depth/owner와 해당 material instance를 함께 유지한다.

frame 자원은 현재 device/viewport/recording/descriptor epoch에 속한다. memory budget 초과, allocation, compile/PSO 실패는 candidate를 거부한다. graph callback과 제출 owner는 completion까지 자원을 보유하고 종료는 device idle 후다.

## 지원 한계와 검증

이 화면 기반 diffusion은 off-screen/backside sample, 두꺼운 닫힌 표면 내부의 다중산란, Random Walk의 path를 재현하지 않는다. 동일 owner라도 한 draw 내부 복잡한 겹침에는 별도 검증이 필요하다. SSS scale=0/off, owner leakage, grazing normal, camera cut, alpha와 Volume 혼합을 각각 확인해야 한다.

과거 Special image target의 미수용 조건은 현재 구현 존재만으로 닫지 않는다. [Special profile 비교](../analysis/MAT9SpecialProfileCorrection.md)의 원자료와 새 실행을 구분한다. 이번에는 실행하지 않았다.

## 구현 근거

- [`Engine/RenderEngine/MaterialGraphSceneSubsurface.h`](../../Engine/RenderEngine/MaterialGraphSceneSubsurface.h)
- [`Engine/RenderEngine/MaterialGraphSceneSubsurface.cpp`](../../Engine/RenderEngine/MaterialGraphSceneSubsurface.cpp)
- [`Engine/RenderEngine/MaterialGraphSceneRuntimeEffects.h`](../../Engine/RenderEngine/MaterialGraphSceneRuntimeEffects.h)
- [`Engine/RenderEngine/MaterialGraphSceneEffectStage.inl`](../../Engine/RenderEngine/MaterialGraphSceneEffectStage.inl)
- [`Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledSubsurface.slang`](../../Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledSubsurface.slang)
- [`Dynamic_CPP/Assets/Shaders/DefaultPassShader/MaterialGraphSceneSubsurfaceFilter.slang`](../../Dynamic_CPP/Assets/Shaders/DefaultPassShader/MaterialGraphSceneSubsurfaceFilter.slang)
