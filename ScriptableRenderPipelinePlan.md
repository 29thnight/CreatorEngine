# Scriptable Render Pipeline · Custom Pass 설계 (PHASE 4)

2026-08-16. PHASE 4의 차세대 GPU 기능 구상에 앞서 확정한 파이프라인 확장 계약.

상태: **설계 기준선 확정. 구현은 PHASE 4의 통합 게이트에서 별도 페이즈와 공수를 정한다.**

---

## 0. 결정 요약

CreatorEngine의 공개 렌더 확장 모델은 다음으로 고정한다.

1. **일반 개발자는 C#으로 파이프라인과 신규 Pass를 작성한다.**
   그래픽스·Compute 셰이더, 입력/출력 슬롯, 렌더 상태, 실행 조건과 폴백을
   `RenderPipelineAsset`·`ScriptableRenderPass` API로 기술한다.
2. **C#은 설계·프레임 데이터 생산 계층이고 GPU 실행 계층이 아니다.**
   Render/CommandBuild/RHI 제출 스레드는 CLR에 진입하지 않는다. C#이 만든 기술과
   프레임 파라미터를 Game 스레드에서 밀봉한 뒤 네이티브가 실행한다.
3. **고급 네이티브 Pass는 소스 수준 확장으로 남긴다.**
   DXR·DLSS·GPU-driven처럼 특수 RHI/SDK가 필요한 기능은 현재의
   `EnhancedRenderPass`를 C++ 소스에 추가하고 엔진을 다시 빌드한다.
4. **외부 Native DLL Pass ABI는 만들지 않는다.**
   이 저장소는 오픈소스이고 전체 엔진을 재빌드할 수 있다. ABI 버전·DLL hot
   unload·인플라이트 GPU 자원 수명까지 떠안는 바이너리 SDK는 현재 소비자가 없다.
5. **`EnhancedRenderGraph`는 유일한 실행 그래프로 유지한다.**
   새 계층은 그래프를 대체하지 않고, 기존 `LivePipelineDesc`가 하던 영속 조립을
   개발자 작성 기술로부터 컴파일한다.

한 줄로 줄이면:

> **Unity식 C# 작성 경험 + CreatorEngine식 Game→Render 밀봉 + 네이티브 RenderGraph 실행.**

---

## 1. 왜 PHASE 4의 첫 계약인가

PHASE 4의 네 기능은 단독 효과가 아니다.

- GPU-driven rendering은 가시성·인스턴스·간접 명령과 material/PSO 분류를 바꾼다.
- Stochastic Tile-Based Lighting은 깊이·노멀·모션·히스토리와 조명 후보 버퍼를 쓴다.
- DXR은 BLAS/TLAS·셰이더 테이블·래스터 폴백을 파이프라인에 끼운다.
- DLSS는 color/depth/motion/jitter/exposure/reactive mask와 UI 합성 위치를 요구한다.

이 네 기능을 지금처럼 C++의 고정 목록에 각각 직접 배선하면, 프로젝트마다 다른
품질 조합과 백엔드 폴백을 넣을 때 `BuildPipelineDesc`가 다시 거대한 조건문이 된다.
그러면 기능 구상 뒤에 파이프라인 확장 모델을 붙이는 순간 네 설계를 다시 뜯어야
한다. 따라서 **확장 계약이 네 기능 설계의 입력**이어야 한다.

PHASE 4의 순서는 다음이다.

```text
지원 행렬·기준선
    ↓
Scriptable Pipeline·Custom Pass 계약
    ↓
GPU-driven / Stochastic Lighting / DXR / DLSS 구상
    ↓
공통 의존 그래프·수직 슬라이스·구현 페이즈 확정
```

---

## 2. 현재 코드의 사실

### 2.1 조립은 기술화됐지만 아직 C++에 고정돼 있다

`RenderEngine/Render/Core/EnhancedLivePipelineDesc.h`는 이미 역할을 둘로 갈랐다.

```text
LivePipelineDesc      영속. 무엇을·어떤 순서로·꺼지면 어떻게 흐를지
EnhancedRenderGraph   프레임당. 배리어·컬링·transient 수명·실행
```

`EnhancedSceneRendererLive.cpp::BuildPipelineDesc`에는 Shadow부터 `live_present`까지
**19개 노드**가 선언돼 있다. 이전 `RenderOnce`의 200여 줄 배선을 한 목록으로
모았다는 점은 맞지만, 각 노드는 여전히 다음을 잡는 C++ 접착이다.

