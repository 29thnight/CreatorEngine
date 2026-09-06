# 렌더 파이프라인 구조와 프레임 플로우

CreatorEngine 의 **렌더 패스 · 렌더러 · 머테리얼** 구조와, 한 프레임을 그리기 위한 실행
흐름을 순서도로 정리한 문서다. 모든 다이어그램은 Mermaid 로 작성했다.

기준 코드:

| 층 | 정본 파일 |
|---|---|
| 렌더러 | [EnhancedSceneRenderer.h](../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.h) · [.cpp](../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp) |
| 파이프라인 기술 | [EnhancedLivePipelineDesc.h](../../Engine/RenderEngine/Render/Core/EnhancedLivePipelineDesc.h) |
| 렌더 그래프 | [EnhancedRenderGraph.h](../../Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h) |
| 패스 기반 | [EnhancedRenderPass.h](../../Engine/RenderEngine/Render/Graph/EnhancedRenderPass.h) |
| 패스 구현 | [Render/Passes/](../../Engine/RenderEngine/Render/Passes) |
| 머테리얼 저작 정본 | [Material.h](../../Engine/RenderEngine/Material.h) · [Experiment/](../../Engine/RenderEngine/Experiment) |
| 머테리얼 밀봉 | [ExperimentMaterialSealing.h](../../Engine/RenderEngine/Render/Scene/ExperimentMaterialSealing.h) |
| 씬 프록시 | [RenderScene.h](../../Engine/RenderEngine/RenderScene.h) |

---

## 1. 전체 계층 — 무엇이 무엇을 소유하는가

렌더는 **게임 스레드가 값을 밀봉하고 렌더 스레드가 그것만 소비하는** 단방향 구조다.
렌더 스레드는 `Scene` · `Camera` · `Material` 같은 살아 있는 게임 객체를 읽지 않는다.

```mermaid
flowchart TB
    subgraph GT["게임 스레드 · Producer"]
        APP["App / PlayerApp 프레임 루프"]
        SCENE["Scene · Component"]
        PROXYQ["ProxyCommandQueue<br/>등록·갱신·해제 delta"]
        PACKET["EnhancedLiveFramePacket<br/>카메라 · 시간 · ShaderMeta generation"]
        APP --> SCENE
        SCENE --> PROXYQ
        APP --> PACKET
    end

    subgraph BOUND["프레임 경계 · 밀봉된 값만 통과"]
        QUEUE["renderQueue<br/>mutex + condition_variable"]
    end

    subgraph RT["렌더 스레드 · Consumer"]
        TICK["EnhancedSceneRenderer::TickLive"]
        RSCENE["RenderScene<br/>PrimitiveProxy · LightProxy · UIProxy"]
        POOL["drawPool · decals · spritePool<br/>프레임당 1회 수집"]
        VIEW["CameraView 별 CaptureFromView<br/>절두체 컬링 · 투명 정렬 · 광원 선별"]
        SEAL["Material Sealing<br/>immutable draw snapshot"]
        TICK --> RSCENE --> POOL --> VIEW --> SEAL
    end

    subgraph PIPE["LivePipeline · 뷰당 인스턴스"]
        DESC["LivePipelineDesc<br/>노드 목록 = 조립"]
        BB["LiveBlackboard<br/>슬롯 이름 → RGHandle"]
        PASSES["EnhancedRenderPass 구현 15종"]
        DESC --> BB
        DESC --> PASSES
    end

    subgraph GRAPH["EnhancedRenderGraph · 프레임당 · 슬롯당"]
        COMPILE["Compile<br/>순서 · 컬링 · transient · 배리어"]
        RECORD["RecordParallel<br/>워커 4"]
        COMPILE --> RECORD
    end

    subgraph RHI["RHI 백엔드"]
        DX12["DX12"]
        VK["Vulkan"]
    end

    PROXYQ --> QUEUE
    PACKET --> QUEUE
    QUEUE --> TICK
    SEAL --> DESC
    PASSES --> COMPILE
    RECORD --> DX12
    RECORD --> VK
    DX12 --> PRESENT["공유 텍스처 → ImGui 표시"]
    VK --> PRESENT
```

