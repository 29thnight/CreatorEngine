# 렌더 저작·설정·진단 창의 책임 경계

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 등록 경로를 2026-10-15에 대조했다. 이 개정에서는 빌드·실행·화면 검증을 하지 않았다. 과거 결과와 현재 구현의 존재를 구분한다.

## 현재 창 지도

렌더 기능은 Scene의 편집 표시, 영속 설정, material 저작, 실시간 계측, compiled graph 설명, frame capture로 나뉜다. 하나의 RenderPass 창이 이 책임을 모두 소유하지 않는다. 셸의 stable window ID와 명시 launch 경로는 [워크스페이스 정본](EditorWorkspaceRedesignPlan.md)을 따른다.

## Material Node Editor

현재 `MaterialGraphWindow::Draw`는 자산 picker를 제공하고 Scene 객체나 MeshRenderer 선택 없이 material 자산을 만들고 열 수 있다. source material/document ID/revision에 맞는 준비 결과만 적용하며 stable revision이 0.6초 유지되고 preview가 유효하면 자동 Apply→Save를 시도한다. 선택 MeshRenderer에 assign하는 것은 자산 저작과 구별되는 동작이고 씬 참조를 유지하려면 씬 저장이 필요하다.

Scene 변경 시 session을 reconcile하고 preview 대상을 정리한다. session 데이터와 준비 job을 UI 창이 열려 있는 동안에만 처리하는 구조가 아니며 shutdown에서 최종 준비와 저장을 시도한다. 과거 “현재 Scene의 MeshRenderer만 편집” 설명은 현재 기능 범위를 제한하지 않는다. 저장 실패/디스크 충돌·draft 복구의 상세 정책은 material 문서 및 해당 session 구현을 기준으로 읽는다.

## Scene Show와 화면 진단

SkyBox 표시 토글은 Editor view의 배경 표시 정책이다. 장면 조명 환경이나 Game 출력의 의미와 같지 않다. Render Statistics는 completed Scene display의 치수·view update 빈도·backend/ready 및 provenance가 맞는 GPU pass sum을 표시한다. 다른 view, 없는 submission, 크기/real frame이 맞지 않는 표본은 unavailable이다. GPU 표본 age는 publication 차이이며 실제 경과 초와 동일하지 않다. Statistics가 켜져 있으면 별도 FPS overlay를 중복 표시하지 않는다.

## Preferences와 Project Settings

Preferences는 UI scale(0.8~1.5), Content Browser tree width(120~600 logical px), Display/Unlimited/Custom frame rate, custom 10~1000fps, background 0~60fps를 제공한다. UI scale과 pacing은 EditorSettingsStore가 EngineSettings.asset에 저장하며 tree width의 이후 저장은 workspace가 소유한다. “Preferences라는 이름이므로 운영체제 사용자 전역 설정”이라고 추정하지 않는다.

Project Settings에는 General / Build, Graphics, Player render defaults, Quality 탭이 있다. Graphics의 AA/upscaling/spatial 선택은 Editor startup defaults를 저장한다. SceneRenderProfile은 scene pass parameter를 계속 소유하고 fallback FXAA를 override할 수 있다. 나머지 live tuning을 건드리는 것이 scene profile 자산 저장과 같은 동작은 아니다.

Player render defaults는 별도의 build.renderFeatures 계약이며 Player backend는 build.render.backend에서 패키징 시 runtime render.backend로 투영된다. Editor ImGui host backend 전환 기능으로 읽지 않는다. Quality는 아직 preset 미지원 안내만 제공한다. 저장 실패가 표시되면 메모리 변화와 디스크 영속화를 구별하고 재시도해야 한다.

## Profiler와 RenderPass

Profiler는 별도 viewer의 명시적인 열기 요청을 사용한다. 예전 FrameProfiler stable ID는 호환용이며 workspace 복원만으로 viewer를 실행하지 않는다. Rendering - Live로 가는 bridge는 선택 view와 bounded snapshot을 전달한다. 최신 진단은 녹화 artifact 또는 GPU capture와 동일하지 않다.

현재 RenderPass는 native C++ pipeline의 읽기 전용 compiled graph snapshot viewer다. generation/graph epoch/frame/view/extent/scene epoch/history revision, declared 및 execute order, resource와 dependency hash를 보여준다. 표시 중인 view와 맞지 않는 이전 snapshot은 지우고 다음 일치 frame을 기다린다. alias/queue/subresource ranges는 화면에도 미구현이라고 표시한다. 이 viewer를 “계획만 있음”으로 설명하지 않는다.

## Render Frame Debugger와 남은 요구

`###Editor.RenderPassDebug`는 현재 capture 및 event/resource inspection이 없음을 알리고 Rendering - Live로 연결한다. compiled graph viewer가 존재한다고 `.ceframe` 조사나 GPU capture가 구현되었다는 뜻은 아니다.

남은 요구는 capture 저장·읽기와 event/resource 탐색, compiled graph의 alias/queue/subresource 정보, Quality preset의 저장/cook/Player 소비, 사용자별 설정 위치와 복구 UX의 명확화다. C# IR 결과를 viewer에 연결하는 미래 작업도 native C++ snapshot만으로 완료 처리하지 않는다. 실제 view 전환·resize·hidden view·미지원 backend의 표시와 저장/재시작을 [수용 계약](../analysis/EditorRenderingSurfacesValidation.md)에 따라 검증한다.

## 구현 근거

- [Editor/EngineGUIWindow/MaterialGraphWindow.cpp](../../Editor/EngineGUIWindow/MaterialGraphWindow.cpp)
- [Editor/EngineGUIWindow/SceneViewportOverlay.cpp](../../Editor/EngineGUIWindow/SceneViewportOverlay.cpp)
- [Editor/EngineGUIWindow/EnhancedRenderDebugWindow.cpp](../../Editor/EngineGUIWindow/EnhancedRenderDebugWindow.cpp)
- [Editor/EngineGUIWindow/MenuBarWindow.cpp](../../Editor/EngineGUIWindow/MenuBarWindow.cpp)
- [Editor/EngineEntry/EditorPreferences.h](../../Editor/EngineEntry/EditorPreferences.h)
- [Editor/EngineEntry/EditorSettingsStore.cpp](../../Editor/EngineEntry/EditorSettingsStore.cpp)
- [Editor/EngineGUIWindow/ProfilerLiveDiagnosticsBridge.cpp](../../Editor/EngineGUIWindow/ProfilerLiveDiagnosticsBridge.cpp)