- 구체 `EnhancedRenderPass` 인스턴스 참조
- `std::function` 활성 조건·initialize·prepare·shutdown·declare
- `LiveBlackboard`와 타입 있는 Pass Inputs 사이 변환
- C++ 구조체 `LiveFrameBinding`

따라서 `LivePipelineDesc`는 좋은 **컴파일 결과물**이지만 파일 직렬화나 C# 공개
API로 그대로 내보낼 수 있는 **작성 형식**은 아니다. 특히 `std::function`과 캡처
참조는 프로세스 밖 데이터도, 관리/네이티브 경계 데이터도 아니다.

### 2.2 Pass와 RHI의 네이티브 실행 계약은 이미 서 있다

`EnhancedRenderPass`의 수명은 다음 네 단계다.

```text
Initialize      PSO·정적 자원 준비
PrepareFrame    프레임 업로드·이번 프레임 자료 준비
Declare         그래프에 자원 사용·실행 콜백 선언
Shutdown        해제
```

`Declare`에서는 자원을 만들거나 GPU 명령을 즉시 기록하지 않는다. 실제 기록은
`EnhancedRenderGraph::ExecuteContext`가 주는 백엔드 중립 `RHIEncoder`를 통한다.
RenderGraph는 사용 선언으로 배리어·컬링·transient 수명·병렬 기록 단위를 정한다.

이 경계를 C# 때문에 다시 열 이유가 없다. C#에서 D3D12/Vulkan 핸들이나 배리어를
말하기 시작하면 PHASE 3에서 닫은 RHI 경계를 재도입하는 셈이다.

### 2.3 현재 ScriptCore에는 렌더 API가 없다

`ScriptCore`·`ScriptBinder`·`GameScripts`에서 다음 공개 이름의 소비자는 모두 0이다.

```text
RenderGraph / RenderPipeline / LivePipelineDesc / IRHI / RHICommand / CommandEncoder
```

`ScriptBinder/ClrHost.h`는 더 중요한 스레딩 규약을 이미 적어 두었다.

- 관리 호출은 스크립트 수와 무관하게 틱 경계에서 일괄 수행한다.
- GC 때문에 관리 호출은 게임 스레드 전용이다.
- 렌더 스레드가 관리 호출에 물리면 프레임 전체가 GC에 묶인다.
- 잡 스레드에서 생긴 자료도 큐에 모은 뒤 게임 스레드에서 한 번에 넘긴다.

새 렌더 스크립팅도 이 규약의 예외가 아니다. Unity처럼 Render 스레드에서 C#
`Execute()`를 직접 호출하는 모델을 복제하면 3-2에서 만든 Game→Render 비동기
경계를 스크립팅 기능이 다시 허문다.

### 2.4 Native Pass 플러그인 기반은 없다

현재 코드에는 `PassRegistry`, `RegisterRenderPass`, 렌더 플러그인 로더가 없다.
`LoadLibraryW` 사용은 Vulkan 로더와 CoreCLR hostfxr 로더뿐이다. 즉 Native DLL Pass
SDK를 선언하면 작은 어댑터를 추가하는 일이 아니라 새 플러그인 시스템 전체를
만드는 일이 된다.

### 2.5 PHASE 3.5가 선행한다

C# Custom Pass가 셰이더 애셋과 entry point를 가리키려면 다음이 먼저 닫혀야 한다.

- DXC 단일 컴파일 서비스
- DXIL/SPIR-V 공통 셰이더 메타와 리플렉션
- 렌더 상태 기술과 PSO 캐시 키
- 안정 핸들·세대 기반 shader/PSO hot reload
- material/pass binding layout 검증

따라서 이 문서는 API 계약을 지금 확정하지만, 실행 구현은 `MaterialPipelinePlan`
M1~M7의 산출물을 소비한다. 별도 셰이더 컴파일 경로를 만들지 않는다.

---

## 3. 목표 / 비목표

### 목표

- 게임 개발자가 엔진 C++을 수정하지 않고 C#+HLSL로 일반적인 Raster·Compute Pass를
  작성하고 파이프라인에 넣는다.