### 스레드 규약

| 규약 | 강제 지점 |
|---|---|
| `BuildLiveFramePacket` 은 게임 스레드 고정 | `frameProducerThread` assert |
| `TickLive` 는 소비 스레드 고정 | `frameConsumerThread` assert |
| 패킷은 **발행 순서대로 정확히 한 번** 소비 | `frame.frameId > consumedFrameId` assert |
| 렌더 스레드는 `DataSystem` · 자산 파일을 다시 읽지 않음 | ShaderMeta 를 패킷에 generation+value 로 밀봉 |

---

## 2. 프레임 렌더 플로우 — 한 프레임의 전 과정

```mermaid
sequenceDiagram
    autonumber
    participant App as 게임 스레드
    participant Q as renderQueue
    participant Tick as TickLive
    participant State as LiveState
    participant Desc as LivePipelineDesc
    participant Graph as EnhancedRenderGraph
    participant GPU as RHI / GPU

    Note over App: ① 프레임 입력 밀봉
    App->>App: 뷰 요청 수집 - Editor view + Game view
    App->>App: CaptureRequiredRenderMaterials
    App->>App: BuildRequiredAssetPacket - pass 별 ShaderMeta GUID
    App->>App: BuildLiveFramePacket - 카메라 · 시간 · 해상도 · ShaderMeta generation · 기즈모
    App->>Q: PublishLiveFrame - 프레임 + ProxyCommand delta
    Q-->>Tick: 렌더 스레드 wake

    Note over Tick: ② 프레임당 1회 · 카메라 무관
    Tick->>State: BeginDisplaySnapshot · PublishDebugSnapshot
    Tick->>State: ApplyAndPublishTuning - 디버그 창 파라미터 반영
    Tick->>State: BeginProxyFrame + ExecuteBatch - 프록시 delta 적용
    Tick->>State: BuildDrawPool - 프록시 → drawPool · decals · spritePool

    Note over Tick: ③ ShaderMeta / Material 밀봉
    Tick->>State: ApplyGBufferShaderMeta · ApplyForwardShaderMeta
    Tick->>State: SealGBufferMaterials · SealForwardMaterials
    Note right of State: propertyBytes + textureBindings + permutationKey<br/>= immutable draw snapshot

    Note over Tick: ④ 뷰 루프 - 회전 시작 인덱스로 공평 분배
    loop 각 CameraView
        Tick->>State: CaptureFromView - 절두체 컬링 · 투명 back-to-front 정렬 · 광원 선별
        Note right of State: 불투명 → draws<br/>투명 → forwardDraws
        Tick->>Graph: RenderOnce
        Graph->>GPU: BeginFrame - allocator reset · 커맨드 리스트 open
        Graph->>Desc: PrepareAll - IBL 생성 · 메시/텍스처 업로드
        Graph->>Desc: DeclareAll - 노드 순서대로 그래프에 선언
        Graph->>Graph: Compile - BuildOrder · CullPasses · CreateTransients · PlanBarriers
        Graph->>GPU: RecordParallel - 워커 4로 커맨드 기록
        Graph->>GPU: EnqueueRecordedBatch · EndFrame - 펜스 signal
        Note right of GPU: 여기서 기다리지 않는다<br/>fenceValue 만 슬롯에 기록
    end

    Note over Tick: ⑤ 완료 승격 - 다음 프레임들에서 논블로킹 확인
    Tick->>State: PromoteCompleted - 펜스 완료된 슬롯을 표시로
    State->>App: DisplaySnapshot / ImTextureId
```

### 핵심 설계 판단

