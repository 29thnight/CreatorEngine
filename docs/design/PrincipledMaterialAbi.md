# Principled Material 공용 Slang 의미 ABI

> 기준: `493536e300e274cbf59989cdd62b20310c8ae64f`의 코드·호출 연결 정적 검토. 이 문서 갱신에서는 빌드·GPU 실행·성능 측정을 수행하지 않았다.

## 평가와 소비

`PrincipledSurface.slang`의 `MaterialInputs → EvaluateMaterial → PrincipledSurface`가 생성 material과 조명 소비자의 논리 ABI다. 이 struct는 local shader 값이며 cbuffer, GBuffer 또는 CPU byte layout이 아니다. CPU↔GPU 전송에는 reflected `BindingLayout`, `IblBakePoint` 같은 별도 구조를 사용한다.

입력은 base/alpha/normal/roughness/metallic/emission/AO, IOR/specular, coat/sheen/anisotropy/film, transmission/SSS를 가진다. Volume은 `VolumeInputs → PrincipledVolume`의 별도 output closure다. 모든 surface field를 GBuffer에 저장하는 계약은 없다.

## 값의 의미

EvaluateMaterial은 baseColor를 nonnegative, alpha/roughness/metallic/AO를 0..1 범위로 정규화하고 normal/tangent의 유한 fallback을 정한다. emission은 color×strength다. IOR/specular level로 dielectric reflection을 만들고 metal에는 별도 F0/F82 correction을 적용한다.

coat normal과 base normal은 다르며 tangent rotation은 한 회전을 1로 표현한다. thin-film thickness는 광학 함수의 nm 단위와 일치해야 한다. transmission은 authored IOR를 유지하며 base Specular IOR Level에 의해 조정된 IOR와 구별한다. SSS radius/scale은 nonnegative이며 Scene은 별도의 길이 환산을 적용한다.

`DefaultStandardMaterialInputs`/`EvaluateStandardMaterial`은 legacy glTF factor product 보존을 위한 명시적 adapter다. 공용 Principled clamp를 약화해 legacy GBuffer 형식을 맞추지 않는다.

## 정적 기능 마스크

bit 0..6은 base color, metallic/roughness, normal, alpha, emission, occlusion, specular IOR다. bit 7..10은 coat, sheen, anisotropy, iridescence, bit 11..13은 transmission, subsurface, volume이다. 허용 전체 mask는 0x3FFF다.

`LX_MATERIAL_FEATURE_MASK`는 cook-time define이다. 알 수 없는 bit와 Core IOR 없는 layered/special 조합은 shader 또는 CPU execution 검증에서 거부한다. special은 `LX_MATERIAL_ROUTE=2`와 해당 transport 자원이 필요하다. 이 define 자체가 제품 pass를 선택하는 것은 아니다.

## 안정성과 확장

논리 field 변경은 생성기, 공용 함수, reflected binding, Scene capture, evaluated-point ABI, cooked host identity를 함께 검토해야 한다. 배열 크기나 packing을 이 문서의 struct 의미만 보고 추정하지 않는다. [Core](PrincipledCoreSemantics.md), [Layered](PrincipledLayeredSemantics.md), [Special](PrincipledSpecialSemantics.md)이 세부 의미를 정의한다.

현재 수치는 소스 상수 확인이며 ABI probe를 새로 실행한 결과가 아니다. 기능 비트의 존재는 Blender 전체 node/socket parity의 증거가 아니다.

## 구현 근거

- [`Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledSurface.slang`](../../Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledSurface.slang)
- [`Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledVolume.slang`](../../Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledVolume.slang)
- [`Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/SpecialMaterialRoute.slang`](../../Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/SpecialMaterialRoute.slang)
- [`Lattice/Material/LXMaterialExecution.h`](../../Lattice/Material/LXMaterialExecution.h)
- [`Engine/RenderEngine/MaterialGraphIblBake.h`](../../Engine/RenderEngine/MaterialGraphIblBake.h)
