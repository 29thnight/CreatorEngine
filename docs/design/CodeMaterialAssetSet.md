# AssetSet의 source-free authored Code Material

> 기준: `493536e300e274cbf59989cdd62b20310c8ae64f`의 코드·호출 연결 정적 검토. 이 문서 갱신에서는 빌드·GPU 실행·성능 측정을 수행하지 않았다.

## 세 가지 identity

Code 경로는 authored Material, MaterialProgram, ShaderMeta를 구분한다. `AuthoredMaterialDocument`는 material 값과 explicit program ID/default texture ID를 가진다. `CodeProgram`은 program/meta ID, metadata/root source 경로, compiler SHA-256, format/ABI stamp, captured input inventory, source-free metadata, reflected layout, 모든 variant bytecode를 가진다.

현재 authored material과 Code program의 representation은 각각 2, version은 1이다. program 최대 크기는 160 MiB, variant는 4096이다. 이 값은 Graph의 CookedProgramVersion 12와 다른 codec 계약이다. 경로 문자열은 진단/identity이며 runtime이 원본 source를 열라는 지시가 아니다.

## 검증된 bundle과 완전한 coverage

`ValidateProgram`은 정렬된 unique Assets-relative input 경로, bounded byte size와 SHA-256, metadata/source 및 양쪽 `.meta` sidecar inventory를 요구한다. embedded ShaderMeta를 source-free로 읽고 root source token을 대조한다.

coverage는 backend/pass/keyword selection/vertex mask/reference path 조합 전체다. compute는 한 compute stage, graphics는 vertex+pixel을 요구한다. supported model vertex mask와 Forward reference variant 규칙, exact renderer/material defines, stage entry/profile/reflection, 모든 variant의 동일 immutable layout을 검사한다. 우연히 선택된 한 variant가 존재하는 것으로 complete bundle을 승인하지 않는다.

## AssetSet producer의 입력 경계

`BuildMaterialProgramAssetSetProduct`는 `.codeprogram` recipe와 explicit previously verified bundle, captured source inventory를 받는다. recipe는 schema, shaderMetaAssetId, metadata/source path, compilerSha256을 bundle과 대조한다. 이 함수는 파일 I/O나 shader compile을 숨겨 수행하지 않는다.

AssetSetBuild가 Assets-contained path에서 source/meta/bundle 및 compiler input을 먼저 capture하고 size/hash를 검증한다. literal include reachability와 허용 inventory를 검사하고 source cook 뒤 bytes가 바뀌지 않았는지 재확인한다. input digest는 role/path/length/hash를 구분해 newline 등으로 다른 inventory가 같은 표현이 되지 않게 한다.

Material, Program, ShaderMeta, Texture는 exact typed Hard dependency와 명시 scope를 가진다. nil/self/type-conflict/중복/누락 dependency, schema와 recipe 불일치는 거부한다. generated Graph ShaderMeta를 독립 authored metadata처럼 cook하지 않는다.

## runtime 소유권

AssetDepot는 captured artifact origin, independently decoded ShaderMeta, default Texture representations를 pin한다. metadata readiness만으로 dependency load/compile을 시작하지 않는다. Program/Material request는 exact dependency request를 보유하고 root 교체/epoch의 stale completion을 차단한다.

`LX::Runtime::RestoreCodeGraphics/RestoreCodeCompute`는 verified program에서 exact backend/pass/keyword/mask/reference variant를 찾고 bytecode/reflection을 복구한다. compiler/source cache fallback은 없다. 공통 Instance는 값·uniform·texture owners·keyword를, graphics/compute generation은 compile identity와 PSO 요청을 보유한다.

## 소비 범위와 남은 검증

Code 자산 codec/runtime 지원이 현재 graph-only METEOR Scene의 임의 Code draw 지원을 뜻하지 않는다. 명시적인 Code helper/회귀 소비자와 제품 Scene 지원 여부를 따로 확인한다. 자산 import/cook/readback, source inventory 변조, complete variant 누락, root 전환, exact owner 수명 회귀가 필요하다.

이번 문서 갱신은 그 검증 분기와 호출을 읽었으며 새로운 bundle을 만들거나 GPU 실행을 하지 않았다. implementation과 serialized format을 바꾸지 않았다.

## 구현 근거

- [`Engine/RenderEngine/Experiment/Cooked/CookedCodeMaterial.h`](../../Engine/RenderEngine/Experiment/Cooked/CookedCodeMaterial.h)
- [`Engine/RenderEngine/Experiment/Cooked/CookedCodeMaterial.cpp`](../../Engine/RenderEngine/Experiment/Cooked/CookedCodeMaterial.cpp)
- [`Engine/RenderEngine/Experiment/Cooked/MaterialAssetSetProducer.h`](../../Engine/RenderEngine/Experiment/Cooked/MaterialAssetSetProducer.h)
- [`Engine/RenderEngine/Experiment/Cooked/MaterialAssetSetProducer.cpp`](../../Engine/RenderEngine/Experiment/Cooked/MaterialAssetSetProducer.cpp)
- [`Tools/AssetCooker/AssetSetBuild.cpp`](../../Tools/AssetCooker/AssetSetBuild.cpp)
- [`Engine/RenderEngine/AssetDepot/MaterialAssetRuntime.h`](../../Engine/RenderEngine/AssetDepot/MaterialAssetRuntime.h)
- [`Engine/RenderEngine/LXMaterialPipeline.h`](../../Engine/RenderEngine/LXMaterialPipeline.h)
