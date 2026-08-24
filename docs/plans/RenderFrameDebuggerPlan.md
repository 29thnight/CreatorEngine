# 렌더 프레임 디버거 — 프레임이 무엇을 그렸는지 재현 가능한 캡처 (PHASE 14 확장)

작성: 2026-08-24

상태: 설계 기준선 · 구현 미착수

소스 기준: `2e691254` + 2026-08-24 작업트리 정적 판독

관련 정본: `ProfilingCapturePlan.md`, `ScriptableRenderPipelinePlan.md`,
`MultiCameraRenderPlan.md`, `EditorWorkspaceRedesignPlan.md`, `RhiBoundaryPlan.md`

> 이 문서는 현재 소스의 `EngineDiagnostics`, Enhanced renderer, RenderGraph,
> RHI encoder/readback, 멀티뷰 표시 슬롯을 정적으로 판독해 작성했다. 이 계획을 위한
> 빌드·런타임·장시간 캡처 검증은 아직 수행하지 않았다. 작업트리에 있는
> `ShaderMetaHandle` 계열 변경은 진행 중인 런타임 캐시 작업으로 보며, 이 계획의 완료
> 구현으로 계산하지 않는다.

---

## 0. 결정 요약

1. **`EngineDiagnostics`가 프레임 캡처의 정본을 소유한다.** 캡처 요청 상태,
   불변 이벤트 목록, 문자열·상태 표, 예산·누락 진단, reader와 파일화를 이 프로젝트에 둔다.
2. **`RenderEngine`이 렌더 의미와 GPU 실행을 소유한다.** pass/draw/dispatch/copy를
   기록하고, 렌더 오브젝트·메시·머테리얼·셰이더 의미를 붙이며, 선택 이벤트의 출력
   재현과 GPU 리소스 수명을 책임진다.
3. **`EngineGUIWindow`는 controller 겸 reader다.** 캡처 요청과 event 선택을 전달하고
   완성된 불변 캡처만 그린다. 렌더러 내부 container나 backend 리소스를 직접 순회하지 않는다.
4. **메타데이터 캡처와 픽셀 재현을 분리한다.** 첫 수직 슬라이스는 정확한 이벤트 트리만
   만든다. 선택 이벤트의 중간 화면은 별도의 격리 replay 슬라이스에서 추가한다.
5. **PHASE 14 P1의 `EngineFrameId`와 P4의 `GpuFrameToken/SubmissionId`가 선행한다.**
   멀티뷰·2-in-flight에서 프레임 귀속이 증명되기 전에는 Frame Debugger UI를 완료로 보지 않는다.
6. **`EngineDiagnostics`에는 `Scene*`, `Mesh*`, `Texture*`, `RHIHandle`, D3D12/Vulkan,
   ImGui 타입을 넣지 않는다.** 고정 폭 값 ID와 string table만 경계를 넘는다.
7. **새 process-global registry와 새 계측 매크로를 만들지 않는다.** composition root가
   diagnostics service와 renderer provider를 명시적으로 배선한다.
8. **`EnhancedLiveDebugSnapshot`은 현행 status 창의 정본으로 유지한다.** 대용량 draw/event
   vector를 여기에 추가하지 않는다.
9. **PIX/RenderDoc을 대체하지 않는다.** 내부 도구는 엔진 의미가 붙은 한 프레임을 설명하고,
   API command stream·shader instruction·descriptor 원시 상태의 정밀 분석은 외부 도구가 맡는다.

---

## 1. 현재 소스 기준선

### 1.1 `EngineDiagnostics`는 물리적으로 생겼지만 아직 CPU profiler 코어다

- `Engine/EngineDiagnostics/EngineDiagnostics.vcxproj:21~27,37~64`는 `StaticLibrary`이며 현재
  `Profiler.{h,cpp}`와 `ProfilerSelfTest.{h,cpp}` 네 파일만 가진다.
- ProjectReference가 없는 독립 라이브러리다. 이 성질은 유지한다.
- `Engine/EngineDiagnostics/Profiler.h:145~168`은 표시를 ImGui 쪽으로 분리해 수집·보관만
  안다는 경계를 이미 선언한다.
- `ProfilingCapturePlan.md`의 P1a 물리 이관은 끝났지만 `ProfilerService`, `MarkerRegistry`,
  단일 `EngineFrameId`, shipping compile-out은 아직 남아 있다.
- 현행 `Engine/EngineDiagnostics/Profiler.cpp:96~170`의 `CPUProfiler::Tick()`은 producer
  TLS의 vector를 직접 읽고 초기화한다. 따라서 Frame
  Debugger가 이 전역 profiler container에 렌더 이벤트를 밀어 넣는 방식은 선택하지 않는다.

판정: **프로젝트 위치는 맞지만 `CPUProfiler`가 Frame Debugger의 모델은 아니다.**
`EngineDiagnostics/RenderCapture`라는 독립 하위 도메인을 둔다.

### 1.2 현재 renderer diagnostics는 최신 상태 표시이지 캡처가 아니다

`Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.h:301~343`의
`EnhancedLiveDebugSnapshot`은 다음을 보유한다.

- 최신 draw/batch/decal/sprite/UI 합계
- 최신 CPU/GPU 합계
- 마지막 수집 성공분의 pass timing
- pipeline description과 validation message

`Editor/EngineGUIWindow/EnhancedRenderDebugWindow.cpp:41~52`의
`EnhancedRenderDebugWindow`는 이를 0.25초마다 복사한다. 특정 frame/submission을 freeze하지
않으며 엔진이 계속 돌면 값이 바뀐다. 따라서 이 구조에 `std::vector<DrawRecord>`를 붙이면
다음 문제가 생긴다.

- 정확한 프레임 귀속 없이 마지막 완료분끼리 섞임
- UI polling 주기와 렌더 제출 주기가 다름
- 매 갱신마다 대용량 vector·string 복사
- 창이 닫혀 있으면 캡처 lifecycle을 정의할 곳이 없음
- 저장·재열람 가능한 불변 세션이 되지 않음

현행 창은 pipeline 상태와 최근 비용을 보는 **Live Status**로 남기고, Frame Debugger는 별도
capture service와 별도 window로 만든다.

### 1.3 pass 경계와 공용 command 경로는 이미 좋은 계측 지점이다

- `Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp:543~572,718~760`의
  `EnhancedRenderGraph::Execute()`와 `RecordParallel()`은 모든 실행 pass를 공통으로 순회한다.
