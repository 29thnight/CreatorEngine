# 이전 SRP 설계의 승계·폐기 경계

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 현재 정본

현재 목표는 [C# RenderPipeline 저작](CSharpRenderPipelinePlan.md)과 [목표 구조](../design/RenderPipelineTargetArchitecture.md)다. 과거 Pipeline Asset Inspector의 Pass Stack을 저작 정본으로 삼던 방향은 현재 구현 지시가 아니다. C#을 값 제어에만 제한했던 옛 역할도 현재 목표와 다르다.

## 지금 존재하는 기반

LivePipelineDesc는 native std::function과 instance owner를 가진 런타임 조립 기술이다. EnhancedRenderGraph의 명시적 버전·의존성 실행과 native pass lifecycle이 실제 제품에서 소비된다. material shader graph는 재질 프로그램을 생성하지만 전체 render pipeline topology를 저작하지 않는다.

## 계속 유효한 요구

- 공개/직렬화 데이터는 stable ID·typed slot·값을 사용하고 C++ callback이나 native pointer를 저장하지 않는다
- 실패한 새 asset/shader/IR 세대는 게시하지 않고 마지막 정상 세대를 유지한다
- read/write/modify·history·fallback·capability·side effect를 명시하고 graph 자원 수명과 충돌하지 않게 한다
- shader metadata·property·keyword/permutation과 PSO input이 같은 schema를 소비하게 한다
- Scene/Game view의 history를 분리하고 크기·형식·pipeline generation 변화의 invalidation 규칙을 둔다
- 특수 native pass는 source 확장으로 연결하며 그 외부 자원 소비·queue 제약을 숨기지 않는다

## 자동 승계하지 않는 항목

Asset-first Pipeline Inspector, 일반 Fullscreen/Compute/RendererList/Copy 템플릿, pass-domain visual graph, 외부 Native DLL registry/marketplace를 모두 현재 제품 기능으로 묶지 않는다. 특히 material graph의 round-trip 지원을 pass graph 저작 지원으로 확대하지 않는다. Native DLL ABI·raw RHI·렌더 스레드 managed callback은 채택 목표가 아니다.

옛 SRP-0~6 33인일이나 과거 설계 작업 완료를 새 CSRP 진척으로 옮기지 않는다. 각 새 기능은 실제 소비자·저장 형식·실행·실패 수용을 가진 별도 구현으로 판정한다.

## 소스 근거

- [Engine/RenderEngine/Render/Core/EnhancedLivePipelineDesc.h](../../Engine/RenderEngine/Render/Core/EnhancedLivePipelineDesc.h)
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp](../../Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp)
- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp](../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp)
- [Lattice/Material/LXMaterialCompiler.cpp](../../Lattice/Material/LXMaterialCompiler.cpp)
- [ScriptCore/ScriptCore.csproj](../../ScriptCore/ScriptCore.csproj)
