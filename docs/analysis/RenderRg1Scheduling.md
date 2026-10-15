# 단일 writer 정렬과 제품 버전 그래프

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 이 문서의 역할

단일 writer 정렬과 제품 버전 그래프에 관한 과거 단계별 기록을 현행 계약으로 다시 정리한다. 공통 용어·수명·실행 모델은 [RenderGraph 정본](../plans/RenderGraphDependencySchedulingPlan.md)을 따른다. 날짜가 붙은 파일명은 링크 호환을 위해 유지한다.

## 현재 구현과 호출 경로

ExplicitSingleWriter는 Read/Write/ReadWrite의 writer→reader 관계를 검증하고 안정적으로 정렬한다. 누락 writer·다중 writer·모호한 접근·불법 state는 컴파일 오류다. 이 모드는 version chain을 사용한 제품 방식과 다르다. 현재 라이브 생성자는 ExplicitVersioned/DependencyOrder를 명시하므로 과거 “제품은 DeclarationOrder” 문구를 적용하지 않는다.

## 변경·검증 기준

새 pass를 RG1 fixture에 추가할 때는 단일 writer 제약을 지킨다. 제품 pass는 Write/Modify 버전과 실제 consumer를 함께 선언하고 역방향 저작 순서·순환 입력의 거부를 확인한다.

과거 로컬 Build/Artifacts 경로의 수치와 완료·기성 표시는 이번 소스 감사에서 재검증하지 않았으므로 현재 통과 결과로 옮기지 않는다. 제품 기본값, 특정 fixture 성공, 여러 backend의 제품 수용은 각각 판정한다.

## 소스 근거

- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h](../../Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h)
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp](../../Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp)
- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp](../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp)