- pass timing도 이 경계에서 `IRHIGpuProfiler::BeginPass/EndPass`로 자동 감싼다.
- pass가 실제 GPU 명령을 내는 통로는 backend-neutral `RHIEncoder`다.
- DX12와 Vulkan encoder 모두 `Draw`, `DrawIndexed`, `Dispatch`, copy/readback 계약을 구현한다.
- `Engine/RenderEngine/RHI/RHIResourceTypes.h:667~809`의 `RHIReadbackImage/RHIReadback`은
  width/height/rowPitch/`RHIFormat`/CPU byte와 handle 수명 계약을 가진 공용 결과다.

따라서 pass 작성자가 매번 진단 코드를 기억하게 하지 않는다.

- pass begin/end와 graph resource/barrier 정보: RenderGraph가 자동 기록
- draw/dispatch/copy와 바인딩 상태: capture가 켜진 동안의 tracing encoder가 기록
- 오브젝트·재질 같은 의미 정보: pass의 draw loop가 typed metadata로 다음 command에 연결

### 1.4 현재 draw 경로는 GPU 명령 전에 오브젝트 신원을 잃는다

현재 `Engine/RenderEngine/RenderProxy.h:6~31`의 `RenderProxy`에는 `m_instancedID`가 있고
`RenderScene`은 이 GUID를 key로 프록시를 보유한다. 그러나
`EnhancedSceneRendererLive.cpp:2381~2451`의 `BuildDrawPool()`이 만드는
`EnhancedRenderPass.h:46~80`의 `EnhancedDrawItem`에는 다음만 남는다.

- `Mesh*`
- world matrix
- 네 장의 `Texture*`와 머테리얼 값
- bone palette pointer와 animator key

즉 source proxy/component ID가 `EnhancedDrawItem`에서 사라진다. GBuffer는 다시 같은
mesh/material을 `DrawBatch`로 묶고
`EnhancedGBufferPass.cpp:546~588`에서 `DrawIndexed(..., instanceCount)` 하나를 발행하므로,
RHI에서 보이는 것은 index/instance 수뿐이다. 한 GPU draw가 어떤 N개 오브젝트를 대표하는지
복구할 수 없다.

같은 신원 손실이 decal, sprite, UI rect에도 있다. 따라서 첫 semantic slice에서 다음을
밀봉 입력에 추가한다.

```cpp
struct RenderObjectRef
{
    uint64_t sceneEpoch;
    uint64_t proxyGuidValue;
};
```

이 값은 capture 시점의 source reference다. 영구 자산 GUID나 새 전역 registry가 아니다.
에디터 resolver가 현재 scene epoch와 GUID를 대조해 살아 있으면 선택 대상으로 연결하고,
이미 파괴됐으면 캡처에 저장한 이름·타입만 표시한다.

### 1.5 현재 frame ID와 GPU query record는 Unity 수준 판정에 부족하다

`EnhancedSceneRenderer.h:100~116`의 `EnhancedLiveFramePacket::frameId`는
`EnhancedSceneRendererLive.cpp:3606~3657`의 `BuildLiveFramePacket()`이 증가시키는 renderer
publication ID다. 같은 packet에 최대 두 view가 있고, 각 view는 서로 다른
submission/display slot을 쓴다. 반면 현행
`EnhancedSceneRendererLiveDX12Adapter.cpp:401~425`의 profiler 수집은 token을 받지 않고
마지막 record 한 벌을 읽는다.

정확한 Frame Debugger는 최소 다음을 구분해야 한다.

- `EngineFrameId`: 게임 업데이트 경계
- `SubmissionId`: 실제 queue 제출 단위
- `RenderViewId`: Scene/Game 등 논리 view
- `sceneEpoch`, pipeline revision, resize generation
- fence와 ring slot

이 구분은 `ProfilingCapturePlan.md` P4가 이미 소유한다. 이 계획은 중복 ID 체계를 만들지 않고
그 결과를 소비한다.

### 1.6 정확한 draw-step replay의 입력 수명은 아직 없다

현재 `EnhancedDrawItem`의 `Mesh*`, `Texture*`, bone palette pointer는 현재 render input을
준비하고 기록하는 동안만 유효한 계약이다. live `RenderScene`을 나중에 다시 읽으면 캡처한
프레임이 아니라 현재 scene을 재생하게 된다.

따라서 선택 draw까지의 정확한 화면을 나중에 다시 만들려면 renderer가 별도
`RenderReplayPacket`을 보유해야 한다.

- post-culling draw 순서와 batch membership
- 카메라·light·UI·sprite·decal·gizmo의 밀봉 값
- strong asset/RHI lifetime token
- pipeline/feature/tuning revision
- 필요한 temporal history 입력
- 원래 submission/view의 extent와 format

이 packet은 GPU 리소스와 renderer 타입을 가지므로 `EngineDiagnostics`에 넣지 않는다.
renderer-owned bounded store가 보유하고 diagnostics에는 generation이 있는 opaque `ReplayTicket`
값만 전달한다. ticket은 직렬화하지 않으며 renderer teardown/rebuild 시 명시적으로 만료한다.

---

## 2. 제품 목표와 비목표

### 2.1 사용자 완료 흐름

1. `Render Frame Debugger`를 연다.
2. Scene/Game view 중 하나를 고르고 **Capture next completed frame**을 누른다.
3. 요청한 view의 실제 fence 완료 뒤 한 capture가 `Frozen`이 된다.
4. 왼쪽 event tree에서 `Submission → Pass → Draw/Dispatch/Copy/Clear` 순서를 본다.
5. draw를 고르면 batch, instance 구성, source object, mesh/material/shader, render state와
   대상 resource를 본다.
6. event step을 이동하면 그 event 직후의 선택 render target을 preview한다.
7. 살아 있는 source object는 에디터 selection으로 이동하고, 사라진 object는 stale로 표시한다.
8. 캡처를 저장하고 다시 열어 같은 metadata와 이미 만들어 둔 preview를 본다.

### 2.2 "Unity 수준"의 이 계획상 정의

다음이 모두 있어야 제품 목표를 충족한다.

- 한 **완료된** frame/submission을 freeze
- view와 pass별 실행 순서
- draw, instanced draw, dispatch, clear, copy/resolve, barrier/present event
- event별 index/vertex/instance/dispatch 크기
- source render object와 batch member 목록
- mesh/submesh, material, shader/pass/variant의 안정된 표시 정보
- render target/depth, viewport/scissor, topology, blend/depth/raster 상태 요약
- resource read/write와 pass culling 여부
- 선택 event 직후의 출력 preview
- metadata/preview 누락과 unsupported 상태 표시
- 멀티뷰·2-in-flight에서도 정확한 `EngineFrameId/SubmissionId` 매핑
- UI가 닫혀 있어도 Development Player에서 metadata 캡처 가능

