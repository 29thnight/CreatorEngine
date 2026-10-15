# 재질 graph 복구와 render graph 관찰의 경계

> 기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스 정적 조사. 이 문서의 재작성에서 빌드·실행·성능 측정은 하지 않았다.

## 저작 복구는 material owner의 작업

DataSystem의 material 복구는 원본 실패를 숨겨 덮어쓰는 대신 새로운 Recovered_<GUID>.shadergraph identity를 만든다. 기본 graph 생성과 게시가 가능한 것은 asset authoring 경계다. inline material prewarm은 recoverable 실패를 실제 owner에게 미루어 임시 준비가 불필요한 복구 자산을 게시하지 않도록 한다.

모델 geometry가 유효하고 material graph만 잘못되었다면 두 실패를 분리해서 다룬다. graph dependency parse 오류의 Editor 복구 경로가 packaged Player의 strict cooked identity/closure 검증을 완화하지 않는다. GPU program이 pending인 것과 device/PSO 실패도 성공 draw로 치환하지 않는다.

## viewer는 실행 결과의 관찰자

EnhancedSceneRenderer가 제공하는 compiled graph snapshot은 live resource owner를 UI로 넘기는 API가 아니다. Editor의 EnhancedRenderDebugWindow는 pass/resource/dependency와 matching identity를 표시한다. view·scene epoch·history·extent가 다른 snapshot을 현재 화면의 graph로 제시하면 안 된다.

명령 경로와 UI 경로는 같은 실행 자료를 관찰하지만 창을 열었다는 사실만으로 실제 frame에 표시했음을 증명하지 않는다. inactive/unready/stale 상태, snapshot 요청과 실제 UI 소비를 구분해 기록해야 한다. graph compile 구조가 queue 제출·GPU 완료를 대신하지 않는다.

## 2026-10-07 보고의 보존 범위

당시 Debug/Release 통과 숫자·copy/UI 비용·capture 경로는 기존 실행의 기록이다. 이번 재작성에서 그 바이너리나 raw capture를 재실행하지 않았다. 현재 소스의 복구·관찰 연결을 설명하는 것으로 대체하며, Vulkan parity나 모든 UI 최악 비용까지 확장하지 않는다.

현행 재질/렌더 연결은 [GpuDrivenMeshletDxrWiring](../design/GpuDrivenMeshletDxrWiring.md), graph 계약은 [RenderGraphDependencySchedulingPlan](../plans/RenderGraphDependencySchedulingPlan.md)을 따른다.

## 구현 근거

- [Engine/RenderEngine/DataSystem.cpp](../../Engine/RenderEngine/DataSystem.cpp)
- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp](../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp)
- [Editor/EngineGUIWindow/EnhancedRenderDebugWindow.cpp](../../Editor/EngineGUIWindow/EnhancedRenderDebugWindow.cpp)
- [Editor/EngineEntry/Commands/DiagnosticsCommands.cpp](../../Editor/EngineEntry/Commands/DiagnosticsCommands.cpp)