- **④에서 GPU 를 기다리지 않는다.** 예전 동기 `WaitForGpu` 는 프레임당 33 ms 를 먹어
  총 대기를 58 ms 로 만들었다. 실제 GPU 는 3.7 ms 였다. 지금은 펜스 값만 슬롯에 적고
  다음 프레임들에서 논블로킹으로 확인해 표시로 승격한다.
- **②는 카메라 수와 무관하게 1회.** `BuildDrawPool` 은 카메라를 보지 않는 수집이고,
  뷰는 그 풀을 절두체로 거르기만 한다.

---

## 3. 렌더 패스 구조

### 3-1. 패스의 계약 — 선언과 기록의 분리

`EnhancedRenderPass` 는 기존 `IRenderPass` 를 상속하지 않는다. 그쪽은 "이 패스가 지금 다
그린다"를 전제하는데, 그래프 위에서는 선언과 기록이 나뉜다.

```mermaid
flowchart LR
    subgraph LIFE["패스 수명"]
        INIT["Initialize<br/>1회 · PSO · 루트 시그니처 · 정적 리소스"]
        PREP["PrepareFrame<br/>프레임마다 · GPU 업로드"]
        DECL["Declare<br/>프레임마다 · 그래프에 사용 선언"]
        REC["Record 람다<br/>그래프가 정한 시점 · 커맨드만 기록"]
        SHUT["Shutdown<br/>노드 역순"]
        INIT --> PREP --> DECL --> REC --> SHUT
    end

    subgraph RULE["규약 3"]
        R1["① Declare 에서<br/>리소스 생성 · 커맨드 기록 금지"]
        R2["② Record 에서<br/>리소스 생성 · 배리어 삽입 금지"]
        R3["③ Record 는 넘겨받은<br/>커맨드 리스트에만 기록"]
    end

    DECL -.-> R1
    REC -.-> R2
    REC -.-> R3
```

이 분리가 필요한 이유는 둘이다. **배리어를 그래프가 유도하려면** 기록 전에 사용 계획을
알아야 하고, **기록을 워커가 병렬로 돌리려면** 기록이 순수해야 한다.

### 3-2. 파이프라인 조립 — 노드 목록이 순서를 정한다

예전에는 `RenderOnce` 안 200 줄이 "무엇을 어떤 순서로 잇는가"를 직접 적었고, 같은 사실
"SSS 는 Forward 뒤"가 `Initialize` · `Shutdown 역순` · `PrepareFrame` · `Declare` 네 곳에
따로 적혀 있었다. 셋은 틀려도 컴파일러가 침묵했다.

```mermaid
flowchart TB
    NODE["LivePassNode"]

    subgraph DATA["데이터 절반 — 검증·덤프의 근거"]
        NAME["name"]
        READS["reads<br/>앞 노드가 발행해야 읽는다"]
        WRITES["writes<br/>이 노드가 처음 발행"]
        MODS["modifies<br/>읽고 같은 이름으로 재발행"]
        PERVIEW["perView<br/>시간축 상태를 가진 패스"]
    end

    subgraph GLUE["접착 절반 — 타입"]
        ACTIVE["active<br/>비활성이면 declare 건너뜀"]
        INST["instance<br/>viewIndex → 패스 포인터"]
        DECLFN["declare<br/>블랙보드 ↔ 타입 있는 Inputs"]
    end

    NODE --> DATA
    NODE --> GLUE

    VAL["LivePipelineDesc::Validate<br/>파이프라인 구축 시 1회"]
    DATA --> VAL
    VAL --> V1["미발행 슬롯을 reads/modifies 가 짚음 → 순서 실수"]
    VAL --> V2["꺼질 수 있는 노드가 writes 보유 → 꺼진 프레임에 무효 핸들"]
    VAL --> V3["같은 슬롯을 둘이 writes → 둘째는 modifies 여야 함"]
```

