# Scene 통계·ProfilerViewer·픽셀 비교의 신원

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 현행 계약

matches_gpu_measurement는 표시 대상과 GPU 측정의 신원을 대조하고 ProfilerViewer가 이를 소비한다. animation, IK, material capture와 environment visibility 비교기는 audit_pixel_measurement를 먼저 호출한다. 부분 attachment만 비교해도 temporal 증거 검사를 건너뛰지 않는다.

## 재현·수용 조건

resize·다른 view·다른 publication·unknown/generated 표본을 주입해 거부를 확인한다. UI cache의 오래된 측정을 현재 프레임 값처럼 표시하지 않으며 지원 codec과 evidence schema를 구분한다.

## 기록의 범위

[시간축 재구성 정본](../plans/TemporalReconstructionPlan.md)과 함께 읽는다. 이 파일명은 이전 검사 주제로 연결되는 경로를 유지한다. 과거 특정 GPU·해상도·바이너리의 성공 표본, 삭제된 원본 픽셀과 보존 manifest를 현재 전체 제품 수용으로 확대하지 않는다. 이번 개정에서는 새 실행·측정 결과를 만들지 않았다.

## 소스 근거

- [Engine/RenderEngine/Render/Temporal/TemporalMeasurementProvenance.h](../../Engine/RenderEngine/Render/Temporal/TemporalMeasurementProvenance.h)
- [Engine/EngineDiagnostics/ProfilerRenderingDiagnostics.cpp](../../Engine/EngineDiagnostics/ProfilerRenderingDiagnostics.cpp)
- [Tools/ProfilerViewer/Presentation/ProfilerRendering.cpp](../../Tools/ProfilerViewer/Presentation/ProfilerRendering.cpp)
- [Tools/regression/base0_artifacts.py](../../Tools/regression/base0_artifacts.py)

## 이전 기록

[기준 커밋의 개정 전 원문](https://github.com/29thnight/CreatorEngine/blob/493536e300e274cbf59989cdd62b20310c8ae64f/docs/analysis/TemporalMeasurementSurfaces20261011.md)에 당시 단계·세부 앵커·측정 이력이 남아 있다. 그 실행 결과를 현재 커밋의 수용으로 승계하지 않는다.
