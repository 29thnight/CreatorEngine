# 카메라 렌즈와 시간축 history

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 현행 계약

FrameCameraSnapshot::HasSameTemporalHistory는 cameraCutRevision, jitter 없는 projection, orthographic 여부, FOV, near/far를 마지막 제출 카메라와 비교한다. view state는 이 비교 실패를 reset 조건에 넣는다. 단순 camera transform 이동은 motion으로 처리되며 렌즈 변경과 구분한다.

## 재현·수용 조건

FOV와 near/far 변경이 실제 projection/depth와 SDK camera 값에 반영되는지 검사한다. 같은 값을 다시 설정한 no-op은 불필요한 reset을 만들지 않아야 한다. camera cut 없이 렌즈만 바뀌는 사례도 포함한다.

## 기록의 범위

[시간축 재구성 정본](../plans/TemporalReconstructionPlan.md)과 함께 읽는다. 이 파일명은 이전 검사 주제로 연결되는 경로를 유지한다. 과거 특정 GPU·해상도·바이너리의 성공 표본, 삭제된 원본 픽셀과 보존 manifest를 현재 전체 제품 수용으로 확대하지 않는다. 이번 개정에서는 새 실행·측정 결과를 만들지 않았다.

## 소스 근거

- [Engine/RenderEngine/FrameCameraSnapshot.h](../../Engine/RenderEngine/FrameCameraSnapshot.h)
- [Engine/RenderEngine/Render/Scene/EnhancedTemporalViewState.h](../../Engine/RenderEngine/Render/Scene/EnhancedTemporalViewState.h)
- [Engine/RenderEngine/RHI/Temporal/DlssTemporalAdapter.cpp](../../Engine/RenderEngine/RHI/Temporal/DlssTemporalAdapter.cpp)
- [Tools/temporal-validation/Compare-TemporalLens.py](../../Tools/temporal-validation/Compare-TemporalLens.py)
