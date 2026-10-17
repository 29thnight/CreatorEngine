# HDR 최초 발행과 SkyBox 소비

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 이 문서의 역할

HDR 최초 발행과 SkyBox 소비에 관한 과거 단계별 기록을 현행 계약으로 다시 정리한다. 공통 용어·수명·실행 모델은 [RenderGraph 정본](../plans/RenderGraphDependencySchedulingPlan.md)을 따른다. 날짜가 붙은 파일명은 링크 호환을 위해 유지한다.

## 현재 구현과 호출 경로

METEOR.Scene.Color가 DeclareHdrTarget으로 최초 HDR을 생성·초기화한다. SkyBox는 기존 color를 Modify하고 입력 depth를 읽는다. 독립 target을 쓰는 SkyBox 경로는 최초 Write를 사용한다. 일반 Deferred가 먼저 lighting을 만들고 LX가 보정한다는 옛 체인은 제거된 설명이다.

## 변경·검증 기준

SkyBox 비활성/숨김에서도 이전 LitColor가 유지되어야 한다. 연결·독립 출력에서 출력 버전과 depth 버전이 다르다는 점을 검사하고 environment 준비 실패와 배경 표시 비활성을 혼동하지 않는다.

과거 로컬 Build/Artifacts 경로의 수치와 완료·기성 표시는 이번 소스 감사에서 재검증하지 않았으므로 현재 통과 결과로 옮기지 않는다. 제품 기본값, 특정 fixture 성공, 여러 backend의 제품 수용은 각각 판정한다.

## 소스 근거

- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp](../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp)
- [Engine/RenderEngine/MaterialGraphSceneHost.cpp](../../Engine/RenderEngine/MaterialGraphSceneHost.cpp)
- [Engine/RenderEngine/Render/Passes/Lighting/EnhancedSkyBoxPass.cpp](../../Engine/RenderEngine/Render/Passes/Lighting/EnhancedSkyBoxPass.cpp)

## 이전 기록

[기준 커밋의 개정 전 원문](https://github.com/29thnight/CreatorEngine/blob/493536e300e274cbf59989cdd62b20310c8ae64f/docs/analysis/RenderRg5ConsumerMigration.md)에 당시 단계·세부 앵커·측정 이력이 남아 있다. 그 실행 결과를 현재 커밋의 수용으로 승계하지 않는다.