> **`modifies` 규약이 하는 일**: 꺼질 수 있는 노드는 `modifies` 만 가질 수 있다.
> 노드가 꺼져도 블랙보드 슬롯 값이 그대로 남아 뒤 노드가 이어받는다.
> 예전 `X.GetOutput().IsValid() ? X : 이전값` 폴백 체인이 통째로 사라진 자리다.

### 3-3. 패스 노드 그래프 — 실제 배선

노드 목록의 **순서가 곧 실행 순서**다. `BuildPipelineDesc` 가 짜 둔 목록을 그대로 돈다.

```mermaid
flowchart TB
    classDef slot fill:#1f3a5f,stroke:#4a90d9,color:#fff
    classDef pass fill:#3a2f1f,stroke:#d9a44a,color:#fff
    classDef opt fill:#3a1f2f,stroke:#d94a7a,color:#fff

    SHADOW["Shadow<br/>캐스케이드 3"]:::pass
    GBUF["GBuffer<br/>불투명 draws"]:::pass
    DECAL["Decal"]:::pass
    SSAO["SSAO"]:::pass
    DEFER["Deferred<br/>라이팅 + IBL"]:::pass
    SKY["SkyBox"]:::pass
    SSGI["SSGI<br/>perView"]:::pass
    FWD["Forward+<br/>투명 forwardDraws"]:::pass
    SPRITE["Sprite"]:::pass
    SSS["SSS"]:::pass
    SSR["SSR"]:::pass
    FOG["VolumetricFog<br/>perView · active 토글"]:::opt
    POST["PostChain<br/>톤맵 · 컬러그레이딩 · AA · 비네트"]:::pass
    UI["UI"]:::pass
    CONTRIB["Host 기여 노드<br/>Editor 그리드 · 기즈모"]:::opt
    PRESENT["live_present<br/>공유 텍스처로 복사"]:::pass

    SM["Shadow.Map"]:::slot
    GD["GBuffer.Diffuse"]:::slot
    GM["GBuffer.MetalRough"]:::slot
    GN["GBuffer.Normal"]:::slot
    GE["GBuffer.Emissive"]:::slot
    GB["GBuffer.Bitmask"]:::slot
    GZ["GBuffer.Depth"]:::slot
    AO["Scene.AmbientOcclusion"]:::slot
    LIT["Scene.LitColor"]:::slot
    LDR["Display.LDR"]:::slot

    SHADOW ==> SM
    GBUF ==> GD & GM & GN & GE & GB & GZ

    GZ --> DECAL
    DECAL -.수정.-> GD & GN & GM

    GZ & GN --> SSAO
    SSAO ==> AO

    GD & GM & GN & GE & GZ & SM --> DEFER
    DEFER ==> LIT

    GZ --> SKY
    SKY -.수정.-> LIT

    GZ & GN & GD & GM & AO --> SSGI
    SSGI -.수정.-> LIT

    GZ --> FWD
    FWD -.수정.-> LIT

    GZ --> SPRITE
    SPRITE -.수정.-> LIT

    GZ --> SSS
    SSS -.수정.-> LIT

    GZ & GM & GN & GB --> SSR
    SSR -.수정.-> LIT

    GZ & SM --> FOG
    FOG -.수정.-> LIT

    LIT --> POST
    POST ==> LDR

    UI -.수정.-> LDR
    CONTRIB -.수정.-> LDR
    LDR --> PRESENT
```

| 표기 | 뜻 |
|---|---|
| `==>` 굵은 화살표 | `writes` — 슬롯을 처음 발행 |
| `-.수정.->` 점선 | `modifies` — 읽고 같은 이름으로 재발행 |
| 실선 | `reads` — 앞 노드가 발행한 것을 읽음 |

**슬롯 요약**

| 슬롯 | 발행자 | 수정자 |
|---|---|---|
| `Shadow.Map` | Shadow | — |
| `GBuffer.*` 6종 | GBuffer | Decal — Diffuse · Normal · MetalRough |
| `Scene.AmbientOcclusion` | SSAO | — |
| `Scene.LitColor` | Deferred | SkyBox → SSGI → Forward+ → Sprite → SSS → SSR → Fog |
| `Display.LDR` | PostChain | UI → Host 기여 노드 |