- 프로젝트별로 Pass 순서·조건·품질·백엔드 폴백을 기술할 수 있다.
- 입력/출력·리소스 형식·바인딩·기능 지원을 구축 시점에 검증한다.
- 같은 파이프라인 기술이 DX12와 Vulkan에서 같은 토폴로지를 만든다.
- Game→Render 스냅샷, RenderGraph 자원 수명, RHI 중립성을 보존한다.
- C# hot reload 실패가 현재 정상 파이프라인을 파괴하지 않는다.
- 소스 기여자가 기존 `EnhancedRenderPass`로 특수 네이티브 기능을 추가할 수 있다.

### 비목표

- C#에 `ID3D12Resource*`, `VkImage`, raw command list/command buffer를 공개하지 않는다.
- C#이 배리어·descriptor heap·fence·transient aliasing을 직접 관리하지 않는다.
- Render/CommandBuild/RHI 제출 스레드에서 CLR 콜백을 호출하지 않는다.
- 매 프레임 임의의 C# 코드가 파이프라인 토폴로지를 통째로 재작성하지 않는다.
- `EnhancedRenderGraph`를 새 관리 그래프로 대체하지 않는다.
- Pass 순서를 데이터 의존으로 자동 위상 정렬하지 않는다. 목록 순서가 실행 순서다.
- Native DLL Pass ABI, DLL hot reload, 외부 바이너리 Pass 마켓을 만들지 않는다.
- PHASE 4에서 실제 DXR·DLSS SDK나 Custom Pass 런타임을 먼저 구현하지 않는다.

---

## 4. 외부 엔진에서 가져올 것과 버릴 것

| 항목 | Unity | Unreal | CreatorEngine 결정 |
|---|---|---|---|
| 전체 파이프라인 | C# `RenderPipeline` | 고정 C++ Renderer, 깊은 변경은 엔진 소스 | C# 기술 → 네이티브 컴파일 |
| 일반 Custom Pass | C# `ScriptableRenderPass`/`CustomPass` | C++ Plugin/Module + RDG | C# `ScriptableRenderPass` |
| GPU 명령 표현 | CommandBuffer/RenderGraph | C++ RDG/RHI command list | 제한된 PassBuilder → 네이티브 RHIEncoder |
| 네이티브 탈출구 | Native Rendering Plugin | C++ Plugin 또는 엔진 포크 | 소스 수준 `EnhancedRenderPass` |
| 프레임 중 C# 실행 | 허용 | 해당 없음 | Game 스레드의 일괄 데이터 생산만 허용 |
| 바이너리 Pass ABI | 별도 Native Plugin API | UBT가 C++ Plugin을 함께 빌드 | 지금 만들지 않음 |

Unity에서 가져오는 것은 **C#으로 전체 파이프라인과 일반 Pass를 작성하는 경험**이다.
가져오지 않는 것은 C# `Render()`/`Execute()`가 매 카메라·매 Pass GPU 명령을 직접
쌓는 실행 모델이다. CreatorEngine에는 이미 명시적인 비동기 프레임 스냅샷과
네이티브 병렬 기록 경계가 있기 때문이다.

Unreal에서 가져오는 것은 **고급 기능이 필요하면 엔진 소스의 C++ Pass를 수정할 수
있다는 탈출구**다. 가져오지 않는 것은 일반 효과부터 C++ 프로젝트 모듈을 요구하고,
공개 삽입점 밖의 파이프라인 변경마다 엔진 포크를 요구하는 기본 경험이다.

참고한 공개 문서:

