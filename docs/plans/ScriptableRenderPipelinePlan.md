# Scriptable Render Pipeline · Custom Pass 설계 (PHASE 4)

2026-08-16 최초 작성, 2026-08-24 Asset-first 작성 모델로 개정, 2026-08-27
Standard PBR native Slang 전환 레인을 추가, 2026-08-28 리소스 의존성 스케줄링
트랙 RG를 확정. PHASE 4의 차세대 GPU 기능 구상에 앞서 확정한 파이프라인 확장 계약.

상태: **SRP 설계 기준선 확정. 트랙 RG는 PHASE 4 구현 계획 확정·미착수이며, 나머지
구현은 PHASE 4의 통합 게이트에서 별도 페이즈와 공수를 정한다.**

---

## 0. 결정 요약

CreatorEngine의 공개 렌더 확장 모델은 다음으로 고정한다.

1. **Pipeline Asset Inspector가 파이프라인과 일반 Pass의 정본이다.**
   개발자는 명시적 Pass 목록, `read`/`write`/`modify` 슬롯, 리소스 schema, 실행 조건,
   폴백과 property override를 Inspector에서 작성한다. 목록은 저작·직렬화 순서이며,
   실행 순서는 리소스 버전 의존성으로 컴파일한다. 독립 Pass만 목록 순서를 tie-break로 쓴다.
2. **일반 Pass는 네이티브 실행 템플릿과 논리 Shader Asset을 조합한다.**
   Fullscreen·Compute·RendererList·Copy/Resolve 템플릿을 제공하고, Shader Asset의 GPU
   코드는 Visual Shader Graph 또는 직접 작성한 `.slang` 중 한 모드로 만든다.
3. **Visual 모드는 Graph Asset으로 완전하게 다시 열고 편집할 수 있어야 한다.**
   `Graph Editor ⇄ .shadergraph → Graph IR → generated .slang` 흐름으로 고정한다.
   `.shadergraph`는 노드·연결뿐 아니라 편집 상태까지 보존하는 Visual 모드 정본이고,
   생성 `.slang`은 읽기 전용 파생물이다. Code 모드는 직접 작성한 `.slang`이 정본이다.
   양쪽 모두 `.shadermeta`를 property·keyword·entry point·render state 정본으로 공유한다.
4. **C#은 선택적인 Game 스레드 값 제어기다.**
   동적 값과 미리 검증된 variant key만 안정 핸들로 갱신할 수 있다. topology, 슬롯
   schema, render state, GPU 실행은 소유하지 않으며 Render/CommandBuild/RHI 제출
   스레드는 CLR에 진입하지 않는다.
5. **고급 네이티브 Pass는 소스 수준 확장으로 남긴다.**
   DXR·DLSS·GPU-driven처럼 특수 RHI/SDK가 필요한 기능은 현재의
   `EnhancedRenderPass`를 C++ 소스에 추가하고 엔진을 다시 빌드한다.
6. **외부 Native DLL Pass ABI는 만들지 않는다.**
   이 저장소는 오픈소스이고 전체 엔진을 재빌드할 수 있다. ABI 버전·DLL hot
   unload·인플라이트 GPU 자원 수명까지 떠안는 바이너리 SDK는 현재 소비자가 없다.
7. **`EnhancedRenderGraph`는 유일한 실행 그래프로 유지한다.**
   새 계층은 그래프를 대체하지 않고, 기존 `LivePipelineDesc`가 하던 영속 조립을
   개발자 작성 기술로부터 컴파일한다.

한 줄로 줄이면:

> **Inspector Pass 계약 + Visual/Code Slang + Game→Render 밀봉 + 네이티브 RenderGraph 실행.**

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
Scriptable Pipeline·Custom Pass 계약   ─┐
                                        ├─ 정점 레이아웃 정본 (ModelImportPipelinePlan 트랙 V)
                                        ─┘
    ↓
GPU-driven / Stochastic Lighting / DXR / DLSS 구상
    ↓
