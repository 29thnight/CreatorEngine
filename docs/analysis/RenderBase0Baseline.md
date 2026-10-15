# 렌더 변경 전 기준선의 재현 계약

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 무엇을 고정하는가

현재 기준선은 특정 과거 실행 수치가 아니라 재현 가능한 입력과 측정 의미다. source commit, build configuration, executable/runtime/shader hash, scene·material·geometry generation, backend·GPU·driver와 render/display extent를 기록한다. 현재 제품 배선은 METEOR SceneHost와 ExplicitVersioned graph다.

## 프레임과 측정

realFrameId·viewId·sceneEpoch·publicationFrameId와 GPU submission token을 구분한다. native golden은 TU/FG/NIS/DeepDVC를 실제 제출 provenance에서 제외해야 하며 요청 설정만 검사하지 않는다. capture/readback이 추가된 그래프와 normal-domain GPU history를 분리한다.

CPU prepare/compile/record/schedule/submit, GPU pass sum·queue critical span과 최종 frame/present cadence는 각각 다른 지표다. 비동기 CPU/GPU 값을 임의 합산하지 않는다. 정상 표본을 제거하는 기준과 워밍업·독립 process 반복·실패 처리 규칙을 미리 정한다.

## 회귀 판정

최종 color뿐 아니라 depth/normal/material/temporal 등 관련 attachment, graph topology·resource versions·barrier/queue plan, validation과 정상 종료를 확인한다. 의도적인 잘못된 evidence·누락 producer·보이는 geometry 누락 변이가 검사기를 실패시켜야 한다.

Tools/regression/base0_artifacts.py와 rg6_acceptance.py는 증거 소비 코드다. 이것들이 현재 입력을 실제로 실행하고 통과했다는 뜻은 아니다. 이번 문서 개정은 새 성능 기준값이나 전체 제품 수용을 만들지 않는다.

## 소스 근거

- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp](../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp)
- [Engine/RenderEngine/Render/Temporal/TemporalMeasurementProvenance.h](../../Engine/RenderEngine/Render/Temporal/TemporalMeasurementProvenance.h)
- [Tools/regression/base0_artifacts.py](../../Tools/regression/base0_artifacts.py)
- [Tools/regression/rg6_acceptance.py](../../Tools/regression/rg6_acceptance.py)
