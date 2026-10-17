# Material Graph에서 Slang으로 가는 생성 계약

> 기준: `493536e300e274cbf59989cdd62b20310c8ae64f`의 코드·호출 연결 정적 검토. 이 문서 갱신에서는 빌드·GPU 실행·성능 측정을 수행하지 않았다.

## 지원 정의와 실제 lowering

`GenerateMaterialSlang(asset, diagnostics, options)`는 typed IR을 만든 뒤 활성 Material Output에서 필요한 식을 생성한다. 결과는 Slang 본문, 정확한 canonical `semanticKey`, feature mask, Surface/Volume 유무, texture sample 수, live parameter/resource 목록, source map, 실행 요구를 가진 `LXMaterialProgram`이다. 노드 registry 등록과 lowering 지원은 별개다.

현재 생성기는 Color/Value, Principled BSDF, Image Texture, Normal Map, typed parameter/reroute, 그룹 invocation, float/color multiply, Separate Color, texture coordinates/sample, Surface Settings, Principled Volume을 처리한다. 그룹 instance 경로는 진단에 보존된다. 일반 Blender shader를 임의로 가져와 실행하는 컴파일러가 아니다.

명시적으로 거부하는 주요 입력은 다음과 같다.

- Material Output target이 `ALL`이 아닌 경우, 0이 아닌 Displacement/Thickness
- Principled의 0이 아닌 Diffuse Roughness/Weight
- Image Texture의 FLAT 외 projection, Linear/Closest 외 interpolation, REPEAT/EXTEND 외 extension
- Normal Map의 named UV map과 TANGENT/WORLD 외 space
- 비어 있는 live texture reference, 미지원 sampler description, 타입·소켓·property 오류
- Surface에 Volume closure 또는 Volume에 Surface closure를 연결한 경우

이 거부는 기본 재질로의 대체가 아니다. 진단을 반환하고 기존 정상 프로그램을 유지한다.

## 결정성과 비용

CompilerVersion은 3이다. 기본 제한은 expression 4096, resource 64, parameter 128이다. 미사용 출력은 코드 생성의 live 경로에 들어오지 않으며 상수 multiply는 접힌다. 다만 IR 구조 검증은 전체 문서를 검사하므로 끊어진 잘못된 정의도 무조건 무시되는 것은 아니다.

`semanticKey`는 `std::hash` 하나가 아니라 canonical 의미 payload다. host가 전달하는 include/backend/profile/options 의존성 문자열도 identity에 들어간다. 레이아웃 좌표나 화면 확대율을 shader identity로 쓰지 않는다. 최종 제품은 실제 compiler dependency와 Scene host identity를 추가로 봉인한다.

Principled 생성은 기본 Core IOR feature를 포함하고 coat/sheen/anisotropy/thin film의 활성 입력에서 기능 비트를 더한다. 상수 metallic 1은 transmission/SSS를 제거할 수 있고 상수 full transmission은 SSS를 제거할 수 있다. 동적 parameter는 실행 중 0이 되더라도 해당 정적 기능의 비용이 남을 수 있다.

## 생성 자원과 수치

원래 Slang은 논리적 `space1` resource를 사용한다. 제품 adapter가 bindless texture/sampler와 reflected uniform으로 투영한다. 숫자 Color의 sRGB default/parameter는 shader에서 decode한다. 이미지 sRGB는 필터링 이전 decode를 보장하는 texture view 또는 이미 linear인 저장소가 필요하며 alpha는 선형이다.

이미지 Vector가 연결되지 않은 기본 0이면 context UV를 사용한다. 그룹 interface를 통한 명시적 입력은 이 생략 규칙과 구별한다. Color와 Alpha 출력을 함께 써도 하나의 이미지 sample 식을 공유한다. 실제 Scene mip footprint는 [텍스처 footprint](MaterialGraphTextureFootprints.md)를 따른다.

## 진단과 마지막 정상 프로그램

`LXMaterialSource`는 scope/node/pin/그룹 instance 경로/property를 식별한다. `MapMaterialCompilerDiagnostics`는 `file(line[,column])`와 `file:line[:column]` 형식을 source map에 연결한다. 공용 include의 오류는 그래프 위치가 없더라도 표시한다.

`LXMaterialProgramStore::Publish`는 생성과 host verifier가 요구하는 모든 artifact의 성공 후에만 후보를 게시한다. 이 store는 CPU 경계다. native PSO, 프레임 자원 수명, 실제 Scene 교체는 [Scene generation](MaterialGraphSceneGeneration.md)의 별도 책임이다. `WriteMaterialProgramMetadata` 또한 renderer-neutral metadata이며 authored ShaderMeta pass schema와 동일하지 않다.

## 확장 시 수용 조건

새 연산은 타입/색 공간·상수 기본값·미지원 enum 진단·그룹 내부 source map·양 backend stage 검증을 함께 갖춰야 한다. CPU generation 성공만으로 Blender parity나 화면 품질을 승인하지 않는다. 생성 fixture는 `Tools/regression/fixtures/material-codegen`에 있으며 이번 갱신에서는 실행하지 않았다.

## 구현 근거

- [`Lattice/Material/LXMaterialCompiler.h`](../../Lattice/Material/LXMaterialCompiler.h)
- [`Lattice/Material/LXMaterialCompiler.cpp`](../../Lattice/Material/LXMaterialCompiler.cpp)
- [`Lattice/Material/LXMaterialOperators.cpp`](../../Lattice/Material/LXMaterialOperators.cpp)
- [`Lattice/Material/LXMaterialExecution.h`](../../Lattice/Material/LXMaterialExecution.h)
- [`Engine/RenderEngine/MaterialGraphProduct.cpp`](../../Engine/RenderEngine/MaterialGraphProduct.cpp)