공통 의존 그래프·수직 슬라이스·구현 페이즈 확정
```

**모델 임포트 파이프라인이 PHASE 4에 편입됐다**(2026-08-25, 구 PHASE 24).
`ModelImportPipelinePlan.md` 를 보라. 네 GPU 기능이 전부 그 출력을 입력으로
요구하기 때문이다 — GPU-driven은 정점 레이아웃이 PSO 분류의 축이 되고, DXR은
BLAS 빌드가 정점 포맷과 stride를 직접 받는다.

이 계약과의 경계는 이렇다.

- **`ModelImportPipelinePlan` 트랙 V** — 정점 데이터가 *어떤 모양으로* GPU까지
  가는가(속성 기술표·메시별 마스크·캐시 버전·스트림 분리).
- **이 문서** — Pass가 그 모양을 *어떻게 소비*하는가.

★ 이 계약에 직접 걸리는 지점이 하나 있다. **Pass를 Asset으로 기술하려면 Pass가
정점 입력 레이아웃을 소유하면 안 된다.** 현재는 오프셋이 C++ 5곳에 손으로 박혀
있다(Forward·GBuffer·Shadow×2·WireFrame). 트랙 V4가 이것을 "Pass는 요구 속성만
선언하고 레이아웃은 `(메시 마스크 ∩ Pass 요구)`에서 유도"로 바꾼다. 그 전까지는
Pipeline Asset이 Raster Pass의 정점 계약을 완결할 수 없다.

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

따라서 `LivePipelineDesc`는 좋은 **컴파일 결과물**이지만 파일 직렬화나 Asset 공개
schema로 그대로 내보낼 수 있는 **작성 형식**은 아니다. 특히 `std::function`과 캡처
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

이 경계를 일반 Pass 작성 기능 때문에 다시 열 이유가 없다. Asset이나 선택적 C#에서
D3D12/Vulkan 핸들이나 배리어를
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

선택적 렌더 제어 스크립트도 이 규약의 예외가 아니다. 일반 Pass를 작성하기 위해
C# 클래스를 요구하지 않으며, C#을 붙이더라도 동적 값과 variant key만 Game
스레드에서 일괄 밀봉한다. Render 스레드에서 C# `Execute()`를 직접 호출하면
3-2에서 만든 Game→Render 비동기 경계를 다시 허문다.

### 2.4 Native Pass 플러그인 기반은 없다

현재 코드에는 `PassRegistry`, `RegisterRenderPass`, 렌더 플러그인 로더가 없다.
`LoadLibraryW` 사용은 Vulkan 로더와 CoreCLR hostfxr 로더뿐이다. 즉 Native DLL Pass
SDK를 선언하면 작은 어댑터를 추가하는 일이 아니라 새 플러그인 시스템 전체를
만드는 일이 된다.

### 2.5 PHASE 3.5가 선행한다

Asset 기반 Custom Pass가 셰이더 애셋과 entry point를 가리키려면 다음이 먼저 닫혀야 한다.

- ✅ `IRHIShaderCompiler` 단일 진입점 + 고정 Slang 2026.14 backend 구현
  (현재 HLSL source 입력, Material M1B, DX12/Vulkan 실행·패키지 smoke 완료)
- ✅ 코드 기반 permutation 정규화·키·캐시 소비와 기존 4패스 이관(Material M2A),
  메타 GUID/pass/다중값 선택 key·ordinal define·전수 열거·Build 상한(Material M2B)
- ✅ `.shadermeta` schema v1·strict loader·catalog GUID·RHI state 변환(M4)
- ✅ Slang DXIL/SPIR-V 공통 reflection과 schema 대조(M7, 2026-08-24)
- ✅ 중립 렌더 상태 기술·메타 변환과 대표 PSO cache 소비 기반(M3·M4)
- ✅ material property override를 안정 키·타입·layout으로 밀봉하는 M5-A 구축
- ✅ material serialization·shader/PSO 세대 교체 hot reload(M5-B/M5-C)
- ✅ 실제 Material/PSO 소비 배선과 legacy 재측정/은퇴는 M6-P0~P2d-e 완료
- ✅ material property의 cbuffer offset/type와 texture binding layout 검증(M7)

따라서 이 문서는 API 계약을 지금 확정하지만, 실행 구현은 `MaterialPipelinePlan`
M1~M7의 산출물을 소비한다. 별도 셰이더 컴파일 경로를 만들지 않는다.

---

## 3. 목표 / 비목표

### 목표

- 게임 개발자가 엔진 C++이나 C# Pass 클래스를 작성하지 않고 Inspector와
  Shader Graph/직접 Slang으로 일반적인 Raster·Compute Pass를 파이프라인에 넣는다.
- 프로젝트별로 Pass 순서·조건·품질·백엔드 폴백을 기술할 수 있다.
- 입력/출력·리소스 형식·바인딩·기능 지원을 구축 시점에 검증한다.
- 같은 파이프라인 기술이 DX12와 Vulkan에서 같은 토폴로지를 만든다.
- Game→Render 스냅샷, RenderGraph 자원 수명, RHI 중립성을 보존한다.
- Asset·Shader hot reload 실패가 현재 정상 파이프라인을 파괴하지 않는다.
- Visual Shader Graph를 저장·닫기·재개방해도 노드 의미와 편집 상태가 보존된다.
- 소스 기여자가 기존 `EnhancedRenderPass`로 특수 네이티브 기능을 추가할 수 있다.

### 비목표

- Asset·Shader Graph·C#에 `ID3D12Resource*`, `VkImage`, raw command list/command buffer를 공개하지 않는다.
- Asset·Shader Graph·C#이 배리어·descriptor heap·fence·transient aliasing을 직접 관리하지 않는다.
- Render/CommandBuild/RHI 제출 스레드에서 CLR 콜백을 호출하지 않는다.
- 매 프레임 임의의 C# 코드가 파이프라인 토폴로지를 통째로 재작성하지 않는다.
- Shader Graph가 Pass 순서·슬롯 schema·history·render state·capability/fallback을 소유하지 않는다.
- ShaderLab 호환 단일 파일 DSL이나 별도 pipeline parser를 만들지 않는다.
- authored/generated Slang에서 전체 Graph를 자동 역복원하거나 generated `.slang`을 직접
  편집하는 경로는 지원하지 않는다. `Graph Editor ⇄ .shadergraph` 왕복은 필수다.
- `EnhancedRenderGraph`를 새 관리 그래프로 대체하지 않는다.
- 리소스 의존성과 무관한 비용 휴리스틱으로 Pass를 비결정적으로 재배치하지 않는다.
  의존성으로 선후가 정해지지 않은 Pass는 저작 목록 순서를 안정 tie-break로 사용한다.
- Native DLL Pass ABI, DLL hot reload, 외부 바이너리 Pass 마켓을 만들지 않는다.
- PHASE 4에서 실제 DXR·DLSS SDK나 Custom Pass 런타임을 먼저 구현하지 않는다.

---

## 4. 외부 엔진에서 가져올 것과 버릴 것

| 항목 | Unity | Unreal | CreatorEngine 결정 |
|---|---|---|---|
| 셰이더 작성 | Shader Graph 또는 ShaderLab/HLSL | Material Graph 또는 HLSL/C++ | Shader Graph 또는 직접 Slang, 두 모드의 정본 분리 |
| 일반 Custom Pass | Renderer Feature Inspector + C# 확장 | Material Graph + C++ Plugin/Module | Pipeline Asset Inspector + 네이티브 Pass Template |
| Pass 데이터 | Renderer Feature/Volume Inspector | Asset/Details Panel | Pipeline Asset의 Pass 목록·슬롯·schema·조건·폴백 |
| GPU 명령 표현 | CommandBuffer/RenderGraph | C++ RDG/RHI command list | 검증된 Template → 네이티브 RHIEncoder |
| 네이티브 탈출구 | Native Rendering Plugin | C++ Plugin 또는 엔진 포크 | 소스 수준 `EnhancedRenderPass` |
| 프레임 중 C# 실행 | 허용 | 일반적으로 C++ | 선택적 값 제어만 Game 스레드에서 일괄 생산 |
| 바이너리 Pass ABI | 별도 Native Plugin API | UBT가 C++ Plugin을 함께 빌드 | 지금 만들지 않음 |

Unity에서 가져오는 것은 **Inspector에서 Pass를 배치·설정하고 Shader Graph로 GPU
수식을 편집하는 경험**이다. 가져오지 않는 것은 일반 Pass마다 C# 클래스를 만들거나
C# `Render()`/`Execute()`가 매 카메라·매 Pass GPU 명령을 직접 쌓는 실행 모델이다.
CreatorEngine에는 이미 명시적인 비동기 프레임 스냅샷과 네이티브 병렬 기록 경계가
있기 때문이다.

Unreal에서 가져오는 것은 **고급 기능이 필요하면 엔진 소스의 C++ Pass를 수정할 수
있다는 탈출구**와 **그래프는 셰이더 계산을 만들고 RDG는 실행을 소유하는 분리**다.
가져오지 않는 것은 일반 효과부터 C++ 프로젝트 모듈을 요구하는 기본 경험이다.

참고한 공개 문서:

- [Unity Shader Graph](https://docs.unity3d.com/kr/6000.0/Manual/shader-graph.html)
- [Unity Full Screen Pass Renderer Feature](https://docs.unity3d.com/kr/6000.0/Manual/urp/renderer-features/renderer-feature-full-screen-pass.html)
- [Unity 6 URP Custom Pass 흐름](https://docs.unity3d.com/cn/6000.0/Manual/urp/renderer-features/custom-rendering-pass-workflow-in-urp.html)
- [Unity ShaderLab reference](https://docs.unity3d.com/kr/6000.0/Manual/SL-Reference.html)
- [Unreal Render Dependency Graph](https://dev.epicgames.com/documentation/unreal-engine/render-dependency-graph-in-unreal-engine?lang=en-US)
- [Unreal Material node](https://dev.epicgames.com/documentation/unreal-engine/using-the-main-material-node-in-unreal-engine?lang=en-US)

---

## 5. 계층 설계

```text
Pipeline Asset Inspector                    Logical Shader Asset
  명시적 Pass Stack                          .shadermeta
  template · slots · ResourceSchema          ├─ Visual: .shadergraph → generated .slang
  condition · fallback · overrides           └─ Code: authored .slang
                 │                                      │
                 └──────── Asset/Shader Compiler ────────┘
                                      │
                          PipelineBlueprint · PassBlueprint
                          ResourceSchema · VariantSchema
                                      │
                 Optional C# value controller (Game thread only)
                                      │ stable handle · sealed POD values
                                      ▼
Native Pipeline Compiler
  Pass type/slot/shader/capability 검증 · 안정 ID 해석 · variant 컴파일
                                      │
                                      ▼
CompiledPipelineDesc (불변 · generation 보유)
  ├─ NativePassFactory → 소스 링크된 EnhancedRenderPass
  └─ TemplatePassAdapter → 셰이더 기반 네이티브 실행 템플릿
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

향후 `Live` 접두사가 제거되거나 구현 구조가 바뀌어도 asset schema와 Shader Asset이
영향받지 않도록 두 형식을 분리한다.

### 5.2 핵심 데이터

이름은 구현 시 바뀔 수 있지만 책임은 다음으로 고정한다.