### 2.3 1차 범위에서 제외

- RenderDoc처럼 임의 API command stream 전체를 저장하고 다른 GPU/프로세스에서 replay
- shader source 단위 step debugging, instruction trace, wave/register 검사
- descriptor heap 원시 주소나 backend object dump를 portable schema로 만들기
- 매 frame 모든 draw 뒤의 render target 상시 복사
- 모든 culled object의 원인 분석. 1차는 pass culling과 draw 수 요약까지만 기록
- Shipping에서 원격 capture server를 상시 열기
- scene/game object를 살려 두기 위한 process-global registry
- live `Scene*`, `Entity*`, `Camera*`, `Mesh*`, `Texture*`를 capture file에 보존
- profiler timeline과 Frame Debugger UI를 한 창으로 합치기

---

## 3. 목표 계층과 의존 방향

```text
[Composition root: Editor/Player Host]
        │ explicit AttachRenderCaptureProvider(provider)
        ▼
[EngineDiagnostics — ProjectReference 0 유지]
  RenderCaptureService
  RenderCaptureStore / Reader / File
  IRenderCaptureProvider / IRenderTraceSink
        ▲                           ▲
        │ typed values              │ request/result
        │                           │
[RenderEngine]
  RenderTraceRecorder ─ TracingRHIEncoder
  RenderReplayStore  ─ PreviewCapture
        ▲
        │ immutable reader/control
[EngineGUIWindow]
  RenderFrameDebuggerWindow
  EditorRenderObjectResolver
```

### 3.1 `EngineDiagnostics` 소유

권장 파일:

```text
Engine/EngineDiagnostics/RenderCapture/
  RenderCaptureTypes.h
  RenderCaptureService.h/.cpp
  RenderCaptureStore.h/.cpp
  RenderCaptureReader.h/.cpp
  RenderCaptureFile.h/.cpp
  IRenderCaptureProvider.h
  IRenderTraceSink.h
  RenderCaptureSelfTest.h/.cpp
```

책임:

- request state machine과 capture ID 발행
- bounded metadata/artifact store
- capture-local string/state/resource/object table
- immutable reader snapshot
- overflow, unsupported, provider failure 진단
- `.ceframe` 저장/불러오기
- provider attach/detach 수명 계약
- Editor/Development/Shipping build policy

포함 금지:

- ImGui와 Editor window
- SceneRuntime/RenderEngine 헤더
- `RHIFormat`, `RHIHandle`, D3D12/Vulkan 타입
- renderer callback을 찾는 전역 registry

`EngineDiagnostics`가 Utility에도 의존하지 않는 현재 성질을 유지하기 위해 자산 GUID는
`DiagnosticGuid128` 같은 고정 16-byte 값으로 복사한다. `FileGuid`나 `ShaderMetaHandle` 타입을
헤더에서 직접 include하지 않는다.

### 3.2 `RenderEngine` 소유

권장 파일:

```text
Engine/RenderEngine/Diagnostics/
  RenderCaptureProvider.h/.cpp
  RenderTraceRecorder.h/.cpp
  TracingRHIEncoder.h/.cpp
  RenderReplayPacket.h
  RenderReplayStore.h/.cpp
  RenderPreviewCapture.h/.cpp
```

책임:

- request를 안전한 render frame 경계에서 수락
- graph/pass/resource/command event 생성
- render object와 asset 의미를 capture-local 값 ID로 변환
- 병렬 record의 deterministic merge
- fence 완료 뒤 capture commit
- replay packet과 RHI resource lifetime
- event-limit 격리 replay와 readback
- `RHIReadbackImage`를 backend-neutral captured image로 변환

### 3.3 `EngineGUIWindow`와 Editor adapter 소유

권장 파일:

```text
Editor/EngineGUIWindow/
  RenderFrameDebuggerWindow.h/.cpp
  RenderFrameEventTree.h/.cpp
  RenderFramePreviewView.h/.cpp

Editor/EngineEntry 또는 Scene adapter/
  EditorRenderObjectResolver.h/.cpp
```

책임:

- view 선택, capture/cancel/save/open 요청
- frozen reader의 event tree와 detail 표시
- preview channel/depth/range 조정
- `RenderObjectRef`를 현재 scene selection으로 연결
- stale object와 unsupported preview 표시

Editor resolver가 실패해도 capture 자체는 유효하다. Player는 resolver와 ImGui 없이 동일한
metadata capture와 파일 저장이 가능해야 한다.

---

## 4. 공통 데이터 모델

### 4.1 capture와 frame identity

```cpp
using RenderCaptureId = uint64_t;
using RenderEventId = uint32_t;
using CaptureStringId = uint32_t;
using CaptureResourceId = uint32_t;
using CaptureStateId = uint32_t;
using CaptureArtifactId = uint32_t;

struct RenderCaptureFrameKey
{
    uint64_t engineFrameId;
    uint64_t sceneEpoch;
    uint64_t pipelineRevision;
    uint64_t resizeGeneration;
};

struct RenderSubmissionKey
{
    uint64_t submissionId;
    uint64_t renderViewId;
    uint64_t fenceValue;
    uint32_t ringSlot;
    uint8_t  queueId;
};
```

`engineFrameId/submissionId`는 이 계획이 새로 정의하지 않는다. PHASE 14 P1/P4의 정본 타입을
사용한다. P4의 view 필드는 현재 `EnhancedLiveViewKey::viewId`와 같은 안정된 논리
`RenderViewId`여야 하며 camera/container slot을 작은 정수로 복사한 값이면 안 된다. capture
service는 그 값을 복사해 보존할 뿐 발행하지 않는다.

### 4.2 pass identity

현재 `RGPassId`는 graph-local index이므로 file의 영속 신원이 될 수 없다. SRP 이전과 이후를
모두 지원한다.

```cpp
struct CapturedPassKey
{
    DiagnosticGuid128 stablePassGuid; // 있으면 Pipeline Asset/Native Pass의 안정 GUID
    CaptureStringId   name;
    uint32_t          declarationOrdinal;
    uint32_t          instanceOrdinal;
};
```

- 현재 LivePipeline: pipeline revision + declaration ordinal + name을 사용
- SRP 도입 후: `RenderPassTypeId`/Pass instance GUID를 채움
- pass 이름 중복은 ordinal로 구분
- stable GUID 부재를 capture failure로 보지 않되 `identityQuality=SessionLocal`로 표시

### 4.3 event tree

