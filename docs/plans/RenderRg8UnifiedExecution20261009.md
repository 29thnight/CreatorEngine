# 통합 큐 실행의 남은 수용 계획

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 출발점

큐 실행의 현행 구현은 [통합 실행 계약](../analysis/RenderRg8UnifiedExecution20261009.md)과 [그래프 정본](RenderGraphDependencySchedulingPlan.md)을 따른다. CPU 계획, native 기록·제출, GPU completion은 각각 구현·검증해야 한다. 과거 파일 날짜로 미구현/완료 여부를 정하지 않는다.

## 다음 작업

1. normal-domain의 정확한 graph signature와 모든 pass 비용이 채택되는지 확인한다
2. 합법적 compute 작업과 independent graphics가 있는 장면, 겹칠 수 없는 장면을 함께 준비한다
3. 실제 제출 batch·wait·queue별 timestamp와 output attachments를 수집한다
4. schedule/record/submit CPU 비용과 GPU critical span, retained memory를 OFF/ON으로 비교한다
5. pending·부분 실패·resize·shutdown에서 마지막 queue completion까지 owner를 보존하는지 검증한다

## 수용 제약

owned queue와 transient aliasing의 조합은 현재 명시적으로 거부한다. 단순 guard 제거를 통합 완료로 취급하지 않는다. 지원을 늘리려면 alias 수명이 모든 queue reader/writer와 마지막 소비에 대해 안전함을 먼저 증명해야 한다. Vulkan의 공용 인터페이스만으로 DX12 queue 실행 결과를 이식된 것으로 볼 수 없다.

이번 문서 개정은 위 검사를 실행하거나 성능 목표를 달성한 결과가 아니다.

## 소스 근거

- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp](../../Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp)
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraphQueues.cpp](../../Engine/RenderEngine/Render/Graph/EnhancedRenderGraphQueues.cpp)
- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp](../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp)