| 타입 | 수명 | 책임 |
|---|---|---|
| `RenderPassTypeId` | 영속 | Pass Template 또는 Native Pass 타입의 안정 GUID |
| `RenderSlotId` | 영속 | 사람이 읽는 이름 + 직렬화용 안정 hash/GUID |
| `PassBlueprint` | 작성 시 | template ID, read/write/modify 슬롯, shader ID, queue, 조건, 설정 blob |
| `ResourceSchema` | 작성 시 | texture/buffer 형식·크기 정책·sample·usage·transient/history 여부 |
| `PipelineBlueprint` | 작성 시 | Pipeline Asset에서 컴파일한 명시적 Pass 목록과 variant/fallback 규칙 |
| `CompiledPassDesc` | 런타임 | factory/adapter, 검증된 슬롯 인덱스, shader/PSO 핸들 |
| `CompiledPipelineDesc` | 런타임 | 불변 노드 배열, generation, dump/hash, backend capability |
| `RenderPassFramePacket` | 프레임 | Asset 기본값·엔진 semantic·선택적 C# override를 밀봉한 Pass별 POD 파라미터 |

문자열은 작성·오류·덤프에는 남기되 프레임 실행에서 map 탐색하지 않는다. 컴파일 시
슬롯과 Pass를 조밀한 정수 인덱스로 낮춘다.

---

## 6. Asset 기반 작성 계약

### 6.1 Pipeline Asset Inspector가 Pass 데이터를 소유한다

첫 버전의 정본은 **Pipeline Asset의 직렬화된 명시적 Pass Stack**이다. 개발자가 일반
Pass마다 C# 클래스를 만들지 않는다. Inspector의 Pass 항목은 최소 다음을 가진다.

```text
Pass Name / Stable ID
Template: Fullscreen | Compute | RendererList | CopyResolve | Native
Shader Asset / Entry Point / Permutation
Slots: read[] / write[] / modify[]
ResourceSchema: texture|buffer, format, size policy, sample, usage, transient|history
Queue / Condition / Capability / Fallback
Property Defaults / Engine Semantics / Project Overrides
```

- Pass Stack의 목록 순서는 저작·직렬화 순서다. Pipeline Compiler가 슬롯의 버전 계보를
  만든 뒤 `EnhancedRenderGraph`가 의존성 실행 순서를 계산하고 독립 Pass만 목록 순서로 묶는다.
- `read`와 `modify`는 앞에서 발행된 슬롯만 선택한다.
- `write`는 새 프로젝트 전용 논리 슬롯을 만들 수 있고 같은 항목에서
  `ResourceSchema`를 반드시 작성한다.
- 이후 Pass는 그 슬롯을 `read`/`modify`할 수 있다.
- 프로젝트 전용 슬롯은 Core `LiveSlots`에 추가하지 않는다. `LiveSlots`는 엔진이
  소유하는 well-known 교환 지점만 유지한다.

따라서 **없는 슬롯을 추가하는 일반 효과는 C++ 수정이 필요 없다.** 다음 중 하나일 때만
C++ 엔진 소스 확장이 필요하다.

- 엔진 자체가 새 well-known 슬롯을 생산·소비해야 할 때
- 새 RHI resource/command 종류나 플랫폼 interop가 필요할 때
- DXR·DLSS 같은 외부 SDK·특수 수명·callback을 다룰 때
- 공개 Template로 표현할 수 없는 엔진 소유 producer를 추가할 때

### 6.2 일반 Pass는 네이티브 Template로 실행한다

첫 공개 표면은 다음 Template로 제한한다.

| Template | Inspector가 기술하는 것 | 네이티브 실행 |
|---|---|---|
| Fullscreen | shader, read texture, color/depth output, viewport | fullscreen raster draw |
| Compute | shader, buffer/texture access, dispatch 식, queue 선호 | direct/indirect dispatch |
| RendererList | filter, queue, material override, output | 엔진 scene packet 기반 draw |
| Copy/Resolve | source, destination, region/resolve policy | backend 중립 copy/resolve |

Template 하나가 내부 RenderGraph 노드 하나일 필요는 없다. 다만 Template 구현만
`RHIEncoder`와 실제 그래프 자원에 접근한다. 반복 횟수가 프레임 값에 따라 달라지는
알고리즘은 native template·indirect dispatch·미리 컴파일한 variant로 표현한다.

### 6.3 Shader Asset은 Visual/Code 두 모드다

에디터에는 하나의 논리 Shader Asset으로 보이게 하되 코드 정본은 모드별로 분리한다.

```text
Visual: Graph Editor ⇄ ShaderGraphAsset(.shadergraph)
                              ↓ load/validate/migrate
                           typed Graph IR
                              ↓ deterministic codegen
                    generated .slang (read-only)

Code:   Code Editor ⇄ authored .slang
```

| 파일/모드 | 정본 책임 | 편집 규칙 |
|---|---|---|
| `.shadermeta` | properties/labels, keywords, pass entry points, render state, queue | 양 모드 공통. Inspector/Graph Blackboard가 편집 |
| Visual `.shadergraph` | node type/ID, pin value, connection, position, group/comment, Blackboard/subgraph 참조, graph/schema version | Visual 코드와 편집 상태의 정본. Graph Editor가 읽고 씀 |
| Visual generated `.slang` | 결정적 codegen 결과와 node source mapping | 파생 캐시, 읽기 전용 |
| Code `.slang` | 직접 작성한 GPU 모듈·함수 | 코드 정본 |

Visual 모드의 최소 보존 계약은 다음이다.

- 저장→닫기→재개방 뒤 node type/ID, pin 값·연결, 노드 위치, group/comment,
  Blackboard property, subgraph 참조가 기능적으로 동일해야 한다.
- graph/schema version을 저장하고 loader가 단계별 migration을 수행한다.
- 알 수 없는 node나 migration 실패 시 원본 payload를 덮어쓰지 않고 recovery/read-only로
  열어 진단한다.
- generated `.slang`이 없어도 `.shadergraph`와 `.shadermeta`만으로 재생성할 수 있어야 한다.
- Editor 프로젝트에는 `.shadergraph`를 보존한다. Player에는 검증된 compiled shader만
  포함하고 graph 편집 데이터는 패키징 정책에 따라 제외할 수 있다.

Visual 모드와 Code 모드는 상호 전환 시 복사본을 새 자산으로 만드는 **일방향 fork**만
허용한다. Visual→Code는 당시 generated Slang을 새 authored `.slang`으로 복제할 수
있지만 원본 Graph와 동기화하지 않는다. authored/generated Slang을 분석해 전체 Graph로
되돌리는 자동 역변환은 지원하지 않는다.
Shader Graph는 GPU 수식과 reflected parameter만 편집하며 Pass 순서, 슬롯 schema,
history, render state, capability/fallback은 Pipeline Asset/`.shadermeta`가 소유한다.

ShaderLab처럼 Pass와 셰이더를 한 파일에 선언하는 새 DSL은 만들지 않는다. 같은 에디터
화면에서 관련 파일을 묶어 보여 줄 수는 있지만, 저장 정본과 컴파일러 입력은 위처럼
분리한다. 이로써 새 parser·include 규칙·중복 property schema를 만들지 않는다.

첫 Code 모드 슬라이스는 기존 `RHIShaderCompiler`가 실제 Slang source language,
module/import, entry point, dependency를 처리하도록 완성한다. 현재 HLSL 입력을 Slang
compiler에 통과시키는 배선과 구분하며, 기존 셰이더의 일괄 확장자 변경은 하지 않는다.
Visual codegen도 같은 컴파일 진입점과 DXIL/SPIR-V reflection 검증을 사용한다.

### 6.4 선택적 C#의 범위

C#은 일반 Pass 정의 API가 아니라 Game 스레드의 선택적 값 제어기다.

