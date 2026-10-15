# Golden 제외와 측정 wire 증거

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 현행 계약

TemporalMeasurementProvenance::IsGoldenEligible는 유효 real frame, native gate, TU/FG 없음, spatial scaling/DeepDVC 없음, render=display와 Native 상태를 요구한다. 테스트 fault는 acceptance evidence를 무효화한다.

## 재현·수용 조건

직렬화된 profiler/manifest 바이트를 변이해 누락·unknown·generated·불일치 해상도가 거부되는지 검사한다. UI에서 native를 요청한 것과 실제 저장된 native 증거는 구분한다.

## 기록의 범위

[시간축 재구성 정본](../plans/TemporalReconstructionPlan.md)과 함께 읽는다. 이 파일명은 이전 검사 주제로 연결되는 경로를 유지한다. 과거 특정 GPU·해상도·바이너리의 성공 표본, 삭제된 원본 픽셀과 보존 manifest를 현재 전체 제품 수용으로 확대하지 않는다. 이번 개정에서는 새 실행·측정 결과를 만들지 않았다.

## 소스 근거

- [Engine/RenderEngine/Render/Temporal/TemporalMeasurementProvenance.h](../../Engine/RenderEngine/Render/Temporal/TemporalMeasurementProvenance.h)
- [Tools/regression/TemporalGoldenExclusionOracle.py](../../Tools/regression/TemporalGoldenExclusionOracle.py)
- [Tools/regression/TemporalMeasurementProvenanceTests.cpp](../../Tools/regression/TemporalMeasurementProvenanceTests.cpp)
