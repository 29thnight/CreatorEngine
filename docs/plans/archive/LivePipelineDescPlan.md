# LivePipelineDesc의 현재 역할과 이전 계획 승계

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 현재 구현

LivePipelineDesc는 살아 있는 native 조립 계층이다. LivePassNode의 name·reads/writes/modifies·enabled·declare와 instance lifecycle을 묶고 LiveBlackboard의 슬롯을 연결한다. EnhancedSceneRenderer::BuildPipelineDesc가 이를 구성하고 각 view frame에서 Declare를 호출해 EnhancedRenderGraph에 실제 pass를 추가한다.

Persistent assembly와 per-frame execution을 분리하는 원래 요구는 유효하다. 그러나 예전 문장의 “노드 목록 순서가 곧 실행 순서”는 현재 틀리다. 제품은 ExplicitVersioned/DependencyOrder를 사용하며 GPU 순서는 data/physical dependency가 결정한다. native Initialize/Prepare/Shutdown 순회와 GPU 실행을 구분한다.

## 활성·fallback 계약

읽기 슬롯은 producer가 있어야 하고 쓰기는 새 version, modifies는 이전 값과 새 version을 연결한다. 꺼진 modifies는 허용된 입력을 그대로 전달할 수 있지만 모든 꺼진 pass가 자동으로 유효 출력을 만든다고 가정하지 않는다. 타입 있는 사이드 데이터는 owner가 유지하고 RGHandle 슬롯으로 raw pointer 수명을 숨기지 않는다.

LivePipelineDesc는 전체 C# pipeline API나 영속 asset 파일 형식이 아니다. native callback을 직렬화하거나 managed callback을 render thread에 직접 넣지 않는다. 향후 C# immutable IR generation 저작은 CSharpRenderPipelinePlan의 별도 구현 목표다.

## 유지할 검증

missing input·중복 producer·잘못된 modify·cycle·disabled fallback·새 pipeline generation을 검증한다. Scene/Game/preview의 owner와 resize·shader reload·old in-flight generation 수명을 분리한다. 현재 graph viewer와 PBR capture는 실제 진단 수단이지만 예전 전용 CLI 제안을 구현된 기능으로 열거하지 않는다.

옛 고정 노드 수·동시 graph 개수·로컬 selftest 합계는 구조 불변식이 아니다. 현재 세부 흐름은 [라이브 파이프라인](../../analysis/RenderPipelineFlow.md), 실행 규칙은 [그래프 정본](../RenderGraphDependencySchedulingPlan.md)을 따른다.

## 소스 근거

- [Engine/RenderEngine/Render/Core/EnhancedLivePipelineDesc.h](../../../Engine/RenderEngine/Render/Core/EnhancedLivePipelineDesc.h)
- [Engine/RenderEngine/Render/Core/EnhancedLivePipelineDesc.cpp](../../../Engine/RenderEngine/Render/Core/EnhancedLivePipelineDesc.cpp)
- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp](../../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp)
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h](../../../Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h)

## 이전 기록

[기준 커밋의 개정 전 원문](https://github.com/29thnight/CreatorEngine/blob/493536e300e274cbf59989cdd62b20310c8ae64f/docs/plans/archive/LivePipelineDescPlan.md)에 당시 단계·세부 앵커·측정 이력이 남아 있다. 그 실행 결과를 현재 커밋의 수용으로 승계하지 않는다.
