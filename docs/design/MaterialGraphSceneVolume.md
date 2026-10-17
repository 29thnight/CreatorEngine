# Graph Volume의 닫힌 균질 매질 transport

> 기준: `493536e300e274cbf59989cdd62b20310c8ae64f`의 코드·호출 연결 정적 검토. 이 문서 갱신에서는 빌드·GPU 실행·성능 측정을 수행하지 않았다.

## 경계와 명칭

Material Output의 Volume은 표면 BSDF와 별도 closure다. `LXPrincipledVolume`이 scattering/absorption/emission 계수를 만든다. Scene의 전역 Volumetric Fog와 다른 자산·geometry·합성 계약이다.

`CompileSceneProduct`는 `LX_MATERIAL_VOLUME_HOMOGENEOUS 1`이 없는 volume program을 거부한다. spatially varying coefficient를 지원하는 것처럼 렌더하지 않는다. 계수는 material별 compute entry `LXSceneVolumeCoefficientCS`에서 계산된다.

## geometry와 예산

`SceneVolumeResources::Prepare`는 view의 medium draw에서 static closed boundary를 모은다. 현재 최대 16 object, 전체 128 triangle이며 event list도 이 상한으로 제한된다. 초과 triangle/interval을 잘라내지 않고 전체 준비를 거부한다.

skinned medium, singular world transform, 비유한 좌표, degenerate edge/triangle, 닫히지 않았거나 방향이 일관되지 않은 manifold, 유효하지 않은 camera projection/유한 far plane은 거부한다. triangle 48 bytes, coefficient 48 bytes, transport constants 4464 bytes는 별도 CPU/GPU 전송 계약이다.

## transport와 composition

medium coefficient는 material bindings와 compute generation을 유지한 채 `DeclareCoefficients`에서 게시한다. ray-boundary event의 interval에 Beer–Lambert transmittance와 single-scattering/emission source를 적분하고 앞에서 뒤로 합성한다. Henyey–Greenstein phase는 -0.999..0.999 anisotropy를 제한한다.

`DeclareComposite`는 lighting/depth/shadow를 소비한다. Blended surface와 refraction도 같은 frame coefficient를 읽되 camera segment와 refracted segment를 구분해 medium을 두 번 적용하지 않는다.

## 소유권과 실패

Frame은 triangle upload, coefficient buffer, environment, compute PSO와 constants를 보유한다. current recording/descriptor epoch를 확인하고 graph output을 같은 frame에만 사용한다. memory budget은 viewport output과 geometry/upload 비용을 포함한다. 실패 시 기존 accepted frame을 덮지 않으며 owner는 제출 완료까지 유지한다.

## 남은 범위

큰/복잡한 mesh volume, animated boundary, heterogeneous density, multiple scattering, path-traced shadow/caustic은 이 구현의 수용 범위가 아니다. 겹친 medium의 interval, camera-inside, emission-only, zero density, alpha/refraction 조합을 독립적으로 검증해야 한다. [Volume 비교 원자료](../analysis/MAT9SpecialTransportComparison.md)는 역사적 조건이며 이번에 새 실행을 하지 않았다.

## 구현 근거

- [`Engine/RenderEngine/MaterialGraphSceneVolume.h`](../../Engine/RenderEngine/MaterialGraphSceneVolume.h)
- [`Engine/RenderEngine/MaterialGraphSceneVolume.cpp`](../../Engine/RenderEngine/MaterialGraphSceneVolume.cpp)
- [`Engine/RenderEngine/MaterialGraphSceneCompiler.cpp`](../../Engine/RenderEngine/MaterialGraphSceneCompiler.cpp)
- [`Engine/RenderEngine/MaterialGraphSceneVolumeStage.inl`](../../Engine/RenderEngine/MaterialGraphSceneVolumeStage.inl)
- [`Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledVolume.slang`](../../Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledVolume.slang)
