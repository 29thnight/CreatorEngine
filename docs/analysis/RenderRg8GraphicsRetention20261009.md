# Graphics 전용 texture 상태 유지

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 이 문서의 역할

Graphics 전용 texture 상태 유지에 관한 과거 단계별 기록을 현행 계약으로 다시 정리한다. 공통 용어·수명·실행 모델은 [RenderGraph 정본](../plans/RenderGraphDependencySchedulingPlan.md)을 따른다. 날짜가 붙은 파일명은 링크 호환을 위해 유지한다.

## 현재 구현과 호출 경로

RHITextureInfo의 retainsExplicitStateOnGraphicsQueue는 backend의 보수적 상태 유지 능력이다. SubmitQueues는 graphics 전용 texture 후보를 표시하고 compute에서 쓰는 resource를 제외한 뒤 barrier 계획에 전달한다. 모든 buffer·texture의 상태를 무조건 유지하는 최적화가 아니다.

## 변경·검증 기준

graphics FIFO 안에 남는 접근만 허용하고 실제 compute 소비가 생기면 제외되어야 한다. 특수화 전후 barrier 수와 imported final state, validation을 함께 확인한다.

과거 로컬 Build/Artifacts 경로의 수치와 완료·기성 표시는 이번 소스 감사에서 재검증하지 않았으므로 현재 통과 결과로 옮기지 않는다. 제품 기본값, 특정 fixture 성공, 여러 backend의 제품 수용은 각각 판정한다.

## 소스 근거

- [Engine/RenderEngine/RHI/RHIResourceTypes.h](../../Engine/RenderEngine/RHI/RHIResourceTypes.h)
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraphQueues.cpp](../../Engine/RenderEngine/Render/Graph/EnhancedRenderGraphQueues.cpp)
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp](../../Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp)