**`perView` 패스**: SSGI · VolumetricFog. 프레임을 넘겨 상태를 잇는다 — 히스토리 2장과
재투영 행렬. 공유하면 각 카메라가 상대의 히스토리를 읽어 잔상이 남는다.

**Host 기여 경계**: Editor 는 `IRenderFeatureContributor` 로 UI 뒤 · `live_present` 앞에
자기 노드를 끼워 넣는다. Player 는 기여자를 설치하지 않으므로 그 노드 자체가 서지 않는다.

### 3-4. 렌더 그래프 내부 — Compile 과 RecordParallel

```mermaid
flowchart TB
    subgraph DECLARE["선언 수집"]
        IMP["ImportTexture / ImportBuffer<br/>외부 리소스 + 현재 상태"]
        CRT["CreateTexture<br/>transient 요청"]
        ADD["AddPass · AddSplitPass<br/>usages + Execute 람다"]
    end

    subgraph COMPILE["Compile"]
        ORDER["BuildOrder<br/>실행 순서 = 선언 순서"]
        CULL["CullPasses<br/>소비자 없는 출력 제거"]
        TRANS["CreateTransients<br/>수명 겹침 계산 → 풀에서 대여"]
        BARR["PlanBarriers<br/>사용 상태 전이에서 배리어 유도"]
        ORDER --> CULL --> TRANS --> BARR
    end

    subgraph RECORD["RecordParallel · 워커 4"]
        SPLIT["패스를 워커에 분배<br/>비용 임계값 기반"]
        RB["RecordPassBarriers<br/>그래프가 이미 넣은 배리어"]
        EXEC["패스 Execute 람다 호출"]
        BATCH["RHIRecordedBatch"]
        SPLIT --> RB --> EXEC --> BATCH
    end

    DECLARE --> COMPILE --> RECORD
    BATCH --> SUBMIT["EnqueueRecordedBatch<br/>제출 스레드"]

    NOTE["lifetimeToken = graph shared_ptr<br/>GPU 완주까지 transient 를 붙든다"]
    BATCH -.-> NOTE
```

> **컬링 주의**: 최종 결과를 소비하는 선언이 없으면 그래프가 체인을 통째로 걷어낸다.
> `live_present` 의 복사가 그 소비 역할을 한다.

---

## 4. 렌더러 구조 — 뷰 · 슬롯 · 표시

`EnhancedSceneRenderer` 는 정적 인터페이스이고, 실체는 `LiveState` 가 든다.

```mermaid
flowchart TB
    subgraph SR["EnhancedSceneRenderer · 정적 API"]
        API1["BuildRequiredAssetPacket"]
        API2["BuildLiveFramePacket"]
        API3["PublishLiveFrame"]
        API4["TickLive"]
        API5["GetLiveDisplaySnapshot / ImTextureId"]
    end

    subgraph LS["LiveState"]
        RQ["renderQueue + 렌더 스레드"]
        RS["RenderScene"]
        POOL["drawPool · decals · spritePool · uiProxy"]
        TUNE["pendingTuning · debugSnapshot"]
        DISP["displaySnapshot · presentationKeys"]
    end

    subgraph LP["LivePipeline · 백엔드별"]
        FCTX["EnhancedFrameContext<br/>services · psoManager · caches"]
        DESCN["LivePipelineDesc"]
        SHARED["공유 패스<br/>Shadow · GBuffer · Decal · SSAO · Deferred<br/>SkyBox · Forward+ · Sprite · SSS · SSR · PostChain · UI"]
        TPOOL["RGTransientPool"]
        V0["CameraView 0 — Editor"]
        V1["CameraView 1 — Game"]
    end

    subgraph CV["CameraView 하나"]
        PVPASS["perView 패스<br/>SSGI · VolumetricFog"]
        S0["DisplaySlot 0"]
        S1["DisplaySlot 1"]
        S2["DisplaySlot 2"]
        PQ["pendingQueue"]
    end

    SR --> LS
    LS --> LP
    LP --> V0 & V1
    V0 --> CV
```

