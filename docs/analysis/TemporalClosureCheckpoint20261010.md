# 시간축 통합 단계의 완료 판정

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 현행 계약

TU adapter와 live inputs, per-view history, Player presentation은 서로 다른 책임이다. renderer는 selected/active provider와 실제 제출을 구분한다. configuration success나 capability query만으로 SDK 평가·화면 품질을 완료로 표시하지 않는다.

## 재현·수용 조건

SDK 없는 build, runtime 누락, 지원 없는 API/장치, native fallback, 첫 제출과 실패 후 복구를 각각 확인한다. 과거 특정 PR 종료가 이후 METEOR geometry/motion 변경의 회귀 수용을 대신하지 않는다.

## 기록의 범위

[시간축 재구성 정본](../plans/TemporalReconstructionPlan.md)과 함께 읽는다. 이 파일명은 이전 검사 주제로 연결되는 경로를 유지한다. 과거 특정 GPU·해상도·바이너리의 성공 표본, 삭제된 원본 픽셀과 보존 manifest를 현재 전체 제품 수용으로 확대하지 않는다. 이번 개정에서는 새 실행·측정 결과를 만들지 않았다.

## 소스 근거

- [Engine/RenderEngine/Render/Temporal/TemporalReconstruction.h](../../Engine/RenderEngine/Render/Temporal/TemporalReconstruction.h)
- [Engine/RenderEngine/Render/Temporal/TemporalUpscalerHost.cpp](../../Engine/RenderEngine/Render/Temporal/TemporalUpscalerHost.cpp)
- [Engine/RenderEngine/Render/Scene/EnhancedTemporalViewState.h](../../Engine/RenderEngine/Render/Scene/EnhancedTemporalViewState.h)
- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp](../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp)
