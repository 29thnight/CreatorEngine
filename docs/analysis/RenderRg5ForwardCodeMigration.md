# 빛 타일과 반투명 재질 명령

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 이 문서의 역할

빛 타일과 반투명 재질 명령에 관한 과거 단계별 기록을 현행 계약으로 다시 정리한다. 공통 용어·수명·실행 모델은 [RenderGraph 정본](../plans/RenderGraphDependencySchedulingPlan.md)을 따른다. 날짜가 붙은 파일명은 링크 호환을 위해 유지한다.

## 현재 구현과 호출 경로

현재 light tile 생산은 EnhancedLightTilePass가 counts/indices의 Write 버전을 만든다. METEOR.Scene.LayerWork와 Blended가 ordered meshlet 목록과 layer/range command를 소비한다. 과거 Code/Graph를 EnhancedForwardPass의 별도 stream으로 조합한다는 설명 대신 현재 SceneHost의 sealed 재질·bucket 계약을 따른다.

## 변경·검증 기준

반투명 back-to-front 객체 순서와 source meshlet 순서 둘 다 확인한다. tile output을 최신 color·shadow 입력과 함께 넘기고 output handle을 선언 시 값으로 봉인한다. 옛 forward guard 해제 기록을 현행 전체 투명 재질 지원으로 확대하지 않는다.

과거 로컬 Build/Artifacts 경로의 수치와 완료·기성 표시는 이번 소스 감사에서 재검증하지 않았으므로 현재 통과 결과로 옮기지 않는다. 제품 기본값, 특정 fixture 성공, 여러 backend의 제품 수용은 각각 판정한다.

## 소스 근거

- [Engine/RenderEngine/Render/Passes/Lighting/EnhancedLightTilePass.cpp](../../Engine/RenderEngine/Render/Passes/Lighting/EnhancedLightTilePass.cpp)
- [Engine/RenderEngine/MaterialGraphSceneLayerStage.inl](../../Engine/RenderEngine/MaterialGraphSceneLayerStage.inl)
- [Engine/RenderEngine/MaterialGraphSceneHost.cpp](../../Engine/RenderEngine/MaterialGraphSceneHost.cpp)
- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp](../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp)

## 이전 기록

[기준 커밋의 개정 전 원문](https://github.com/29thnight/CreatorEngine/blob/493536e300e274cbf59989cdd62b20310c8ae64f/docs/analysis/RenderRg5ForwardCodeMigration.md)에 당시 단계·세부 앵커·측정 이력이 남아 있다. 그 실행 결과를 현재 커밋의 수용으로 승계하지 않는다.
