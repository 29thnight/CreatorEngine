# CreatorEngine: 실제 실행 경로로 읽는 기술 안내

기준은 master `493536e300e274cbf59989cdd62b20310c8ae64f`(2026-10-15)다. 이 문서는 소스의 현재 조립·호출·데이터 소유권을 설명한다. 이번 개정에서 빌드, Editor/Player 실행, GUI 조작, GPU 이미지·성능 검증은 하지 않았다. 코드의 지원 경로와 장치에서의 수용 결과를 구분한다.

<a id="실행-구조와-모듈-경계"></a>

## 1. 실행 구조와 모듈 경계

Editor와 Player는 서로 다른 Host다. [Editor App](../Editor/EngineEntry/App.cpp)의 launch config는 개발 프로젝트 경로·Editor 설정 초기화·창 정책을 제공하고, [Player App](../Player/PlayerApp.cpp)은 런타임 콘텐츠 준비와 Player 실행 정책을 제공한다. 두 쪽은 [EngineBootstrap::Run](../Engine/RuntimeHost/EngineBootstrap.h)을 부른다.

부팅은 profiler/worker 관측 훅을 먼저 설치한 뒤 host 경로와 runtime ownership capability를 검증한다. PathFinder와 로그·덤프 기반을 준비하고, host가 제공한 콘텐츠 준비를 실행한 다음 RuntimeSettings와 선택적인 host 설정을 읽는다. 공통 singleton과 job scheduler를 세운 후 CoreWindow와 App을 만든다. 경로·콘텐츠·설정 실패를 임의 기본값으로 무시하지 않고 초기화를 중단한다.

창은 App보다 먼저 생성되어 App의 표시 자원이 창보다 먼저 정리된다. Run의 guard는 SceneManager와 scheduler 등 runtime을 정리하고 COM을 마지막에 해제한다. 종료 추적은 로그가 닫힌 이후에도 확인할 수 있도록 별도 `shutdown_trace.txt`에 단계별로 기록한다. 이 순서는 호출 연결의 설명이며 모든 비정상 종료 경로의 안전성을 실행으로 입증한 것은 아니다.

| 코드 영역 | 현재 책임 |
|---|---|
| [Utility_Framework](../Engine/Utility_Framework/) | 타입·컨테이너·직렬화·리플렉션·작업 실행 기반 |
| [SceneRuntime](../Engine/SceneRuntime/) | Scene/Entity/Component, 시뮬레이션, 스크립트·애니메이션·오디오 연결과 렌더 입력 |
| [Physics](../Engine/Physics/) | 물리 World/backend, step·쿼리·접촉 경계 |
| [RenderEngine](../Engine/RenderEngine/) | 콘텐츠 소비·Lattice material runtime·Scene renderer·RenderGraph·RHI |
| [RuntimeHost](../Engine/RuntimeHost/) | 공통 host bootstrap과 launch configuration. 폴더 자체를 독립 DLL로 간주하지 않음 |
| [EngineDiagnostics](../Engine/EngineDiagnostics/) | 기록·프로파일·진단 서비스 |
| [CommandService](../Engine/CommandService/) | 명령 registry·실행·개발 진단 표면 |
| [Editor](../Editor/) | 저작 객체·설정·창·Undo·자산 처리와 DX12 ImGui 표시 |
| [Player](../Player/) | 게임 실행·native DX12/Vulkan 표시·패키지 소비 |

링크 경계는 폴더 이름보다 `.vcxproj`의 소스 편입·참조·Shipping 조건을 확인한다. 상세: [계층 경계](plans/EngineLayerSeparationPlan.md), [Player 경계](analysis/PlayerModuleBoundaryAnalysis.md).


## 2. 한 프레임의 진행

[RuntimeFrame.cpp](../Engine/SceneRuntime/RuntimeFrame.cpp)는 두 Host의 재생 중 시뮬레이션 순서를 소유한다.

1. SceneManager Initialization과 InputEvents
2. pause 상태면 Pausing 후 반환
3. pending scene structure change가 없고 CLR이 ready이면 managed PrePhysics
4. SceneManager Physics → GameLogic
5. 동일한 구조 변경 guard를 통과하면 managed 등록·물리 이벤트·애니메이션 이벤트·스크립트 메시지·AI tick을 flush하고 PostPhysics