```cpp
enum class RenderEventKind : uint8_t
{
    Submission,
    Pass,
    PassSlice,
    Clear,
    Draw,
    DrawIndexed,
    Dispatch,
    Copy,
    Resolve,
    BarrierBatch,
    Present,
    SkippedDraw,
};

struct RenderEventRecord
{
    RenderEventId       id;
    RenderEventId       parent;
    RenderEventKind     kind;
    uint32_t            logicalSequence;
    uint32_t            passIndex;
    uint32_t            sliceIndex;
    CaptureStringId     label;
    CaptureStateId      state;
    CaptureResourceId   primaryOutput;
    CaptureArtifactId   preview;
    uint32_t            detailIndex;
};
```

draw/dispatch/copy 세부 값은 종류별 dense array에 두고 `detailIndex`로 참조한다. 모든 event가
큰 variant를 들지 않게 하고 file schema도 종류별 versioning이 가능하게 한다.

### 4.4 object와 asset reference

```cpp
struct CapturedRenderObject
{
    uint64_t          sceneEpoch;
    uint64_t          proxyGuidValue;
    CaptureStringId   displayName;
    CaptureStringId   componentType;
    uint32_t          flags;
};

struct CapturedAssetRef
{
    DiagnosticGuid128 catalogGuid;
    CaptureStringId   displayName;
    CaptureStringId   assetType;
    uint64_t          contentRevision;
};
```

- source object: 현재 `RenderProxy::m_instancedID`를 `sceneEpoch`와 함께 사용
- asset: catalog GUID가 있으면 복사하고 없으면 capture-local 이름만 기록
- material shader: runtime `ShaderMetaHandle`이 아니라 catalog GUID + pass/variant key를 기록
- reload generation handle은 replay lifetime 검증에만 쓰며 파일에 영속 신원으로 저장하지 않음

### 4.5 instancing과 batch 관계

한 `DrawIndexedInstanced` event는 GPU command 하나다. 이를 instance 수만큼 draw event로
부풀리지 않는다.

```text
DrawIndexedInstanced — mesh A / material B / instances 4
  ├─ object 17
  ├─ object 29
  ├─ object 42
  └─ object 83
```

`EnhancedGBufferPass::BuildBatches()`와 forward/shadow/decal/sprite/UI batching이 capture가
armed인 경우 member `RenderObjectRef`의 연속 구간을 함께 만든다. 평상시에는 이 진단용
vector를 만들지 않는다.

### 4.6 render state snapshot

`TracingRHIEncoder`는 state-setting call을 모두 event로 내지 않는다. 현재 state shadow를
갱신하고 실제 draw/dispatch 때 deduplicated snapshot ID를 붙인다.

현재 `RHIEncoder::SetPipeline()`은 `RHIPipelineHandle`만 받으므로 encoder wrapper만으로는
blend/depth/raster/shader descriptor를 역산할 수 없다. native PSO를 resolve하지 말고,
`IRenderPipelineCache::GetOrCreate(desc)` 경계에서 handle과 backend-neutral
`RHIGraphicsPipelineDesc/RHIComputePipelineDesc`의 capture-local descriptor를 provider에
등록한다. capture가 꺼져 있으면 이 등록 경로도 allocation/hash를 추가하지 않는다.

최소 상태:

- graphics/compute pipeline logical ID와 shader/pass/variant
- primitive topology
- viewport/scissor
- color/depth target resource ID
- blend/depth/stencil/raster 요약
- vertex/index buffer descriptor 요약
- bound resource table의 logical resource ID와 slot
- index/vertex/instance/dispatch arguments

원시 GPU virtual address, descriptor handle, native pointer는 저장하지 않는다.

### 4.7 resource와 preview artifact

```cpp
enum class CapturedImageFormat : uint8_t
{
    Rgba8Unorm,
    Rgba8Srgb,
    Rgba16Float,
    Rgba32Float,
    R32Float,
    R32Uint,
    Depth32Float,
};

struct CapturedImage
{
    uint32_t width;
    uint32_t height;
    uint32_t rowPitch;
    uint32_t sliceCount;
    CapturedImageFormat format;
    std::vector<uint8_t> bytes;
};
```

RenderEngine이 `RHIFormat`을 위 stable enum으로 변환한다. 미지원 format은 임의로 검은 이미지로
바꾸지 않고 metadata만 남기며 `UnsupportedFormat` 진단을 기록한다.

### 4.8 completeness는 first-class data다

각 capture에 다음을 둔다.

- metadata complete/incomplete
- preview complete/partial/unavailable
- dropped event/state/string/artifact 수
- unresolved object/asset 수
- replay ticket available/expired
- provider/backend/build 정보
- abort/failure reason
- profiler/capture 자체 CPU 시간과 byte 수

한도 초과를 조용히 잘라 정상 capture처럼 보이지 않게 한다.

---

## 5. 수집·완료·재현 lifecycle

### 5.1 request state machine

```text
Idle
  └─ Arm(view/options)
       └─ Armed
            ├─ renderer accepts exact frame/view → RecordingMetadata
            │    └─ submission queued → WaitingForGpu
            │         ├─ fence complete → Frozen
            │         ├─ provider/rebuild failure → Failed
            │         └─ cancel/teardown → Cancelled
            └─ invalid target/timeout → Failed

Frozen
  ├─ RequestPreview(event) → PreviewPending → Frozen(updated artifact)
  ├─ Save
  └─ Clear → Idle
```

UI가 `Armed`를 곧 capture 완료로 표시하지 않는다. metadata 기록이 끝났어도 fence가 완료되지
않으면 event 결과와 target preview는 아직 공개하지 않는다.

### 5.2 request 수락 경계

- UI/CLI는 diagnostics service에 request를 넣는다.
- RenderThread는 `TickLive()`가 immutable frame packet을 소비한 뒤, view를 순회하기 전에
  pending request를 확인한다.
- request는 논리 `RenderViewId` 또는 display target을 명시한다.
- 같은 EngineFrame에 Scene/Game 두 view가 있으면 각각 다른 SubmissionId를 가진다.
- `all views`는 후속 옵션이며 1차 기본값은 선택한 view 하나다.
- frame coalescing이 일어나도 실제로 소비·제출한 EngineFrameId를 capture에 기록한다.

### 5.3 pass/command trace

1. RenderGraph가 실행 pass와 culled pass 목록을 확정한다.
2. capture가 armed이면 pass order마다 recorder lane을 만든다.
3. `TracingRHIEncoder`가 backend encoder를 forward하며 state와 command를 기록한다.
4. pass draw loop는 `DescribeNextDraw()` 같은 typed API로 object/batch/asset 의미를 제공한다.
5. 다음 `Draw/DrawIndexed`가 그 metadata를 consume한다.
6. metadata가 없이 draw가 나오면 command는 보존하되 `semanticMissing`을 표시한다.
7. `DescribeNextDraw()` 뒤 draw 없이 빠져나오면 `SkippedDraw`와 이유를 기록하거나 metadata를
   명시적으로 cancel한다.

