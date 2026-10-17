# Material Graph → Slang 생성 계약 (MAT-6)

**2026-09-28 · material domain의 초기 지원 범위.**

`LXMaterialAsset` → `BuildMaterialIR` → `GenerateMaterialSlang` → host의 Slang 검증 →
`LXMaterialProgramStore` 순서로 처리한다. 생성기는 Engine/Editor/Slang DLL을 링크하지 않는다.
검증 host가 DXIL/SPIR-V와 필요한 stage를 컴파일한다. 제품 cooker·ShaderMeta pass adapter·
reflection upload·PSO와 Scene generation 교체는 MAT-7, Editor 저작 창은 LX-3이다.

## 지원 정의

| 정의 | 생성 범위 | 명시적으로 거부하는 입력 |
|---|---|---|
| `ShaderNodeRGB`, `ShaderNodeValue` | typed 상수; Color의 Linear/Data/SRGB 의도 | shader float 범위 밖의 실제 사용 값 |
| `ShaderNodeTexImage` | FLAT, Linear/Closest, REPEAT/EXTEND, Color/Alpha 공유 sample | Cubic/Smart, CLIP/MIRROR, BOX/SPHERE/TUBE, 비어 있는 실제 사용 image |
| `ShaderNodeNormalMap` | TANGENT/WORLD, Strength, geometric normal fallback, mirrored/degenerate tangent | OBJECT/BLENDER_OBJECT/BLENDER_WORLD, named UV map |
| `ShaderNodeBsdfPrincipled` | MAT-3~5 core/Layered/Transmission/SSS 입력을 공용 `MaterialInputs`로 연결 | nonzero/dynamic Diffuse Roughness·Weight |
| `ShaderNodeOutputMaterial` | active output, 별도 Surface/Volume 역할, target ALL | EEVEE/CYCLES target, nonzero/dynamic Displacement·Thickness, 잘못된 closure 역할 |
| 공유·중첩 Group | 안정 interface ID, 인스턴스별 입력 override, legacy interface와 Input/Output 경계 | IR 검증의 순환·미등록 정의·revision/domain/type 오류 |
| `LXParameter*`, `LXReroute*` | 실제 사용 Blackboard의 typed 값·resource, 값/closure 전달 | 잘못된 parameter ID/type, 지원하지 않는 live sampler |
| `LXMultiplyFloat`, `LXMultiplyColor` | 동형 곱셈, float32 상수 접기 | overflow |
| `LXTextureSample` | Texture/Sampler/Vector 핀, Color/Alpha 공유 sample | empty resource, 지원하지 않는 sampler/color-space enum |
| `LXPrincipledVolume` | MAT-5의 Color/Density/g/Absorption Color/Emission Strength/Color homogeneous subset | full Blender Volume의 string attribute/blackbody는 이 정의에 포함하지 않음 |

`LX*` 추가 정의는 엔진 소유 operator다. Blender의 전체 Math/Mix/Volume 등록으로 표시하지 않는다.
Blender `ShaderNodeVolumePrincipled` 전체를 가져오면 미등록 node로 보존하고 IR을 거부한다.
문자열 attribute·Blackbody를 조용히 무시해 cook하지 않는다. 전체 Blender 목록이나 enum 조합,
mute·implicit type conversion·procedural texture·Closure Mix/Layer Shader의 지원 완료가 아니다.

Material 그룹 생성 시 Core가 만든 interface 중 원래 연결이 없는 disabled socket과
숨긴 implicit geometry/UV input은 그룹 내부 기본값으로 남긴다. 이를 synthetic link로
노출하면 disabled socket 검증이 실패하거나 Image Texture의 implicit UV 의미가 바뀐다.
이미 연결된 입력과 개발자가 명시적으로 만든 interface는 유지한다. Material 문서의
전체 asset transaction으로 그룹·metadata를 함께 Undo/Redo한다.

## 결정성과 비용

- 같은 안정 ID·지원 의미는 UTF-8/LF Slang과 자원 순서가 동일하다. node/link/Blackboard 저장 순서,
  위치·접힘·Frame·view·표시 이름은 shader 의미에 들어가지 않는다. 다른 ID의 동형 그래프를 같은
  이름으로 재작성하는 canonical graph isomorphism은 구현하지 않는다.
- active output에서 역방향으로 실제 사용하는 입력을 해석한다. 유효하지만 사용하지 않는 node,
  parameter·resource와 상수 weight 0의 dependent lobe는 제거한다. unknown/schema/enum 오류는
  문서 전체 IR 검증에서 거부한다. runtime parameter의 기본값 0으로 lobe를 제거하지 않는다.
- full constant metal은 Transmission/SSS, full constant Transmission은 SSS를 제거한다.
  lobe의 재질 의미를 바꾸는 근사 최적화는 하지 않는다. texture Color/Alpha는 한 번 sample한다.
  서로 다른 node의 동일 sample을 합치는 CSE는 Slang의 최적화 단계가 담당한다.
- 기본 상한은 4,096개 expression, 64개 texture+sampler, 128개 live parameter다.
  초과하면 소유 node/pin을 진단한다. shader byte 수·제품 permutation/cache budget은 MAT-7에서 판정한다.
- `semanticKey`는 길이로 구분한 정확한 payload이며 `std::hash`가 아니다. Slang·resource intent·
  실제 사용 parameter 계약을 포함한다. host는 공용 include digest, compiler/backend/profile/options를
  `LXMaterialCompilerOptions.dependencies`에 넣는다. 이 값이 바뀌면 source가 같아도 다시 검증한다.
  현재 독립 검증은 이 invalidation 경계를 검사한다. 제품 dependency digest 수집은 MAT-7이다.