- [Unity RenderPipeline.Render](https://docs.unity3d.com/kr/current/ScriptReference/Rendering.RenderPipeline.Render.html)
- [Unity 6 URP Custom Pass 흐름](https://docs.unity3d.com/cn/6000.0/Manual/urp/renderer-features/custom-rendering-pass-workflow-in-urp.html)
- [Unity HDRP C# Custom Pass](https://docs.unity3d.com/kr/Packages/com.unity.render-pipelines.high-definition%4010.5/manual/Custom-Pass-Scripting.html)
- [Unreal Render Dependency Graph](https://dev.epicgames.com/documentation/unreal-engine/render-dependency-graph-in-unreal-engine?lang=en-US)
- [Unreal Plugin](https://dev.epicgames.com/documentation/en-us/unreal-engine/plugins-in-unreal-engine)

---

## 5. 계층 설계

```text
GameScripts / Project Assembly
  RenderPipelineAsset · ScriptableRenderPass · 직렬화 설정
                         │
                         │ Configure/Describe (로드·리로드·설정 변경 시)
                         ▼
ScriptCore.Rendering
  PipelineBlueprint · PassBlueprint · ResourceSchema · VariantSchema
                         │
                         │ 단일 배치 경계
                         ▼
Native Pipeline Compiler
  Pass type/slot/shader/capability 검증 · 안정 ID 해석 · variant 컴파일
                         │
                         ▼
CompiledPipelineDesc (불변 · generation 보유)
  ├─ NativePassFactory → 소스 링크된 EnhancedRenderPass
  └─ ScriptedRenderPassAdapter → 셰이더 기반 네이티브 실행 템플릿
                         │
                         ▼
LivePipelineDesc → EnhancedRenderGraph → RHIEncoder → DX12/Vulkan
```

### 5.1 `LivePipelineDesc`는 컴파일 타깃이다

첫 구현은 `CompiledPipelineDesc`를 기존 `LivePipelineDesc` 노드로 낮춘다. 이미
검증된 Initialize/Prepare/Declare/Shutdown 순서와 블랙보드 슬롯 검증을 재사용한다.

단, 공개 API가 `LivePassNode`를 직접 만들지는 않는다.

- `LivePassNode`는 C++ 함수 객체와 인스턴스 포인터를 가진다.
- 공개/직렬화 기술은 안정 ID와 POD 설정만 가진다.
- 네이티브 컴파일러가 안정 ID를 실제 Pass factory·adapter로 해석한다.

향후 `Live` 접두사가 제거되거나 구현 구조가 바뀌어도 C# API와 asset schema가
영향받지 않도록 두 형식을 분리한다.

### 5.2 핵심 데이터

이름은 구현 시 바뀔 수 있지만 책임은 다음으로 고정한다.

| 타입 | 수명 | 책임 |
|---|---|---|
| `RenderPassTypeId` | 영속 | Pass 타입의 안정 GUID. C# 타입명·C++ 클래스명 변경과 분리 |
| `RenderSlotId` | 영속 | 사람이 읽는 이름 + 직렬화용 안정 hash/GUID |
| `PassBlueprint` | 작성 시 | 타입 ID, read/write/modify 슬롯, queue, 조건, 설정 blob |
| `ResourceSchema` | 작성 시 | texture/buffer 형식·크기 정책·sample·usage·history 여부 |
| `PipelineBlueprint` | 작성 시 | 명시적 순서의 Pass 목록과 variant/fallback 규칙 |
| `CompiledPassDesc` | 런타임 | factory/adapter, 검증된 슬롯 인덱스, shader/PSO 핸들 |
| `CompiledPipelineDesc` | 런타임 | 불변 노드 배열, generation, dump/hash, backend capability |
| `RenderPassFramePacket` | 프레임 | 관리 측이 Game 스레드에서 밀봉한 Pass별 POD 파라미터 |

문자열은 작성·오류·덤프에는 남기되 프레임 실행에서 map 탐색하지 않는다. 컴파일 시
슬롯과 Pass를 조밀한 정수 인덱스로 낮춘다.

---

## 6. C# 작성 API

### 6.1 파이프라인

첫 버전의 정본은 **C# 클래스가 토폴로지를 정의하고 Pipeline Asset이 클래스 타입과
직렬화 필드를 보유하는 방식**이다. 별도 JSON과 C# 두 군데에 순서를 중복하지 않는다.

```csharp
[RenderPipeline("8c969cae-6708-47b0-bf0b-3de811333a63")]
public sealed class NextRenderPipeline : RenderPipelineAsset
{
    [SerializeField] public bool EnableRayTracing;

    protected override void Configure(RenderPipelineBuilder pipeline)
    {
        pipeline.Add<ShadowPass>();
        pipeline.Add<GpuVisibilityPass>();
        pipeline.Add<GBufferPass>();
        pipeline.Add<StochasticLightingPass>();

        pipeline.When(Capability.RayTracing && EnableRayTracing)
            .AddNative("Creator.DXR.Reflections")
            .Fallback<SSRPass>();

        pipeline.When(Capability.DLSS)
            .AddNative("Creator.DLSS")
            .Fallback<TaaUpscalePass>();
    }
}
```

`Configure` 호출 시점:

- Pipeline Asset 최초 로드
- C# assembly reload 성공
- Inspector에서 토폴로지/품질 설정 변경
- backend·GPU capability 또는 품질 tier 변경으로 variant 재선택이 필요한 때

매 프레임 호출하지 않는다. 매 프레임 변하는 on/off는 미리 검증된 native predicate나
`PipelineVariantKey`로 선택한다.

### 6.2 신규 C# Pass

일반 Pass 하나가 내부 RenderGraph 노드 하나일 필요는 없다. `Describe`는 하나 이상의
Raster·Compute·Copy 작업을 정적인 순서로 기술할 수 있다.

```csharp
[RenderPass("f0354d44-6ccc-4d80-a047-2d55a89c3b23")]
public sealed class OutlinePass : ScriptableRenderPass
{
    [SerializeField] public Color Color = Color.Black;
    [SerializeField] public float Threshold = 1.0f;

    public override void Describe(RenderPassBuilder pass)
    {
        TextureRef depth = pass.ReadTexture("Scene.Depth");
        TextureRef color = pass.ModifyTexture("Scene.LitColor");

        pass.AddFullscreen("Outline", "Shaders/Outline.shader", "PSMain")
            .Bind("Depth", depth)
            .RenderTarget(color);
    }

    public override void WriteFrameData(RenderPassFrameDataWriter data)
    {
        data.Set("Color", Color);
        data.Set("Threshold", Threshold);
    }
}
```

`Describe`는 리로드/설정 변경 시 Pipeline Blueprint를 만든다. `WriteFrameData`는
필요한 Pass만 Game 스레드에서 실행하며, 모든 Pass의 값을 한 연속 packet에 쓴 뒤
네이티브 경계를 **프레임당 최대 한 번** 넘는다. 렌더 스레드에서 C# 메서드를 다시
부르지 않는다.

### 6.3 지원할 작업

첫 공개 표면은 다음으로 제한한다.

- Fullscreen raster draw
- RendererList draw(필터·queue·material override는 선언 데이터)
- Direct/indirect draw의 엔진 제공 템플릿
- Compute dispatch와 출력 크기 기반 dispatch 식
- Copy/resolve
- transient texture/buffer 선언
- persistent history 요청
- graphics/async-compute queue 선호와 capability fallback
- shader parameter block과 reflected resource binding

반복 횟수가 프레임 값에 따라 달라지는 알고리즘은 C#에서 커맨드 반복문을 실행하지
않고, 최대 범위를 가진 native template·indirect dispatch·variant로 표현한다.

### 6.4 공개하지 않을 작업

- raw `RHIEncoder`
- backend command list/buffer
- descriptor table/heap 직접 조립
- barrier/fence 직접 삽입
- 그래프 리소스의 실제 native pointer 획득
- 실행 람다에 관리 객체 캡처

---

## 7. C++ Native Pass의 위치

### 7.1 필요한 것은 소스 확장이지 플러그인 ABI가 아니다

다음 기능은 C++ `EnhancedRenderPass`로 구현한다.

- DXR acceleration structure·shader table
- DLSS/Streamline 등 벤더 SDK 수명과 callback
- GPU-driven visibility·indirect command 생성의 특수 RHI 경로
- readback·외부 공유 자원·플랫폼 전용 interop
- 공개 PassBuilder로 표현할 수 없는 새 RHI 기능의 첫 수직 슬라이스

이 Pass는 엔진 또는 프로젝트가 함께 빌드하는 소스 모듈이다. 배포된 DLL을 런타임에
찾아 ABI를 협상하는 시스템이 아니다.

### 7.2 정적 `NativePassRegistry`

C# Pipeline Asset이 네이티브 Pass를 안정 ID로 참조하려면 작은 정적 registry는
필요하다.

```text
"Creator.DXR.Reflections" + settings schema version
        ↓ NativePassRegistry (링크 시 등록)
factory(context, validated settings)
        ↓
EnhancedRenderPass 인스턴스
```

Registry의 책임:

- 안정 ID → factory
- settings schema/version 검증
- 요구 capability·queue·입출력 schema 공개
- 중복 ID·누락 backend fallback 거부

Registry의 비책임:

- DLL 검색·로드·언로드
- C++ ABI 호환
- 네이티브 hot reload
- 패키지 마켓/서명

`REGISTER_RENDER_PASS` 같은 매크로는 현재 존재하지 않는다. 구현 시 정적 생성자에
의존하지 않는 명시적 module registration이나 생성 코드 중 하나를 고른다.

---

## 8. 스레딩과 프레임 경계

### 8.1 두 packet

관리/네이티브 경계를 두 종류로 분리한다.

```text
PipelineCompilePacket   드묾. 토폴로지·schema·설정·variant
RenderPassFramePacket   매 프레임. 이번 generation의 POD 파라미터
```

둘 다 Game 스레드에서 만들고 네이티브 소유 메모리에 일괄 기록한다. packet은
`generation`, `frameId`, `viewCount`, Pass별 offset/size를 가진다. 렌더 측은 packet
안의 값만 읽으며 관리 객체, 문자열 포인터, GC handle을 보유하지 않는다.

### 8.2 스레드별 허용 작업

| 스레드 | 허용 | 금지 |
|---|---|---|
| Game | C# Configure/Describe/WriteFrameData, packet 밀봉 | GPU command 기록 |
| Render/Presentation | compiled desc 선택, RenderGraph 선언 | CLR 호출, live GameObject 접근 |
| CommandBuild worker | native ExecuteCallback, RHIEncoder 기록 | CLR 호출, pipeline rebuild |
| RHI submission | 제출·fence | CLR 호출, 설정 해석 |

### 8.3 세대 교체

1. Game 스레드가 새 Blueprint와 hash를 만든다.
2. 네이티브 컴파일러가 별도 `CompiledPipelineDesc(generation+1)`를 완성하고 검증한다.
3. 다음 안전 프레임 경계에서 포인터를 교체한다.
4. 이전 generation은 그것을 참조한 마지막 submission fence 뒤에 해제한다.
5. 컴파일·초기화 실패 시 이전 정상 generation을 계속 사용하고 오류를 에디터에 낸다.

Pipeline reload가 Game/Render 배리어가 되어서는 안 된다. 일시 정지가 필요한 초기
구현이라면 에디터 전용 명시적 stall로 계수하고, 플레이 런타임 계약으로 승격하지
않는다.

### 8.4 Unity와 다른 결정

Unity는 C# `Render()`·`AddRenderPasses()`·Custom Pass `Execute()`를 프레임/카메라
단위로 호출한다. CreatorEngine은 같은 작성 능력을 **정적 기술 + 프레임 packet**으로
나눠 얻는다. 임의 C# 제어 흐름을 GPU 기록 시점에 허용하지 않는 대신 다음을 얻는다.

- GC pause가 Render/CommandBuild 스레드에 전파되지 않음
- 관리 객체 수명과 인플라이트 GPU 수명 분리
- C# 유무와 무관한 native Player 렌더 실행
- DX12/Vulkan 공통 graph validation
- 병렬 command recording 유지

---

## 9. 리소스·셰이더 계약

### 9.1 슬롯 접근

Pass는 각 슬롯을 `read`, `write`, `modify` 중 하나로 선언한다.

- `read`: 앞선 Pass가 발행한 값을 소비한다.
- `write`: 새 값을 최초 발행한다.
- `modify`: 기존 값을 읽고 새 버전을 같은 논리 슬롯에 발행한다.

기존 `LivePipelineDesc::Validate` 규칙을 확장해 형식·sample·dimension·queue까지
검사한다. 실행 순서는 C# 목록 순서이며 자동 재정렬하지 않는다.

### 9.2 transient와 history

- transient 자원은 RenderGraph가 만들고 마지막 소비 뒤 회수한다.
- history는 `(pipeline generation, viewId, slotId)`가 소유 키다.
- Scene/Game View가 같은 history를 공유하지 않는다.
- 해상도·format·view count 변경은 history invalidation 사유를 명시한다.
- 비활성 fallback이 history를 유지할지 폐기할지 schema에 적는다.

### 9.3 셰이더와 바인딩

Custom Pass는 PHASE 3.5의 shader asset ID, entry point, permutation key만 참조한다.
Pipeline Compiler는 리플렉션 결과로 다음을 검증한다.

- 선언한 슬롯과 shader resource binding 일치
- constant/structured buffer layout과 frame packet layout 일치
- render target/depth format과 PSO metadata 일치
- backend target(DXIL/SPIR-V) 존재
- permutation 수 상한

C# API가 자체 shader compiler나 자체 binding 번호 체계를 만들지 않는다.

---

## 10. 검증 규칙

### 10.1 타입·직렬화

- Pipeline/Pass 안정 GUID 중복 거부
- 누락·버전 불일치 type ID 거부
- 직렬화할 수 없는 설정 타입 거부
- C# 타입 rename 뒤에도 GUID가 같으면 asset 유지

### 10.2 토폴로지

- 앞에서 발행하지 않은 슬롯 read/modify 거부
- 동일 슬롯 중복 write 거부
- optional writer가 새 필수 슬롯을 유일하게 발행하는 구성 거부
- 동일 Pass 인스턴스의 금지된 중복 삽입 거부
- 명시되지 않은 side effect는 Pass culling 대상임을 경고

### 10.3 리소스

- format/sample/dimension 불일치 거부
- 동일 Pass의 충돌하는 read/write usage 거부
- history ownership·view policy 누락 거부
- graphics 전용 resource를 async compute에서 쓰는 구성 거부
- transient handle을 frame packet에 저장하는 구성 거부

### 10.4 capability와 폴백

- DXR/DLSS 같은 조건부 Pass는 모든 지원 행렬에 fallback 또는 명시적 pipeline
  invalid 사유를 가져야 한다.
- DX12 전용 Pass를 Vulkan variant가 조용히 건너뛰지 않는다.
- fallback 전후 출력 슬롯 schema가 같아야 한다.

### 10.5 스레딩

- Render/CommandBuild/RHI 스레드의 managed entry count는 항상 0이다.
- frame packet generation과 compiled pipeline generation이 다르면 실행하지 않는다.
- hot reload 중 이전 generation의 GPU fence가 끝나기 전에 자원을 해제하지 않는다.

---

## 11. 캐시·직렬화·에디터

### 11.1 정본과 캐시

정본은 다음 둘이다.

- C# Pipeline/Pass 소스
- Pipeline Asset의 직렬화 설정

Compiled Pipeline은 파생 캐시다. 키에는 최소 다음이 들어간다.

```text
pipeline/pass stable IDs + assembly/source fingerprint
+ serialized settings hash
+ shader metadata/permutation hash
+ backend + capability mask + quality tier
+ pipeline schema version
```

캐시가 없거나 키가 다르면 다시 컴파일한다. 캐시 파일을 수동 편집하는 경로는 없다.
물리 asset 포맷과 Player 패키징 위치는 PHASE 12 BuildPipelinePlan의 규약을 따른다.

### 11.2 에디터 표면

첫 표면은 Inspector + 읽기 전용 graph preview다.

- Pipeline Asset 선택과 직렬화 필드
- Pass 순서·활성 variant·fallback 표시
- 슬롯 생산자/소비자와 history 소유자
- backend/capability별 compile 결과
- compile error가 Pass·슬롯·shader binding을 함께 지목
- compiled hash·generation·cache hit/miss

노드 기반 시각 편집기는 후속이다. 추가하더라도 같은 `PipelineBlueprint`를 생성하며
C# 토폴로지와 별도 실행 경로를 만들지 않는다.

---

## 12. 구현 후보 슬라이스

PHASE 4는 설계 게이트이므로 아래 번호는 구현 페이즈를 미리 확정하지 않는다.
최종 4-6에서 네 GPU 기능의 공통 기반과 함께 묶거나 분리한다.

### SRP-0 — Blueprint schema와 순수 검증기

- managed/native 공통 안정 ID·POD schema
- C# Configure/Describe 결과를 dump하는 비렌더 selftest
- 잘못된 슬롯·fallback·capability fixture

### SRP-1 — 기본 파이프라인 구조·픽셀 동등 컴파일

- 현재 C++ 기본 파이프라인 19개 노드를 C# asset으로 기술
- `CompiledPipelineDesc → LivePipelineDesc` adapter
- 기존 `dx12.pipeline` dump와 노드 이름·순서·슬롯 문자 일치
- 기존 C++ builder와 A/B 픽셀 동일

### SRP-2 — Fullscreen Custom Pass

- C# Pass + PHASE 3.5 shader asset + frame parameter packet
- Outline/Grayscale fixture
- DX12/Vulkan 같은 출력과 binding validation

### SRP-3 — Compute·RendererList·history

- transient buffer/texture, async compute, renderer filtering
- 카메라별 history와 resize invalidation
- 잘못된 queue/resource 사용 validation

### SRP-4 — variant·hot reload·에디터 preview

- capability/quality fallback
- generation 교체와 fence retirement
- reload 실패 시 이전 pipeline 유지
- graph preview·hash·cache 계측

### SRP-5 — 소스 Native Pass 연결

- 정적 `NativePassRegistry`
- settings schema/version
- DXR 또는 DLSS 최소 native fixture 하나를 C# pipeline에서 선택
- DLL loader 없이 소스 빌드만으로 확장됨을 문서화

---

## 13. 완료 기준

1. **기본 파이프라인 무회귀** — C# asset이 현재 19개 노드의 이름·순서·슬롯을
   동일하게 만들고 DX12/Vulkan 픽셀·validation 회귀를 통과한다.
2. **일반 신규 Pass는 C#으로 완결** — C++ 수정 없이 Fullscreen Raster와 Compute
   예제 각각 하나가 양 backend에서 실행된다.
3. **고급 확장은 소스 Pass** — `EnhancedRenderPass`를 추가하고 정적 registry에
   연결해 C# pipeline이 선택할 수 있다. 외부 DLL ABI는 없다.
4. **관리 실행 격리** — Render/CommandBuild/RHI 제출 스레드 managed callback 0,
   프레임 파라미터 경계 왕복 최대 1회.
5. **RHI 누수 0** — ScriptCore 공개 API와 asset에 D3D12/Vulkan 타입 0건.
6. **구축 시 실패** — 슬롯·format·binding·capability·fallback 오류가 GPU 실행 전에
   Pass와 원인을 지목한다.
7. **안전한 reload** — 성공한 generation만 프레임 경계에서 교체하고 이전 generation은
   fence 뒤 해제, 실패 시 마지막 정상 pipeline 유지.
8. **배포 일치** — Player 빌드가 Pipeline Asset, C# assembly, 셰이더 permutation과
   native Pass 모듈을 빠짐없이 포함하며 editor 전용 API에 링크하지 않는다.

---

## 14. 다른 계획과의 관계

| 계획 | 관계 |
|---|---|
| `LivePipelineDescPlan.md` | 현재 C++ 조립 기술을 첫 native compiler target으로 사용. 공개 asset schema로 직접 노출하지 않음 |
| `MaterialPipelinePlan.md` | Custom Pass shader asset·DXC·DXIL/SPIR-V·reflection·PSO cache의 필수 선행 |
| `RhiBoundaryPlan.md` | RHIEncoder·texture/buffer handle·양 backend 계약을 그대로 소비. ScriptCore로 raw RHI를 올리지 않음 |
| `RenderSceneViewPlan.md` | RendererList와 카메라별 frame packet/history의 입력 경계 |
| PHASE 3-15 | RHI 제출 스레드와 compiled generation fence retirement가 충돌하지 않아야 함 |
| PHASE 4 GPU-driven | Native Pass + C# feature/variant 조립의 첫 대형 소비자 |
| PHASE 4 Stochastic Lighting | Compute/history/resource schema의 첫 대형 소비자 |
| PHASE 4 DXR·DLSS | capability/fallback + source Native Pass registry의 첫 소비자 |
| `BuildPipelinePlan.md` | C# assembly·pipeline cache·shader permutation·native source module의 Player 패키징 담당 |

---

## 15. 확정한 것 / 나중에 정할 것

### 확정

- 공개 작성 언어는 C#이다.
- 일반 Custom Pass는 C#+Shader로 완결한다.
- Render/CommandBuild/RHI 스레드에서 CLR을 호출하지 않는다.
- `EnhancedRenderGraph`와 `RHIEncoder`가 유일한 실행 경로다.
- C++ 고급 Pass는 소스 재빌드 방식이다.
- Native DLL Pass ABI는 범위 밖이다.
- 목록 순서가 실행 순서이며 자동 위상 정렬하지 않는다.
- 현재 파이프라인의 19개 노드와 dump·픽셀 동등 치환이 첫 수직 슬라이스다.

### 구현 페이즈에서 정할 것

- Pipeline Asset의 물리 확장자와 패키지 디렉터리
- managed/native Blueprint binary encoding
- 안정 GUID 생성·충돌 검사 도구
- RendererList 필터의 첫 공개 범위
- frame packet의 최대 크기와 overflow 정책
- Pipeline Compiler를 Game 스레드 안에서 돌릴지 별도 worker로 분리할지
- 읽기 전용 graph preview 뒤에 시각 편집기를 추가할지

이 항목들은 위의 스레딩·수명·RHI 경계를 바꾸지 않는 구현 선택이다.
