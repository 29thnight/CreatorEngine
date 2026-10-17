# 시간축 측정 소비자와 비용 단위

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 현행 계약

provenance는 제출/완료된 이미지와 함께 복사되며 나중의 global settings로 옛 GPU 측정을 재표기하지 않는다. profiler rendering 진단과 pixel 비교 소비자는 실제 display/view/publication 및 native gate 계약을 확인한다.

## 재현·수용 조건

CPU record, GPU pass 합, queue critical span, 실제 frame 시간·present cadence를 별도 열로 남긴다. 실프레임과 보간 프레임을 한 FPS 분모로 섞지 않는다. Player recording·외부 도구·material pixel 검사도 같은 증거 경계를 적용한다.

## 기록의 범위

[시간축 재구성 정본](../plans/TemporalReconstructionPlan.md)과 함께 읽는다. 이 파일명은 이전 검사 주제로 연결되는 경로를 유지한다. 과거 특정 GPU·해상도·바이너리의 성공 표본, 삭제된 원본 픽셀과 보존 manifest를 현재 전체 제품 수용으로 확대하지 않는다. 이번 개정에서는 새 실행·측정 결과를 만들지 않았다.

## 소스 근거

- [Engine/RenderEngine/Render/Temporal/TemporalMeasurementProvenance.h](../../Engine/RenderEngine/Render/Temporal/TemporalMeasurementProvenance.h)
- [Engine/EngineDiagnostics/ProfilerRenderingDiagnostics.cpp](../../Engine/EngineDiagnostics/ProfilerRenderingDiagnostics.cpp)
- [Tools/regression/base0_artifacts.py](../../Tools/regression/base0_artifacts.py)
- [Tools/temporal-validation/analyze-render-budget.py](../../Tools/temporal-validation/analyze-render-budget.py)

## 이전 기록

[기준 커밋의 개정 전 원문](https://github.com/29thnight/CreatorEngine/blob/493536e300e274cbf59989cdd62b20310c8ae64f/docs/analysis/TemporalMeasurementConsumers20261010.md)에 당시 단계·세부 앵커·측정 이력이 남아 있다. 그 실행 결과를 현재 커밋의 수용으로 승계하지 않는다.
