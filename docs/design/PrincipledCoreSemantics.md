# Core Principled 표면과 반사 의미

> 기준: `493536e300e274cbf59989cdd62b20310c8ae64f`의 코드·호출 연결 정적 검토. 이 문서 갱신에서는 빌드·GPU 실행·성능 측정을 수행하지 않았다.

## 기준과 입력

Core 의미는 `PrincipledSurface.slang`과 `PrincipledBrdf.slang`에 있으며 생성기는 Principled node의 기본값/연결을 `MaterialInputs`에 기록한다. Blender 5.1.1 추출 fixture는 비교 기준으로 사용되지만 전체 Cycles renderer를 구현한 것은 아니다.

baseColor는 선형 nonnegative 색, roughness/metallic/alpha/AO는 bounded factor다. normal은 기하 normal을 fallback으로 정규화하며 emission은 color×strength다. IOR 기본값은 1.5, Specular IOR Level은 0.5다. 전자는 물리 interface, 후자는 base dielectric reflectivity 조정에 사용된다.

## 반사와 lobe 분리

normal-incidence dielectric reflectivity는 ((IOR−1)/(IOR+1))²에서 나온다. specular level이 0.5와 다르면 반사율을 조정하고 유효 IOR를 재구성한다. tint는 dielectric F0에 적용되고 metal tint는 F82 correction으로 처리한다. metal의 tint를 단순 F0 곱으로 대체하면 grazing/중간 각도 의미가 달라진다.

`BuildPrincipledReflection`과 Fresnel 함수가 이 값을 소비한다. 현재 공용 GGX 경로는 dielectric와 metal의 nonlinear energy compensation을 metallic mix 이전에 계산한다. compensation을 혼합된 Fss에 한 번 적용하거나 IBL 소비 시 추가 Lambert compensation을 더하면 중복/다른 결과가 된다.

## 조명과 IBL 경계

직접광과 환경광은 같은 표면 의미를 쓰지만 적분 방식은 다르다. evaluated-point/reference IBL은 방향별 적분과 정확한 입력 identity를 사용한다. live는 `PrincipledRuntimeIbl.slang`의 isotropic split-sum으로 F0/F90 및 prefiltered environment를 조합한다.

Core feature가 곧 Deferred를 보장하지 않는다. 현재 `SelectRoute`는 host GBuffer ABI로 표현 가능한 feature 집합과 installed Forward capability를 확인한다. generated Principled의 IOR 의미를 legacy fixed 0.04 surface로 조용히 대체하지 않는다.

## 명시적 차이와 검증

`EvaluateStandardMaterial`은 glTF factor product와 기존 MRT 의미를 유지하는 별도 adapter다. Diffuse Roughness/Weight의 nonzero 입력, 임의 Blender closure, 새로운 분포 모델은 현재 생성기 지원 밖이다.

검증은 zero/one metallic, IOR 1 및 양쪽 범위, specular level, F82 tint, rough/grazing, emission/input control을 분리한다. 수치 reference·동일입력 이미지·실제 live render는 서로 다른 gate다. 이번 정리에서는 코드를 읽었으며 기존 MAT-9 error 수치를 최신 값으로 복사하지 않았다.

## 구현 근거

- [`Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledSurface.slang`](../../Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledSurface.slang)
- [`Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledBrdf.slang`](../../Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledBrdf.slang)
- [`Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledLayered.slang`](../../Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledLayered.slang)
- [`Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledRuntimeIbl.slang`](../../Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledRuntimeIbl.slang)
- [`Lattice/Material/LXMaterialCompiler.cpp`](../../Lattice/Material/LXMaterialCompiler.cpp)
