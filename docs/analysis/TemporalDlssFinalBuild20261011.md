# DLSS 최종 바이너리 증거의 일치

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 현행 계약

현재 DLSS 동작은 adapter preset/exposure, view history와 live submit 배선에 함께 의존한다. 같은 문서에 서로 다른 소스 revision·runtime DLL·shader 산출물 결과를 합치면 최종 바이너리 수용이 성립하지 않는다.

## 재현·수용 조건

startup/warmed/전환/lens 검사를 재사용하려면 source·실행 파일·runtime·shader hash와 입력 scene이 최종 대상과 일치해야 한다. 이전 binary의 pass를 현재 재빌드 성공으로 대신하지 않는다.

## 기록의 범위

[시간축 재구성 정본](../plans/TemporalReconstructionPlan.md)과 함께 읽는다. 이 파일명은 이전 검사 주제로 연결되는 경로를 유지한다. 과거 특정 GPU·해상도·바이너리의 성공 표본, 삭제된 원본 픽셀과 보존 manifest를 현재 전체 제품 수용으로 확대하지 않는다. 이번 개정에서는 새 실행·측정 결과를 만들지 않았다.

## 소스 근거

- [Engine/RenderEngine/RHI/Temporal/DlssTemporalAdapter.cpp](../../Engine/RenderEngine/RHI/Temporal/DlssTemporalAdapter.cpp)
- [Engine/RenderEngine/Render/Passes/PostProcess/EnhancedTemporalUpscalePass.cpp](../../Engine/RenderEngine/Render/Passes/PostProcess/EnhancedTemporalUpscalePass.cpp)
- [Engine/RenderEngine/Render/Scene/EnhancedTemporalViewState.h](../../Engine/RenderEngine/Render/Scene/EnhancedTemporalViewState.h)
- [Tools/temporal-validation/Verify-TemporalSdkSequence.ps1](../../Tools/temporal-validation/Verify-TemporalSdkSequence.ps1)

## 이전 기록

[기준 커밋의 개정 전 원문](https://github.com/29thnight/CreatorEngine/blob/493536e300e274cbf59989cdd62b20310c8ae64f/docs/analysis/TemporalDlssFinalBuild20261011.md)에 당시 단계·세부 앵커·측정 이력이 남아 있다. 그 실행 결과를 현재 커밋의 수용으로 승계하지 않는다.