pause에서는 ResolveFrameDelta가 0을 반환한다. 이 함수가 각 Host의 편집 전용 동작·표시·오디오를 모두 소유하지는 않는다. [EditorMain::Update](../Editor/EngineEntry/EditorMain.cpp)는 편집 모드 경로를 따로 가지고, [PlayerMain::Update](../Player/PlayerMain.cpp)는 Player 표시와 temporal latency marker를 다룬다. Host는 입력 갱신 뒤 이 공통 simulation 함수를 호출하고 EndOfFrame 경계를 수행한다.

이후 App은 필요한 렌더 자산과 카메라 뷰를 캡처해 live frame packet을 만들고 게시한다. 게임 객체 포인터를 renderer worker가 자유롭게 순회하는 방식이 아니라 프레임에 귀속된 입력·owner·generation을 넘기는 경계를 읽어야 한다. 프레임 게시 성공과 GPU 완료·화면 표시 완료는 서로 다른 시점이다.

<a id="씬-수명과-자원-소유권"></a>
<a id="작업-실행과-애니메이션"></a>

## 3. 씬·객체·자산의 수명

현재 Scene, Entity, Component는 GCCE managed 객체다. SceneManager는 domain과 Scene root를, Scene/Entity는 강한 tracing 관계를 소유한다. 컴포넌트의 owner 신원, entity generation, 씬 이동과 DDOL 이전은 단순한 원시 포인터 생존과 다르다. 구현의 factory/root/trace 규약을 우회하는 일반 new·unique_ptr 소유 모델을 새 코드에 복사하지 않는다.

반면 AssetDepot 캐시, 불변 자원 generation, native SDK 객체와 GPU in-flight 객체는 별도 소유권을 가진다. Scene GC가 shader blob, texture upload, command allocator나 descriptor retirement까지 대신하지 않는다. 새 asset 결과는 준비·검증·현재 요청 확인을 거쳐 owner 경계에서 게시한다. 오래된 worker 결과와 이미 seal된 프레임의 exact generation을 구분한다.

공통 bootstrap이 `ce::get_job_scheduler().start()`와 shutdown을 소유하므로 Scene 교체가 worker pool의 수명 경계가 아니다. 장기 대기 루프와 일반 작업 실행을 구분하고 [JobScheduler](design/JobSchedulerDesign.md), [애니메이션 평가](plans/AnimationSchedulerPlan.md)의 입력·완료 게시 계약을 따른다.

상세: [GCCE 통합](design/GCCEIntegration.md), [native 객체 수명](plans/NativeObjectMemoryPolicyPlan.md), [AssetDepot](plans/AssetDepotImplementationPlan.md), [GPU 수명](design/RhiGpuMemoryLifetimeDesign.md).

<a id="렌더링과-rendergraph"></a>

## 4. 렌더링: Host에서 METEOR와 그래프까지

[EditorMain::Initialize](../Editor/EngineEntry/EditorMain.cpp)는 Editor scene overlay contributor와 ImGui display sink를 설치하고 renderer를 DX12로 초기화한다. Scene/Game/material preview를 runtime 설정에 따라 Vulkan으로 바꾸는 경로가 아니다. [PlayerMain](../Player/PlayerMain.cpp)은 RuntimeSettings의 backend에서 DX12 또는 Vulkan을 선택하고 native presenter를 연결한다.

실제 제품 조립은 [EnhancedSceneRenderer::BuildPipelineDesc](../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp)를 따른다. 현재 METEOR 경로의 중심은 Geometry → Cull.First → Occluder → Cull.Final → Surface다. 여기에 shadow, material transport, 후처리와 Host의 표시/overlay 소비가 이어진다. 예전 MAT/RG 단계 문서에 나온 개별 패스를 현재 제품 파이프라인으로 간주하지 않는다.

현재 SceneHost의 cooked meshlet·mesh-indirect 계약은 이전 direct/indexed surface fallback 설명과 다르다. backend capability와 입력 layout을 검사하는 실제 gate를 따라야 한다. DXR 실험·회귀용 helper의 존재는 기본 scene 렌더 경로에서의 채택을 뜻하지 않는다.

