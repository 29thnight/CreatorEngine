# 렌더링 후속 작업의 현행 로드맵

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 구현 기준선

현재 native 제품에는 METEOR geometry·meshlet visibility/LOD·hybrid occluder·surface·CSM·effect·post/UI 체인, 명시적 버전 그래프와 의존성 실행·CPU 병렬 기록·owned queue 계획, temporal 입력과 optional vendor adapter, graph viewer·PBR capture가 있다. 이는 각 기능의 모든 장치·품질·성능 수용이 끝났다는 뜻이 아니다.

구현 부재가 확인된 주요 목표는 C# immutable pipeline IR 저작, 완성 lightmap baker, PT/Hybrid RT mode와 AS/hit material 실행, 범용 frame event debugger/replay다. 환경 독립 가중치·local probe 등 품질 확장은 현행 일부 기능과 미래 기능을 구분한다.

## 다음 작업의 순서

1. BASE-0/PBR: 현재 topology와 cooked format의 sealed 입력·native golden·픽셀 oracle을 고정한다. 예전 GBuffer/Deferred 또는 indexed fallback 기준을 제품에 복원하지 않는다
2. RenderGraph/RHI: 실제 dependency·barrier·submission·retirement를 유지하고 owned queues의 legal plan·exact measurement·전체 workload 이득을 수용한다. 구조 구현을 기본 활성화 또는 성능 성공으로 계상하지 않는다
3. Temporal: 실제 TU/FG requested·active·source frame·display provenance와 입력/수명 품질을 SDK/API별로 닫는다. Editor TU와 Player presentation FG를 구분한다
4. C# 저작: 현재 C++ assembly를 immutable typed IR generation으로 교체한다. native rendering을 다시 구현하지 않는다
5. Lightmap/quality: 필요한 UV/BVH·worker·환경/조명 소비자만 독립적으로 구현하고 각 제품 출력으로 수용한다
6. GPU/RT: 현행 GPU geometry를 재감사하고 기본 PT+Hybrid를 먼저 만든다. 효과 탐색·ReSTIR/cage 연구는 별도 채택 gate로 추가한다
7. Backend parity: 동일 입력의 DX12/Vulkan capture·resource·픽셀·수명 비교를 전담한다

## 선행을 과장하지 않는다

모든 RG7~9나 C# 저작 완료를 모든 효과의 선행으로 묶지 않는다. lightmap background compute는 필요한 queue 계약만 소비할 수 있다. GPU 설계는 C#·라이트맵·품질 완료를 기다릴 필요가 없다. 반대로 새 queue·RT·baker가 실제로 사용하는 resource/ownership 계약을 생략해서는 안 된다.

Vulkan 교차 비교는 다른 페이즈의 DX12 주 수용에 역방향 선행을 추가하지 않는다. 그러나 공용 RHI 계약과 필요한 backend 구현을 삭제하는 근거도 아니다. RG9의 subresource/range·split transition 확장은 소비자와 실제 구현 범위를 다시 계수하는 후속 요구로 유지한다.

## 진척 보고 방법

행별로 source implemented, entry-point wired, correctness accepted, performance accepted, product enabled를 분리한다. 과거 측정 binary와 현재 소스가 다르면 역사 자료로만 인용한다. 원래 계획 인일·earned·remaining을 문서 개정만으로 이동시키지 않는다. 공수의 비교 기준은 RenderPhaseEffortEstimate, 실제 기능별 완료선은 연결된 개별 계획이다.

## 소스 근거

- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp](../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp)
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp](../../Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp)
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraphQueues.cpp](../../Engine/RenderEngine/Render/Graph/EnhancedRenderGraphQueues.cpp)
- [Engine/RenderEngine/Render/Temporal/TemporalUpscalerHost.cpp](../../Engine/RenderEngine/Render/Temporal/TemporalUpscalerHost.cpp)
- [Engine/RenderEngine/MaterialGraphSceneHost.cpp](../../Engine/RenderEngine/MaterialGraphSceneHost.cpp)
- [Editor/EngineGUIWindow/EnhancedRenderDebugWindow.cpp](../../Editor/EngineGUIWindow/EnhancedRenderDebugWindow.cpp)