### 표시 슬롯 회전 — 뷰당 3개 · 표시 1 + 인플라이트 2

```mermaid
stateDiagram-v2
    [*] --> Free
    Free --> Recording: RenderOnce 가 슬롯 선택
    Recording --> Pending: EndFrame · fenceValue 기록
    Pending --> Pending: 펜스 미완료 — 논블로킹 확인
    Pending --> Displayed: 펜스 완료 + key 일치<br/>PromoteCompleted
    Displayed --> Free: 다음 승격이 이 슬롯을 대체<br/>graph.reset → transient 반환
    Pending --> Free: view key 불일치 — 카메라 교체로 폐기
```

- `pendingQueue` 가 2 이상이면 그 프레임의 렌더를 건너뛴다 — `framesInFlight` 증가.
- `graph.reset()` 시점이 곧 **transient 텍스처가 풀로 돌아가는** 시점이다. GPU 완주 전에
  놓으면 사용 중인 리소스를 재사용하게 된다.
- 뷰 루프는 `viewRotation++ % cameraCount` 로 시작 인덱스를 돌린다 — 인플라이트 한도에
  걸릴 때 한 뷰만 계속 굶는 것을 막는다.

---

## 5. 머테리얼 구조

### 5-1. 자산에서 GPU 까지의 전 경로

```mermaid
flowchart TB
    subgraph AUTHOR["저작 정본 · 디스크"]
        MASSET[".asset — experiment::Material<br/>properties · keywords · blendMode<br/>shaderAssetId · TextureReference GUID"]
        SMETA[".shadermeta — ShaderMeta<br/>properties · keywords 축 · passes"]
        HLSL["HLSL / Slang 소스"]
        SMETA --> HLSL
    end

    subgraph COOK["쿠킹"]
        MCOOK["MaterialCookProducer"]
        TCOOK["텍스처 cooked artifact"]
        CATALOG["CookedAssetCatalog<br/>AssetId → artifact 경로"]
        MASSET --> MCOOK --> CATALOG
        TCOOK --> CATALOG
    end

    subgraph RUNTIME["런타임 · 게임 스레드"]
        LEGACY["Material — legacy 저작 객체<br/>m_propertyValues · m_shaderMetaGuid<br/>m_textureOwners · m_renderingMode"]
        AUTHORED["experiment::Material — 저작 정본 사본"]
        MINST["MaterialInstance<br/>propertyOverrides · keywordOverrides"]
        MR["MeshRenderer / FoliageComponent"]
        MR --> LEGACY
        MR --> AUTHORED
        MINST -.BuildEffectiveMaterial.-> AUTHORED
    end

    subgraph RESOLVE["해석 · MaterialResolver"]
        RSHADER["shaderAssetId → ShaderMetaHandle + generation"]
        RTEX["TextureReference → texture generation owner"]
        RKEY["keywords → 축 순서 정규화 인덱스"]
    end

    subgraph SEALED["밀봉 · 렌더 스레드"]
        PACK["MaterialPropertyPacker<br/>논리 값 → propertyBytes b2"]
        TBIND["textureBindings<br/>propertyName · GUID · register · owner"]
        PERM["RHIShaderPermutationKey"]
        SNAP["EnhancedMaterialDrawSnapshot<br/>EnhancedForwardMaterialDrawSnapshot"]
        PACK --> SNAP
        TBIND --> SNAP
        PERM --> SNAP
    end

    subgraph GPU["GPU"]
        BATCH["MaterialKey 로 배치<br/>textures 4 + snapshot"]
        PSO["ShaderVariantKey → PSO"]
        CB["b2 상수 버퍼 + SRV + InstanceData"]
    end

    AUTHORED --> RESOLVE
    LEGACY -.전환기 폴백.-> RESOLVE
    CATALOG --> RTEX
    SMETA --> RSHADER
    RESOLVE --> SEALED
    SNAP --> BATCH --> PSO --> CB
```