새 범용 매크로를 만들지 않고 typed recorder/scope를 사용한다.

### 5.4 병렬 record의 결정적 병합

워커가 shared vector에 도착 순서대로 append하면 capture가 실행마다 달라진다. mutex로 data
race만 막는 것은 해결이 아니다.

- lane key: `{passDeclarationOrder, sliceIndex, localCommandSequence}`
- worker는 자기 lane에만 기록
- `FinalizeRecordedBatch`가 실제 submission list order와 같은 순서로 lane을 병합
- event ID는 병합 뒤 부여
- 순차와 병렬 경로의 event semantic digest가 같아야 함
- split pass의 raw slice는 보존하고 UI가 기본적으로 pass 아래 접어 표시

### 5.5 fence 완료와 publication

- recorder metadata는 submission slot/ticket과 함께 renderer가 보유한다.
- `GpuFrameToken`의 fence가 완료된 뒤에만 diagnostics store에 commit한다.
- query/readback 실패가 있어도 metadata가 유효하면 partial capture로 commit한다.
- pipeline rebuild, resize, backend generation 변경이 capture 중간에 발생하면 이전 generation의
  lifetime token으로 안전하게 완주시키거나 명시적으로 abort한다.
- `Frozen` reader는 엔진이 계속 실행돼도 변하지 않는다. preview artifact 추가는 새 immutable
  revision을 publish한다.

### 5.6 preview와 event-step replay

#### A. metadata MVP

- 원래 submission의 최종 display output 하나만 선택적으로 readback
- event list와 detail은 즉시 탐색 가능
- 개별 event preview가 없으면 `NotCapturedYet` 표시

#### B. pass output preview

- 선택 pass가 쓴 graph resource 중 preview 가능한 texture를 고른다.
- retained `RenderReplayPacket`으로 별도 debug target에서 동일 pass 경로를 실행한다.
- 선택 pass가 끝난 뒤 full texture `CopyToReadback`을 넣는다.
- depth/R32Uint/float는 UI의 channel/range 변환으로 보여 주되 raw bytes는 보존한다.

#### C. draw-level step

- replay controller가 capture event logical sequence를 따라간다.
- 선택 draw/dispatch 직후 target copy를 삽입하고 이후 command는 실행하지 않는다.
- 원래 display slot, temporal history, GPU profiler query, live transient pool을 수정하지 않는다.
- replay 전용 transient/resource namespace와 target을 사용한다.
- temporal pass는 필요한 history를 packet에 보존하지 못했으면 정확한 preview를 지원한다고
  가장하지 않고 `TemporalInputUnavailable`을 표시한다.

매 event 결과를 원래 frame에서 전부 복사하는 방식은 금지한다. draw 1,000개·1080p에서
수천 장의 target copy가 command stream과 VRAM/readback을 지배해 관측 대상을 바꾼다.

### 5.7 replay packet 수명

- renderer-owned bounded store, capture별 generation handle
- strong asset/RHI lifetime token 또는 안전한 소유 snapshot
- capture가 삭제되거나 budget eviction될 때 fence-aware release
- asset reload는 새 generation을 만들고 기존 packet은 잡고 있던 generation으로만 replay
- device lost/backend switch/shutdown은 ticket을 expire하고 metadata는 유지
- ticket table은 renderer instance가 소유하며 process-global singleton으로 만들지 않음

---

## 6. 에디터 UX

### 6.1 별도 ToolPanel

창 이름은 `Render Frame Debugger`로 한다. 현행 `Pipeline Setting/EnhancedRenderDebugWindow`는
live pipeline 설정·최근 상태 창으로 남는다.

```text
┌──────────────────────────────────────────────────────────────────────┐
│ [View: Scene ▼] [Capture next completed] [Cancel] [Save] [Open]     │
│ State: Frozen · Engine 1842 · Submission 3701 · Complete            │
├──────────────────────────┬───────────────────────────────────────────┤
│ Event tree               │ Preview                                   │
│  Scene View              │ [Color][Depth][R][G][B][A] Range/Exposure │
│   GBuffer                │                                           │
│    DrawIndexed (12 inst) │             captured image                │
│    DrawIndexed (1 inst)  │                                           │
│   Shadow                 ├───────────────────────────────────────────┤
│   Deferred               │ Details                                   │
│   PostChain              │ object / mesh / material / shader / state │
└──────────────────────────┴───────────────────────────────────────────┘
```

### 6.2 tree 기본 정책

- 기본: Submission → Pass → GPU command
- split slice와 barrier는 접힌 advanced node
- instanced draw는 child object 목록
- pass culled/skipped는 회색 node와 이유
- metadata missing/incomplete는 노란 badge
- provider/backend failure는 빨간 banner
- 검색: pass/object/material/mesh/shader
- filter: draw/dispatch/copy/clear/barrier, Scene/Game, opaque/transparent/editor overlay

### 6.3 detail과 editor selection

- object가 현재 scene/epoch에서 resolve되면 `Select in Hierarchy`
- component ID가 owner Entity로 resolve되면 Entity를 선택하고 해당 component를 highlight
- scene이 바뀌었거나 object가 파괴됐으면 저장된 이름·타입과 `stale`만 표시
- asset GUID가 resolve되면 Project/Inspector로 이동
- capture reader가 SceneRuntime을 직접 include하지 않고 Editor adapter가 연결

### 6.4 preview 정책

- sRGB/linear 표시 모드
- float exposure와 min/max
- color channel mask
- depth linearize near/far 또는 raw depth
- integer target은 palette/bit view
- slice/mip 선택은 artifact가 가진 범위 안에서만 제공
- unsupported format을 검은 화면으로 대체하지 않음

### 6.5 입력과 workspace

- Space/화살표 단축키는 창 focus일 때만 event step에 사용
- 중앙 ViewportHost 입력을 가로채지 않음
- PHASE 21 workspace가 먼저 구현되면 persistent ToolPanel ID를 사용
- 먼저 구현되더라도 기존 window registry에 붙이고 PHASE 21에서 wrapper만 이관

---

## 7. 저장 포맷

### 7.1 `.ceprof`와 분리한 `.ceframe`

continuous profiler capture에 draw event와 여러 이미지 blob을 섞지 않는다.

