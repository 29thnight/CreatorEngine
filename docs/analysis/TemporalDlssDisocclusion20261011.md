# DLSS 첫 이동·가림 해제 검사

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 현행 계약

DLSS adapter는 다섯 품질의 preset을 J로 지정한다. TemporalUpscalePass는 엔진 수동 노출과 일치하는 1×1 R32Float exposure를 만들어 전달하고 adapter는 exposure 유무에 따라 auto exposure를 정한다. current→previous motion, history reset과 실제 제출 frame identity는 EnhancedTemporalViewState에서 온다.

## 재현·수용 조건

첫 reset 프레임 다음 이동·사라짐·회복을 warmed/startup으로 나눠 캡처한다. motion 방향·jitter 제거·depth·exposure·provider 실제 활성 여부를 먼저 확인하고 HDR와 display 차이를 나눠 비교한다. 특정 preset 선택이 모든 장면의 잔상 제거를 보장하지 않는다.

## 기록의 범위

[시간축 재구성 정본](../plans/TemporalReconstructionPlan.md)과 함께 읽는다. 이 파일명은 이전 검사 주제로 연결되는 경로를 유지한다. 과거 특정 GPU·해상도·바이너리의 성공 표본, 삭제된 원본 픽셀과 보존 manifest를 현재 전체 제품 수용으로 확대하지 않는다. 이번 개정에서는 새 실행·측정 결과를 만들지 않았다.

## 소스 근거

- [Engine/RenderEngine/RHI/Temporal/DlssTemporalAdapter.cpp](../../Engine/RenderEngine/RHI/Temporal/DlssTemporalAdapter.cpp)
- [Engine/RenderEngine/Render/Passes/PostProcess/EnhancedTemporalUpscalePass.cpp](../../Engine/RenderEngine/Render/Passes/PostProcess/EnhancedTemporalUpscalePass.cpp)
- [Engine/RenderEngine/Render/Scene/EnhancedTemporalViewState.h](../../Engine/RenderEngine/Render/Scene/EnhancedTemporalViewState.h)
