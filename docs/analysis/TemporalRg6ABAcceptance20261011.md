# Temporal 제외 RG6 A/B의 비교 범위

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 현행 계약

현재 제품 graph는 ExplicitVersioned이며 METEOR의 geometry·hybrid visibility·recovery 단계가 실제 workload를 구성한다. 예전 DeclarationOrder/reference 실행과 pass 수가 다르면 순수 scheduler 비용 비교가 아니다. provenance의 native gate로 TU/FG/spatial 효과 제외를 확인한다.

## 재현·수용 조건

같은 source·scene·runtime·GPU validation 조건에서 독립 반복을 수집하고 각 graph topology를 저장한다. CPU와 GPU 신원을 따로 보존하며 일부 대표 attachment 비교를 전체 모든 쌍 비교라고 부르지 않는다.

## 기록의 범위

[시간축 재구성 정본](../plans/TemporalReconstructionPlan.md)과 함께 읽는다. 이 파일명은 이전 검사 주제로 연결되는 경로를 유지한다. 과거 특정 GPU·해상도·바이너리의 성공 표본, 삭제된 원본 픽셀과 보존 manifest를 현재 전체 제품 수용으로 확대하지 않는다. 이번 개정에서는 새 실행·측정 결과를 만들지 않았다.

## 소스 근거

- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp](../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp)
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp](../../Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp)
- [Engine/RenderEngine/Render/Temporal/TemporalMeasurementProvenance.h](../../Engine/RenderEngine/Render/Temporal/TemporalMeasurementProvenance.h)
- [Tools/regression/rg6_acceptance.py](../../Tools/regression/rg6_acceptance.py)

## 이전 기록

[기준 커밋의 개정 전 원문](https://github.com/29thnight/CreatorEngine/blob/493536e300e274cbf59989cdd62b20310c8ae64f/docs/analysis/TemporalRg6ABAcceptance20261011.md)에 당시 단계·세부 앵커·측정 이력이 남아 있다. 그 실행 결과를 현재 커밋의 수용으로 승계하지 않는다.