- `.ceprof`: 여러 frame의 CPU/GPU/GC/counter 추세
- `.ceframe`: 명시적으로 잡은 한 개 또는 소수 submission의 render event와 artifact

두 파일은 `sessionId`, `EngineFrameId`, `SubmissionId`, build ID로 상호 참조할 수 있다.
PHASE 14 P6의 chunk envelope/CRC/임시 파일 원자 교체 코드는 공유하되 schema는 분리한다.

### 7.2 `.ceframe` 1차 chunk

```text
Header
  magic = "CEFRAME"
  formatVersion / buildId / git commit / platform / backend
  EngineFrameId / sceneEpoch / pipelineRevision / resizeGeneration

Chunk table
  StringTable
  SubmissionTable
  PassTable
  EventTable
  DrawDetails
  DispatchDetails
  ObjectTable
  AssetTable
  StateTable
  ResourceTable
  ArtifactTable
  Diagnostics
```

- chunk마다 type/version/size/CRC
- 1차 metadata는 무압축, image artifact 압축은 크기·시간 실측 뒤 결정
- replay ticket, GPU handle, pointer, command allocator/list는 저장하지 않음
- 파일을 다시 열면 metadata와 저장된 preview만 볼 수 있음
- arbitrary GPU replay는 file format 목표가 아님
- 저장 실패 시 frozen capture 유지, 임시 파일 완성 뒤 최종 경로로 교체

---

## 8. 실행 슬라이스

초기 추정은 총 18~22일이다. RF0의 event 수·byte 수·record overhead 실측 뒤 RF4~RF6의
artifact budget과 공수를 다시 산정한다. 각 슬라이스는 별도 커밋이며 앞 단계 gate를 통과해야
다음 단계로 간다.

### RF0 — 데이터 계약·상태 기계·자가 검증 (P0, 2일)

선행: PHASE 14 P1의 `EngineFrameId` 타입/발행 위치 확정

할 일:

- `EngineDiagnostics/RenderCapture` 파일 뼈대
- capture/pass/event/object/asset/resource/state/artifact 값 타입
- request state machine과 bounded store
- immutable reader revision
- string/state table dedup
- overflow/incomplete 진단
- `render.framecapture selftest/status` CLI
- include/project boundary ratchet

완료 조건:

- EngineDiagnostics ProjectReference 0 유지
- ImGui/SceneRuntime/RenderEngine/D3D12/Vulkan include 0
- state transition, overflow, stale reader, attach/detach selftest 통과
- stopped mode service에서 frame당 allocation 0
- `RENDER_FRAME_CAPTURE_SELFTEST_OK=true`

### RF1 — Frame/View/Pass/Command metadata capture (P0, 3일)

선행: PHASE 14 P4 `GpuFrameToken/SubmissionId/Collect(token)` 완료

할 일:

- explicit renderer provider 배선
- RenderGraph pass begin/end와 culled pass 기록
- capture-on일 때만 `TracingRHIEncoder` wrapper 사용
- draw/dispatch/clear/copy/resolve/barrier/present command 기록
- pipeline cache descriptor 등록, state shadow와 capture-local resource table
- 순차/병렬 deterministic lane merge
- fence 완료 뒤 frozen publication
- `render.framecapture next --view scene|game --metadata`

완료 조건:

- 동일 fixture의 순차/병렬 event semantic digest 동일
- Scene/Game 동시에서 같은 EngineFrameId, 다른 SubmissionId/view
- 2-in-flight heavy/light 교대가 올바른 capture에 붙음
- fence 미완료 capture가 Frozen으로 노출되지 않음
- resize/rebuild/teardown 실패 사유가 진단에 남음
- DX12/Vulkan metadata event tree 동일

### RF2 — 오브젝트·배치·자산 의미 연결 (P0, 3일)

할 일:

- sealed draw/decal/sprite/UI/gizmo item에 capture source reference 추가
- GBuffer/Forward/Shadow batch member 보존
- decal/sprite/UI batching member 보존
- material/mesh/shader/pass/variant display metadata
- skipped draw와 semantic-missing 계수
- editor resolver 전용 값 contract

완료 조건:

- 4개 object가 한 instanced draw로 합쳐진 fixture에서 GPU event 1개 + child 4개
- opaque/transparent 정렬 순서가 실제 draw 순서와 일치
- source component 파괴 뒤 reader crash 0, stale 표시
- asset reload 뒤 metadata 불변, replay ticket만 유효성 재판정
- 모든 live pass draw/dispatch command의 semantic coverage 비율 표시

### RF3 — Metadata 중심 Editor MVP (P1, 2.5일)

할 일:

- `RenderFrameDebuggerWindow`
- capture/cancel/clear/live status
- event tree, 검색/filter, detail panel
- object/asset selection adapter
- completeness/overflow/provider 상태 banner
- 최종 display output 한 장의 선택적 preview

완료 조건:

- capture 후 엔진이 600 frame 더 돌아도 선택 metadata 불변
- 창을 닫고 CLI로 capture한 뒤 다시 열어 동일 결과
- 창 focus 밖에서 viewport 입력 무간섭
- UI는 renderer mutable container와 GPU handle을 직접 읽지 않음
- Development Player metadata capture가 Editor 없이 성공

### RF4 — Pass output preview (P1, 3일)

할 일:

- renderer-owned `RenderReplayPacket/Store`
- replay ticket generation/expiry/budget
- isolated replay target/transient namespace
- selected pass 종료 뒤 common `CopyToReadback`
- RGBA8/RGBA16F/R32F/R32U/D32F preview 변환
- color/depth/channel/range UI

완료 조건:

- GBuffer MRT, depth, shadow, HDR, final LDR 대표 target preview
- preview replay 전후 live display/history checksum 동일
- unsupported format은 명시적 unavailable
- DX12/Vulkan 대표 pass pixel이 허용 오차 내 일치
- packet eviction/device reset/backend teardown 뒤 metadata 유지·ticket expire

### RF5 — Draw/Dispatch 단위 정확한 step replay (P1, 4~5일)

할 일:

- event-limit replay controller
- draw/dispatch 직후 target copy 삽입
- pass 내부 state와 batch member 재현
- temporal input 보존/unsupported 판정
- step previous/next와 preview cache
- replay queue budget과 cancellation

완료 조건:

- 색이 다른 세 draw fixture에서 step 1/2/3의 pixel 누적이 기대값과 일치
- instanced draw는 한 step이며 member object N개 표시
- split pass 순차/병렬 replay 결과 동일
- replay가 original display slot, history, GPU profiler query를 변경하지 않음
- capture frame이 아닌 현재 live Scene을 읽는 경로 0

