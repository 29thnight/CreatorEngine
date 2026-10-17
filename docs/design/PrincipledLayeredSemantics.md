# Layered Principled: coat, sheen, anisotropy, 박막

> 기준: `493536e300e274cbf59989cdd62b20310c8ae64f`의 코드·호출 연결 정적 검토. 이 문서 갱신에서는 빌드·GPU 실행·성능 측정을 수행하지 않았다.

## 활성 기능과 입력

Layered mask 0x0780은 coat/sheen/anisotropy/iridescence다. Core Specular/IOR 없이 활성화할 수 없다. 생성기는 활성 weight/thickness에서 feature를 추출하며 상수 0으로 꺼진 lobe는 정적으로 제거할 수 있다. 동적 값은 런타임에 0이어도 해당 variant의 준비 비용이 남는다.

coat는 별도 normal, roughness, IOR, tint, weight를 갖는다. sheen은 weight/tint/roughness를 쓰고, anisotropy는 tangent와 회전 및 roughness의 두 축을 사용한다. 기본 tangent가 퇴화하면 normal에 직교하는 유한 축을 만든다.

## 공용 반사와 에너지

`PrincipledAnisotropicAlpha`는 roughness²와 anisotropy에서 두 GGX alpha를 계산한다. `BuildLayeredEnergy`는 32×32 GGX energy, average energy와 dielectric albedo table을 참조한다. metal/dielectric/coat compensation과 lobe attenuation을 분리한다.

coat Fresnel은 물리 dielectric interface를 사용하며 base specular level/F0 tint/박막을 그대로 재사용하지 않는다. sheen LTC table은 sheen 기능을 켠 경우에만 포함된다. layered weight를 모든 lobe의 단순 합으로 해석하면 하위층 감쇠가 사라진다.

## 박막 모델

`PrincipledThinFilm.slang`은 Fourier Airy series를 linear Rec.709 sensitivity로 적분한 근사다. 512-entry complex sensitivity를 보간하고 최대 3차 series를 사용한다. conductor n/k는 phase와 F82 magnitude를 구성하며 dielectric와 conductor substrate를 구분한다. 고정 RGB 세 파장만 평가하는 설명은 현재 구현과 맞지 않는다.

host는 sensitivity를 정적 include 또는 `LX_FILM_SENSITIVITY_GPU_TABLE`의 constant table로 공급할 수 있다. 단위/정확도/고각도 한계는 이 finite-order 근사에 속한다. spectral path tracing이나 모든 grazing 조건의 일치를 보장하지 않는다. fixture NOTICE의 출처·라이선스도 보존해야 한다.

## IBL 두 경로

reference bake는 각 표본의 normal/view/lobe에서 별도 base/coat convolution과 sheen/coat irradiance를 준비한다. 반환된 singleScatter가 이미 보상된 모델이면 소비자가 보상을 다시 더하면 안 된다.

live split-sum은 isotropic roughness와 Fresnel endpoint로 축약하며 anisotropy 방향, thin-film/F82 각도 세부, sheen lobe shape를 reference처럼 적분하지 않는다. coat는 자기 normal/roughness를 사용하지만 이 근사 전체를 Blender parity로 표시할 수 없다.

## 남은 수용 요구

박막 subnanometer/두께 sweep, grazing, conductor/dielectric, HDRI mirror, coat+sheen 조합은 별도 비교가 필요하다. 과거 MAT-9 JSON은 고정 조건의 역사 자료로 유지한다. 새로운 shader/host version의 수용은 같은 입력과 reference noise control로 재측정해야 하며 이번 문서 갱신은 이를 수행하지 않았다.

## 구현 근거

- [`Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledLayered.slang`](../../Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledLayered.slang)
- [`Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledThinFilm.slang`](../../Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledThinFilm.slang)
- [`Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledRuntimeIbl.slang`](../../Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledRuntimeIbl.slang)
- [`Lattice/Material/LXMaterialCompiler.cpp`](../../Lattice/Material/LXMaterialCompiler.cpp)
