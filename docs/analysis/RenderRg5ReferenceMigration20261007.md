# 재질 reference API와 제품 배선의 구분

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 이 문서의 역할

재질 reference API와 제품 배선의 구분에 관한 과거 단계별 기록을 현행 계약으로 다시 정리한다. 공통 용어·수명·실행 모델은 [RenderGraph 정본](../plans/RenderGraphDependencySchedulingPlan.md)을 따른다. 날짜가 붙은 파일명은 링크 호환을 위해 유지한다.

## 현재 구현과 호출 경로

IblBakeResult, SurfaceBatch, RasterSurfaceBatch에는 독립 결과를 선언하는 reference API가 남아 있다. 해당 owner의 GraphOutput과 latest inputs를 소비해야 한다. 실제 라이브 제품은 SceneHost/visibility/material work를 통해 실행되므로 reference API의 존재가 별도 제품 표면 경로를 의미하지 않는다.

## 변경·검증 기준

reference fixture는 실제 Initialize/Prepare/Declare/Execute 중 무엇을 수행했는지 적는다. CPU 계획 검사와 shader/GPU readback을 구분하고 current SceneHost와 비교할 때 입력 geometry/material/shader 세대를 봉인한다.

과거 로컬 Build/Artifacts 경로의 수치와 완료·기성 표시는 이번 소스 감사에서 재검증하지 않았으므로 현재 통과 결과로 옮기지 않는다. 제품 기본값, 특정 fixture 성공, 여러 backend의 제품 수용은 각각 판정한다.

## 소스 근거

- [Engine/RenderEngine/MaterialGraphIblBake.cpp](../../Engine/RenderEngine/MaterialGraphIblBake.cpp)
- [Engine/RenderEngine/MaterialGraphSurfaceBatch.cpp](../../Engine/RenderEngine/MaterialGraphSurfaceBatch.cpp)
- [Engine/RenderEngine/MaterialGraphRasterSurface.cpp](../../Engine/RenderEngine/MaterialGraphRasterSurface.cpp)
- [Engine/RenderEngine/MaterialGraphSceneHost.cpp](../../Engine/RenderEngine/MaterialGraphSceneHost.cpp)
