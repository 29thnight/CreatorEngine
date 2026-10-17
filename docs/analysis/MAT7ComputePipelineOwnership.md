# MAT-7 compute generation의 현재 수명 계약

> 기준: `493536e300e274cbf59989cdd62b20310c8ae64f`의 코드·호출 연결 정적 검토. 이 문서 갱신에서는 빌드·GPU 실행·성능 측정을 수행하지 않았다.

## 공통 owner

`ComputePipeline`은 한 번 게시하는 immutable `ComputeGeneration`을 가진다. generation은 CS bytes를 복사하고 desc의 pointer를 그 사본에 연결하며 layout/native handle과 `ComputeShaderDescription`을 보관한다. generated material은 `ShaderGeneration`도 유지하고, renderer helper는 material schema 없이 사용할 수 있다.

`ComputeCompileIdentity`는 backend/source/entry/profile/permutation/options와 dependency 또는 sealed program identity를 보존한다. 신원이 있는 요청은 완전한 stage 계약을 요구한다. 외부에서 이미 컴파일한 독립 fixture만 빈 identity를 허용하며, 이 경우 원래 compiler/include provenance를 복원했다고 주장할 수 없다.

## 실패와 retirement

Create는 bytecode·layout과 identity를 검증하고 native cache 생성이 성공한 뒤 generation을 게시한다. Replace 실패는 이전 owner를 보존한다. 이전과 같은 native handle이면 invalidate하지 않으며 공유 holder가 있으면 설치 renderer가 retirement를 맡는다. compute도 graphics와 같은 completion 경계를 지켜야 한다.

frame, render graph callback, 반환된 batch/result가 사용하는 compute generation을 붙잡아야 한다. CPU owner가 해제되었다는 이유만으로 GPU가 완료된 것은 아니며, CPU owner 보존만으로 native cache의 영구 유효성도 보장되지 않는다.

## 제품과 독립 helper

Scene의 geometry transform, lookup, SSS/refraction/volume과 별도 IBL/mesh/surface helper는 이 owner를 사용한다. `DescribeComputeShader`는 verified product의 정확한 backend/entry CS target과 owned bytes를 선택하는 source-free 경로다. Code compute는 exact CodeProgram variant를 복원한다.

MeshSurface의 즉시 sample 결과와 deferred graph transform 준비는 다른 상태다. sampler generation을 transform owner에 넣어 `IsPreparedForGraph`가 잘못 true가 되면 즉시 결과를 다시 변환할 수 있으므로 별도 보관한다.

## 과거 실행을 현재 판정으로 올리지 않기

2026-10-02의 212-check runtime/독립 GPU/Scene 결과는 당시 host·fixture의 기록이다. 기존 identity 10/11과 LXMC v3는 현재 host 34/cooked version 12의 호환표가 아니다. 현재 산출물은 현재 계약으로 재쿠킹해야 한다.

이 변경은 비동기 compute PSO 생성, global eviction 정책, 전체 UI 저장·reload, 모델 성능 또는 Vulkan 제품 수용을 자동 완료하지 않는다. 회귀 시 source-free 복구·실패 출력 보존·in-flight owner·즉시/deferred 상태를 각각 확인한다. [generation 정본](../design/MaterialGraphSceneGeneration.md)을 함께 읽는다.

## 구현 근거

- [`Engine/RenderEngine/LXMaterialPipeline.h`](../../Engine/RenderEngine/LXMaterialPipeline.h)
- [`Engine/RenderEngine/MaterialGraphSceneCompiler.cpp`](../../Engine/RenderEngine/MaterialGraphSceneCompiler.cpp)
- [`Engine/RenderEngine/MaterialGraphMeshSurface.h`](../../Engine/RenderEngine/MaterialGraphMeshSurface.h)
- [`Engine/RenderEngine/MaterialGraphMeshSurface.cpp`](../../Engine/RenderEngine/MaterialGraphMeshSurface.cpp)
- [`Engine/RenderEngine/MaterialGraphProduct.h`](../../Engine/RenderEngine/MaterialGraphProduct.h)
- [`Tools/regression/material_pipeline_runtime_tests.cpp`](../../Tools/regression/material_pipeline_runtime_tests.cpp)
