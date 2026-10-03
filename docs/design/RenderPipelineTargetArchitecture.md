# Render Pipeline 목표 구조 — C# 저작과 네이티브 실행

**결정 2026-09-23.** 사용자가 제공한 `CreatorEngine_RenderPipeline_Target_Architecture.md`를
현행 소스와 계획에 대조해 채택한 설계 정본이다. 작업 순서와 공수는
[`RenderPhaseRoadmap.md`](../plans/RenderPhaseRoadmap.md), 실행 슬라이스는
[`RenderGraphDependencySchedulingPlan.md`](../plans/RenderGraphDependencySchedulingPlan.md)와
[`CSharpRenderPipelinePlan.md`](../plans/CSharpRenderPipelinePlan.md)가 소유한다.

## 책임 경계

```text
C# RenderPipeline.Build()       저작 시점에 구성·연결·조건·설정 선언
        ↓
typed PassSchema + ShaderMeta   입출력·파라미터·키워드 계약 검증
        ↓
immutable Pipeline IR          안정 Pass ID·버전 핸들·값·root·제약
        ↓  Game-thread/저작 경계에서 세대 단위 게시
LivePipelineDesc               native Pass 수명·per-view·Host 기여·진단
        ↓
EnhancedRenderPass             PrepareFrame·Declare·실제 알고리즘
        ↓
EnhancedRenderGraph            리소스 DAG·컬링·수명·배리어·기록 스케줄
        ↓
RHIEncoder → DX12/Vulkan        백엔드 명령 인코딩과 제출
```

`EnhancedSceneRenderer`는 multi-view, frame-in-flight, fence 완료, 표시 승격과 graph
lifetime을 계속 소유한다. `EnhancedLiveFramePacket`은 불변 입력을 게시한다. C# `Build()`는
GPU 명령을 실행하지 않고 **파이프라인 세대가 만들어지거나 교체될 때** 호출한다. Render,
CommandBuild, RHI 제출 스레드는 관리 코드를 호출하지 않는다. 실패한 새 IR은 게시하지
않으며 마지막 정상 세대를 유지한다.

## 무엇을 대체하는가

현재 [`EnhancedSceneRenderer.cpp`](../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp)의
`BuildPipelineDesc`는 C++ 람다와 구체 `EnhancedRenderPass` 멤버로 Pass 목록을 만든다.
C# 저작 + native IR compiler는 이 **고정 조립부**를 대체할 수 있다. GBuffer의 재질
generation 검증·배치/업로드, Shadow의 cascade·masked draw, GPU-driven·DXR 같은 특수
기능의 C++ 본체는 그대로 native Pass다. 일반 효과에 필요한 Fullscreen/Compute
template는 별도 수직 슬라이스에서 열 수 있지만 C# 저작의 성립 조건은 아니다.

Meshlet/Mesh Shader raster와 DXR의 공통 geometry·별도 visibility·BLAS/TLAS·동기화·
fallback·수명 계약은 [GpuDrivenMeshletDxrWiring.md](GpuDrivenMeshletDxrWiring.md)가 소유한다.
EnhancedSceneRenderer가 같은 frame/graph를 조율하고 Mesh Shader의 출력이 자동으로
DXR geometry가 된다고 가정하지 않는다. 이 상세 설계는 제품 구현 완료의 증거가 아니다.

기존 [`LivePipelineDesc`](../../Engine/RenderEngine/Render/Core/EnhancedLivePipelineDesc.h)는
`std::function`/인스턴스 포인터를 갖는 native runtime 기술이다. 직렬화 IR로 직접
노출하지 않는다. C#과 향후 시각 편집기·preset·C++ builder는 **같은 immutable IR**로
내려간다. 별도의 제품 실행 그래프를 만들지 않는다.

### 이전 SRP 계획과의 비교

| 경계 | 이전 Asset-first 계획 | 현재 결정 |
|---|---|---|
| 저작 정본 | Pipeline Asset Inspector의 Pass Stack | C# `RenderPipeline.Build(builder)`가 typed IR 생성 |
| C# 역할 | Game-thread 값·variant 제어에 한정 | 구성·Pass 선택·리소스 연결·조건·설정 저작 |
| native Pass | Asset이 선택하는 template/소스 Pass | 기존 `EnhancedRenderPass`를 안정 ID registry로 연결·계속 실행 |
| graph 순서 | Asset authored stack과 향후 version DAG | RG1 단일 writer DAG를 먼저 구현, RG2 version/Modify로 확장 |
| 생성·검증 | Asset schema/Inspector 우선 | PassSchema·ShaderMeta를 Roslyn typed 표면과 native 검증에 공유 |

