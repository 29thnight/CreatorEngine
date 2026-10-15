# DLSS 품질·해상도 전환과 history

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 현행 계약

view Begin은 실제 render/display 크기, 재구성 설정 또는 provider 변화에서 history를 reset한다. TU host는 context를 바꿀 때 owner retirement를 확인하고 입력 실패 시 그 프레임을 버려 fallback extent로 다시 준비한다. 수동 exposure는 별도 graph resource로 봉인된다.

## 재현·수용 조건

요청 generation과 observed 제출 generation을 맞추고 resize 전후 실제 attachment 크기를 대조한다. 전환 중 실패·재시도에서 이전 history가 새 크기에 섞이지 않는지 확인한다. native/FSR/DLSS/XeSS 모든 지원 전환은 별도 행렬이다.

## 기록의 범위

[시간축 재구성 정본](../plans/TemporalReconstructionPlan.md)과 함께 읽는다. 이 파일명은 이전 검사 주제로 연결되는 경로를 유지한다. 과거 특정 GPU·해상도·바이너리의 성공 표본, 삭제된 원본 픽셀과 보존 manifest를 현재 전체 제품 수용으로 확대하지 않는다. 이번 개정에서는 새 실행·측정 결과를 만들지 않았다.

## 소스 근거

- [Engine/RenderEngine/Render/Scene/EnhancedTemporalViewState.h](../../Engine/RenderEngine/Render/Scene/EnhancedTemporalViewState.h)
- [Engine/RenderEngine/Render/Temporal/TemporalUpscalerHost.cpp](../../Engine/RenderEngine/Render/Temporal/TemporalUpscalerHost.cpp)
- [Engine/RenderEngine/Render/Passes/PostProcess/EnhancedTemporalUpscalePass.cpp](../../Engine/RenderEngine/Render/Passes/PostProcess/EnhancedTemporalUpscalePass.cpp)
- [Engine/RenderEngine/Render/Temporal/TemporalRuntimeControl.cpp](../../Engine/RenderEngine/Render/Temporal/TemporalRuntimeControl.cpp)
