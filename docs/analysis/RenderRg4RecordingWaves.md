# 의존 wave와 병렬 command 기록

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 이 문서의 역할

의존 wave와 병렬 command 기록에 관한 과거 단계별 기록을 현행 계약으로 다시 정리한다. 공통 용어·수명·실행 모델은 [RenderGraph 정본](../plans/RenderGraphDependencySchedulingPlan.md)을 따른다. 날짜가 붙은 파일명은 링크 호환을 위해 유지한다.

## 현재 구현과 호출 경로

BuildDependencyWaves는 GPU 의존 DAG의 wave·critical path를 만들고 RecordParallel은 command target의 append 순서와 CPU callback 독립성까지 고려해 recording wave를 만든다. critical path의 패스 개수는 GPU ms가 아니다. DeclareRecordingIndependent로 준비된 불변 callback을 표시할 수 있고 CPU side effect는 별도 순서 제약을 갖는다.

## 변경·검증 기준

한 pass의 slice가 공유 mutable state를 쓰는지 확인한다. worker 수를 늘렸다는 사실만으로 병렬성이 생긴다고 판단하지 않고 dependencyWaveCount·recordingWaveCount·실제 worker 구간·wall time을 분리한다. command pool과 descriptor 세대가 바뀐 prepared packet은 재사용하지 않는다.

과거 로컬 Build/Artifacts 경로의 수치와 완료·기성 표시는 이번 소스 감사에서 재검증하지 않았으므로 현재 통과 결과로 옮기지 않는다. 제품 기본값, 특정 fixture 성공, 여러 backend의 제품 수용은 각각 판정한다.

## 소스 근거

- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h](../../Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h)
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp](../../Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp)
- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp](../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp)