### 5-2. 텍스처 해석 우선순위 — fail-closed

```mermaid
flowchart TB
    START["TextureReference · AssetId"] --> NIL{"nil assetId?"}
    NIL -->|예| NONE["텍스처 없음 — 정상"]
    NIL -->|아니오| GEN{"model generation<br/>closure 안에 있는가?"}
    GEN -->|예| G["embedded texture owner<br/>fromGenerationClosure"]
    GEN -->|아니오| COOKED{"cooked artifact 가<br/>해석되는가?"}
    COOKED -->|예| C["cooked owner<br/>fromCookedArtifact"]
    COOKED -->|아니오| SRC{"source 경로가<br/>해석되는가?"}
    SRC -->|예| S["source owner<br/>sourceFallbackTextures 증가"]
    SRC -->|아니오| FAIL["해석 실패 — false 반환<br/>호출부가 legacy 경로로 내려감"]
```

> 폴백은 **관측 가능해야** 한다. `ResolvedMaterialNotes` 가 cooked / source / generation
> 건수를 센다 — cooked 가 늘 비고 조용히 source 로 도는 상태는 "느리지만 동작하는"
> 모습이라 아무도 알아채지 못한다.

### 5-3. 프레임 밀봉 결정 — 어느 경로로 seal 하는가

```mermaid
flowchart TB
    START["PooledDraw · materialSource + authoredMaterialSource"] --> CACHE{"이번 프레임<br/>캐시에 있는가?"}
    CACHE -->|예| REUSE["기존 snapshot 재사용"]
    CACHE -->|아니오| AUTH{"authoredMaterialSource<br/>가 있는가?"}

    AUTH -->|예| FROMAUTH["BuildSealSourceFromAuthored<br/>legacy Material 을 읽지 않는 유일한 입구"]
    AUTH -->|아니오| FROMLEG["BuildSealSourceFromLegacy<br/>MaterialInfo 3필드 폴백 승계"]
    FROMLEG --> APPLY["ApplyAuthoredMaterial<br/>저작본이 있으면 properties·keywords 교체"]

    FROMAUTH --> CORE
    APPLY --> CORE

    CORE["SealCore"] --> NORM["NormalizeMaterialKeywordSelections<br/>이름 기반 keywords 가 정본"]
    NORM --> PACKB["propertyBytes 생성<br/>MaterialPropertyPacker"]
    PACKB --> BINDS["textureBindings 생성<br/>register 검증 · 중복 거부"]
    BINDS --> VARIANT["EnsureShaderMetaVariant<br/>permutation 별 PSO 확보"]
    VARIANT --> OUT["immutable snapshot → EnhancedDrawItem"]
```

### 5-4. 왜 스냅샷인가 — 렌더가 게임 객체를 들지 않는 이유

`EnhancedDrawItem` 은 `Material*` · `Mesh*` 를 **신원으로 쓰지 않는다**.

| 예전 | 지금 | 이유 |
|---|---|---|
| `Material*` 를 draw packet 에 실음 | `EnhancedMaterialDrawSnapshot` 값 복사 | 수명·스레드 규약이 얽힘 |
| `Mesh*` 포인터로 정렬 | `geometryKey` — 자산 신원 ⊕ 메시 인덱스 | 주소는 할당 순서라 실행마다 순서가 달라짐 |
| `Texture*` 배열 순서 = shader register | `EnhancedMaterialTextureBinding` 에 register 명시 | 배열 순서가 register 를 암묵적으로 뜻하면 조용히 밀림 |
| Shadow 패스가 `draw.mesh` 역참조 | `boundRadius` 를 값으로 실음 | 패스가 게임 자료구조를 읽는 마지막 자리 |

