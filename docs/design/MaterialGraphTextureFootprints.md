# 이미지별 UV footprint, mip, 저장 encoding

> 기준: `493536e300e274cbf59989cdd62b20310c8ae64f`의 코드·호출 연결 정적 검토. 이 문서 갱신에서는 빌드·GPU 실행·성능 측정을 수행하지 않았다.

## Scene pixel의 샘플링 계약

`LXSampleMaterialImage`는 `LX_MATERIAL_PIXEL_FOOTPRINT=1`인 host에서 각 이미지의 실제 width/height/mip 수와 그 sample의 UV 식을 사용한다. `ddx_fine/ddy_fine`의 texel 공간 길이 중 큰 값으로 log2 LOD를 구하고 0..마지막 mip으로 clamp해 `SampleLevel`한다.

다른 이미지 크기나 Vector 식을 한 material-wide LOD로 합치지 않는다. derivative는 divergent coverage/owner test 이전에 계산되어야 한다. Scene compiler는 이 define과 fineDerivatives/quadOperations compile option을 명시적으로 활성화한다.

## 다른 host와 색 공간

compute/evaluated-point host는 implicit pixel derivative가 없으므로 context의 explicit LOD를 사용한다. `SurfaceTextureFootprint`와 `ResolveSurfaceLod`는 CPU에서 같은 isotropic 계산을 점검하는 계약이다. 이것이 anisotropic filtering, 모든 projection mode 또는 named UV map 지원을 의미하지는 않는다.

sRGB 이미지의 decode는 interpolation 전에 이루어져야 한다. 생성기의 숫자 Color decode와 texture view/storage decode는 별도다. alpha는 linear로 유지한다. image Vector 미연결 기본값은 context UV를 사용하지만 명시적으로 연결한 값은 그대로 평가한다.

## BC5 normal 처리

runtime storage metadata의 encoding 1은 BC5 XY normal이다. sample 후 RG를 -1..1로 복원하고 positive hemisphere Z를 계산해 graph Normal Map 노드가 기대하는 RGB 형태로 다시 넣는다. blue 채널이 0인지 보고 BC5라고 추측하지 않는다. RG/alpha와 필터된 mip 값은 그대로 유지한다.

encoding uniform은 reflected product layout의 내부 field이며 artist parameter가 아니다. resource slot/offset 검증과 parameter overlap 검사를 거친다. texture representation이 바뀌면 정확한 generation/encoding과 함께 packet을 다시 준비한다.

## 지원·검증 경계

현재 Image Texture는 FLAT, Linear/Closest, REPEAT/EXTEND lowering만 지원한다. Cubic/Smart, CLIP/MIRROR, 다른 projection은 registry enum에 있어도 컴파일 시 거부한다. Normal Map은 TANGENT/WORLD와 기본 UV 경로를 지원하며 named UV는 거부한다.

검증은 서로 다른 해상도/UV 변환, perspective, minification, BC5와 RGBA normal, sRGB edge interpolation을 따로 확인해야 한다. 기존 캡처는 historical 자료이며 이번 문서에서는 shader와 compiler 연결만 확인했다.

## 구현 근거

- [`Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/MaterialGraphTextureSample.slang`](../../Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/MaterialGraphTextureSample.slang)
- [`Lattice/Material/LXMaterialCompiler.cpp`](../../Lattice/Material/LXMaterialCompiler.cpp)
- [`Engine/RenderEngine/MaterialGraphSceneCompiler.cpp`](../../Engine/RenderEngine/MaterialGraphSceneCompiler.cpp)
- [`Engine/RenderEngine/MaterialGraphProduct.cpp`](../../Engine/RenderEngine/MaterialGraphProduct.cpp)
- [`Engine/RenderEngine/MaterialGraphMeshSurface.h`](../../Engine/RenderEngine/MaterialGraphMeshSurface.h)
