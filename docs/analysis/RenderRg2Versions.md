# 리소스 버전과 Modify 계약

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 이 문서의 역할

리소스 버전과 Modify 계약에 관한 과거 단계별 기록을 현행 계약으로 다시 정리한다. 공통 용어·수명·실행 모델은 [RenderGraph 정본](../plans/RenderGraphDependencySchedulingPlan.md)을 따른다. 날짜가 붙은 파일명은 링크 호환을 위해 유지한다.

## 현재 구현과 호출 경로

RGHandle은 index/version/kind/epoch를 포함한다. import는 외부 초기 v0, transient는 미초기화 선언으로 시작한다. Write는 내용을 새로 쓰고 Modify는 직전 내용을 보존해 갱신한다. BuildExplicitOrder는 중복 물리 import, stale parent, writer 누락과 잘못된 access/state를 거부한다. 버전은 GPU allocation을 복제하는 명령이 아니다.

## 변경·검증 기준

핸들 IsValid만 믿지 말고 graph epoch와 실제 producer를 확인한다. imported v0 직접 덮어쓰기와 같은 pass의 중복 resource usage는 수정해야 한다. AtomicMin64는 별도 제한 결합 쓰기 계약이며 일반 다중 writer 허용이 아니다.

과거 로컬 Build/Artifacts 경로의 수치와 완료·기성 표시는 이번 소스 감사에서 재검증하지 않았으므로 현재 통과 결과로 옮기지 않는다. 제품 기본값, 특정 fixture 성공, 여러 backend의 제품 수용은 각각 판정한다.

## 소스 근거

- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h](../../Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h)
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp](../../Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp)
- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp](../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp)

## 이전 기록

[기준 커밋의 개정 전 원문](https://github.com/29thnight/CreatorEngine/blob/493536e300e274cbf59989cdd62b20310c8ae64f/docs/analysis/RenderRg2Versions.md)에 당시 단계·세부 앵커·측정 이력이 남아 있다. 그 실행 결과를 현재 커밋의 수용으로 승계하지 않는다.