```text
Pipeline/Pass Stable Handle
    └─ SetProperty(value) / SelectVariant(key)
          └─ Game thread에서 Asset 기본값·엔진 semantic과 병합
                └─ 연속 RenderPassFramePacket으로 프레임당 최대 한 번 밀봉
```

C#은 topology, 슬롯 생성/schema, shader entry, render state, queue, GPU command를
변경하지 않는다. 값 변경은 `.shadermeta` reflection으로 타입·offset을 검증하고,
variant 선택은 Asset에 미리 선언·검증된 key 안에서만 허용한다. 렌더 스레드에서 C#
메서드를 다시 부르지 않는다.

### 6.5 시각 편집기 범위

- Pipeline 편집의 첫 표면은 **authored Pass Stack Inspector + 읽기 전용 compiled graph preview**다.
- Shader Graph만 editable node graph로 구현한다.
- 기존 `Editor/ImGuiHelper/NodeEditor`는 integer node ID와 UI 연결을 제공하는 scaffold일
  뿐 typed shader IR/codegen이 아니다. 그대로 실행 schema로 승격하지 않는다.
- Material M0에서 제거한 소비자 없는 `VisualShader*` 데이터/parser를 복구하지 않는다.
- 최소 graph IR, deterministic Slang codegen, compiler consumer, node 단위 diagnostic
  source mapping을 한 수직 슬라이스로 구축한다.
- 후속 Pipeline node editor가 필요해도 같은 Pipeline Asset을 편집해야 하며 별도
  Blueprint 정본이나 별도 실행 경로를 만들지 않는다.

### 6.6 공개하지 않을 작업

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

Pipeline Asset이 네이티브 Pass를 안정 ID로 참조하려면 작은 정적 registry는
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

Asset 컴파일과 프레임 값 경계를 두 종류로 분리한다.

```text
PipelineAssetCompilePacket   드묾. 토폴로지·schema·설정·variant
RenderPassFramePacket        매 프레임. 이번 generation의 POD 파라미터
```

Asset compile packet은 로드·리로드·Inspector 변경 때 만들고, frame packet은 Asset
기본값·엔진 semantic·선택적 C# 값을 Game 스레드에서 네이티브 소유 메모리에 일괄
기록한다. frame packet은 `generation`, `frameId`, `viewCount`, Pass별 offset/size를
가진다. 렌더 측은 packet 안의 값만 읽으며 에디터 객체, 관리 객체, 문자열 포인터,
GC handle을 보유하지 않는다.

### 8.2 스레드별 허용 작업

| 스레드 | 허용 | 금지 |
|---|---|---|
| Game | Asset 기본값·engine semantic·선택적 C# 값 병합, packet 밀봉 | topology/schema 변경, GPU command 기록 |
| Render/Presentation | compiled desc 선택, RenderGraph 선언 | CLR 호출, live GameObject 접근 |
| CommandBuild worker | native ExecuteCallback, RHIEncoder 기록 | CLR 호출, pipeline rebuild |
| RHI submission | 제출·fence | CLR 호출, 설정 해석 |

### 8.3 세대 교체

1. Asset/Shader Compiler가 새 Blueprint와 hash를 만든다.
2. 네이티브 컴파일러가 별도 `CompiledPipelineDesc(generation+1)`를 완성하고 검증한다.
3. 다음 안전 프레임 경계에서 포인터를 교체한다.
4. 이전 generation은 그것을 참조한 마지막 submission fence 뒤에 해제한다.
5. 컴파일·초기화 실패 시 이전 정상 generation을 계속 사용하고 오류를 에디터에 낸다.

Pipeline reload가 Game/Render 배리어가 되어서는 안 된다. 일시 정지가 필요한 초기
구현이라면 에디터 전용 명시적 stall로 계수하고, 플레이 런타임 계약으로 승격하지
않는다.

### 8.4 선택적 C#과 실행의 분리

CreatorEngine은 Inspector 작성 능력과 동적 값 제어를 **정적 Asset 기술 + 프레임
packet**으로 나눈다. 임의 C# 제어 흐름을 GPU 기록 시점에 허용하지 않는 대신 다음을
얻는다.

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
검사한다. `read`는 현재 버전을 소비하고 `write`/`modify`는 새 버전을 발행한다. 이 버전의
producer/consumer edge가 실행 순서를 결정하며 목록 순서는 독립 Pass의 안정 tie-break다.

`write`는 well-known 이름 목록에 없는 프로젝트 전용 슬롯도 최초 발행할 수 있다.
이때 Inspector가 `ResourceSchema`를 함께 저장하고 Pipeline Compiler가 조밀한 런타임
slot index로 낮춘다. 이름을 Core `LiveSlots`에 추가하는 것은 엔진 소유 교환 지점으로
승격할 때만 한다.

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
- Visual generated Slang 또는 authored Slang의 backend target(DXIL/SPIR-V) 존재
- permutation 수 상한

Pipeline Asset·Shader Graph·C#은 자체 shader compiler나 자체 binding 번호 체계를
만들지 않는다. 첫 구현은 현재 명시적 register/binding과 reflection 검증을 보존한다.
Slang `ParameterBlock` 기반 auto-binding은 Material M6 실제 소비가 닫힌 뒤 별도
최적화로 판단한다.

---

## 10. 검증 규칙

### 10.1 ID·직렬화

- Pipeline/Pass 안정 GUID 중복 거부
- 누락·버전 불일치 template/native pass ID 거부
- 직렬화할 수 없는 설정 타입 거부
- Asset rename 뒤에도 GUID가 같으면 reference 유지

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

### 10.6 Shader source mode와 Graph

- Visual/Code source mode가 둘 다 활성화되거나 둘 다 누락된 Shader Asset 거부
- generated `.slang`의 수동 수정은 저장 대상이 아니며 다음 codegen 때 덮어씀
- `.shadergraph` node ID 중복, 끊긴 typed edge, cycle, 누락 output 거부
- `.shadergraph` save→reload round-trip 뒤 의미·stable ID·편집 layout 동등 검증
- graph/schema version migration fixture와 알 수 없는 node payload 보존 검증
- `.shadermeta` property/entry와 Slang reflection 불일치 거부
- codegen/compile 오류가 Shader Asset, Pass, node ID와 원본 pin을 함께 지목

---

## 11. 캐시·직렬화·에디터

### 11.1 정본과 캐시

정본은 다음이다.

- Pipeline Asset의 Pass Stack·slot·ResourceSchema·condition·fallback·override
- 공통 Shader metadata인 `.shadermeta`
- Visual 모드의 재편집 가능한 `.shadergraph` 또는 Code 모드의 authored `.slang` 중 하나

generated `.slang`, `PipelineBlueprint`, `CompiledPipelineDesc`, DXIL/SPIR-V와 PSO는
파생 캐시다.

Compiled Pipeline은 파생 캐시다. 키에는 최소 다음이 들어간다.

```text
pipeline/pass stable IDs + asset/source fingerprint
+ serialized settings hash
+ shader metadata + graph/authored Slang + permutation hash
+ backend + capability mask + quality tier
+ pipeline schema version
```

캐시가 없거나 키가 다르면 다시 컴파일한다. 캐시 파일을 수동 편집하는 경로는 없다.
물리 asset 포맷과 Player 패키징 위치는 PHASE 12 BuildPipelinePlan의 규약을 따른다.

### 11.2 에디터 표면

Pipeline의 첫 표면은 Inspector + 읽기 전용 graph preview이고, Shader의 첫 시각 표면은
editable Shader Graph다.

- Pipeline Asset 선택, authored Pass Stack, Template와 직렬화 필드
- Pass 순서·활성 variant·fallback 편집
- 슬롯 생산자/소비자, 새 `write` schema와 history 소유자 편집
- 논리 Shader Asset에서 Visual/Code 모드와 `.shadermeta` property 편집
- `.shadergraph` 저장·재개방·schema migration과 recovery/read-only 진단
- Visual 모드의 generated Slang read-only 보기와 node/source diagnostic 이동
- backend/capability별 compile 결과
- compile error가 Pass·슬롯·shader binding을 함께 지목
- compiled hash·generation·cache hit/miss

