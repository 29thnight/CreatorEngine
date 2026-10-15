# C# 파이프라인 저작 구현 계획

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

<a id="1-실제-소비-경로와-대체-범위"></a>

## 현재 상태와 범위

C# pipeline IR/build/registry 연결은 미구현 목표다. 실제 실행은 BuildPipelineDesc의 C++ 조립이며 versioned graph와 METEOR native 알고리즘은 이미 존재한다. 새 작업은 이 고정 조립의 저작 경계를 교체하고 기존 GPU 실행을 재사용하는 것이다. [목표 구조](../design/RenderPipelineTargetArchitecture.md)가 계약 정본이다.

<a id="3-구현-슬라이스--2026-10-01-재산정-32인일"></a>

## 구현 순서

- CSRP-0: 현재 node/slot·per-view·host 기여·shader schema 소비자를 다시 계수하고 sealed baseline을 만든다. 과거 19 node 수를 복사하지 않는다
- CSRP-1: native immutable IR, stable PassTypeId·slot/setting schema와 C++ builder/registry를 구현한다. missing producer·중복 ID·잘못된 root·version을 GPU 전 거부한다
- CSRP-2: typed C# builder와 한 번의 generation interop 게시를 구현한다. managed exception이나 오류 입력은 현재 정상 세대를 손상하지 않는다
- CSRP-3: PassSchema와 ShaderMeta/Keyword를 연결하고 native/C# ID·layout·permutation을 대조한다
- CSRP-4: 실제 반복 schema를 근거로 Roslyn generator/analyzer와 Editor metadata를 만든다. 생성물 불일치 변이가 검사를 실패시켜야 한다
- CSRP-5: 현행 기본 pipeline을 C# 저작으로 재구성하고 같은 sealed 입력에서 기존 C++ 조립과 topology·HDR·display·validation을 비교한다
- CSRP-6: 조건·fallback·native escape·queue 선호와 reload/fence 수명을 닫는다. viewer와 실제 실행 generation을 대조한다

<a id="2-공통-계약"></a>

<a id="4-제외분리한-작업"></a>

<a id="2026-10-01-editor-viewer--설정-착지-완료-조건"></a>

## 안전·제품 경계

Build는 세대 변화 때 호출하며 Render/CommandBuild/RHI 스레드에 CLR callback을 넣지 않는다. old generation은 실제 최종 GPU 소비까지 유지한다. SceneRenderProfile은 파라미터이고 topology 저작을 대체하지 않는다. Project Graphics의 선택·설정 UI는 공통 schema를 사용해야 한다.

기본 geometry·shadow·material·vendor SDK 본체는 native에 남는다. barrier·descriptor·fence를 C#에서 직접 제어하거나 외부 DLL Pass ABI를 제공하는 것은 범위가 아니다. shader-only template와 pass-domain visual graph도 별도 범위다.

<a id="5-완료-판정과-공수-규칙"></a>

## 검증·산정

제품 전환의 기본 실행·픽셀 수용은 기존 계획의 DX12 Debug/Release 범위를 유지한다. Vulkan 실제 교차 비교는 BackendParityPlan이 소유하며 중립 계약은 여기서 유지한다. graph viewer 자체의 기존 구현을 C# 저작 완료로 계상하지 않는다.

기존 CSRP-0~6의 32인일은 2026-10-01 범위의 계획 추정이다. 현재 METEOR 소비자·세대·입력 계약으로 범위를 다시 계수하기 전 확정 일정이나 실적이 아니다. 구현 증거가 없는 행을 문서 개정만으로 완료 처리하지 않는다.

## 소스 근거

- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp](../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp)
- [Engine/RenderEngine/Render/Core/EnhancedLivePipelineDesc.h](../../Engine/RenderEngine/Render/Core/EnhancedLivePipelineDesc.h)
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h](../../Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h)
- [ScriptCore/ScriptCore.csproj](../../ScriptCore/ScriptCore.csproj)
- [ScriptCore.Generators/ScriptCore.Generators.csproj](../../ScriptCore.Generators/ScriptCore.Generators.csproj)
- [Editor/EngineGUIWindow/EnhancedRenderDebugWindow.cpp](../../Editor/EngineGUIWindow/EnhancedRenderDebugWindow.cpp)
