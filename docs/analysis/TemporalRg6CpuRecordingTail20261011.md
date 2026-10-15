# CPU 기록 tail과 구조 스케줄 재사용

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 현행 계약

현행 Compile은 정확한 structural key가 같을 때 order/waves/edges/culling 메타데이터를 재사용한다. RecordParallel은 single-worker wave 구간을 별도로 처리해 불필요한 dispatch를 줄이는 경로가 있다. 이는 GPU 작업량을 줄였다는 뜻이 아니다.

## 재현·수용 조건

normal-domain 동일 workload에서 중앙값·tail·최대와 독립 반복을 비교한다. Grid/overlay 같은 끝단 pass도 준비된 불변 input인지 확인한다. 과거 v3/v6 감소율을 현재 commit의 수치로 인용하지 않는다.

## 기록의 범위

[시간축 재구성 정본](../plans/TemporalReconstructionPlan.md)과 함께 읽는다. 이 파일명은 이전 검사 주제로 연결되는 경로를 유지한다. 과거 특정 GPU·해상도·바이너리의 성공 표본, 삭제된 원본 픽셀과 보존 manifest를 현재 전체 제품 수용으로 확대하지 않는다. 이번 개정에서는 새 실행·측정 결과를 만들지 않았다.

## 소스 근거

- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp](../../Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp)
- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp](../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp)
- [Engine/RenderEngine/Render/Temporal/TemporalMeasurementProvenance.h](../../Engine/RenderEngine/Render/Temporal/TemporalMeasurementProvenance.h)
