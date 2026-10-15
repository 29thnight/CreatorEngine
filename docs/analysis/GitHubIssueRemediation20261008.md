# 2026-10-08 이슈 조치 기록을 현재 코드에서 읽는 법

> 기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스 정적 조사. 이 문서의 재작성에서 빌드·실행·성능 측정은 하지 않았다.

## 이슈 상태와 구현 상태

이 문서는 과거 이슈 조치 보고의 호환 진입점이다. GitHub의 현재 open/closed 상태나 모든 회귀 통과를 새로 조회·인증하지 않았다. 현재 검토 기준은 오류가 발생한 owner와 실제 consumer의 연결이다. 특정 PR에서 수정했다는 선언이나 build 성공만으로 런타임 실패 경로를 닫지 않는다.

## 현재 소스에서 유지하는 세 경계

첫째, recording과 submission acceptance를 구분한다. graph compile이나 command 기록만으로 temporal history·IBL ready 상태를 확정할 수 없다. 실패·abort 시 후보 메타데이터를 폐기하고 accepted submission과 완료 token에 맞춰 소유권을 이동해야 한다. 관련 구현은 EnhancedRenderGraph와 renderer의 프레임 상태에 있다.

둘째, 자원 퇴출과 native 파괴를 분리한다. RHIAssetEvictionPolicy의 retiredBytes는 completion 대기 상태로 옮긴 논리 바이트다. RHICompletionRetireQueue는 유효한 완료 값에 도달해야 Collect하며 값 0은 quarantine으로 남겨 device teardown의 Drain까지 보존한다. 이 정책이 실제 cache와 display consumer에 연결되는지도 따로 확인해야 한다.

셋째, Editor 복구와 Player strict load를 분리한다. DataSystem의 graph recovery는 authoring owner에서 새 graph를 게시한다. temporary prewarm은 게시를 미루며 mounted Player 실패를 원본 재임포트로 우회하지 않는다.

## 아직 필요한 증거

동일 작업량에서의 CPU/allocation 비교, BeginFrame 중간 실패, cross-device lease·resize·device-loss, 유효 handle과 nonempty error가 함께 오는 consumer 경로는 각각 targeted 실행 증거가 필요하다. 현재 소스 분기가 존재한다는 사실을 장치별 통과로 올리지 않는다.

과거 #150, #158, #160 등의 실패 주제는 이 경계를 설명하는 역사적 식별자다. live 이슈 상태는 별도 조회가 필요하다. 현행 정본은 [표시 소유권](../design/OwnedBoundedPresentation.md), [비동기 준비](../design/AsyncAssetPreparation.md), [렌더 roadmap](../plans/RenderPhaseRoadmap.md)이다. 당시 JSON·이미지·측정 자료는 변경하지 않는다.

## 구현 근거

- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h](../../Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h)
- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp](../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp)
- [Engine/RenderEngine/RHI/RHIAssetEvictionPolicy.h](../../Engine/RenderEngine/RHI/RHIAssetEvictionPolicy.h)
- [Engine/RenderEngine/RHI/RHICompletionRetireQueue.h](../../Engine/RenderEngine/RHI/RHICompletionRetireQueue.h)
- [Engine/RenderEngine/DataSystem.cpp](../../Engine/RenderEngine/DataSystem.cpp)
- [Player/PlayerDX12Presentation.cpp](../../Player/PlayerDX12Presentation.cpp)
