# 보관: Material·Shader 파이프라인 재설계의 결정과 현재 대응

> 기준: `493536e300e274cbf59989cdd62b20310c8ae64f`의 코드·호출 연결 정적 검토. 이 문서 갱신에서는 빌드·GPU 실행·성능 측정을 수행하지 않았다.

## 보관 상태

PHASE 3.5의 M0~M7은 당시 shader/material 기반을 재정비한 완료 이력이다. 이 문서는 현재 실행 계획이나 당시 수천 줄 변경 기록의 재게시가 아니다. 유효한 설계 이유와 현재 소비 경계를 남긴다. 원래 23인일 등의 산정·소스 개수·check 수·binary hash는 현재 규모나 재검증 결과로 사용하지 않는다.

현재 material 의미·Graph는 [Blender Material 계획](../BlenderMaterialGraphPlan.md), renderer 배선은 [PBR 안정화](../PBRWiringStabilizationPlan.md), backend 수용은 [Backend parity](../BackendParityPlan.md)가 소유한다. [보관 색인](README.md)에서 이전 계획의 위치를 확인할 수 있다.

## 1. 이 계획을 만든 문제

소비자가 없는 compiler/PSO 추상화는 실제 화면까지 연결되지 않은 별도 체계를 만들기 쉽다. 재설계의 출발점은 Material → authoring metadata → pass/state/permutation → compiler → backend bytecode → 실제 frame 소비의 사슬을 함께 정의하는 것이었다.

shader source를 파일화해 include/dependency를 식별한 뒤 compiler 중립화를 진행해야 했다. 런타임 문자열과 특정 OS compiler를 전제로 한 계약을 그대로 추상화하면 다른 backend에서 성립하지 않는다. 저장/compile probe만 성공한 산출물을 실제 material 배선 완료로 세지 않는 이유도 여기에 있다.

## 2. 현재 남아 있는 단일 compiler 계약

현재 `RHIShaderCompiler`/`IRHIShaderCompiler`는 backend-neutral request·owned blob·reflection을 제공한다. `VerifyFile` 결과는 bytecode, reflection, 실제 읽은 dependency identity를 묶는다. permutation·entry/profile/backend·compile options는 cache 의미에 포함된다. strict math, fine derivatives, quad/atomic capability 같은 option은 무관한 shader에 무조건 켜지 않는다.

선택한 Slang headers/DLL/license와 DXIL 지원 파일은 `ThirdParty/Slang` 번들로 고정한다. 설치된 Vulkan SDK/PATH에 따라 compiler가 바뀌게 하지 않는다. bundle에 DXIL 지원용 DXC 구성요소가 있다는 사실과 엔진의 영구적인 이중 runtime compiler 선택은 다르다.

초기 전환은 compiler backend 교체와 binding 자동화/언어 전면 개명을 한 번에 하지 않는 전략이었다. 비교 가능한 대조군을 유지하기 위한 결정이다. 이후 Graph의 bindless ABI는 별도 계약으로 진화했으므로 당시 explicit register를 현재의 영구 고정값으로 주장하지 않는다.

## 3. 저작 schema와 reflection

ShaderMeta는 property default/label/keyword axis/pass/render-state/queue 같은 저작 의미를 소유한다. reflection은 compiled cbuffer/resource의 physical layout을 검증한다. reflection이 기본값·노출·artist 의미의 새 원본이 되어서는 안 된다.

Graph 모드는 `.shadergraph`가 원본이며 generated ShaderMeta/Slang을 같은 generation으로 만든다. Code는 별도 authored metadata와 verified CodeProgram을 가진다. 현재 [Code AssetSet](../../design/CodeMaterialAssetSet.md)과 [Graph product](../../design/MaterialGraphProduct.md)의 version·dependency·restore 계약을 따른다.

permutation은 정렬된 소유 name/value와 bounded 축으로 다루며 수·binary/cache 크기를 관리한다. caller의 임시 define 배열이나 shader handle만으로 complete compile identity를 대체하지 않는다.

## 4. M0~M7의 현재 대응