### RF6 — `.ceframe` 저장·불러오기 (P2, 2일)

선행: PHASE 14 P6 chunk envelope 또는 동등한 공통 container 완성

할 일:

- versioned chunk writer/reader
- metadata와 이미 생성한 artifact 저장
- lazy artifact load
- CRC/schema/build/backend metadata UI
- `.ceprof` session 상호 참조

완료 조건:

- metadata-only, metadata+preview, incomplete capture round-trip
- event/object/state/resource/artifact 수와 semantic digest 동일
- 손상/절단/구버전 파일 crash 없이 거절
- 저장 중 live capture가 바뀌어도 저장 대상 불변

### RF7 — Trigger·Player·Shipping·성능 hardening (P2, 2.5일)

할 일:

- profiler frame 선택에서 다음 동일 view capture 연결
- `Capture next matching frame in PIX/RenderDoc` metadata 연결
- Development Player CLI save
- Shipping compile-out/disabled 정책 검증
- capture off/metadata/replay 세 모드 overhead 측정
- budget default를 실측으로 확정

완료 조건:

- `.ceprof` spike frame과 `.ceframe`/외부 capture가 ID로 연결
- PIX/RenderDoc 불가 환경에서도 내부 metadata 보존
- Shipping에서 UI/provider/file entry point 노출 정책 일치
- capture off 상태의 P95/P99 회귀가 합의 예산 안
- capture failure가 engine frame이나 renderer lifecycle을 중단하지 않음

---

## 9. 의존 관계와 병행 가능성

```text
Profiling P1: EngineFrameId ───────┐
                                   ├─ RF0
Profiling P4: GpuFrameToken ───────┴─ RF1 ─ RF2 ─ RF3 ─ RF4 ─ RF5
Profiling P6: chunk container ────────────────────────────────└─ RF6
Profiling P7: external trigger ───────────────────────────────── RF7

SRP stable Pass GUID ───── optional enrichment, RF1의 hard blocker 아님
Editor Workspace W1/W2 ─── window shell 이관점, capture core blocker 아님
SceneGraph Entity 전환 ─── resolver 개선점, RenderObjectRef adapter로 선행 가능
```

### 9.1 hard dependency

- RF0: canonical `EngineFrameId`
- RF1 이후: `GpuFrameToken`, `SubmissionId`, view/fence/ring slot 정확성
- RF6: 공통 chunk envelope를 재사용할 수 있는 PHASE 14 P6

### 9.2 soft dependency

- SRP의 stable Pass GUID가 없으면 현재 pipeline revision + ordinal + name으로 동작
- SceneGraph 최종 EntityHandle이 없어도 sceneEpoch + proxy GUID로 live resolve
- PHASE 21 workspace 이전에는 기존 window registry를 사용

### 9.3 병행 금지 또는 조율 필요

- RF1/RF4/RF5와 RenderGraph/RHIEncoder 대수술을 같은 파일에서 병행
- RF2와 draw batching 구조 변경을 조율 없이 병행
- RF3와 PHASE 21 window registry 교체를 같은 커밋에서 병행
- RF6와 `.ceprof` chunk schema 구현을 별도 중복 container로 병행

---

## 10. 검증 행렬

### 10.1 metadata 정확성

| 사례 | 기대 |
|---|---|
| pass 3개, 하나 culled | 실행 2개 + culled 1개와 이유 |
| draw 3, dispatch 2, copy 1 | kind/순서/argument 정확 |
| instanced object 4 | GPU draw 1, member 4 |
| skipped upload/binding | GPU draw로 세지 않고 SkippedDraw와 이유 |
| 같은 pass 이름 2개 | ordinal/instance로 구분 |
| split pass 4 slice | raw slice 보존, UI pass 아래 병합 |
| 순차 vs 병렬 | semantic digest 동일 |

### 10.2 frame/submission 정확성

| 사례 | 기대 |
|---|---|
| Scene view 단독 | 선택 view의 한 SubmissionId |
| Game view 단독 | 선택 view의 한 SubmissionId |
| 두 view 동시 | 같은 EngineFrameId, 다른 SubmissionId/view |
| 2-in-flight heavy/light 교대 | token별 event/timing/preview 혼동 0 |
| published frame coalescing | 실제 소비·제출 frame ID 기록 |
| resize/rebuild 직전·직후 | generation 혼동 없이 완주 또는 명시적 abort |
| fence 미완료 | Frozen 미노출 |

### 10.3 preview/replay

| 사례 | 기대 |
|---|---|
| RGBA8 final output | pixel/readback 일치 |
| RGBA16F HDR | raw 유지 + exposure preview |
| D32F | raw/linearized 표시 구분 |
| R32Uint bitmask | integer 값 보존 |
| draw 1/2/3 step | 기대 누적 pixel |
| temporal pass | history 보존 또는 unsupported 명시 |
| replay cancel | resource/fence leak 0 |
| renderer teardown | ticket expire, metadata reader 생존 |

### 10.4 lifecycle와 failure

- capture armed 상태에서 scene unload
- provider detach/shutdown
- 창 닫기/재열기
- asset reload/remove
- artifact budget overflow
- event/string/state table overflow
- readback format unsupported
- file save 실패/디스크 부족
- device lost 또는 backend generation 변경

모든 실패에서 renderer는 계속 실행하고 capture는 incomplete/failed 이유를 보존한다.

### 10.5 build/backend/product

- `CreatorEngine.sln` Debug|x64 unity
- Release|x64 non-unity leg
- Editor와 Player
- DX12와 Vulkan metadata capture
- DX12/Vulkan 대표 preview pixel
- `Tools/dx12-validation/Invoke-Dx12Suite.ps1`
- `Tools/profiling-validation/Invoke-ProfilingValidation.ps1`
- 신규 `Tools/render-capture-validation/Invoke-RenderFrameCaptureValidation.ps1`
- `render.livecheck` 양 backend
- include/project boundary ratchet

### 10.6 overhead

같은 scene·해상도·frame 수에서 측정한다.

1. feature compile-out
2. compiled but idle
3. metadata capture armed 1 frame
4. pass preview replay
5. draw-step replay

기록값:

- RenderThread record CPU median/P95/P99
- GameThread frame median/P95/P99
- GPU frame과 queue span
- event/state/string/object 수와 bytes
- replay packet bytes와 retained asset/resource bytes
- readback bytes/time
- dropped/unsupported 수

idle 경로는 per-draw allocation·string copy·hash·mutex 0을 목표로 한다. 최종 허용 수치는 RF0/RF1
기준선 실측 뒤 문서에 기록하며, 통과시키기 위해 event를 조용히 누락하지 않는다.

