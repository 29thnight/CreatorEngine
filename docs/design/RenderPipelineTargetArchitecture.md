# 파이프라인 저작의 목표 구조와 현재 구현

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 현재 실행 구조

EnhancedSceneRenderer::BuildPipelineDesc는 C++ LivePassNode와 구체 owner를 조립한다. LivePipelineDesc는 initialize/prepare/declare/shutdown 및 per-view/host contributor를 관리하고 EnhancedRenderGraph가 실제 resource DAG·barrier·기록을 담당한다. 최신 노드 이름은 METEOR.Scene.*이며 과거 고정 19개라는 개수를 미래 API 계약으로 사용하지 않는다.

ScriptCore에는 이 조립을 생성하는 RenderPipeline.Build 또는 RenderPipelineBuilder API가 없다. 기존 material graph compiler, 설정용 managed 필드, native graph viewer는 C# pipeline 저작 구현의 증거가 아니다.

## 보존하는 목표

목표는 C# RenderPipeline.Build(builder)가 stable PassTypeId, typed resource/version handle, 설정·조건·root와 queue 선호를 가진 immutable IR을 만드는 것이다. native compiler가 IR을 registry의 pass factory와 LivePipelineDesc adapter로 연결한다. 실제 geometry·shadow·material·temporal SDK와 GPU command 기록은 native에 남는다.

Build는 매 프레임 callback이 아니라 프로젝트/코드/설정/variant 세대 변경 시 수행한다. 게임 스레드 저작 경계에서 검증된 세대만 원자 게시한다. 관리 예외·schema/ABI 불일치·shader 실패가 있으면 새 세대를 게시하지 않고 마지막 정상 세대를 유지한다. 참조 중인 frame과 GPU 소비가 끝날 때까지 옛 세대를 보존한다.

## schema와 공개 표면

PassSchema는 입력·출력·수정·설정의 안정 ID와 형식을 갖고 ShaderMeta의 binding/property/keyword/permutation 계약을 참조한다. 같은 정보를 C#과 C++에 수작업으로 중복 선언하지 않는다. Roslyn generator/analyzer는 typed builder·interop layout·Editor metadata와 누락 입력·잘못된 의미·범위·keyword 검사에 사용한다는 계획이다. 이 pipeline용 생성기는 아직 구현된 것으로 확인되지 않는다.

C#에 raw RHIEncoder, native resource pointer, descriptor heap, barrier/fence 또는 렌더 스레드 managed callback을 공개하지 않는다. 특수 native escape는 input/output·side effect·ownership·queue 제약을 그래프에 명시한다. 일반 shader-only Fullscreen/Compute template와 pass-domain visual graph는 별도 수요·수직 구현 범위이며 이 목표의 선행으로 자동 확장하지 않는다.

## 실행과 저작의 분리

현재 versioned graph는 이미 구현되어 있다. 새 IR은 이를 복제하지 않고 Write/Modify·실제 resource dependency·export root를 소비해야 한다. 저작 순서와 native 수명 순회는 유지하되 GPU 실행은 compiled graph가 결정한다. Scene/Game/Player의 view history와 display completion은 EnhancedSceneRenderer가 계속 소유한다.

## 완료 증거

native IR/C++ builder fixture부터 구현하고 그다음 managed generation 게시를 연결한다. C++ 조립과 새 C# 조립의 sealed 입력·node/slot·compiled order·HDR/display를 비교한다. 잘못된 ID·schema·stale handle·keyword·queue 변이를 GPU 전에 거부하고 reload 실패·두 view·GPU 지연 회수를 검증한다. viewer가 표시하는 IR generation과 실제 실행 generation도 일치해야 한다.

구 Asset-first Inspector 저작 정본과 Native DLL Pass ABI는 현재 채택 목표가 아니다. 자세한 실행 범위는 [C# 구현 계획](../plans/CSharpRenderPipelinePlan.md)을 따른다. 이 문서는 구현 완료 선언이 아니다.

## 소스 근거

- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp](../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp)
- [Engine/RenderEngine/Render/Core/EnhancedLivePipelineDesc.h](../../Engine/RenderEngine/Render/Core/EnhancedLivePipelineDesc.h)
- [Engine/RenderEngine/Render/Core/EnhancedLivePipelineDesc.cpp](../../Engine/RenderEngine/Render/Core/EnhancedLivePipelineDesc.cpp)
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h](../../Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h)
- [ScriptCore/ScriptCore.csproj](../../ScriptCore/ScriptCore.csproj)
- [ScriptCore.Generators/ScriptCore.Generators.csproj](../../ScriptCore.Generators/ScriptCore.Generators.csproj)