이전 계획의 구현되지 않은 `SRP-0~6`을 완료로 간주하거나 새 슬라이스로 이름만
바꾸지 않는다. `4-1`의 2일 설계 완료도 새 C# 계약의 증거가 아니며, 과거 공수는
[`ScriptableRenderPipelinePlan.md`](../plans/ScriptableRenderPipelinePlan.md)에 보존한다.

## 리소스 의미와 순서

`RGPassUsage`는 접근 의미 `Read/Write/ReadWrite`와 요구 `RHIResourceState`를 분리한다.
**첫 구현은 단일 writer 리소스와 명시적 ordering token에 한정한 stable DAG 정렬**이다.
접근 의미만 먼저 명시하고 같은 리소스의 다중 writer/`ReadWrite` 연쇄는 모호한 채
추정하지 않고 compile 오류로 거부한다. 다음 슬라이스에서 `Write`/`Modify`의 논리
버전 핸들을 도입해 다중 writer와 `Scene.LitColor` 연쇄를 DAG에 편입한다. 이 순서는
사용자 결정인 **DAG 정렬 우선**을 따르되, 아직 정의하지 않은 선후를 실행하지 않는다.

실행 순서는 resource version edge가 결정하고, 독립 Pass만 authored index로 안정적으로
묶는다. 명시적 present/export root와 side effect·semantic ordering token은 필요할 때
edge로 선언한다. `RenderPhase`/before-after는 표현할 수 없는 실제 순서 제약이 있을 때만
사용하며, 숨은 전역 phase 순서로 resource edge를 대체하지 않는다. `LivePipelineDesc`의
목록은 저작·수명·진단 순서로 남고 GPU 실행 순서는 compiled graph가 소유한다.

## PassSchema와 C# 표면

- native `PassTypeId`/설정 schema가 Pass의 실행 계약이다. C# typed wrapper는 같은
  schema에서 생성하며 native 구현을 대신하지 않는다.
- `ShaderMeta`가 shader entry, property·binding, keyword/permutation의 단일 정본이다.
  PassSchema는 이를 참조·합성하고 keyword 축을 중복 선언하지 않는다.
- Roslyn generator는 typed handle/builder·interop layout·Editor metadata를 생성한다.
  analyzer는 누락 입력·의미 불일치·잘못된 keyword·범위·queue hint를 잡는다. 생성기
  출력과 native schema의 ID·layout 일치를 빌드 게이트로 검사한다.
- C#은 read/write/modify, 조건, 값, 지원 기능, queue **선호**를 기술한다. raw RHI,
  command list, descriptor heap, barrier/fence 삽입 API는 공개하지 않는다.
- 특수 동기화·외부 API는 native escape hatch로 두되 input/output/side effect/ownership/
  queue 제약을 graph에 게시한다.

## 단계와 완료 증거

PHASE 4.3은 `BASE-0 → RG1` 명시적 접근·단일 writer DAG → `RG2` 리소스 버전과
다중 writer DAG → `RG3~RG6` 제품 전환을 닫는다.
`Q0` queue/fence와 `RG7~RG9` aliasing·multi-queue·subresource는 별도 후속이다.
PHASE 4.6은 native IR/C++ fixture → C# 저작·managed 경계 → schema/Roslyn →
기본 19 node A/B cutover 순서로 닫는다. PHASE 4.7 라이트맵, PHASE 4.75 renderer 품질,
PHASE 4.8 GPU 설계는 C# 저작 구현 완료를 일괄 선행으로 받지 않는다.

완료 증거는 잘못된 producer/version/semantic/queue/keyword 변이가 빌드 또는 graph
compile에서 실패하는 것, sealed frame 기준 19 node dump와 DX12 픽셀·validation,
관리 호출이 렌더 스레드에서 0건인 것, reload 실패 시 마지막 정상 세대와 GPU 자원이
fence 완료까지 유지되는 것이다. 이 문서는 설계 결정이며 구현·런타임 완료 증거가 아니다.
