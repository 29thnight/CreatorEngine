# 현재 라이브 렌더 파이프라인

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 호스트에서 제출까지

Editor의 EngineEntry/App.cpp와 PlayerApp.cpp는 BuildLiveFramePacket으로 프레임 입력을 만들고 PublishLiveFrame으로 넘긴다. renderer가 소비하는 입력에는 프레임·씬·논리 뷰 신원, 카메라 snapshot, 자산과 재질 입력이 있다. 게임/편집 객체를 렌더 콜백에서 다시 임의 조회하는 방식으로 확장하지 않는다.

EnhancedSceneRenderer의 LiveState가 render-owned 상태를 갖는다. BuildPipelineDesc는 영속 노드 기술을 만들고, 각 뷰의 RenderOnce는 준비된 자산·프레임에서 새 EnhancedRenderGraph를 선언·컴파일·기록·제출한다. 이미 제출한 그래프는 불변으로 유지하며, 구조 스케줄 캐시는 메타데이터만 다음 그래프로 넘긴다.

## 현재 노드 배선

- Geometry는 현재·이전 pose의 고유 정점 생산자와 프레임 geometry table을 선언한다
- Cull.First는 첫 카메라 메시렛 목록을 발행하고 Occluder가 그 목록으로 HW/SW 혼합 깊이를 만든다
- Cull.Final은 그 깊이의 HZB를 읽어 불투명 복구 목록을 발행한다. 반투명 목록은 별도 frustum 검사와 원본 순서 보존을 사용한다
- LayerWork는 반투명 목록의 명령을 준비한다. Shadow는 Geometry와 광원 투영으로 독립적인 3개 cascade 목록·간접 명령·깊이를 선언한다
- Surface는 최종 가시성에서 GBuffer를 재구성하고 decal을 반영한다. SceneSurfaceTargets는 타깃 생성·클리어 책임만 가진다
- History는 최종 표면 깊이로 다음 프레임 카메라 HZB를 만들고, Temporal.Inputs는 실제 소비자 또는 진단 요청이 있을 때 motion/depth/mask를 만든다
- SSAO 뒤 METEOR.Scene.Color가 HDR LitColor를 처음 발행한다. SkyBox, SSGI, SSS, SSR, Volume, light tiles, Blended, Sprite, Fog와 temporal 처리가 해당 자원을 읽고 수정한다
- Temporal.Upscale은 tone mapping 전 HDR을 처리한다. PostChain과 SpatialPost 뒤 HUD 없는 입력을 보존하고 UI를 합성한다. Host 기여 overlay와 live_present가 최종 표시 경로를 완성한다

이 목록은 저작·슬롯 연결 순서다. 실행 순서는 ExplicitVersioned 그래프의 데이터·물리 저장소 의존성으로 정해진다. 모든 기능이 매 프레임 실행되는 것도 아니다. 비활성 modifies 노드는 이전 슬롯을 통과시키고, 소비되지 않는 transient 출력의 생산자는 그래프에서 제거될 수 있다.

## 데이터 계약과 소유권

LiveBlackboard는 RGHandle 또는 명시적 handle 목록을 옮긴다. 그림자 행렬·재질·카메라 같은 타입 있는 사이드 데이터는 해당 owner가 유지한다. SceneHost 단계는 전달받은 핸들이 그 단계가 실제 발행한 핸들인지 검사하며 누락·다른 그래프·다른 epoch를 거부한다. 빈 장면은 geometry 목록이 없더라도 HDR 생성·클리어와 표시가 가능하도록 별도로 처리한다.

SceneHost의 SelectReadyInput은 요청한 정확한 재질 표현과 coverage만 선택한다. pending 준비는 전체 프레임을 미루며 실패를 이전 재질의 성공으로 바꾸지 않는다. PrepareResidency와 Prepare는 기록 전 자산 운반·상수·geometry를 준비한다. PublishSubmittedCache는 제출 ticket 또는 이미 성공한 CPU admission과 completion을 받아 캐시 업로드를 게시한다.

## DX12와 Vulkan의 실행 경계

DX12 RenderOnce는 기본 병렬 기록 경로와 owned queue 경로를 구분한다. 기본 경로는 RecordParallel에서 sealed batch를 만들고 EnqueueRecordedBatch로 제출한다. owned 경로는 SubmitOwnedGraph로 queue plan을 실행한다. owned queue 실행과 transient aliasing을 동시에 요청하면 오류로 거부한다.

Vulkan도 같은 파이프라인 기술과 그래프를 쓰지만 별도의 device·command pool·표시 자원 경로를 가진다. 공통 클래스나 SPIR-V 생성기가 있다는 사실만으로 DX12와 기능·품질·queue 실행이 동등하다고 판단하지 않는다.

제출 성공 뒤에만 temporal history를 Commit한다. display slot에는 fence, frameId, view key, scene epoch, 카메라와 측정 provenance가 봉인된다. 이후 완료 확인으로 표시를 승격한다. FG를 쓰는 Player는 HUD 없는 색·UI·깊이·motion과 그래프 lifetime token을 presentation sink에 전달한다.

## 변경할 때 확인할 지점

새 기능은 BuildPipelineDesc의 reads/writes/modifies, 해당 Declare의 실제 RG usage, shader 입력과 준비 수명까지 함께 맞춘다. 노드 이름이나 등록만 추가하고 실제 소비를 생략하면 구현된 기능이 아니다. 캡처 readback을 추가한 프레임과 일반 프레임의 비용도 분리해야 한다.

이전 EnhancedGBufferPass/EnhancedDeferredPass 중심 설명과 LX.Scene.* 이름은 현행 경로 설명으로 쓰지 않는다. 현재 이름은 METEOR.Scene.*이며 surface·HDR의 소유자는 SceneHost다. 기존 날짜 문서의 수치·완료 표시는 해당 시점의 원본 실행 자료를 다시 확인하기 전까지 현행 수용 근거가 아니다.

## 소스 근거

- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp](../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp)
- [Engine/RenderEngine/Render/Core/EnhancedLivePipelineDesc.h](../../Engine/RenderEngine/Render/Core/EnhancedLivePipelineDesc.h)
- [Engine/RenderEngine/Render/Core/EnhancedLivePipelineDesc.cpp](../../Engine/RenderEngine/Render/Core/EnhancedLivePipelineDesc.cpp)
- [Engine/RenderEngine/MaterialGraphSceneHost.cpp](../../Engine/RenderEngine/MaterialGraphSceneHost.cpp)
- [Engine/RenderEngine/MaterialGraphSceneStages.inl](../../Engine/RenderEngine/MaterialGraphSceneStages.inl)
- [Editor/EngineEntry/App.cpp](../../Editor/EngineEntry/App.cpp)
- [Player/PlayerApp.cpp](../../Player/PlayerApp.cpp)
