# DLSS Native AA 품질 경계

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 현행 계약

TemporalQuality::NativeAA는 DLSS eDLAA로 매핑된다. 이는 provider=None의 native fallback과 다른 SDK 실행이다. display와 render 크기가 같더라도 jitter·history·SDK 평가와 temporal AA 관찰이 필요하다.

## 재현·수용 조건

Native AA 초기화와 첫 이동, 해상도 변화, native↔NativeAA 전환을 검사한다. 출력 크기 같음만으로 golden/native baseline에 포함하지 말고 activeUpscaler와 provenance를 확인한다.

## 기록의 범위

[시간축 재구성 정본](../plans/TemporalReconstructionPlan.md)과 함께 읽는다. 이 파일명은 이전 검사 주제로 연결되는 경로를 유지한다. 과거 특정 GPU·해상도·바이너리의 성공 표본, 삭제된 원본 픽셀과 보존 manifest를 현재 전체 제품 수용으로 확대하지 않는다. 이번 개정에서는 새 실행·측정 결과를 만들지 않았다.

## 소스 근거

- [Engine/RenderEngine/RHI/Temporal/DlssTemporalAdapter.cpp](../../Engine/RenderEngine/RHI/Temporal/DlssTemporalAdapter.cpp)
- [Engine/RenderEngine/Render/Scene/EnhancedTemporalViewState.h](../../Engine/RenderEngine/Render/Scene/EnhancedTemporalViewState.h)
- [Engine/RenderEngine/Render/Temporal/TemporalMeasurementProvenance.h](../../Engine/RenderEngine/Render/Temporal/TemporalMeasurementProvenance.h)

## 이전 기록

[기준 커밋의 개정 전 원문](https://github.com/29thnight/CreatorEngine/blob/493536e300e274cbf59989cdd62b20310c8ae64f/docs/analysis/TemporalDlssNativeAA20261011.md)에 당시 단계·세부 앵커·측정 이력이 남아 있다. 그 실행 결과를 현재 커밋의 수용으로 승계하지 않는다.