[EnhancedRenderGraph](../Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp)는 자원 버전·producer/consumer 의존성, live pass, 실행 순서, 상태 전이와 수명을 컴파일한다. 호환 모드가 남아 있어도 현재 그래프를 단순한 선언 순서 실행기로 설명하면 틀린다. 컴파일 성공, command recording 성공, native submit 성공, queue completion은 별개이며 계획된 상태를 제출 완료 상태로 미리 게시해서는 안 된다.

- [현재 패스 배선](analysis/RenderPipelineFlow.md)
- [그래프 선언·스케줄·실행 계약](plans/RenderGraphDependencySchedulingPlan.md)
- [meshlet·GPU 경계](design/GpuDrivenMeshletDxrWiring.md)
- [큐·GPU 수명](design/RhiGpuMemoryLifetimeDesign.md)

<a id="재질-저작과-실행"></a>

## 5. 재질: 저작과 현재 제품 소비

Lattice는 문서/typed IR/Slang 생성 계층이고, material_graph와 LX runtime은 검증된 제품·generation·instance/pipeline을 제공한다. graph 문서가 저장되었다는 사실은 현재 backend의 모든 shader stage가 준비되었다는 뜻이 아니다. 제품은 feature/capability/coverage와 reflected layout·resource 예산을 검증하고, 지원하지 않는 의미를 조용히 기본 PBR로 바꾸지 않는다.

현재 SceneHost의 staged 제품과 회귀 도구의 SceneMaterialPacket/MaterialPipelineSlot을 혼동하지 않는다. accepted generation의 CPU owner가 보존되어도 pending exact generation을 현재 화면에서 자동으로 구세대와 혼합해 렌더한다는 계약이 아니다. 선택과 게시에서 신원·세대·host ABI가 맞아야 한다.

쿠킹은 backend의 완전한 stage 집합, metadata/layout 및 host identity를 포함한다. 런타임 로더는 버전과 bytecode를 검증하며 원본 Slang을 다시 열어 source-free 배포를 복구하지 않는다. Code authoring 자산이 존재하는 것과 graph-only Scene 제품이 그것을 동일하게 소비하는지는 별도다.

상세: [schema](design/MaterialGraphSchema.md), [제품과 generation](design/MaterialGraphProduct.md), [SceneHost](design/MaterialGraphSceneHost.md), [저작 편집](design/MaterialAssetAuthoring.md).

<a id="모델과-텍스처-파이프라인"></a>
<a id="에디터의-리소스-자동-처리"></a>

## 6. 텍스처와 자동 편집

현재 텍스처 경계는 Editor/offline cooker의 원본 decode·resize·mip·압축과 runtime의 검증된 CECT 소비를 나눈다. DirectXTex를 모든 Player 원본 이미지 로더로 설명하지 않는다.

Inspector에서 유효한 import 설정을 변경하면 EditorAssetDatabase가 자동 저장을 예약한다. 연속 편집 후 400ms 안정 구간에서 저장하며, 처음 읽은 디스크 bytes와 외부 변경이 충돌하면 덮어쓰지 않는다. 설정 저장과 이후 cook 성공은 별도이고 실패 시 정상 런타임 세대가 보존될 수 있다.

worker는 source/meta와 revision을 캡처하고 불변 generation을 준비한다. 게시 직전 최신 요청인지 다시 확인한 뒤 DataSystem의 queued publication을 통해 owner 경계에서 바꾼다. descriptor/CPU image 준비, GPU upload, fence 완료는 하나의 Ready 값으로 합쳐 읽지 않는다. 이미 seal된 프레임은 자신이 가진 owner를 유지한다.

상세: [텍스처 파이프라인](plans/TexturePipelinePlan.md), [codec 경계](design/TextureCodecBoundaryDesign.md), [모델 저장 공유](design/ModelAnimationStorageSharing.md).

<a id="c-스크립팅과-리플렉션"></a>
<a id="물리오디오ui"></a>

## 7. 스크립트·물리·오디오·UI