Pipeline node editor는 후속이다. 추가하더라도 같은 Pipeline Asset을 편집하며 별도
`PipelineBlueprint` 정본이나 실행 경로를 만들지 않는다.

### 11.3 외부 렌더러 재감사에서 확정한 재사용·공백 경계

`vk_gltf_renderer`, `LuminaEngine`, `D3D12LookDevPTwithAI`, `VanishingGround`를
현재 CreatorEngine 소스와 다시 대조했다. 기능 이름만 보고 새 기반으로 잡지 않고,
다음 구현은 **PHASE 4의 선행 자산으로 재사용**한다.

- `EnhancedRenderGraph`의 선언 순서 기준선, read-before-write 검증, 자동 Transition/UAV
  배리어, pass culling, transient pool·수명, RHI-neutral 병렬 기록. 트랙 RG는 이 기반을
  교체하지 않고 명시적 접근·버전 핸들·stable DAG 순서로 확장한다
- Render Debug의 파이프라인 topology, 패스별 GPU timing, validation, SSAO·SSGI·SSS·SSR·
  Fog·Post tuning과 DX12/Vulkan 공용 Pass 픽셀 fixture
- Slang 기반 DXIL/SPIR-V 컴파일, `.shadermeta`, reflection, material logical value,
  keyword permutation·cache
- glTF/GLB의 계층·스키닝·embedded texture와 기본 PBR material import

따라서 Lumina의 일반 reflection Inspector나 barrier helper, VanishingGround의 pass property
panel, 별도 기본 glTF loader는 도입 항목이 아니다. PHASE 4에서 실제로 닫을 공백은 다음이다.

1. **전체 live scene backend 동등성** — 같은 밀봉 frame packet·카메라·해상도·tuning을
   backend별 새 프로세스에서 실행하고 final PNG, 선형 색공간 RMSE/최대 오차/변경 픽셀,
   차영상, CPU record·pass별 GPU timing·RenderGraph stats를 한 결과 묶음으로 남긴다.
   기존 pass fixture는 유지하되 이 검사를 전체 파이프라인 acceptance gate로 추가한다.
2. **RenderGraph resource Inspector** — pass의 read/write/modify, producer/consumer,
   요구·실제 state, barrier 수, transient lifetime과 선택한 중간 texture를 읽기 전용으로
   보여 준다. 새 실행 그래프나 디버그 전용 resource ownership은 만들지 않는다.
3. **Material/Shader Graph 저작 표면** — Lumina의 graph→typed IR→shader stage compile 구조를
   참고하되 기존 `.shadermeta`·Slang reflection·permutation을 유일한 backend로 사용한다.
4. **안전한 shader reload** — 파일 변경을 새 compiler/pipeline generation으로 만들고,
   성공분만 프레임 경계에서 교체하며 실패 시 마지막 정상 generation을 유지한다.
5. **DXR material reference** — `vk_gltf_renderer`처럼 raster와 ray path가 동일 material
   evaluation 입력을 공유하게 한다. KHR/EXT 확장은 기본 importer를 교체하지 않고 지원
   행렬·fixture를 늘리는 수직 슬라이스로 다룬다.

`D3D12LookDevPTwithAI`의 MCP는 가치가 높지만 렌더 실행 기능은 아니다. 기존 console action과
값 snapshot 위에 read-only resource/tool을 먼저 두고, mutation은 canonical arguments와
명시적 승인 경계를 거치는 **인접 tooling slice**로 분리한다. Render/CommandBuild/RHI 스레드가
MCP transport나 JSON을 알게 해서는 안 된다. VanishingGround의 Particle/VFX Editor도 PHASE 4
SRP 기반을 소비할 수 있지만 별도 VFX 계획의 범위이며 이 페이즈 완료 조건에는 넣지 않는다.

### 11.4 Standard PBR native Slang 재작성 방향

PBR의 정본은 **glTF 2.0 metallic-roughness와 M5-D 표준 property**로 두고, raster와
향후 ray path가 같은 surface 입력을 소비하게 한다. 외부 렌더러의 셰이더 파일을 통째로
옮기지 않고 아래 강점만 CreatorEngine의 RHI·RenderGraph·ShaderMeta 계약 안으로 흡수한다.

| 출처 | 흡수할 강점 | CreatorEngine 적용 경계 |
|---|---|---|
| CreatorEngine 현행 | height-correlated Smith GGX, split-sum IBL, backend-neutral RenderGraph/RHI, Slang DXIL/SPIR-V reflection | 첫 parity 기준. 수식과 GBuffer를 동시에 바꾸지 않고 공용 모듈 이관 뒤 개선 |
| `vk_gltf_renderer` | `MaterialInputs → PbrMaterial` 정규화, raster/path 공용 material evaluation, glTF extension feature gating | `MaterialInputs → StandardSurface`의 모델. importer를 교체하지 않고 M5-D property와 확장 지원 행렬을 사용 |
| `LuminaEngine` | per-pixel shading context, multi-scatter energy compensation, specular AO, local reflection probe, clearcoat, normal-offset shadow bias, 선택적 PCSS, HDR 색 파이프라인 | 공용 shading/IBL/shadow/post module에 단계적으로 이식. PCSS와 고급 lobe는 기본 비용으로 강제하지 않음 |
| `D3D12LookDevPTwithAI` | Heitz GGX VNDF, exact dielectric Fresnel, IOR/refraction, Beer attenuation, colored transmissive shadow | raster 기본 BRDF 교체가 아니라 DXR material-reference 후속. MCP는 §11.3의 tooling 경계를 유지 |
| `VanishingGround` | point/spot shadow atlas 개념과 pass parameter 저작 표면 | atlas 크기·bias·cascade를 Asset/quality 설정으로 노출. 고정 atlas 크기, 비물리 attenuation, cascade transition 부재는 가져오지 않음 |

공유 셰이더 구조는 다음 책임으로 나눈다. 이름은 구현 때 현재 디렉터리 규약에 맞출 수
있지만 **책임 경계와 데이터 흐름은 유지**한다.

```text
.shadermeta / Material Asset
        ↓
MaterialInputs → Material/Standard.slang → StandardSurface
        ↓
Shading/GGX.slang + EnergyCompensation.slang + IBL.slang
        ↓
Lighting/LightEvaluation.slang + Shadow/ShadowSampling.slang
        ↓
Passes/GBuffer.slang / DeferredLighting.slang / ForwardLighting.slang
        ↓
Post/DisplayTransform.slang
```

색·수학 공통 계약은 `Core/Color.slang`에 둔다. GBuffer/Forward의 material 평가와
Deferred/Forward의 BRDF·IBL을 중복 구현하지 않는다. 공유 모듈에는 새 기능 매크로를
확산하지 않고, 기존 define permutation은 진입점 compatibility wrapper에만 남긴다.
Slang specialization/interface는 cache 수와 GPU timing을 측정한 뒤에만 도입한다.

기본 MR은 Deferred에 유지하고 GBuffer를 즉시 확장하지 않는다. clearcoat·transmission처럼
추가 데이터가 필요한 lobe는 우선 Forward+에서 세운 뒤 feature mask/추가 MRT의 실제 비용을
측정해 이동 여부를 정한다. shadow는 Low=현행 비용, Medium=넓은 PCF/Vogel, High=PCSS의
quality tier로 두며 normal-offset, cascade blend/far fade, 설정 가능한 3/4 cascade,
point/spot atlas를 한 sampling module에서 다룬다. post 순서는
`Linear HDR → Bloom → Exposure → Grading → Tone map → Display OETF → AA/UI`로 고정하고,
ACES 기준 golden을 먼저 유지한 뒤 canonical AgX와 auto exposure를 각각 별도 슬라이스로 연다.

