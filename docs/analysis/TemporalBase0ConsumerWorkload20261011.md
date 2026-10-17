# BASE-0 실제 소비 workload

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 현행 계약

BASE-0 측정은 native capture exclusion과 real-frame provenance를 확인해야 한다. renderer의 CPU prepared draw/batch 수와 실제 GPU workload는 다르며 authored plane 한 개와 대규모 material population은 동일 fixture가 아니다.

## 재현·수용 조건

population·재질·shader·geometry 세대, visible/caster 수와 실제 pass topology를 고정한다. 같은 frame의 CPU record와 GPU 비동기 표본은 source identity를 따로 기록한다. scene ready가 아닌 fallback 이미지를 측정에 포함하지 않는다.

## 기록의 범위

[시간축 재구성 정본](../plans/TemporalReconstructionPlan.md)과 함께 읽는다. 이 파일명은 이전 검사 주제로 연결되는 경로를 유지한다. 과거 특정 GPU·해상도·바이너리의 성공 표본, 삭제된 원본 픽셀과 보존 manifest를 현재 전체 제품 수용으로 확대하지 않는다. 이번 개정에서는 새 실행·측정 결과를 만들지 않았다.

## 소스 근거

- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp](../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp)
- [Engine/RenderEngine/Render/Temporal/TemporalMeasurementProvenance.h](../../Engine/RenderEngine/Render/Temporal/TemporalMeasurementProvenance.h)
- [Tools/regression/new-temporal-base0-fixture.ps1](../../Tools/regression/new-temporal-base0-fixture.ps1)
- [Tools/regression/verify-temporal-base0-workload.ps1](../../Tools/regression/verify-temporal-base0-workload.ps1)

## 이전 기록

[기준 커밋의 개정 전 원문](https://github.com/29thnight/CreatorEngine/blob/493536e300e274cbf59989cdd62b20310c8ae64f/docs/analysis/TemporalBase0ConsumerWorkload20261011.md)에 당시 단계·세부 앵커·측정 이력이 남아 있다. 그 실행 결과를 현재 커밋의 수용으로 승계하지 않는다.
