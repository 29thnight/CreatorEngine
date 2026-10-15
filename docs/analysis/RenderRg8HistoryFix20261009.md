# 측정 history의 구조 신원

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 이 문서의 역할

측정 history의 구조 신원에 관한 과거 단계별 기록을 현행 계약으로 다시 정리한다. 공통 용어·수명·실행 모델은 [RenderGraph 정본](../plans/RenderGraphDependencySchedulingPlan.md)을 따른다. 날짜가 붙은 파일명은 링크 호환을 위해 유지한다.

## 현재 구현과 호출 경로

EnhancedGpuMeasurementHistory는 normal/capture domain을 구분하고 정확한 graph signature·view·frame·신선도를 확인한다. SSGI는 물리 ping-pong slot과 현재 read/write 역할이 다르다. 구조 key가 frame마다 불필요하게 바뀌면 유효 비용이 있어도 다음 계획에서 거부될 수 있다.

## 변경·검증 기준

signature 차이를 실제 declaration 순서·버전·입력 의미로 추적한다. 일치시키려고 서로 다른 graph variant의 비용을 합치지 않는다. reset/resize/provider 변경 뒤의 stale 측정은 그대로 거부해야 한다.

과거 로컬 Build/Artifacts 경로의 수치와 완료·기성 표시는 이번 소스 감사에서 재검증하지 않았으므로 현재 통과 결과로 옮기지 않는다. 제품 기본값, 특정 fixture 성공, 여러 backend의 제품 수용은 각각 판정한다.

## 소스 근거

- [Engine/RenderEngine/Render/Graph/EnhancedGpuMeasurementHistory.h](../../Engine/RenderEngine/Render/Graph/EnhancedGpuMeasurementHistory.h)
- [Engine/RenderEngine/Render/Passes/Lighting/EnhancedSSGIPass.cpp](../../Engine/RenderEngine/Render/Passes/Lighting/EnhancedSSGIPass.cpp)
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp](../../Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp)