C# 게임 API는 [ScriptCore](../ScriptCore/)에 있고 프로젝트 target은 net10.0이다. [생성기 프로젝트](../ScriptCore.Generators/)는 컴파일 때 연결되며 CLR host는 네이티브 시뮬레이션 경계에서 호출된다. 웹 API는 선별된 설명이므로 모든 overload나 binding table을 대체하지 않는다. 미병합 InputGraph 브랜치나 자동 binding 브랜치를 현재 master 기능으로 설명하지 않는다.

물리 변경·쿼리·접촉 전달은 [물리 API](design/PhysicsAPIContract.md)와 [접촉 실행](design/PhysicsContactExecutionContract.md)을 함께 읽는다. C# PrePhysics라는 이름만으로 네이티브 fixed step·thread ownership과 동치라고 가정하지 않는다.

EditorMain/PlayerMain은 miniaudio backend, AudioHost와 PlaybackService를 구성하고 frame update에서 갱신한다. 오래된 FMOD 설치 지침을 현재 부팅 전제로 사용하지 않는다. [오디오 계약](plans/AudioBackendModernizationPlan.md)은 backend/service/asset/graph의 역할과 장치 실패를 구분한다.

게임 UI runtime과 Editor ImGui 창은 별개다. [UI 계약](plans/UISystemRedesignPlan.md), [SDF 텍스트](design/SdfTextRendering.md), [작업 공간](plans/EditorWorkspaceRedesignPlan.md)을 각각 본다. UI 구상 PNG·SVG는 현재 실행 화면을 검증한 증거가 아니다.

<a id="빌드배포게임-패키징"></a>

## 8. 빌드·배포·패키징

현재 native 프로젝트는 Windows/MSVC v145와 C++23 설정을 사용한다. ScriptCore와 CreatorBuildTool은 .NET 10 계열이다. vcpkg manifest/overlay, 프로젝트 설정과 SDK 배치가 실제 빌드 전제이며 이 Linux 문서 작업에서 Windows 빌드를 수행하지 않았다.

[EngineOutput.props](../EngineOutput.props)는 `Bin/<platform>-<config-key>/` 아래 Editor, Player, Tools, Managed, Resources를 나눈다. `EngineShipping=true`는 Debug/Release와 별개인 output key·진단 제거 정책이다. EXE 하나만 복사하면 shared/private runtime, managed와 리소스 계약이 충족되지 않는다.

[CreatorBuildTool Program](../BuildTool/Program.cs)의 실제 command dispatch는 publish-engine, compile-game, package-game, build-asset-set, build-runtime-bootstrap, verify-engine, select-engine, open-project다. 패키지는 candidate에서 생성·검증한 뒤 current pointer를 바꾼다. `--skip-verify`는 unpublished candidate를 남기며 성공 배포와 동의어가 아니다. 실패/취소 시 기존 published 결과를 유지하는 경계를 따라야 한다.

배포 manifest·프로젝트 pin·content ABI·파일 digest를 검증하는 CLI 구현과 설치 프로그램/Launcher 전체 제품화는 별개다. 상세: [빌드](plans/BuildPipelinePlan.md), [배포와 Launcher](plans/EngineDistributionAndLauncherPlan.md), [source-free 패키징](design/SourceFreeAssetSetPackaging.md).

<a id="프로파일링과-명령-서비스"></a>
<a id="구현과-검증의-경계"></a>

## 9. 검증과 진단을 읽는 원칙

개발 명령이 소스 파일에 있어도 현재 registry/host/Shipping 조건에 등록되는지 확인한다. 퇴역 commandlet의 기록은 현재 실행 명령이 아니다. [명령 표면](analysis/CommandSurfaceImplementation.md)에서 실제 등록 경로를 찾는다.

프로파일러의 collector, CPU span, GPU submission timing, presentation timing은 다른 축이다. historical JSON/PNG의 시간·품질 결과는 그 기록의 commit/장면/설정에 한정한다. 현재 성능 향상이나 backend parity를 문서 갱신만으로 주장하지 않는다.

이 문서 세트의 확인 범위는 소스·연결·문서 일관성과 링크다. Windows 컴파일, 장치별 렌더 결과, GUI 조작, 장시간 soak와 배포 실행은 별도 수용 작업이다. [문서 지도](RefactoringPlanDashboard.html)에서 상세 계약과 남은 요구사항을 찾는다.
