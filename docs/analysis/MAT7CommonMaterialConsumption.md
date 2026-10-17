# MAT-7 공통 값·texture·cooked 소비 경계

> 기준: `493536e300e274cbf59989cdd62b20310c8ae64f`의 코드·호출 연결 정적 검토. 이 문서 갱신에서는 빌드·GPU 실행·성능 측정을 수행하지 않았다.

## 단일 accepted 표현

현재 `Material`의 Graph instance는 verified generation과 값 override, texture owner를 보존한다. generated ShaderMeta/layout/property/constant-buffer 조회는 같은 accepted 표현에서 나온다. 편집 UI, serialization, renderer가 각자 다른 최신 값을 갖는 구조를 만들지 않는다.

숫자 override는 stable parameter ID를 기준으로 저장하고 공통 packer로 uniform bytes를 만든다. texture source ID가 SRGB/Data 등 여러 reflected slot에 쓰이면 해당 별칭의 GUID와 CPU owner를 일관되게 갱신한다. `RenderBindingCache::PrepareResourcesWithUniforms`는 accepted uniform을 소비하며 매 프레임 별도 의미 packer로 재해석하지 않는다.

## 실패 보존과 수명

값/type/private property/keyword/layout 또는 texture 로딩 실패는 새 후보를 게시하지 않는다. 이전 instance와 그 bytes/owner를 유지한다. CPU texture owner는 asset 수명이며 descriptor heap의 GPU residency나 command recording 수명을 자동으로 보장하지 않는다.

공통 `LX::Runtime::Instance`를 복사해 새 편집 상태를 만들면 새 representation ID를 얻고, move는 identity를 유지한다. generation과 instance를 frame에 고정한 뒤 최신 registry를 다시 조회해 내용을 바꾸지 않는다. [렌더 binding](../design/MaterialGraphPassRecording.md)이 별도 recording 유효성을 검사한다.

## 현재 cooked 경로

초기 보고서의 LXMC v3는 역사적 단계다. 현재 Graph artifact version 12는 generated 계약, validated layout, backend별 target bytecode와 semantic identity를 함께 검증한다. Editor warm cache와 Player의 source-free 경로는 동일한 accepted 계약을 복원해야 한다. package에는 DXIL/SPIR-V 양 backend가 필요하다.

Code asset과 Graph asset의 표현/version은 별개다. 공통 값 타입을 공유한다는 이유로 CodeProgram을 Graph artifact처럼 읽거나 live graph-only Scene에 넣을 수 없다. [Code AssetSet](../design/CodeMaterialAssetSet.md)의 독립 계약을 따른다.

## 회귀와 후속 요구

`material_common_consumer_probe`는 실제 Material/DataSystem 소비, 값 변경, warm cache와 복구를 검사한다. `material_datasystem_probe`는 texture owner, clone/저장/재개방·제거와 실패 보존을 담당한다. 역사적 25/31항목 통과는 해당 당시 fixture 기록이며 현재 PSO·GPU·제품 UI 전체의 통과를 뜻하지 않는다.

새 검증에서는 alias 전체 갱신, 실패 후 old owner 생존, cold/warm 동일 uniform, source 없는 package 복구, Editor 저장 뒤 Scene 재개방을 따로 확인해야 한다. 정본은 [자산 저작](../design/MaterialAssetAuthoring.md)과 [generation](../design/MaterialGraphSceneGeneration.md)이다.

## 구현 근거

- [`Engine/RenderEngine/Material.cpp`](../../Engine/RenderEngine/Material.cpp)
- [`Engine/RenderEngine/LXMaterialRuntime.h`](../../Engine/RenderEngine/LXMaterialRuntime.h)
- [`Engine/RenderEngine/LXMaterialRuntime.cpp`](../../Engine/RenderEngine/LXMaterialRuntime.cpp)
- [`Engine/RenderEngine/MaterialGraphRenderBindings.cpp`](../../Engine/RenderEngine/MaterialGraphRenderBindings.cpp)
- [`Engine/RenderEngine/MaterialGraphProduct.h`](../../Engine/RenderEngine/MaterialGraphProduct.h)
- [`Tools/regression/material_common_consumer_probe.cpp`](../../Tools/regression/material_common_consumer_probe.cpp)
- [`Tools/regression/material_datasystem_probe.cpp`](../../Tools/regression/material_datasystem_probe.cpp)
