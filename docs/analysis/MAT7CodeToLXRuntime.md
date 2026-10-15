# MAT-7 Code 입력의 공통 runtime 호환 범위

> 기준: `493536e300e274cbf59989cdd62b20310c8ae64f`의 코드·호출 연결 정적 검토. 이 문서 갱신에서는 빌드·GPU 실행·성능 측정을 수행하지 않았다.

## 현재 지원하는 연결

`Material::ConfigureShaderProperties`는 Code ShaderMeta/layout/handle을 검증해 `LX::Runtime::ShaderGeneration`과 `Instance`로 옮긴다. shader owner는 metadata/layout과 verified CodeProgram을 유지하고, instance는 property 값·packed uniform·texture owner·keyword selection을 유지한다. ShaderMeta 자체는 material instance의 소유자가 아니다.

공통 값 경로는 Float/Float2/Float3/Float4/Int/Bool/Float4x4/Texture2D를 다룬다. typed setter는 검증된 새 immutable instance를 만들고 성공한 뒤 compatibility fields를 동기화한다. graph의 stable Blackboard ID 처리와 Code의 이름 기반 property 계약은 어댑터에서 구분한다.

## 변경·복구의 원자성

`ReloadCodeShader`는 기존 keyword 계약을 몰래 버릴 수 없다. 새 shader로 현재 값/keyword/texture를 모두 구성할 수 있을 때만 게시한다. 타입·private field·layout·texture 실패는 old owner와 bytes를 보존한다. weak shader cache는 조회 최적화이며 accepted instance의 ownership을 대신하지 않는다.

`RestoreCodeGraphics`와 `RestoreCodeCompute`는 검증된 CodeProgram에서 backend/pass/keyword/vertex-mask/reference 조건이 정확히 일치하는 variant만 선택한다. source-free restore에 compiler cache 탐색이나 가장 비슷한 variant fallback은 없다. 자세한 package 계약은 [Code Material AssetSet](../design/CodeMaterialAssetSet.md)에 있다.

## live Scene과의 경계

초기 MAT-7 보고서는 native GBuffer/Forward와 Graph의 공통 소비 전환을 설명했다. 현재 live Scene은 graph-only admission을 적용한다. 공통 runtime·Code serialization·명시적 helper가 남아 있다는 사실을 mixed Code/Graph 제품 draw 완료로 읽으면 안 된다.

`ExperimentMaterialSealing`과 Code runtime probe는 compatibility와 격리된 explicit/reference 경로를 검증하는 데 의미가 있다. 새 제품 기능이 Code를 요구하면 admission·geometry·pass·cook·failure 정책을 별도로 설계해야 하며, 예전 fixture 통과만으로 우회해서는 안 된다.

## 역사적 증거와 남은 검증

`material_code_runtime_tests`와 common consumer의 과거 DXIL/SPIR-V compile/reflection·DataSystem 왕복 결과는 당시 입력 범위의 기록이다. CPU compile 성공은 native Vulkan 이미지나 전체 모델 성능의 증거가 아니다.

현재 문서는 공통 타입과 복구 선택 코드를 확인했지만 실행하지 않았다. generic matrix packing, immutable 실패 보존, source-free exact variant 복구는 여전히 회귀해야 하며 현재 live Graph의 성능/Blender 품질은 별도 MAT-9 gate다.

## 구현 근거

- [`Engine/RenderEngine/Material.cpp`](../../Engine/RenderEngine/Material.cpp)
- [`Engine/RenderEngine/LXMaterialRuntime.h`](../../Engine/RenderEngine/LXMaterialRuntime.h)
- [`Engine/RenderEngine/LXMaterialRuntime.cpp`](../../Engine/RenderEngine/LXMaterialRuntime.cpp)
- [`Engine/RenderEngine/LXMaterialPipeline.h`](../../Engine/RenderEngine/LXMaterialPipeline.h)
- [`Engine/RenderEngine/Render/Scene/ExperimentMaterialSealing.cpp`](../../Engine/RenderEngine/Render/Scene/ExperimentMaterialSealing.cpp)
- [`Tools/regression/material_code_runtime_tests.cpp`](../../Tools/regression/material_code_runtime_tests.cpp)
