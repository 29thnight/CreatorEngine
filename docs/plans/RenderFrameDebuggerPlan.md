# 프레임 디버거의 제품 경계와 구현 계획

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 현재 사용할 수 있는 진단

EnhancedRenderDebugWindow는 EnhancedSceneRenderer::GetLiveGraphSnapshot의 불변 CPU snapshot을 읽어 pass·resource usage·dependency·barrier·wave와 뷰 일치를 보여 준다. RequestLivePbrCapture/EnhancedPbrCapture는 명시 요청에 한해 live attachment와 stage HDR를 readback하고 camera/draw/material replay 입력을 다룬다. 현재 이 두 기능을 모든 draw/dispatch 이벤트의 동결·선택 실행·범용 replay 디버거로 설명하지 않는다.

이 계획의 새 제품은 profiler와 분리된 단일 프레임 capture다. 우선 정확한 metadata event tree를 만들고, 선택 지점 픽셀 재현은 별도의 격리 replay 단계로 구현한다. 현재 소스 감사에서는 이 완성 제품의 진입·저장·UI·replay 배선을 확인하지 못했다.

## 캡처·신원 계약

대상 view를 고르고 queued→recording→submitted→waiting→frozen 또는 failed/cancelled 상태를 명시한다. renderer 소유 frame/view/submission/backend generation으로 신원을 연결하고 profiler query ringSlot을 영속 신원으로 쓰지 않는다. pass는 pipeline generation·ordinal·name으로 시작해 필요 시 stable ID를 추가한다. object는 scene epoch·instance/incarnation 등 살아 있는 객체 신원을 사용하며 asset GUID만으로 runtime instance를 식별하지 않는다.

RHI는 중립 command/event payload를 발행하고 renderer가 mesh/material/shader/object 의미를 결합한다. Editor 창은 요청·선택과 완성 snapshot 읽기만 담당하며 renderer의 live container나 native pointer를 순회하지 않는다.

## metadata 수직 슬라이스

Draw/Dispatch/Copy/Barrier와 PSO·shader·binding·target·viewport·argument를 기록한다. batch/instance가 어떤 source 객체에 대응하는지도 보존한다. parallel worker는 pass ordinal·slice·local sequence를 기록하고 실제 제출 순서로 병합한 뒤 event ID를 부여한다. worker 완료 시각을 GPU 순서로 표시하지 않는다.

문자열·event·artifact 메모리 상한을 두고 drop/partial/unsupported를 명시한다. query 실패를 전체 capture 성공으로 위장하지 않는다. fence 완료 후 불변 store를 게시하며 request cancel·device loss·resize·view 종료에서도 owner와 callback을 안전하게 회수한다.

## replay·UI·보관

선택 event까지 실행하는 replay는 고정된 입력·resource·shader generation을 pin하고 격리 output을 사용한다. live frame의 event limit를 바꾸는 방식은 피한다. pass 뒤 snapshot과 event 직후 정확 재현을 UI에서 구분한다. UI는 capture/cancel/save/open, stale selection, color channel·depth range 미리보기를 제공해야 한다.

보관 파일은 versioned schema와 논리 asset/instance 신원을 사용하고 raw pointer·native handle을 영속화하지 않는다. 범용 import는 크기·범위·버전 검사를 갖춰야 한다. 저장 후 열기·두 view·resource 지연 회수·불완전 파일·잘못된 generation이 수용 gate다. 초기 metadata 구현 완료와 replay 완료를 별도 판정한다.

## 소스 근거

- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp](../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp)
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h](../../Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h)
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp](../../Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp)
- [Engine/RenderEngine/Render/Scene/EnhancedPbrCapture.h](../../Engine/RenderEngine/Render/Scene/EnhancedPbrCapture.h)
- [Editor/EngineGUIWindow/EnhancedRenderDebugWindow.cpp](../../Editor/EngineGUIWindow/EnhancedRenderDebugWindow.cpp)
- [Engine/RenderEngine/RHI/RHIEncoder.h](../../Engine/RenderEngine/RHI/RHIEncoder.h)