| 역사적 슬라이스 | 현재 남는 책임 |
|---|---|
| M0 구 체계 정리 | 실제 소비자가 없는 중복 경로는 제거하되 사용 중인 script/serialization seam을 무근거로 삭제하지 않음 |
| M1 compiler 중립화·Slang | 단일 service와 pinned toolchain, dependency-aware verified result |
| M2 permutation | owned canonical selection·상한·정확한 cache identity |
| M3 pipeline 기술 | RHI-neutral desc/request와 native cache ownership |
| M4 shader asset | 파일 source·strict metadata·authoring/cook dependency |
| M5 material 데이터/수명 | typed 값·uniform packing·texture/generation owner·실패 보존 |
| M6 실제 consumer 연결 | immutable frame seal과 실제 pixel/owner 검증 |
| M7 reflection 대조 | authoring schema와 실제 backend layout/bytecode 일치 |

이 표는 옛 native material draw가 현재 live admission이라는 뜻이 아니다. 현재 Scene은 graph-only이며 예전 Water/Wind·GBuffer/Forward custom Code fixture는 역사적 소비 증거 또는 남은 explicit/reference helper 범위로 읽는다.

## 5. 수명과 실패 원칙

accepted material/generation은 불변 snapshot으로 frame에 고정한다. 새 shader/PSO/texture 후보의 일부 성공만으로 기존 정상 상태를 바꾸지 않는다. 실패 시 caller output과 accepted owner를 유지하고, draw 중 최신 registry를 재조회해 다른 layout/bytecode를 섞지 않는다.

LX graphics/compute generation은 요청 bytes와 identity를 소유하고 RHI cache는 native PSO를 소유한다. frame holder와 GPU completion을 확인한 retirement가 필요하다. CPU shared owner, upload recording/descriptor validity, GPU residency는 서로 다른 경계다.

source-free cooked 복구는 exact verified target을 선택해야 한다. 소스나 compiler가 없는 Player에서 “비슷한 variant” 또는 loose source fallback을 찾는 방식으로 package 누락을 숨기지 않는다.

## 6. 변경 시 다시 요구할 증거

compiler/toolchain 변경에는 양 target compile/reflection과 실제 DX12/Vulkan PSO·pixel·package 검증이 필요하다. front-end 소스 호환성 probe만으로 전체 실행을 승인하지 않는다. 정밀 부동소수점 option 이름이 같아도 IBL 적분·극점 결과가 같다는 보장이 없다.

hot reload/값·texture 변경, in-flight owner·failed 후보·shared native handle, source-free package·warm cache를 각각 검증한다. backend bytecode 생성 성공과 native 실행, numeric fixture와 Blender 품질, CPU/GPU correctness와 timing은 별도 결과다.

이번 갱신은 현재 코드 연결을 읽은 정리다. 새로운 build/test 실행·성능 수용을 주장하지 않는다. Material Graph의 remaining quality/UI gate나 일반 pipeline/post/shadow/probe 요구를 이 보관 문서의 완료 상태로 닫지 않는다.

## 구현 근거

- [`Engine/RenderEngine/RHI/RHIShaderCompiler.h`](../../../Engine/RenderEngine/RHI/RHIShaderCompiler.h)
- [`Engine/RenderEngine/RHI/RHIShaderCompiler.cpp`](../../../Engine/RenderEngine/RHI/RHIShaderCompiler.cpp)
- [`Engine/RenderEngine/RHI/RHIShaderPermutation.h`](../../../Engine/RenderEngine/RHI/RHIShaderPermutation.h)
- [`Engine/RenderEngine/ShaderMeta.h`](../../../Engine/RenderEngine/ShaderMeta.h)
- [`Engine/RenderEngine/ShaderMetaReflection.h`](../../../Engine/RenderEngine/ShaderMetaReflection.h)
- [`Engine/RenderEngine/LXMaterialPipeline.h`](../../../Engine/RenderEngine/LXMaterialPipeline.h)
- [`Engine/RenderEngine/Material.cpp`](../../../Engine/RenderEngine/Material.cpp)
- [`Engine/RenderEngine/MaterialGraphShaderMeta.cpp`](../../../Engine/RenderEngine/MaterialGraphShaderMeta.cpp)
- [`ThirdParty/Slang/README.md`](../../../ThirdParty/Slang/README.md)