native Slang 전환에는 source language를 요청과 cache key에 명시하고, stable module search
root·import dependency tracking, `.slang` Editor/Asset/packaging 분류, native DXIL/SPIR-V
reflection fixture가 필요하다. 기존 column-major, Vulkan binding shift와
`-fvk-use-dx-layout` 계약은 그대로 보존한다. `ParameterBlock` 자동 바인딩은 M6 실제
Material 소비와 PBR 출력 동등 이관이 끝나기 전에 도입하지 않는다.

---

## 12. 구현 후보 슬라이스

> **2026-09-01 — 이 절의 슬라이스 순서·공수는 [`Phase4UnifiedPlan.md`](Phase4UnifiedPlan.md)가 정본이다.**
> `SRP-0`~`SRP-6`와 `PBR-S1`~`PBR-S8`(16슬라이스)은 대시보드에 **한 행도 없어** PHASE 4 공수 합계에서
> 통째로 빠져 있었다. 통합 계획서 §8이 이를 **미산정 백로그**로 명시하고 `4-6`에서 공수를 확정한다.

4-0~4-6은 설계 게이트다. 다만 현재 `EnhancedRenderGraph`의 선언 순서 계약을
Asset-first Pass의 `read/write/modify` 의미와 맞추는 **트랙 RG0~RG9는 PHASE 4 구현
트랙으로 확정**했다. 나머지 SRP 번호는 최종 4-6에서 네 GPU 기능의 공통 기반과 함께
묶거나 분리한다.

### 트랙 RG0~RG9 — 리소스 의존성 RenderGraph

정본은 [`RenderGraphDependencySchedulingPlan.md`](RenderGraphDependencySchedulingPlan.md)다.

- **RG0~RG1:** 현행 픽셀·graph 기준선을 잠그고 state 기반 쓰기 추론을 명시적
  `Read/Write/Modify`와 versioned texture/buffer handle로 바꾼다.
- **RG2~RG4:** stable single-queue DAG, edge 기반 culling·lifetime·barrier, dependency
  wave 병렬 기록과 cycle/resource 진단을 닫는다.
- **RG5~RG6:** 기본 19개 node와 제품 28곳/test·fixture 80곳을 이관하고 동일 밀봉 입력의
  DX12/Vulkan 전체 live frame 픽셀·validation gate로 제품 전환을 닫는다.
- **RG7~RG9:** RG6 뒤에만 transient buffer/aliasing → multi-queue/async compute →
  subresource/split barrier/Resource Inspector 순으로 연다.
- 공수는 RG0~RG6 57일, RG7~RG9 60일, 총 117일이다. 각 단계는 직전 gate 통과 뒤
  진행하며 declaration-order 제품 경로와 새 제품 경로를 장기 병행하지 않는다.

### SRP-G0 — 전체 live backend 동등성·관측 게이트 — **`BASE-0`에 흡수 (2026-09-01)**

> 이 슬라이스와 `4-0`(기능 범위·기준선), `RG0`(현행 graph·픽셀 기준선), `PBR-S0`(PBR 기준선)이
> **같은 하네스·같은 artifact를 서로 다른 이름으로 네 번** 적고 있었다. 세 계획서가 이미
> "별도 캡처 체계를 만들지 않는다"·"중복 기준선을 만들지 않는다"·"동일 하네스/artifact를 공유"라고
> 서로에게 적어 두고도 넷 다 독립 슬라이스로 남아 있었다 — 합의는 있었고 통합만 없었다.
> 통합 이름은 **`BASE-0`**이고 하네스는 한 벌만 만든다. 아래 항목은 `BASE-0`의 acceptance 내용으로
> 그대로 승계되며, `RG6`의 하드 선행도 `BASE-0`이 진다.
> 근거와 판정: [`Phase4UnifiedPlan.md`](Phase4UnifiedPlan.md) §1.1 C1.

- backend는 부팅 고정이므로 동일 scene/frame/tuning을 DX12·Vulkan 별도 프로세스에 재생
- final PNG·차영상·허용 오차와 CPU record·pass별 GPU timing·graph stats artifact
- pass fixture 통과와 전체 live frame 통과를 별도 판정하고 둘 중 하나로 다른 하나를 대체하지 않음
- RenderGraph resource Inspector는 기존 graph/snapshot의 읽기 전용 소비자이며 GPU resource를 소유하지 않음
- `PBR-S0`는 이 artifact와 실행 하네스를 그대로 소비하며 별도 PBR 기준선 캡처 경로를 만들지 않음

### SRP-0 — Blueprint schema와 순수 검증기

- Pipeline Asset, Pass Template, slot, `ResourceSchema` 안정 ID·직렬화 schema
- Asset을 `PipelineBlueprint`로 컴파일하고 dump하는 비렌더 selftest
- 잘못된 슬롯·fallback·capability fixture

### SRP-1 — 기본 파이프라인 구조·픽셀 동등 컴파일

- 현재 C++ 기본 파이프라인 19개 노드를 authored Pass Stack Inspector로 기술
- `CompiledPipelineDesc → LivePipelineDesc` adapter
- 기존 `dx12.pipeline` dump와 노드 이름·순서·슬롯 문자 일치
- 기존 C++ builder와 A/B 픽셀 동일

### SRP-2 — Slang Code 모드 + Fullscreen Custom Pass

- 요청·cache identity에 HLSL/Slang 언어를 명시하고 실제 Slang
  source/module/import/entry/dependency를 컴파일
- stable module search root와 import dependency invalidation, `.slang`의
  Editor/Asset/Player packaging 분류
- `.shadermeta` + authored `.slang` + Fullscreen Template
- Inspector에서 프로젝트 전용 output 슬롯을 만든 Outline/Grayscale fixture
- DX12/Vulkan 같은 출력과 binding validation
- 기존 HLSL 셰이더 전수 개명·제품 PBR 진입점 변경·`ParameterBlock` 자동 바인딩은 이
  슬라이스에 포함하지 않음

### SRP-3 — 최소 Visual Shader Graph

- typed graph IR, stable node ID, deterministic Slang codegen
- node/pin/connection/layout/Blackboard/subgraph를 보존하는 save→reload round-trip fixture
- graph/schema version migration과 unknown-node recovery fixture
- generated `.slang` read-only cache와 node/source diagnostic mapping
- SRP-2와 같은 Fullscreen fixture의 Code/Visual 픽셀 동등
- 기존 `RHIShaderCompiler`와 reflection 경로만 소비

### SRP-4 — Compute·RendererList·history

- transient texture와 renderer filtering. transient buffer는 RG7, async compute는 RG8을 소비
- 카메라별 history와 resize invalidation
- 잘못된 queue/resource 사용 validation

### SRP-5 — variant·hot reload·preview·선택적 C# 값

- capability/quality fallback
- generation 교체와 fence retirement
- reload 실패 시 이전 pipeline 유지
- graph preview·hash·cache 계측
- stable handle로 property/variant를 갱신하는 선택적 Game-thread controller

### SRP-6 — 소스 Native Pass 연결

- 정적 `NativePassRegistry`
- settings schema/version
- DXR 또는 DLSS 최소 native fixture 하나를 Pipeline Asset에서 선택
- DLL loader 없이 소스 빌드만으로 확장됨을 문서화

### PBR-S0~S8 — Standard PBR native Slang 전환 레인

