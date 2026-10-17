# Decal snapshot과 후속 재질 입력

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 이 문서의 역할

Decal snapshot과 후속 재질 입력에 관한 과거 단계별 기록을 현행 계약으로 다시 정리한다. 공통 용어·수명·실행 모델은 [RenderGraph 정본](../plans/RenderGraphDependencySchedulingPlan.md)을 따른다. 날짜가 붙은 파일명은 링크 호환을 위해 유지한다.

## 현재 구현과 호출 경로

EnhancedDecalPass는 diffuse/normal/ORM snapshot을 Write하고 적용 결과를 Modify로 반환한다. BuildPipelineDesc의 Surface 노드는 이 결과와 decal baseline 세 핸들을 블랙보드에 게시한다. temporal decal 처리도 명시적 target 버전을 갱신한다. 없는 decal은 원래 입력을 통과시킨다.

## 변경·검증 기준

snapshot 이전값과 적용 뒤 최신값을 구별한다. 원본 texture 중복 import를 만들지 않으며 color/material/temporal 소비자가 맞는 버전을 읽는지 확인한다. decal 후보 수만으로 실제 GPU 합성 성공을 판정하지 않는다.

과거 로컬 Build/Artifacts 경로의 수치와 완료·기성 표시는 이번 소스 감사에서 재검증하지 않았으므로 현재 통과 결과로 옮기지 않는다. 제품 기본값, 특정 fixture 성공, 여러 backend의 제품 수용은 각각 판정한다.

## 소스 근거

- [Engine/RenderEngine/Render/Passes/Geometry/EnhancedDecalPass.cpp](../../Engine/RenderEngine/Render/Passes/Geometry/EnhancedDecalPass.cpp)
- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp](../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp)
- [Engine/RenderEngine/MaterialGraphSceneTemporalStage.inl](../../Engine/RenderEngine/MaterialGraphSceneTemporalStage.inl)

## 이전 기록

[기준 커밋의 개정 전 원문](https://github.com/29thnight/CreatorEngine/blob/493536e300e274cbf59989cdd62b20310c8ae64f/docs/analysis/RenderRg5DecalMigration.md)에 당시 단계·세부 앵커·측정 이력이 남아 있다. 그 실행 결과를 현재 커밋의 수용으로 승계하지 않는다.