## 생성 자원과 수치

`LXMaterialProgram`이 features, Surface/Volume 존재, sample 수, live Blackboard,
typed resource table, Forward/refraction/SSS/Volume 요구 조건과 source map을 소유한다.
Volume-only의 `surfaceInputs`는 구조 초기화용이다. host는 `surface=false`를 보고 표면 draw를 만들지 않는다.
`WriteMaterialProgramMetadata`는 같은 내용을 결정적 `.materialprogram.json`으로 내보낸다.
기존 authored `.shadermeta` schema를 복제하지 않는다. pass/queue/constant-buffer byte layout은
제품 ShaderMeta/reflection adapter의 책임이다. generated Slang과 metadata는 읽기 전용 산출물이다.

texture/sampler의 논리 register는 각각 `tN/sN, space1`이다. 숫자 parameter는 값 struct로
전달하므로 공용 `MaterialInputs`와 마찬가지로 cbuffer byte layout을 가정하지 않는다.
shader symbol은 안정 ID로 만들며 표시 이름·asset reference를 Slang 식별자나 코드에 삽입하지 않는다.
Vulkan host는 **space1에도** 기존 `VulkanBindingModel`의 b/t/u/s shift를 적용해야 한다.
독립 검증은 실제 SPIR-V decoration의 set/binding을 resource table과 대조한다.

SRGB texture는 **SRGB view 또는 미리 선형화한 저장소**를 바인딩한다. decode는 보간 전에,
alpha는 선형 그대로 처리한다. sampled RGB를 다시 SRGB decode하지 않는다. Color 상수와
SRGB Color parameter는 각각 상수 접기와 shader helper에서 변환한다. HDR Color는 1.0으로 자르지 않는다.
Normal texture는 Data 의도를 사용한다. 제품 texture format/view 선택 검증은 MAT-7이다.

host가 UV·LOD·geometric normal·tangent/bitangent를 전달한다. 생성기 v2는
`LXSampleMaterialImage`를 사용한다. compute·evaluated-point host는 명시적 `context.lod`를
유지하고, 실제 Scene PS는 각 노드의 최종 Vector fine derivative와 바인딩 이미지의
크기·mip 수로 독립 LOD를 구한다. [샘플링 계약과 검증](MaterialGraphTextureFootprints.md)을 따른다.
named UV/object transform은 후속 지원 계약이다. generated module은 feature mask를 정의하므로
공용 Principled include보다 먼저 포함한다. Special host는 `LX_MATERIAL_ROUTE=2`를 지정한다.

## 진단과 마지막 정상 프로그램

진단 위치는 scope/group ID, node ID, pin ID, property와 인스턴스 호출 경로다.
IR 검증의 group 오류는 정의 scope를 가리키며, 실제 생성/컴파일 위치는 해당 인스턴스 호출 경로도 포함한다.
생성 줄 범위를 통해 Slang의 한 줄 `file(line)`/`file:line:column` 및 pinned Slang의
여러 줄 `error[...]` + `--> file:line:column` 메시지를 원래 그래프로 매핑한다.
공용 include와 위치 없는 compiler 오류도 버리지 않으며 graph 위치를 임의로 지정하지 않는다.
접힌 상수는 별도 statement가 없을 수 있다. 실제 대입 줄은 소비 input pin을 가리킨다.

`Publish`는 임시 program/artifact를 검증한 뒤 함께 교체한다. generator 실패, backend 실패,
exception, 빈 bytecode/target, 중복 target과 부분 backend 성공은 마지막 정상 program·
전체 artifact·generation을 보존한다. 같은 identity의 배치 변경은 재컴파일하지 않는다.
host는 요구하는 backend/stage **전부**를 성공했을 때만 verifier에서 true를 반환해야 한다.
이 경계는 CPU publication이다. RenderThread/PSO/GPU resource lifetime까지 교체한 증거는 아니다.

## 검증

`Tools/regression/verify-material-codegen.ps1`는 VS18/v145 `/W4 /WX`로 native 생성기를 빌드한다.
상수 HDR emission/별도 alpha, dynamic SRGB parameter, texture Color/Alpha와 factor,
4 sampler 조합, explicit resource/UV parameter, tangent/world/mirrored/degenerate normal,
공유·중첩 group과 전체 Principled 그룹, dead lobe, full metal/glass, Special+Volume/Volume-only를 검사한다.

GPU는 RTX 4070 Ti D3D12의 실제 2×2 Texture2D/Sampler 바인딩·readback이다.
SRGB 사례는 보간 전 선형화한 RGBA32F fixture를 사용하며 실제 제품 SRGB view 배선 검증은 아니다.
sampler의 분수 정밀도가 다른 GPU에서도 대조할 수 있도록 정확한 texel/보간 좌표를 사용한다.
CS/PS를 DXIL/SPIR-V로 컴파일하고 Special의 Deferred 컴파일 거부, SPIR-V binding 충돌,
실제 shader compile 실패의 node/pin 진단과 마지막 artifact 유지, dependency 변경 재컴파일을 검사한다.

고정 source/graph 4쌍은 `Tools/regression/fixtures/material-codegen/`에 있다.
일반 검증은 expected 파일을 변경하지 않고 정확히 비교한다. 초기 기준선을 다시 고정할 때만
검토 후 `-WriteGolden`을 사용한다. raw shader, metadata, manifest, GPU CSV와 실패 로그는
`Build/Obj/MaterialCodegenProbe/`에 남긴다. Blender rendered parity·Scene route·성능 검증은 MAT-9다.