이 레인은 구현 공수를 4-6에서 확정한다. 2026-08-28 Material M5-C4까지 ShaderMeta
frame packet→대표 GBuffer request generation 전환과 장기 Model/Mesh/Material 소유 수명을
닫아 M5를 완료했다. 이어 M6-P0에서 Standard Material 숫자 property 7개의 48B `b2`를
실제 ShaderMeta PSO와 5 MRT로 양 backend 관통했고, M6-P1a는 제품 `BuildDrawPool`의
immutable material snapshot과 GBuffer property batch까지 닫았다. M6-P1b1은 Material의
texture owner 5개와 draw packet의 GBuffer texture owner 4개를 연결해 Material 해제 뒤에도
packet generation을 유지하고 packet 해제 뒤 반환됨을 판정했다. 같은 owned texture의 다른
property가 양 backend에서 2 batch·동일 픽셀로 통과했다. M6-P1b2a는 제품 meta의 숫자 7개+
texture 4개를 reflection의 `t0..t3/space0`와 packet의 property/GUID/register/owner에 연결했고,
같은 texture property의 서로 다른 GUID/owned texture가 양 backend 2 batch·동일 픽셀로
통과했다. M6-P1b2b1은 같은 texture/property의 `SHADING_QUALITY=full/reduced`를 서로 다른
batch PSO로 선택해 양 backend normal 픽셀과 generation retirement까지 닫았다. M6-P1b2b2는
GT frame packet에 복수 ShaderMeta generation/value를 소유시키고 material별 meta+permutation
PSO와 shared-handle-safe retirement를 양 backend 네 draw로 닫았다. M6-P2a는 제품 Forward
draw의 숫자 값과 texture property/GUID/고정 `t4..t7/space0`/owner를 immutable packet으로
밀봉하고, 원 texture owner 해제 뒤 양 backend 두 material 픽셀과 packet 해제 뒤 반환을 닫았다.
M6-P2b는 `Forward.shadermeta`의 Standard `b2/48B`·reflection `t4..t7`, GT frame generation
owner와 material keyword별 일반/Reference PSO pair를 제품 pass에 연결했다. back-to-front A/B/A는
전역 PSO 정렬 없이 6 draw→인접 3 batch로 남았고 DX12/Vulkan overlap RGB와 validation이
일치했다. M6-P2c는 실제 `ForwardWater`·`ForwardWind` Material GUID가 별도 Meta와 Standard
48B prefix+custom float 4개의 64B `b2`를 선택하게 했다. 7 draw→인접 4 batch·frame Meta 3개,
양 backend overlap `0.125/0.25/0.5`, wind G `0→0.325`, coverage `1134/1134`, backend·기대식
편차 `0`과 다음-frame property 변경이 통과했다. 표준 texture 이름과 `t4..t7`은 유지했다.
canonical Water/Wind seed는 Scene/proxy 소유 non-cache 대표 Material을 위한 고정 bridge였으며,
M6-P2d-a는 실제 `FoliageRenderProxy`의 type별 mesh/material owner·instance world matrix와
transformed AABB를 제품 draw pool과 view별 culling에 연결했다. M6-P2d-b는 dynamic
time/flow·`m_flowInfo`를 32B immutable snapshot과 128B Forward instance에 연결하고 양 backend
동일 픽셀·invalid time fail-closed를 닫았다. M6-P2d-c는 generic texture schema/owner
  vector와 `windMap@t4` owner 수명·픽셀을 양 backend에서 닫았다. M6-P2d-d는 Host가 활성 Scene의
  Mesh/Foliage Material owner에서 pass별 ShaderMeta GUID required packet을 만들고 cache 로드 전
  generation owner를 밀봉해 고정 Water/Wind seed를 제거했다. M6-P2d-e는 제품 frame의
  material-cache scan과 raw Material texture alias/setter, draw pool legacy writer를 은퇴해
  M6 전체를 닫았다.
`PBR-S0` 기준선과 소비자 없는 `PBR-S1/SRP-2` 기반은 병렬 준비할 수 있지만 제품 셰이더
전환은 M6 전체 뒤에 시작한다.

1. **PBR-S0 기준선** — **`BASE-0`에 흡수(2026-09-01)**. `BASE-0`의 동일 하네스/artifact에서 DX12 현재 설정을 정본으로
   고정하고 Vulkan 기본값을 복원하지 않은 채 같은 밀봉 입력·tuning으로 pre-tone HDR,
   final LDR, 표준 material grid, pass timing과 RenderGraph stats를 캡처한다.
2. **PBR-S1 native Slang 기반** — SRP-2의 source/module/import·asset/package·cache·reflection
   fixture를 시각 변화 없이 통과시킨다.
3. **PBR-S2 공용 모듈 동등 이관** — `MaterialInputs → StandardSurface`와 공용
   GGX/IBL/light/shadow를 도입하되 현행 출력 golden을 먼저 맞춘다.
4. **PBR-S3 glTF 의미 교정** — `D2/D5 → I5/V4` 생산 소비가 선행한다. metallic factor
   곱셈, ORM AO, normal scale, occlusion strength, emissive, alpha cutoff뿐 아니라
   `doubleSided`, `emissiveStrength`, texture UV set/transform/wrap을 importer부터
   Deferred/Forward까지 닫는다.
5. **PBR-S4 에너지·IBL** — multi-scatter, specular AO, local reflection probe를 furnace와
   material-grid fixture로 각각 연다.
6. **PBR-S5 그림자** — 공용 sampling, normal-offset, cascade blend/far fade, 3/4 cascade,
   point/spot atlas와 Low/Medium/High 품질 tier를 구현한다.
7. **PBR-S6 display/post** — 명시적 display OETF와 post 순서를 고정한 뒤 AgX,
   auto exposure, bloom을 독립 A/B한다.
8. **PBR-S7 확장 lobe** — specular/IOR, clearcoat, transmission/volume, sheen,
   anisotropy/iridescence/dispersion 순으로 추가하고 RT 전용 참조는 DXR slice로 분리한다.
9. **PBR-S8 Shader Graph** — 검증된 authored Slang module API를 typed graph codegen target으로
   사용하고 save→reload·generated/authored parity를 통과시킨다.

---

## 13. 완료 기준

1. **기본 파이프라인 무회귀** — Pipeline Asset이 현재 19개 노드의 이름·순서·슬롯을
   동일하게 만들고 DX12/Vulkan 픽셀·validation 회귀를 통과한다.
2. **일반 신규 Pass는 Asset으로 완결** — C++/C# Pass 작성 없이 Fullscreen Raster와
   Compute 예제 각각 하나가 양 backend에서 실행된다.
3. **새 슬롯은 프로젝트 데이터로 완결** — Inspector의 `write`가 `ResourceSchema`와
   프로젝트 전용 슬롯을 만들고 후속 Pass가 소비한다. Core `LiveSlots` 수정은 없다.
4. **Visual 재편집과 Code 정본 분리** — `.shadergraph` 저장·닫기·재개방 뒤 노드 의미와
   편집 상태가 보존되고 Visual Graph와 authored Slang이 같은 compiler/reflection 경로를
   쓴다. generated Slang은 읽기 전용이며 Slang→Graph 자동 역변환 경로는 없다.
5. **고급 확장은 소스 Pass** — `EnhancedRenderPass`를 추가하고 정적 registry에
   연결해 Pipeline Asset이 선택할 수 있다. 외부 DLL ABI는 없다.
6. **관리 실행 격리** — Render/CommandBuild/RHI 제출 스레드 managed callback 0,
   프레임 파라미터 경계 왕복 최대 1회.
7. **RHI 누수 0** — Asset·Shader Graph·선택적 ScriptCore API에 D3D12/Vulkan 타입 0건.
8. **구축 시 실패** — 슬롯·format·binding·capability·fallback 오류가 GPU 실행 전에
   Pass와 원인을 지목한다.
