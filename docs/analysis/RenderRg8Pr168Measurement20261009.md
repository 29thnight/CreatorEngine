# 제품 큐 성능 측정의 재현 조건

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 이 문서의 역할

제품 큐 성능 측정의 재현 조건에 관한 과거 단계별 기록을 현행 계약으로 다시 정리한다. 공통 용어·수명·실행 모델은 [RenderGraph 정본](../plans/RenderGraphDependencySchedulingPlan.md)을 따른다. 날짜가 붙은 파일명은 링크 호환을 위해 유지한다.

## 현재 구현과 호출 경로

현재 planner는 유효한 전체 pass 비용이 없으면 missing-measurement로 graphics fallback한다. 과거 특정 TestShadow 실행의 missing signature 원인·수치가 현행 바이너리에서도 같다고 가정할 수 없다. 구조 신원과 실제 normal-domain 비용 채택 여부를 먼저 확인해야 한다.

## 변경·검증 기준

현재 소스·shader·runtime·장치·드라이버와 viewport 크기를 봉인한다. compile/prepare/schedule/record/submit 및 GPU span을 나누고 느린 정상 표본을 임의 제거하지 않는다. correctness가 통과해도 성능 채택은 별도 판정이다.

과거 로컬 Build/Artifacts 경로의 수치와 완료·기성 표시는 이번 소스 감사에서 재검증하지 않았으므로 현재 통과 결과로 옮기지 않는다. 제품 기본값, 특정 fixture 성공, 여러 backend의 제품 수용은 각각 판정한다.

## 소스 근거

- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp](../../Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp)
- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp](../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp)
- [Tools/regression/measure-rg8-adoption.ps1](../../Tools/regression/measure-rg8-adoption.ps1)
- [Tools/regression/audit-rg8-adoption.py](../../Tools/regression/audit-rg8-adoption.py)
