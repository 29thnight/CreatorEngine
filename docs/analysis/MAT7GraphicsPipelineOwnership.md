# MAT-7 graphics generation과 native PSO 수명

> 기준: `493536e300e274cbf59989cdd62b20310c8ae64f`의 코드·호출 연결 정적 검토. 이 문서 갱신에서는 빌드·GPU 실행·성능 측정을 수행하지 않았다.

## 무엇을 누가 소유하는가

`LX::Runtime::GraphicsGeneration`은 shader contract owner, full compile identity, `RHIGraphicsPipelineRequest`를 보유한다. request는 stage bytecode와 input semantic 문자열 등 native 요청이 참조하는 데이터를 소유한다. RHI cache는 실제 native PSO의 생성·공유·완료 후 폐기를 소유한다. CPU generation이 살아 있다는 사실만으로 cache가 retire한 handle을 다시 사용할 수는 없다.

compile identity는 material keyword key보다 넓다. backend, source/entry/profile, effective permutation/options, vertex mask, reference 여부, stage dependency 또는 sealed cooked identity, visibility opt-in이 포함된다. root-layout ID와 authored handle만으로 두 shader를 동등하게 취급하지 않는다.

## 준비와 교체

`GraphicsPipeline::Prepare`는 비동기 후보를 만들고 `Poll`로 Pending/Ready/Failed를 확인한다. 이미 준비하거나 게시한 owner를 덮어써 pending request의 bytes를 잃을 수 없다. `GetGeneration`은 유효한 Ready generation만 반환한다.

Replace는 새 후보 성공 뒤에 교체한다. 같은 native handle은 중복 invalidate하지 않으며, 공유 holder가 있는 설치자는 `invalidatePrevious=false`로 retirement를 맡을 수 있다. fence/completion과 남은 holder를 확인하지 않은 즉시 폐기는 금지한다.

## frame 소비

현재 frame은 자신이 봉인한 정확한 generation을 보관한다. draw 시 최신 ShaderMeta registry를 재조회하여 다른 bytecode/layout으로 바꾸지 않는다. Graph Scene은 필요한 모든 요청이 ready인 전체 입력만 선택한다. source-free cooked Graph는 sealed target bytes를 사용하고 source/reflection/compiler를 다시 요청하지 않는다.

초기 MAT-7에 남아 있던 “owner 없는 fixture의 registry fallback”은 현행 제품 계약이 아니다. Code helpers의 지원 범위와 graph-only live admission은 [Code 연결 기록](MAT7CodeToLXRuntime.md)에서 구분한다.

## 검증 범위

`material_pipeline_runtime_tests`는 임시 입력 소유·실패 보존·shared handle·async 상태·정확한 compile identity를 검사한다. `material_vulkan_generation_probe`는 별도 native fixture다. 예전 186 checks/768 pixels 등의 결과는 당시 실행 기록이며 현재 모든 Vulkan 제품 경로의 승인이 아니다.

새 수용에는 hot reload 중 in-flight frame, failed 후보 후 old generation, cache holder별 retirement, backend별 ready/failed/timeout을 포함한다. 이미지 품질과 FPS는 이 수명 검사로 대신할 수 없다. 정본은 [Scene generation](../design/MaterialGraphSceneGeneration.md)이다.

## 구현 근거

- [`Engine/RenderEngine/LXMaterialPipeline.h`](../../Engine/RenderEngine/LXMaterialPipeline.h)
- [`Engine/RenderEngine/RHI/RHIGraphicsPipelineRequest.h`](../../Engine/RenderEngine/RHI/RHIGraphicsPipelineRequest.h)
- [`Engine/RenderEngine/MaterialGraphSceneHost.cpp`](../../Engine/RenderEngine/MaterialGraphSceneHost.cpp)
- [`Tools/regression/material_pipeline_runtime_tests.cpp`](../../Tools/regression/material_pipeline_runtime_tests.cpp)
- [`Tools/regression/material_vulkan_generation_probe.cpp`](../../Tools/regression/material_vulkan_generation_probe.cpp)
