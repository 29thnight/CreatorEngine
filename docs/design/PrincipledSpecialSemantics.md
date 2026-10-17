# Special 재질: transmission, SSS, Volume의 의미

> 기준: `493536e300e274cbf59989cdd62b20310c8ae64f`의 코드·호출 연결 정적 검토. 이 문서 갱신에서는 빌드·GPU 실행·성능 측정을 수행하지 않았다.

## 실행 요구

transmission 0x0800, subsurface 0x1000, volume 0x2000은 local BRDF만으로 끝나지 않는 기능이다. CPU `LXAnalyzeMaterialExecution`과 shader `SpecialMaterialRoute.slang`은 Forward route와 필요한 transport 자원을 요구한다. resource 부재를 Standard fallback으로 숨기지 않는다.

Surface와 Volume은 별도 closure다. full-metal 상수에서는 transmission/SSS가 제거될 수 있고 full-glass 상수에서는 SSS가 제거될 수 있다. alpha coverage는 이 physical transport weight와 별개다.

## Transmission

표면은 authored IOR를 `transmissionIor`로 유지한다. base Specular IOR Level로 조정된 reflection IOR와 같다고 가정하지 않는다. glass reflection/transmission은 Fresnel 및 roughness 분포를 사용하고 Scene 배경 transport가 공급한 radiance를 조합한다.

현재 Scene refraction은 farther object까지 합성된 color/depth snapshot에서 screen ray를 찾고 miss 시 environment로 넘어간다. opaque/foreground silhouette를 뒤쪽 배경으로 잘못 읽지 않도록 depth 조건을 둔다. 정확한 내부 굴절, caustic, off-screen geometry, 다중 interface 경로 추적은 별도 미지원 범위다.

## Subsurface

`BuildSubsurfaceProfile`는 radius×scale을 world-unit mean free path로 해석하는 normalized diffusion dipole다. Random Walk 구현이 아니다. `BuildSceneSubsurfaceProfile`는 artist radius의 Scene 의미를 맞추기 위해 scale에 1/(4π)를 적용한다. 물리 closure reference와 Scene 입력 환산을 혼합하지 않는다.

Scene filter는 world distance/projected area와 exact owner mask로 visible sample을 모은다. 다른 draw, 가려진 pixel을 무차별 blur하지 않는다. local/zero channel과 0 weight는 중립 경로를 유지한다. off-screen/backside diffusion, 두꺼운 geometry의 전체 다중산란은 표현하지 못한다.

## Volume

`EvaluateVolume`은 color/density에서 scattering, absorption color에서 absorption, emission color/strength에서 emission을 만든다. density와 emission은 독립적이다. `VolumePhase`는 안정화된 Henyey–Greenstein, `IntegrateVolumeSegment`는 Beer–Lambert와 source 적분, `ComposeVolumeSegments`는 앞 segment의 transmittance로 뒤 radiance를 감쇠한다.

제품 Scene은 closed, consistently oriented, static homogeneous boundary만 지원하며 최대 16 object/128 triangle이다. skinned medium, spatially varying coefficient, open/nonmanifold/degenerate boundary를 거부한다. froxel 기반 전역 Volumetric Fog와 graph Volume을 같은 기능으로 부르지 않는다.

## 품질·비용과 검증

Special은 추가 capture/filter/ray/medium 자원과 pass 비용을 가진다. runtime special tile 경로와 reference capture 적분을 구별한다. 숫자 closure probe, Scene regression, Blender image comparison은 각각 다른 gate이며 한쪽 통과로 전체 transport parity를 선언하지 않는다. 자세한 배선은 [SSS](MaterialGraphSceneSubsurface.md), [굴절](MaterialGraphSceneRefraction.md), [Volume](MaterialGraphSceneVolume.md)을 따른다.

## 구현 근거

- [`Lattice/Material/LXMaterialExecution.h`](../../Lattice/Material/LXMaterialExecution.h)
- [`Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/SpecialMaterialRoute.slang`](../../Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/SpecialMaterialRoute.slang)
- [`Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledSurface.slang`](../../Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledSurface.slang)
- [`Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledSubsurface.slang`](../../Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledSubsurface.slang)
- [`Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledVolume.slang`](../../Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledVolume.slang)
- [`Engine/RenderEngine/MaterialGraphSceneVolume.cpp`](../../Engine/RenderEngine/MaterialGraphSceneVolume.cpp)