9. **안전한 reload** — 성공한 generation만 프레임 경계에서 교체하고 이전 generation은
   fence 뒤 해제, 실패 시 마지막 정상 pipeline 유지.
10. **배포 일치** — Player 빌드가 Pipeline Asset, `.shadermeta`, 선택한 graph/authored
    Slang 산출물, shader permutation, 사용 시 C# assembly와 native Pass 모듈을 빠짐없이
    포함하며 editor 전용 API에 링크하지 않는다.
11. **전체 backend 동등성·관측 가능성** — 동일한 밀봉 입력의 DX12/Vulkan live frame이
    final 이미지 허용 오차와 validation 기준을 통과하고, 실패 시 pass timing·graph stats·
    resource state/lifetime·중간 이미지만으로 원인을 좁힐 수 있다.
12. **PBR 단계 전환** — native Slang 공용 모듈 이관은 현행 출력 동등성으로 먼저 닫고,
     glTF 의미 교정·에너지/IBL·그림자·display transform·확장 lobe를 서로 다른 golden과
     성능 게이트로 연다. 한 슬라이스의 이미지 차이를 다음 개선으로 덮지 않는다.
13. **리소스 의존성 실행** — Pipeline Asset의 저작 순서와 compiled 실행 순서를 분리하고,
    명시적 resource version edge로 stable DAG를 만든다. RG6에서 기본 19개 node와 제품
    Pass 이관, 양 backend live 픽셀·validation을 통과한 뒤에만 aliasing과 async compute를 연다.

---

## 14. 다른 계획과의 관계

| 계획 | 관계 |
|---|---|
| **`RenderGraphDependencySchedulingPlan.md` (같은 PHASE 4 · 트랙 RG)** | **이 계획의 실행 순서 정본** — Pipeline Asset의 `read/write/modify`를 versioned handle로 낮추고 stable DAG를 만든다. RG0~RG6 단일 큐 제품 전환 뒤 RG7 aliasing, RG8 async compute, RG9 subresource/관측 순으로 확장한다 |
| **`ModelImportPipelinePlan.md` (같은 PHASE 4)** | **트랙 V4가 이 계약의 전제다.** ★ 2026-09-01 — V4는 독립 슬라이스가 아니라 `I5-D2`(마스크→`RHIInputElement` 유도)·`I5-D34a/b/c`(GBuffer 정적·스킨·Forward 전환)로 **이행 완료**됐다. 잔여는 "손으로 박힌 오프셋 0" 판정이며 `I6-E`로 legacy가 죽은 뒤 확정한다([`Phase4UnifiedPlan.md`](Phase4UnifiedPlan.md) §1.3 C6). 트랙 V의 퍼뮤테이션 축은 `.shadermeta` 키 체계를 공유한다. PBR-S3의 import→GPU 의미 완결도 D2/D5 재질 ID와 I5/V4 생산 소비 뒤에만 판정한다 |
| **`LightmapBakerPlan.md` (같은 PHASE 4 · 트랙 L)** | **2026-09-01 정정 — `RG8`이 아니라 독립 슬라이스 `Q0`(queue/fence RHI 계약)의 소비자다.** 백그라운드 베이킹이 별도 COMPUTE 큐와 큐 간 펜스를 요구하는데 현재 큐는 `TYPE_DIRECT` 하나뿐이다. "먼저 세우는 쪽이 소유"는 순서를 정하지 않는 문장이었고 RG 계획서는 반대로 "L4는 RG8을 기다린다"고 적어 P0를 임계 경로 142일째에 묶었다 — 소유를 RHI 계층(`Q0`)에 두어 협상을 없앴다([`Phase4UnifiedPlan.md`](Phase4UnifiedPlan.md) §1.2 C2). `LightMapPass`를 Pipeline Asset이 선택하는 Pass로 둘지 소스 Native Pass로 둘지 결정 필요 |
| `LivePipelineDescPlan.md` | 현재 C++ 조립 기술을 첫 native compiler target으로 사용. 공개 asset schema로 직접 노출하지 않음 |
| `MaterialPipelinePlan.md` | `.shadermeta`, Slang, DXIL/SPIR-V, reflection, property override, PSO cache의 필수 선행. M5 generation/소유 경계와 M6-P0~P2d-e의 GBuffer/Forward 소비·required assets·legacy 은퇴가 완료됐다. native Slang source fixture는 격리 선행 가능하며 공용 Standard PBR 소비와 auto-binding은 PBR-S2부터 시작 |
| `RhiBoundaryPlan.md` | RHIEncoder·texture/buffer handle·양 backend 계약을 그대로 소비. ScriptCore로 raw RHI를 올리지 않음 |
| `RenderSceneViewPlan.md` | RendererList와 카메라별 frame packet/history의 입력 경계 |
| PHASE 3-15 | RHI 제출 스레드와 compiled generation fence retirement가 충돌하지 않아야 함 |
| PHASE 4 GPU-driven | Native Pass + Asset feature/variant 조립의 첫 대형 소비자 |
| PHASE 4 Stochastic Lighting | Compute/history/resource schema의 첫 대형 소비자 |
| PHASE 4 DXR·DLSS | capability/fallback + source Native Pass registry의 첫 소비자 |
| `BuildPipelinePlan.md` | Pipeline/Shader Asset·generated cache·shader permutation·선택적 C# assembly·native source module의 Player 패키징 담당 |

---

## 15. 확정한 것 / 나중에 정할 것

### 확정

- Pipeline Asset Inspector가 topology·Pass 데이터·슬롯 schema·조건·폴백의 정본이다.
- 일반 Custom Pass는 네이티브 Template + Shader Asset으로 완결하며 C# Pass를 요구하지 않는다.
- Shader 코드는 재편집 가능한 Visual `.shadergraph` 또는 authored `.slang` 중 하나가 정본이다.
- `.shadermeta`는 양 모드의 property·keyword·entry·render state 정본이다.
- `Graph Editor ⇄ .shadergraph` 저장·재개방 round-trip은 필수다.
- generated `.slang`은 읽기 전용 파생 캐시이며 Slang→Graph 자동 역변환은 없다.
- 프로젝트 전용 슬롯은 Inspector의 `write`와 `ResourceSchema`로 만들 수 있다.
- Shader Graph는 GPU 수식만 소유하고 pipeline topology와 render state를 소유하지 않는다.
- C#은 안정 핸들을 통한 동적 값·미리 선언한 variant 선택에만 선택적으로 사용한다.
- Render/CommandBuild/RHI 스레드에서 CLR을 호출하지 않는다.
- `EnhancedRenderGraph`와 `RHIEncoder`가 유일한 실행 경로다.
- Pipeline Asset 목록은 저작·직렬화 순서이고, 명시적 리소스 버전 edge가 compiled 실행
  순서를 결정한다. 독립 Pass는 목록 순서를 stable tie-break로 사용한다.
- C++ 고급 Pass는 소스 재빌드 방식이다.
- Native DLL Pass ABI는 범위 밖이다.
- ShaderLab 호환 단일 파일 DSL은 만들지 않는다.
- 현재 파이프라인의 19개 노드와 dump·픽셀 동등 치환이 첫 수직 슬라이스다.

### 구현 페이즈에서 정할 것

- Pipeline Asset의 물리 확장자와 패키지 디렉터리
- Pipeline Asset/native Blueprint binary encoding
- 안정 GUID 생성·충돌 검사 도구
- RendererList 필터의 첫 공개 범위
- frame packet의 최대 크기와 overflow 정책
- Pipeline Compiler를 Game 스레드 안에서 돌릴지 별도 worker로 분리할지
- Pipeline Pass Stack의 읽기 전용 graph preview를 editable node editor로 확장할지

이 항목들은 위의 스레딩·수명·RHI 경계를 바꾸지 않는 구현 선택이다.