---

## 11. 실패 정책과 관측 가능성

- event buffer 부족: 이후 event를 drop하고 capture incomplete + dropped count
- string/state table 부족: fallback ID와 누락 수, 빈 값으로 가장하지 않음
- semantic metadata 누락: raw command 유지 + `semanticMissing`
- GPU fence timeout: Frozen 금지, request timeout/failed
- readback 실패: metadata 보존 + preview unavailable
- unsupported format: format/name/extent metadata 보존
- replay packet 만료: 저장된 artifact만 표시, 새 step replay 비활성화
- object/asset resolve 실패: stale/unresolved 표시
- resize/pipeline rebuild: generation별 완주 또는 명시적 abort
- save 실패: frozen capture 유지
- provider 예외/오류: capture 중지, renderer 실행 유지

Diagnostics panel 또는 Frame Debugger banner에 최소 다음을 표시한다.

- request/capture/replay state
- EngineFrameId/SubmissionId/view/fence
- metadata/preview completeness
- event/object/state/string/artifact 사용량과 budget
- dropped/semantic-missing/unsupported 수
- replay packet generation/bytes/expiry reason
- capture 자체 CPU/GPU/readback cost
- provider/backend/build 정보

---

## 12. 구현 중 지켜야 할 결정

1. **frame identity가 UI보다 먼저다.** 최신 pass timing과 다른 frame의 draw 목록을 섞지 않는다.
2. **GPU command와 semantic object를 구분한다.** instancing N개를 draw N개로 거짓 표시하지 않는다.
3. **원본 순서를 보존한다.** 정렬·검색·pass aggregation은 reader의 일이다.
4. **병렬 worker 도착 순서를 실행 순서로 쓰지 않는다.** logical lane을 병합한다.
5. **capture off hot path에 allocation과 lock을 넣지 않는다.** nullable recorder branch만 허용한다.
6. **backend 타입을 core로 올리지 않는다.** provider가 stable value schema로 변환한다.
7. **live Scene을 replay 입력으로 다시 읽지 않는다.** capture 시점의 render-owned packet만 쓴다.
8. **replay가 live history/display를 바꾸지 않는다.** 별도 target/resource namespace를 쓴다.
9. **incomplete를 complete처럼 보이지 않는다.** 누락은 데이터 모델과 UI에 남긴다.
10. **현재 live debug snapshot을 capture store로 쓰지 않는다.** status와 forensic capture를 분리한다.
11. **외부 도구와 경쟁하지 않는다.** 내부 의미 정보에서 PIX/RenderDoc 정밀 분석으로 연결한다.
12. **Player와 Vulkan을 마지막에 몰아 넣지 않는다.** RF1부터 공용 metadata path를 검증한다.
13. **새 process-global registry를 만들지 않는다.** provider와 resolver는 composition root가 소유한다.
14. **런타임 세대 handle을 영속 자산 ID로 쓰지 않는다.** catalog GUID와 capture-local revision을 구분한다.

---

## 13. 최종 완료 조건

- [ ] `EngineDiagnostics/RenderCapture`가 ProjectReference 0과 backend/editor include 0을 유지
- [ ] selected next completed Scene/Game submission을 정확히 freeze
- [ ] pass/draw/dispatch/clear/copy/resolve/barrier/present event 순서 보존
- [ ] 모든 instanced draw가 GPU command 1개와 member object N개로 표시
- [ ] mesh/material/shader/pass/variant와 render state 요약 표시
- [ ] 멀티뷰·2-in-flight에서 `EngineFrameId/SubmissionId/view` 혼동 0
- [ ] 순차/병렬 record semantic digest 동일
- [ ] pass output과 draw-step preview가 원래 frame input을 사용
- [ ] replay가 live display/temporal history/profiler query를 변경하지 않음
- [ ] overflow·semantic missing·unsupported·expired 상태를 숨기지 않음
- [ ] `.ceframe` metadata/artifact round-trip
- [ ] UI가 닫혀도 Development Player CLI capture/save 가능
- [ ] Shipping compile-out/disabled 정책 검증
- [ ] DX12/Vulkan metadata와 대표 preview 검증
- [ ] Debug unity + Release non-unity + Editor + Player build 통과
- [ ] capture idle/metadata/replay overhead와 budget 실측 기록
- [ ] PIX/RenderDoc capture와 build/frame/submission ID 상호 참조

---

## 14. 다른 계획과의 관계

### `ProfilingCapturePlan.md`

- `EngineFrameId`, `SubmissionId`, `GpuFrameToken`, capture file envelope를 공급한다.
- profiler는 여러 frame에서 문제 frame을 찾고 이 계획은 한 frame의 렌더 의미를 설명한다.
- `.ceprof`와 `.ceframe`은 session/frame/submission ID로 연결한다.

### `ScriptableRenderPipelinePlan.md`

- stable `RenderPassTypeId`, Pipeline Asset pass instance GUID, slot/resource schema가 들어오면
  capture identity를 보강한다.
- Frame Debugger는 SRP의 별도 실행 경로가 아니라 동일 native RenderGraph/RHI path를 관측한다.

### `MultiCameraRenderPlan.md`

- 논리 display target과 view별 slot/fence lifecycle을 그대로 사용한다.
- Scene/Game을 camera pointer나 container index로 식별하지 않는다.

### `EditorWorkspaceRedesignPlan.md`

- Frame Debugger는 dockable ToolPanel이다.
- central ViewportHost 입력·표시 소유권을 침범하지 않는다.

### `RhiBoundaryPlan.md`

- 공용 `RHIEncoder`, `RHIReadback`, `RHIReadbackImage`를 사용한다.
- debug 기능을 이유로 raw DX12/Vulkan command/resource access를 공용 pass에 다시 들이지 않는다.

---

## 15. 갱신 규칙

- 단계 상태는 코드 존재가 아니라 해당 슬라이스의 selftest/build/runtime/backend gate가 모두
  통과했을 때만 `완료`로 바꾼다.
- current source, 계획 상태, 마지막 검증 결과와 날짜를 구분해 기록한다.
- 계획과 소스가 다르면 소스를 다시 판독하고 이 문서를 갱신한다.
- RF0 실측 뒤 event/artifact budget, 허용 overhead, RF4~RF6 공수를 갱신한다.
- 새 backend나 pass template이 들어오면 semantic coverage와 preview format matrix를 추가한다.
- 외부 도구가 지원하지 않는 환경에서도 내부 metadata capture의 완료 조건을 낮추지 않는다.