**바이트 레이아웃 계약**은 `static_assert` 로 고정한다 — `EnhancedLight` 64 B,
`EnhancedForwardMaterialFlowSnapshot` 32 B, `InstanceData` 96 B. HLSL 쪽과 어긋나면
값이 조용히 밀려 "재질이 이상하다"로만 드러난다.

---

## 6. 드로우 수집 — 프록시에서 배치까지

```mermaid
flowchart TB
    subgraph COLLECT["BuildDrawPool · 프레임당 1회 · 카메라 무관"]
        PROXY["PrimitiveRenderProxy 스냅샷"]
        PM["poolMesh — MeshRenderProxy"]
        PF["poolFoliage — FoliageRenderProxy"]
        PS["poolSprite — SpriteRenderProxy"]
        PD["poolDecal — DecalRenderProxy"]
        MV["BuildRHIModelMeshView<br/>generation descriptor ↔ immutable 저장소 대조"]
        POOLED["PooledDraw<br/>item + materialSource + worldBounds + isTransparent"]
        PROXY --> PM & PF & PS & PD
        PM --> MV --> POOLED
        PF --> MV
    end

    subgraph PERVIEW["CaptureFromView · 뷰마다"]
        FRUS{"절두체 생성 가능?<br/>원근 투영만"}
        CULLED["절두체 컬링<br/>스키닝은 자르지 않음"]
        SPLIT{"isTransparent?"}
        OPAQUE["draws — 불투명"]
        TRANSP["forwardDraws — 투명"]
        SORT["back-to-front stable_sort<br/>깊이 동률은 mesh 포인터로 결정"]
        LIGHTS["SelectLightsForView<br/>절두체 밖 지역광 제외 · 기여도 순 정렬"]
        FRUS -->|예| CULLED --> SPLIT
        FRUS -->|아니오| SPLIT
        SPLIT -->|아니오| OPAQUE
        SPLIT -->|예| TRANSP --> SORT
    end

    subgraph DRAW["패스"]
        GB["GBuffer<br/>MaterialKey 로 배치 · InstanceData 업로드"]
        FW["Forward+<br/>받은 순서대로 그림"]
    end

    POOLED --> FRUS
    OPAQUE --> GB
    SORT --> FW
    LIGHTS --> GB
```

**투명 정렬의 동률 처리**: 같은 원점을 쓰는 투명면 — 십자 빌보드, 같은 피벗의 유리
여러 장 — 은 깊이가 정확히 같다. 비교자가 `false` 만 돌려주면 순서가 미정이라 프록시
스냅샷 순서가 바뀔 때마다 앞뒤가 뒤집혀 깜빡인다. 정렬을 넣은 이유가 "순서를
고정한다"인데 그 자리에서 새는 셈이라, 동률은 포인터로 가른다.

---

## 7. 한눈에 보는 데이터 흐름

```mermaid
flowchart LR
    C["Component<br/>MeshRenderer · Light · Image"] --> P["RenderProxy"]
    P --> PD["PooledDraw"]
    PD --> DI["EnhancedDrawItem<br/>+ MaterialDrawSnapshot"]
    DI --> PASS["EnhancedRenderPass"]
    PASS --> RG["RGHandle · 블랙보드 슬롯"]
    RG --> ENC["RHIEncoder 커맨드"]
    ENC --> TEX["공유 텍스처"]
    TEX --> IMG["ImGui 표시"]
```

각 화살표는 **값 복사 경계**다. 오른쪽 층은 왼쪽 층의 객체를 포인터로 붙들지 않는다.
`RenderProxy → PooledDraw` 만 예외로 `shared_ptr` 을 들고, 그것도 `BuildDrawPool` 의
안정된 프레임 경계 동안 generation owner 의 수명을 보장하기 위해서다.
