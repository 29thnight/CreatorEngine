# RHI 경계 재설계 (PHASE 3-1 재정의)

2026-08-07. DX11 구 렌더러 은퇴 직후 작성 — 그 은퇴가 이 재설계의 계기다.

---

## 1. 지금 무엇이 있는가 — 측정

### 1.1 기존 RHI는 죽었다

`RenderEngine/RHI/`의 RHI 층(633줄)은 PHASE 3-1에서 **DX11 즉시 컨텍스트
모델**로 만들어졌다. DX11 렌더러를 들어내면서 마지막 소비자가 사라졌다:

| 표면 | 실사용 |
|---|---|
| `RHI::Initialize` | 1곳 (`Dx11Main.cpp` — 등록만 하고 아무도 안 쓴다) |
| `RHI::Device()` · `RHI::Immediate()` | **0곳** |
| `RHIDevice`의 쿼리 4종 | **0곳** |
| `RHICommandContext` | `RenderPassData::BindFrameCameraBuffers` 하나 — **그 함수의 호출자도 0곳** |
| `IRenderPass` | `EffectManager`가 상속만 한다(Execute 호출자 없음) |

`RHINativeRenderTarget`·`RHINativeBuffer` 등 열 개의 핸들 타입은 전부
`using = void*`다. 헤더 주석이 스스로 "전환기 임시 통로"라고 적어 두었고,
그 전환기가 끝났는데 통로만 남았다.

**이 층을 DX12로 확장하는 것은 틀린 출발점이다.** 즉시 컨텍스트·즉시 상태
설정 모델이라 DX12(커맨드 리스트 기록 · 명시적 배리어 · 디스크립터 힙 ·
PSO에 구운 상태)와 개념이 맞지 않는다. DX11에 맞춘 인터페이스에 DX12를
끼우면 3-6에서 패스를 새로 쓰기로 한 것과 같은 실수가 된다 — 껍데기를
맞추는 노력이 새로 쓰는 것보다 크고 결과도 나쁘다.

### 1.2 DX12는 밖으로 새지 않는다 — 상위가 DX12 안에 산다

`d3d12.h`/`ID3D12`를 include하는 파일 중 `RHI/DX12/` 밖은 **둘뿐**이다
(`ImGuiHelper/Profiler.h`, `Utility_Framework/Core.Fence.h`). 즉 "DX12가
엔진 전체로 번지는" 문제는 없다.

문제는 반대 방향이다. **렌더링의 상위 개념이 전부 `RHI/DX12/` 안에 있고
`ID3D12*`를 직접 쓴다.**

```
RHI/DX12/EnhancedRenderPass.h        ← 패스 인터페이스 · 드로우 아이템 · 광원 · 프레임 컨텍스트
RHI/DX12/EnhancedRenderGraph.h       ← 그래프 · 리소스 상태 · 배리어
RHI/DX12/EnhancedLivePipelineDesc.h  ← 파이프라인 기술 (방금 만든 것)
RHI/DX12/Enhanced*Pass.{h,cpp}       ← 패스 17종
```

이 중 백엔드에 묶일 이유가 있는 것은 마지막 줄뿐이다. 앞의 셋은 "무엇을
어떤 순서로 그리는가"이지 "어떻게 GPU에 말하는가"가 아니다.

### 1.3 결합의 크기 — 세어 본 값

패스 17종, 총 12,523줄, **DX12 직접 접촉 1,081건**.

무엇을 쓰는지 빈도순(상위):

| 건수 | API | 성격 |
|---|---|---|
| 49 | `CreateShaderResourceView` | 뷰 생성 |
| 25 | `SetPipelineState` | 파이프라인 바인딩 |
| 20 | `SetDescriptorHeaps` | 힙 바인딩 |
| 17 | `CreateRenderTargetView` | 뷰 생성 |
| 17 | `SetGraphicsRootDescriptorTable` | 리소스 바인딩 |
| 16 | `SetGraphicsRootConstantBufferView` | 상수 바인딩 |
| 16 | `RSSetViewports` / `RSSetScissorRects` / `OMSetRenderTargets` | 렌더 상태 |
| 15 | `SetGraphicsRootSignature` · `IASetPrimitiveTopology` | 〃 |
| 12 | `DrawInstanced` | 드로우 |
| 11 | `CreateUnorderedAccessView` · `SetComputeRootConstantBufferView` | 〃 |
| 9 | `Dispatch` | 디스패치 |

★ 읽어야 할 것: **가장 큰 덩어리가 "뷰 생성 + 바인딩"이고, 드로우/디스패치가
아니다.** 패스마다 RTV/SRV/UAV 힙을 손으로 만들고 디스크립터를 손으로 링에서
잘라 테이블에 건다. 이 반복이 결합의 절반 이상이며, 동시에 **가장 실수하기
쉬운 자리**다(포맷 불일치, 링 오버런, 힙 재바인딩 누락 — 전부 조용히 틀린다).

### 1.4 최상위 절단 지점

`EnhancedFrameContext`가 DX12 구현 클래스 다섯을 그대로 노출한다:

```cpp
DX12DeviceResources*    resources;      // 디바이스 · 큐 · 링 · 프레임
DX12PSOManager*         psoManager;
DX12RootSignatureCache* rootSignatures;
DX12MeshCache*          meshCache;
DX12TextureCache*       textureCache;
```

패스가 DX12에 닿는 **모든 경로가 이 구조체를 지난다**. 여기가 첫 절단선이고,
여기만 인터페이스로 바꿔도 "패스가 백엔드 구현 타입을 안다"는 사실이 사라진다.

## 2. 무엇을 하려는가

**목표**: 렌더링 상위 개념(패스 · 그래프 · 파이프라인 기술)이 백엔드 타입을
모르게 한다. 패스 코드가 `ID3D12GraphicsCommandList`가 아니라 RHI 인코더에
기록하고, `ID3D12Resource*`가 아니라 RHI 핸들을 든다.

★ **이 절의 범위 절단 둘이 2026-08-10에 뒤집혔다.** Vulkan 도입이 예정으로
확정됐기 때문이다. 아래 취소선 둘은 "두 번째 백엔드는 부산물"이라는 전제 위의
문장이었고, 그 전제가 사라졌다. 새 범위는 §7이 정의한다.

**하지 않을 것**:

- ~~**Vulkan/Metal 백엔드 구현 없음.** 두 번째 백엔드는 이 작업의 *동기*가
  아니라 *부산물*이다.~~ → **동기가 됐다.** Vulkan이 목적지이므로 "DX12를
  가리는 것"이 아니라 "두 API가 같은 어휘로 들어오는 것"이 기준이다.
- **기존 RHI(DX11 모델) 확장 없음.** 은퇴시킨다(§4, R5 — 완료).
- **무비용 추상화 강박 없음.** 프레임당 수천 번 도는 자리(드로우 루프 안쪽)는
  인라인 가능한 얇은 래퍼로 두고, 가상 함수는 프레임당 수십 번 이하인
  자리에만 쓴다. 판단 근거는 실측이다 — 3-6의 API 오버헤드 벤치가 기준선이다.
  (이것은 유지된다. §5의 벤치가 근거이고 백엔드 수와 무관하다.)
- ~~**패스 셰이더 재작성 없음.** HLSL과 루트 시그니처 레이아웃은 그대로다.~~
  → **셰이더 *소스*는 그대로지만 컴파일 경로와 루트 시그니처 기술은 범위
  안이다.** HLSL은 DXC로 SPIR-V까지 가므로 소스를 다시 쓸 이유는 없다.
  그러나 `D3DCompile` 93건(§7.2.3 에서 **패스 0건**이 됐다)과 루트 시그니처
  기술 160건은 Vulkan에서 각각
  SPIR-V 경로와 파이프라인 레이아웃·디스크립터 셋으로 갈린다 — 옮기지 않으면
  두 번째 백엔드가 패스 17종을 통째로 다시 쓰는 일이 된다.

### 2.1 진짜 동기 셋 — 백엔드 이식성이 아니다

**① 반복되는 실수 자리를 구조로 없앤다.** 뷰 생성·디스크립터 바인딩 1,081건
중 대부분이 같은 형태의 손코드다. 여기서 나는 오류는 컴파일되고, 검증 레이어를
통과하기도 하며, "가끔 이상하게 보인다"로만 드러난다. 실제로 이번 이식에서
겪은 것들이 그 부류였다(RTV 포맷 불일치 → 디바이스 제거, 링 할당 실패 시
조용한 반환).

**② 패스를 테스트 가능하게 한다.** 지금 자가 검증 30여 개는 전부 진짜
디바이스를 세운다. 인코더가 인터페이스면 기록된 명령을 그대로 받아 적는
가짜 백엔드를 만들 수 있고, "이 패스가 무엇을 어떤 순서로 걸었는가"를
픽셀 없이 단정할 수 있다.

**③ 상위 개념을 백엔드 폴더 밖으로 꺼낸다.** `LivePipelineDesc`가
`RHI/DX12/`에 있는 것은 지금도 이상하다 — 그 파일에 DX12 코드는 한 줄도
없는데 위치가 백엔드 안이다. 개념의 자리와 구현의 자리가 어긋나 있다.

## 3. 설계

### 3.1 계층과 폴더

```
RenderEngine/Render/            ← 백엔드를 모르는 상위 (신설)
  RenderPass.h                    패스 인터페이스 · DrawItem · Light · FrameContext
  RenderGraph.h                   그래프 (핸들 · 상태 · 배리어 계획)
  LivePipelineDesc.h              파이프라인 기술
  Passes/*.{h,cpp}                패스 17종 — RHI 인코더에만 기록

RenderEngine/RHI/               ← 경계 (재작성)
  RHITypes.h                      핸들 · 포맷 · 상태 열거
  RHIDevice.h                     리소스 · 뷰 · 파이프라인 생성
  RHIEncoder.h                    커맨드 기록
  RHIFrame.h                      프레임 자원(업로드 링 · 디스크립터 링)

RenderEngine/RHI/DX12/          ← 구현 (기존 자산 유지)
  DX12Device.cpp                  DX12DeviceResources 등 기존 코드가 여기 구현으로
  ...
```

### 3.2 핵심 인터페이스 스케치

핸들은 불투명 정수다. 포인터가 아닌 이유: 백엔드가 리소스를 재배치·풀링해도
상위가 모르게 하고, 값 복사가 싸며, 잘못된 포인터를 역참조할 길이 없다.

```cpp
// RHITypes.h — 어떤 그래픽 API 헤더도 include하지 않는다(기존 RHI의 규약 계승)
struct RHITextureHandle { uint32_t id = kInvalid; };
struct RHIBufferHandle  { uint32_t id = kInvalid; };
struct RHIPipelineHandle{ uint32_t id = kInvalid; };
struct RHIBindingHandle { uint32_t id = kInvalid; };   // 잘라 둔 디스크립터 테이블

enum class RHIFormat : uint16_t { Unknown, RGBA8Unorm, RGBA16Float, R32Float, D32Float, /*…*/ };
enum class RHIResourceState : uint8_t { /* RGResourceState와 같은 집합 */ };
```

**인코더 — 패스가 유일하게 만지는 것.** 1.3의 빈도표가 이 API 목록을 정했다:

```cpp
class RHIEncoder
{
public:
    // 렌더 상태
    virtual void SetRenderTargets(std::span<const RHITextureHandle> colors,
                                  RHITextureHandle depth, bool depthReadOnly = false) = 0;
    virtual void SetViewport(const RHIViewport&) = 0;
    virtual void SetScissor(const RHIRect&) = 0;
    virtual void ClearRenderTarget(RHITextureHandle, const float rgba[4]) = 0;
    virtual void ClearDepth(RHITextureHandle, float depth) = 0;

    // 파이프라인 · 바인딩
    virtual void SetPipeline(RHIPipelineHandle) = 0;
    virtual void SetBindings(uint32_t slot, RHIBindingHandle) = 0;
    virtual void SetConstants(uint32_t slot, const void* data, size_t bytes) = 0;  // 업로드 링 경유

    // 드로우 · 디스패치
    virtual void SetPrimitiveTopology(RHIPrimitiveTopology) = 0;
    virtual void SetVertexBuffer(uint32_t slot, RHIBufferHandle, uint32_t stride) = 0;
    virtual void SetIndexBuffer(RHIBufferHandle, RHIFormat) = 0;
    virtual void Draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex) = 0;
    virtual void DrawIndexed(uint32_t indexCount, uint32_t instanceCount,
                             uint32_t firstIndex, int32_t baseVertex) = 0;
    virtual void Dispatch(uint32_t x, uint32_t y, uint32_t z) = 0;
    virtual void CopyTexture(RHITextureHandle dst, RHITextureHandle src) = 0;
};
```

★ **배리어가 없다 — 라고 적었으나 틀렸다(2026-08-08 정정).** 상태 *전이*는
그래프가 계획하고 백엔드가 실행하는 것이 맞다. 그러나 세어 보니 패스에
`ResourceBarrier`가 9건 남아 있고, 그중 넷은 **그래프가 구조적으로 볼 수 없는
것**이다 — 한 패스 안, 디스패치 둘 사이의 UAV 배리어:

```cpp
// EnhancedForwardPass.cpp:1194 — 컬링 디스패치 직후, 같은 Record 안
uavBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
commandList->ResourceBarrier(2, uavBarriers);
```

그래프는 패스 **간** 전이만 추적한다. 패스 하나가 여러 디스패치로 이루어지고
그 사이에 쓰기-읽기 의존이 있으면 그래프의 어휘로는 표현할 자리가 없다.
"부를 방법 자체가 없다"는 그 자리를 없애지 못한 채 표현만 막는 것이고,
그러면 패스는 배리어를 못 걸어 조용히 틀린 결과를 낸다.

그래서 인코더는 **UAV 배리어만** 갖는다:

```cpp
/// 같은 리소스에 쓰고 나서 읽기 전. 전이(Transition)는 여전히 없다 —
/// 그것은 그래프의 몫이고, 이 하나만 패스의 몫이다.
virtual void UavBarrier(std::span<const RHITextureHandle>) = 0;
```

전이 배리어를 부를 방법이 없다는 계약은 그대로다. 나머지 다섯 건은 리드백
경로(자가 검증)이고 R2c에서 다룬다.

**바인딩 — 최대 덩어리를 없애는 자리.** 지금 패스가 "링에서 N개 자르고,
뷰를 하나씩 만들고, 테이블을 건다"를 손으로 한다. RHI는 그것을 한 번에 받는다:

```cpp
// 프레임 자원. 잘라 둔 테이블의 수명은 그 프레임이다.
class RHIFrame
{
public:
    /// SRV/UAV/CBV를 연속 테이블로 잘라 핸들 하나로 돌려준다.
    /// 지금 패스마다 반복하는 "Allocate(N) → CreateXxxView ×N → 테이블 바인딩"이
    /// 이 한 줄이 된다. 링 오버런은 여기서 한 번만 검사한다.
    virtual RHIBindingHandle CreateBindings(std::span<const RHIBindingDesc>) = 0;

    /// 상수 업로드. 정렬·오프셋 계산이 백엔드 몫이 된다.
    virtual RHIBufferSlice UploadConstants(const void* data, size_t bytes) = 0;
};
```

### 3.3 그래프와의 관계 — 무엇을 바꾸고 무엇을 그대로 두는가

`EnhancedRenderGraph`의 **알고리즘은 손대지 않는다**(검증 완료·39곳이 쓴다).
바꾸는 것은 서명에 드러난 DX12 타입뿐이다:

| 지금 | 뒤 |
|---|---|
| `ImportTexture(ID3D12Resource*, …)` | `ImportTexture(RHITextureHandle, …)` |
| `Compile(ID3D12Device*, …)` | `Compile(RHIDevice&, …)` |
| `Execute(ID3D12GraphicsCommandList*, …)` | `Execute(RHIEncoder&, …)` |
| `ExecuteContext::Resolve() → ID3D12Resource*` | `→ RHITextureHandle` |
| `ExecuteContext::commandList` | `ExecuteContext::encoder` |

`RGResourceState`는 이미 백엔드 중립이다(`ToD3D12`가 유일한 접점) —
`RHIResourceState`로 옮기고 그 변환 함수만 백엔드로 내린다.

## 4. 이행 — 슬라이스

원칙은 지금까지와 같다. 슬라이스마다 A/B 가능하고, 판정은 **패스 목록 문자
일치 + 픽셀 대조 + 자가 검증 통과**다. 기술화 슬라이스에서 세운 기준을
그대로 쓴다(`dx12.live status`의 패스 이름 목록).

**R0 — 상위 개념을 백엔드 폴더 밖으로.** 코드 변경 없이 이동만:
`EnhancedRenderPass.h` · `EnhancedRenderGraph.*` · `EnhancedLivePipelineDesc.*`
→ `RenderEngine/Render/`. 이 시점에는 아직 `d3d12.h`를 include하므로 경계가
생기진 않지만, **무엇이 상위인지 목록이 확정된다**. 이후 슬라이스의 완료
기준이 "이 폴더에서 d3d12.h가 사라지는 것"으로 명확해진다.

**R1 — FrameContext 절단.** DX12 구현 타입 다섯을 인터페이스로 교체
(`RHIDevice&` · `RHIFrame&` · 캐시 3종의 인터페이스). 패스 본문은 아직
`ID3D12`를 쓰지만, **패스가 백엔드 구현 클래스를 아는 상태는 여기서 끝난다.**

**R2 — 바인딩·뷰 생성 이관(최대 이득).** 1.3의 상위 다섯 항목(뷰 생성 66건,
디스크립터 테이블 33건, 힙 바인딩 20건)을 `RHIFrame::CreateBindings`로.
패스당 20~40줄이 3~5줄이 된다. 여기서 조용한 실수 부류(링 오버런·포맷
불일치)가 한 곳으로 모인다.

**R2a — SRV/UAV 테이블 (2026-08-08, 완료).**
패스 13종이 `CreateBindings`로 옮겼다. 직접 `SetDescriptorHeaps`를 부르던
자리도 `BindDescriptorHeaps`로 바뀌었다.

옮기면서 인터페이스에 셋을 더했고, 셋 다 **패스마다 손으로 반복하던 판단을
한 곳으로 내린 것**이다:

| 더한 것 | 없앤 반복 |
|---|---|
| `SrvDepth(resource)` | 깊이→색 포맷 대응(D32→R32 등)을 읽는 패스마다 손으로 적던 것. 예전 코드는 대부분 D32 하나만 다뤘다 |
| `OrNull()` | "비어도 되는 자리"의 표시. 기본은 널이면 테이블 거절이고, 표시한 자리만 널 디스크립터를 깐다 |
| 포맷 UNKNOWN 해석 | 명시 SRV 설명 경로에서 UNKNOWN은 유효한 값이 아니다 — 리소스에게 물어 채운다 |

★ **동작이 바뀐 곳이 둘 있다.** 둘 다 예전 쪽이 조용히 틀리던 자리다:

- **GBuffer** — 재질 텍스처 넷 중 널인 슬롯만 건너뛰고 나머지로 테이블을
  걸었다. 그 칸에는 힙의 이전 내용이 남는다. 이제 넷이 다 있어야 그리고,
  아니면 그 배치를 건너뛴다. `PrepareFrame`이 폴백으로 넷을 채우므로 널은
  업로드 실패뿐이다.
- **SSGI 공통 디스패치** — 같은 형태(`continue`로 구멍)였다. 이제 폴백까지
  널이면 그 디스패치를 접는다.

**남긴 것과 이유:**

- **RTV/DSV (17건)** — 디스크립터 링이 아니라 패스마다 자기 힙을 만든다.
  같은 인터페이스로 다룰 수 없어 R2b로 나눈다(아래).
- **VolumetricFog의 볼륨 클리어** — `ClearUnorderedAccessViewFloat`은 셰이더
  가시 GPU 핸들과 비가시 CPU 핸들을 **짝으로** 요구한다. `RHIBindingTable`은
  테이블 시작 핸들만 준다. 원소별 GPU 핸들을 노출하면 추상화가 도로 풀리므로
  두었다.
- **자가 검증 하네스·IBL 생성기·ImGui 셸** — 패스가 아니다.

**R2b — RTV/DSV 힙 통합.** 지금 패스가 각자 `ID3D12DescriptorHeap`을
만들어 들고 있고(Initialize에서 생성·Shutdown에서 해제), 매 프레임 같은
자리에 뷰를 다시 만든다. 프레임 링 하나로 모으면 그 힙들과 수명 관리가
통째로 사라진다. R2a와 성격이 같지만 인터페이스가 달라 분리했다.

★ **위에 "패스 12종·RTV 링"이라 적었으나 다시 세니 더 컸다(2026-08-08).**
정확한 범위:

| 건수 | 무엇 |
|---|---|
| **21** | `CreateDescriptorHeap` — 힙을 소유한 패스 헤더는 **14개** (+IBL 생성기 1) |
| 17 | `CreateRenderTargetView` |
| 7 | `CreateDepthStencilView` |
| 6 | `GetDescriptorHandleIncrementSize` — 핸들 산술이 패스에 그대로 있다 |

그리고 **"RTV 링"이라는 이름이 요구 셋을 감춘다.** 인터페이스는 DSV까지
받아야 하고, DSV 쪽이 오히려 까다롭다:

- **배열 슬라이스** — `EnhancedShadowPass`가 캐스케이드마다
  `Texture2DArray.FirstArraySlice`로 DSV를 만든다. 슬라이스 개념이 없으면
  그림자가 통째로 못 옮겨진다.
- **포맷 명시** — 깊이는 리소스 포맷 그대로 뷰를 만들 수 없는 경우가 있어
  패스가 `kDepthFormat`·`kShadowFormat`을 손으로 적는다. R2a의 `SrvDepth`가
  읽는 쪽에서 없앤 것과 **같은 부류의 실수 자리**가 쓰는 쪽에 남아 있다.
- **읽기 전용 DSV** — `EnhancedRenderGraph.h`가 이미 쓰는 쪽 계약으로
  `DSV_FLAG_READ_ONLY_DEPTH`를 요구한다(깊이를 읽으면서 묶는 패스).

★ **힙 21개 중 하나는 RTV/DSV가 아니다.** VolumetricFog의 `m_clearHeap`은
비셰이더 가시 UAV 힙이고, `ClearUnorderedAccessViewFloat`이 GPU·CPU 핸들을
짝으로 요구해서 생긴 것이다(위 R2a "남긴 것" 참조). "RTV 힙만 걷는다"로
범위를 잡으면 이것이 남아 패스 하나가 계속 힙을 든다. 인코더에
`ClearUnorderedAccess(handle, rgba)`를 두면 힙과 이중 뷰 생성이 함께
사라지므로 **R3에서 같이 닫는 것으로 묶어 둔다** — R2b에서는 남는다는
사실만 기록한다.

★ **R2b 결과 (2026-08-08, 완료).** 패스 14종이 옮겼다. 인터페이스는 넷이다
(`CreateRenderTargets` · `BindRenderTargets` · `ClearRenderTargets` ·
`ClearDepthTarget`), R2a의 `CreateBindings`/`RHIBindingTable`과 같은 모양이다 —
만들고 → 핸들 하나를 받고 → 그 핸들로만 쓴다.

**힙을 셋째 자료구조로 뒀다.** `DX12DescriptorRing`이 CBV/SRV/UAV 아닌
타입을 거절하며 적어 둔 이유("CPU 전용 힙은 GPU가 읽지 않으므로 프레임
구간으로 나눌 필요가 없다")가 맞아서, 링을 넓히는 대신 더 단순한
`DX12TargetViewHeap`을 만들었다 — 프레임 3분할도 펜스 대기도 없고
`BeginFrame`에서 커서만 0으로 되돌린다.

★ **그 판단의 근거는 추론이 아니라 기존 코드다.** RTV/DSV 디스크립터는
기록 시점에 소비되는데, 그것을 확인해 주는 것이 R2b 이전의 패스 14종이다 —
전부 자기 힙의 **같은 칸에** 매 프레임 뷰를 다시 만들면서 인플라이트
3프레임으로 돌고 있었다. 아니었다면 진작 깨졌을 것이다.

**측정 결과 — 패스의 디바이스 호출 68 → 14:**

| | 전 | 후 |
|---|---|---|
| `CreateDescriptorHeap` | 21 | **1** (VolFog 클리어 힙) |
| `CreateRenderTargetView` · `CreateDepthStencilView` | 17 · 7 | **0** |
| `GetDescriptorHandleIncrementSize` | 6 | **1** (같은 자리) |
| `OMSetRenderTargets` · `ClearRenderTargetView` · `ClearDepthStencilView` | 16 · 8 · 5 | **0** |
| 힙을 든 패스 헤더 | 14 | **0** |

★ **딸려 나온 것: SSS의 `rtvIndex`가 두 가지를 뜻하고 있었다.** RTV 힙
슬롯 번호이면서 동시에 '마지막 축인가'의 판정이었다(`(0 == rtvIndex) ?
false : m_keepAlive`). 슬롯이 사라지자 그 겸직이 드러나 `isFinal`로 나눴다 —
동작은 같고, 뜻이 하나씩 적혔다.

**검증 (2026-08-08):** 전체 솔루션 Debug x64 빌드 0오류·새 경고 0. 자가 검증
24종 중 **22종 통과**(R2b가 손댄 패스 14종 전부 포함, 픽셀 대조 포함).

★ **이 "24종"이 틀린 모수였다(R2c-a에서 발견·정정).** 검사 목록을 CLI 도움말
텍스트에서 뽑았는데, 도움말은 사람이 손으로 유지하는 것이라 코드보다 적었다 —
실제 `cmd == "dx12.*"`는 **35개**이고 도움말에는 26개만 실려 있었다. 빠진
아홉이 이것들이다:

```
dx12.forward  dx12.forwardscale  dx12.forwardshade
dx12.ssgi     dx12.ssao          dx12.ssaoscale
dx12.post     dx12.postscale     dx12.ui
```

★ **하필 그중 다섯이 R2b·R2c-a가 바꾼 자리를 도는 검사다** —
`dx12.ui`·`dx12.post`·`dx12.ssao`는 R2b가 RTV를 이관한 패스이고,
`dx12.forward`·`dx12.ssgi`는 R2c-a가 바꾼 `EnsureTileBuffers`·`EnsureHistory`를
돈다. 즉 변경을 가장 직접 검증하는 것들이 빠져 있었다. 뒤늦게 돌려 전부
통과했으나, R2b 시점의 "회귀 없음"은 그 아홉에 대해서는 근거가 없었다.

판정 어휘도 둘만 알고 있었다. `*scale` 계측 검사는 통과·실패가 아니라
`완료`를 찍는데 그것을 실패로 셌다.

**고친 방법:** 목록을 도움말이 아니라 소스의 `cmd == "dx12.*"`에서 뽑고,
판정을 셋(`통과`/`실패`/`완료`)으로 읽고, 검사마다 stderr 바이트 수를 함께
남긴다. 어서션을 stderr로 돌려 둔 뒤로는 그 수가 0이 아닌 것 자체가 신호다.
**검증 목록이 검증 대상보다 낡으면 "통과"가 아무것도 뜻하지 않는다.**

**전수 기준선 (33종 · R2c-a 시점):** 29 통과 · 3 계측 · 1 실패.
유일한 실패는 `dx12.scene`이고 그 검사가 명시한 전제조건이다(씬에 메시 없음).
stderr는 33종 모두 0바이트 — 어서션이 하나도 나지 않았다.
`dx12.live status`의 패스 이름 목록이 착수 전과 문자 그대로 같다.
타깃 뷰 힙 넘침 0(RTV 최대 10/512 · DSV 6/256).

★ **용량을 50배 과하게 잡았다.** 위 주석에 "GBuffer 5 × 조각 8 = 40이 가장
크다"고 적고 512를 잡았는데 실측 최대는 10이다. 다만 이 값은 드로우 0인
씬의 것이라 상한이 아니다 — 드로우가 있는 씬에서 다시 재고 줄인다.

**통과하지 못한 둘은 R2b 이전에도 같다** — 착수 전 코드를 stash·빌드해
나란히 돌려 확인했다:

| 검사 | R2b | 착수 전 |
|---|---|---|
| `dx12.gizmoscene` | 실패(점등 0) | **판정 줄이 글자까지 동일** |
| `dx12.scene` | 어서션 정지 | **종료 코드·출력 크기 동일**(exit 0x80000003 · 756B) |

★ **둘만 에디터 씬의 살아 있는 카메라를 쓴다**(`CameraManagement->GetCameras()`).
나머지 22종은 자기 고정 카메라를 세우고 전부 통과한다. 원인은
`XMMatrixOrthographicOffCenterLH(-radius, radius, …)`의 `radius == 0` —
드로우 0인 씬에서 캐스케이드 절두체가 한 점으로 붕괴한다(`DirectXMathMatrix.inl:3014`).
`ComputeCascades`는 R2b가 건드리지 않았고, 라이브 렌더러는 같은 그림자 패스를
매 프레임 돌면서 멀쩡하다.

★ **뒤이어 원인을 찾아 고쳤다 — 퇴화 캐스케이드는 증상이었다.**
`radius == 0`의 위가 있었다: `RenderPassData`의 프레임 카메라 스냅샷
이중 버퍼(PHASE 3-2)에 **게시자가 없었다.** `LatchFrameSnapshot()`과
`UpdateData(Camera*)` 둘 다 호출자 0곳이라 `GetFrameSnapshot()`이 계속
영행렬과 `near = far = 0`을 돌려주고 있었고, 그 값으로 절두체를 세우니
코너 여덟이 한 점으로 모였다.

`git log -S`가 끊긴 지점을 짚는다 — `ccca6964`(DX11 구 렌더러 은퇴).
게시·래치를 부르던 것이 DX11 렌더 루프였고, **생산자만 사라지고 소비자는
남았다.** 소비자는 테스트뿐이 아니었다(`ParticleSystem`의 뷰·투영,
`Mesh`, `RenderPassData::ConvertScreenToWorld`, CLI 진단). 라이브 DX12는
자기 스냅샷을 따로 만들어 써서 멀쩡했고, 그래서 이 층이 죽은 것이
화면으로는 드러나지 않았다.

고친 자리는 라이브의 프레임 경계 밀봉 지점(`CaptureFromCamera`)이다.
거기에 이미 만들어 둔 스냅샷을 그대로 게시한다 — `UpdateData(Camera*)`를
`PublishFrameSnapshot(const FrameCameraSnapshot&)`로 바꾼 이유가 그것이다.
같은 계산을 두 번 하지 않고, 살아 있는 카메라를 한 번 더 읽지 않으며
(그 재읽기를 없애는 것이 애초에 3-2의 목적이었다), 라이브가 쓰는 값과
밖이 보는 값이 정의상 같아진다.

| | 전 | 후 |
|---|---|---|
| `dx12.gizmoscene` | 점등 0 · 실패 | **점등 512 · 통과** |
| `dx12.scene` | 어서션 정지 | 어서션 소멸 · 자기 전제조건으로 정상 종료 |
| 그림자 캐스케이드 분할 | (near=far=0) | 43.0 / 105.3 / 500.0 |

자가 검증이 22 → **23 통과**가 됐고 나머지 22종의 판정은 하나도 바뀌지
않았다. `dx12.scene`의 남은 실패는 결함이 아니라 그 검사가 명시한
전제조건이다 — "드로우가 0건이다 — 씬에 메시를 배치한 뒤 다시 실행할 것".

★ 그 전제조건을 채우려 모델을 로드하다 별건을 하나 밟았다.
`Scene::RegisterComponent`가 ThreadPool 워커에서 `std::vector<Component*>`에
락 없이 push_back 해 간헐 액세스 위반이 난다(모델 계층 생성 경로).
이 축과 무관해 따로 둔다.

★ **정지 지점이 한 줄로 확정됐다(동기화 후 재검증).** 다른 세션이 `dx12.scene`에
`fflush` 단계 마커를 넣어 준 덕이다(커밋 `691e1b68`):

```
[dx12.scene] 진입
[dx12.scene] [1/4] 씬 입력 확보 완료
[dx12.scene] [3/4] 씬 카메라 렌더
[dx12.scene]   render #1 (드로우 후보 0 · 병렬 끔)     ← 여기서 어서션
```

**첫 렌더 호출, 드로우 후보 0.** 위 가설("드로우 0인 씬")이 그대로 확인된
값이라, 후속 작업은 이 한 줄에서 출발하면 된다.

★ 그 커밋의 판단 근거가 이 절의 것과 같다 — "조용해지면 '멈춘 것'인지 '느린
것'인지부터 갈려야 하는데, 마커가 없으면 그 질문조차 못 한다." 같은 함정을
두 세션이 따로 밟고 같은 결론에 왔다.

★ **이 실패가 25분을 먹은 이유는 따로 있다.** 어서션이 `--script` 실행에서
**모달 대화상자**로 떠서 프로세스가 사람 입력을 기다린다. 로그에는 아무것도
안 남고 CPU만 도니 '느린 것'과 구분되지 않았다. 자가 검증을 한 프로세스에
몰아 넣었던 것도 겹쳐서, 뒤의 14종이 통째로 막혀 있었다 —
**테스트당 프로세스 하나로 나누고 나서야** 어디서 막히는지 보였다.
비대화형 실행에서 어서션을 stderr로 돌리는 것은 R2b 범위 밖이라 따로 둔다.

**남긴 것:** VolumetricFog의 `m_clearHeap` 하나. `ClearUnorderedAccessViewFloat`이
셰이더 가시 GPU 핸들과 비가시 CPU 핸들을 짝으로 요구해서 있는 힙이라
RTV/DSV 인터페이스로는 다룰 수 없다 — 인코더의 `ClearUnorderedAccess`가
생기는 R3에서 함께 닫힌다. IBL 생성기의 힙 하나도 남는데, R2a와 같은
기준이다(패스가 아니다).

**R2c — 패스 소유 리소스와 리드백.** R2a·R2b가 *뷰*를 옮기고 나면 남는
디바이스 접촉은 리소스 *생성* 쪽이다:

- `CreateCommittedResource` **10건** — Forward+ 타일 버퍼, SSGI 히스토리·
  리저버, VolFog 볼륨. 그래프는 트랜지언트만 소유하므로 프레임을 넘어 사는
  것은 패스가 직접 만든다. `RHITextureDesc`/`RHIBufferDesc`가 필요하다.
- **리드백** — READBACK 힙 생성 → `CopyTextureRegion` → `WaitForGpu` →
  `Map`/`Unmap`. `D3D12_PLACED_SUBRESOURCE_FOOTPRINT`의 행 피치 정렬까지
  패스가 계산한다.

★ **이 슬라이스가 `GetDevice()`·`GetCommandList()`를 인터페이스에서 빼는
열쇠다.** 패스의 `GetCommandList()` 8곳 중 7곳이 자가 검증 코드이고, R2a에서
"자가 검증 하네스는 패스가 아니다"라며 범위 밖에 두었다. 그러나 **R6(가짜
백엔드)의 목적이 바로 그 하네스를 대체하는 것**이라, 리드백에 인터페이스가
없으면 R6도 성립하지 않는다. 미뤄 둔 값이 여기서 청구된다.

★ **둘로 나눴다(2026-08-08).** 10건을 호출 함수별로 세니 정확히 반반이었다:

| | 곳 | 성격 |
|---|---|---|
| 프로덕션 5 | `Forward+::EnsureTileBuffers` ×2 · `SSGI::EnsureHistory` ×2 · `VolFog::CreateVolumes` ×1 | 프레임을 넘겨 값을 잇는 패스 소유 리소스 |
| 하네스 5 | `RunForwardPlusTest` ×2 · `RunSSGITest` ×3 | 리드백 버퍼 · 업로드 채우기 |

성격이 다르고 판정 단위도 다르다 — R2a가 R2b를 낳은 것과 같은 이유다.

**R2c-a — 패스 소유 리소스 생성 (2026-08-08, 완료).**
`RHIBufferDesc`/`RHITextureDesc` + `CreateBuffer`/`CreateTexture`로 프로덕션
5건을 옮겼다. **프로덕션 경로에서 `GetDevice`가 사라졌다** — 남는 것은
VolumetricFog의 클리어 힙 하나뿐이고 그것은 R3에서 인코더의
`ClearUnorderedAccess`와 함께 닫힌다.

★ **이 슬라이스의 값은 R2a·R2b보다 작고, 그렇게 적어 둔다.** 저쪽은 같은
손코드가 패스마다 반복되는 것을 걷었지만 리소스 생성은 세 곳뿐이라 줄어드는
줄 수가 크지 않다. 그런데도 옮기는 값이 셋이다:

- **조용히 틀리는 기본값이 한곳으로 모인다.** 버퍼는 `Height`·
  `DepthOrArraySize`·`MipLevels`·`SampleDesc.Count`가 전부 1이어야 하고
  `Layout`이 `ROW_MAJOR`여야 한다. 하나만 빠져도 생성이 실패하는데 증상은
  '리소스가 널이다'로만 드러나 원인이 멀다.
- **이름이 규약이 된다.** 전에는 VolFog만 `SetName`을 불러 나머지는 PIX
  캡처와 DRED 숨결에 정체 모를 리소스로 떴다. `debugName`을 desc 필드로 두면
  만들면서 이름을 안 정할 수가 없다 — 다섯이 이름을 얻었다.
- **초기 상태의 규칙이 기본값에 박힌다.** 버퍼는 COMMON에서 출발해 첫
  사용에서 승격되는 것이 맞는데, UAV를 초기 상태로 주면 검증 레이어가
  '무시한다'고 경고만 남기고 지나간다.

★ **초기 상태는 아직 `D3D12_RESOURCE_STATES`다.** `RGResourceState`가 이미
백엔드 중립이라 그쪽이 옳지만, 그 타입은 그래프 헤더에 있고 경계 헤더가
그래프를 끌어오는 것은 방향이 거꾸로다. §3.3대로 R4에서 `RGResourceState`를
RHI로 올릴 때 이 필드도 함께 중립화한다.

**R2c-b — 리드백과 업로드 복사.** R6의 선결 조건이 이쪽이다.

★ **"하네스 5건"이 틀린 규모였다(2026-08-08 측정).** 그 5는
`CreateCommittedResource` 중 READBACK 힙만 센 값이고, 리드백 *패턴* 자체는
자가 검증 전반에 퍼져 있다. 다시 센 값:

| 건수 | 무엇 |
|---|---|
| **81** | `Map` / `Unmap` |
| **63** | `D3D12_TEXTURE_DATA_PITCH_ALIGNMENT` 행 피치 계산 |
| **53** | `CopyTextureRegion` |
| **32** | READBACK 힙 생성 |
| 7 · 6 | `CopyBufferRegion` · `CopyResource` |

**파일 28개.** 그리고 검사마다 자기 것을 따로 만들어 두었다:

- **`XxxHalfToFloat` 17종** — 이름만 다르고 **본문이 바이트 단위로 같다**.
  `XMConvertHalfToFloat`가 하는 일 그대로다.
- **`kXxxRowPitch` 18종** — 같은 정렬 계산.
- **`XxxCapture` 8종 이상** — `data` + `At(x, y, channel)`이 공통이고
  질의 메서드만 검사마다 다르다.

★ 즉 R2a가 걷은 것과 **같은 형태의 중복**이다. 같은 것을 열일곱 번 적어 둔
자리이고, 리드백 시퀀스(힙 생성 → 복사 위치 조립 → 제출·대기 → Map/assign/Unmap)가
검사마다 약 40줄씩 반복된다.

**둘로 나눈다.** 28개 파일을 한 번에 훑으면 A/B 판정 단위가 너무 커진다:

- **R2c-b1** — 인터페이스(`CreateReadback`·`CopyToReadback`·`MapReadback`)와
  포맷을 아는 중립 캡처 타입. 서로 다른 모양 셋(단일 텍스처 · 배열/다중 슬라이스 ·
  버퍼)을 대표 검사로 옮겨 계약을 증명한다.
- **R2c-b2** — 나머지. 기계적이다.

**R2c-b1 결과 (2026-08-08, 완료).** 인터페이스 셋(CreateReadback ·
CopyToReadback · MapReadback)과 포맷을 아는 RHIReadbackImage를 두고, 서로 다른
모양 둘을 옮겨 계약을 증명했다.

★ 세 단계로 나뉜 것은 GPU 리드백의 성질이 정한 것이지 취향이 아니다 —
만드는 시점(프레임 밖) · 기록하는 시점 · 읽는 시점(제출·대기 뒤)이 서로 달라
한 함수로 묶을 수 없다. MapReadback이 Unmap까지 하는 것은 의도다(호출부가
빠뜨릴 자리를 없앤다).

| 형태 | 검사 | 결과 |
|---|---|---|
| 단일 텍스처 | `dx12.grid` | 79줄 삭제 · 18줄 추가 (순감 61) |
| 다중 슬라이스 | `dx12.decal` | 이미지 하나를 캡처 셋이 슬라이스 번호로 가리킨다 |

Decal이 특히 값을 했다. 예전에는 Map 뒤에 base + sliceBytes * n으로 손수 잘라
세 캡처에 나눠 담았는데, 이제 나눌 것이 없다.

★ **판정 근거: 측정값이 변환 전후로 글자 단위까지 같다.** Grid의 점등
9840(15.0%)·원점 선 R 0.225, Decal의 확산 0.2520이 그대로다. 새 디코드
경로가 손코드 열일곱 종과 같은 수를 낸다는 뜻이고, 리드백 변환에서 이보다
강한 증거는 없다. 전수 33종도 29 통과로 직전과 동일하다.

★ 남긴 것: **버퍼 리드백**(CopyBufferRegion — Forward+ 타일 버퍼). 텍스처와
시그니처가 달라 CopyBufferToReadback이 따로 필요하고, 그 형태는 b2에서 실제
쓰임을 보고 정하는 것이 맞다.

★ 캡처 타입이 `At(x, y, channel)`을 포맷을 보고 디코드하면 `XxxHalfToFloat`
17종과 `kXxxRowPitch` 18종이 함께 사라진다. 리드백 배관보다 이쪽이 큰 이득이다.

**R2c-b2 결과 (2026-08-09, 완료).** 배치 여섯으로 나눠 파일 28개를 훑었고,
`RHI/DX12/`에서 손코드 리드백이 **0**이 됐다. 남는 READBACK 힙 생성 둘은
`DX12DeviceResources`(구현 자신)와 `DX12GpuProfiler`(타임스탬프 쿼리 — 픽셀이
아니다)다.

| 지표 | 착수 전 | 완료 |
|---|---|---|
| READBACK 힙 생성 | 32 | **2**(구현·프로파일러) |
| `Map`/`Unmap` 쌍 | 81 | **6**(구현·프로파일러·DX11 벤치 절반) |
| 행 피치 정렬 계산 | 63 | **1**(`CreateReadback` 안) |
| `XxxHalfToFloat` | 17 | **0** |
| `kXxxRowPitch` | 18 | **0** |
| 패스 본문의 `commandList->` | 4 | **0** |

★ **인터페이스가 셋에서 일곱이 됐고, 넷 다 실제 쓰임이 정한 모양이다.**
계획서가 b1 시점에 "형태는 b2에서 실제 쓰임을 보고 정하는 것이 맞다"고 미뤄
둔 판단이 여기서 청구됐다:

| 더한 것 | 실제 쓰임이 요구한 것 |
|---|---|
| `CreateBufferReadback` · `CopyBufferToReadback` | 구조화 버퍼 셋(Forward+ 타일 카운트 ×2 · 타일 목록). 포맷도 행 간격도 없다 — 픽셀이 아니라 정렬할 행이 없어서다 |
| `CopyVolumeToReadback` | 포그 격자(160×90×128). D3D12의 배치 풋프린트가 3D도 행 간격 하나로 z·y를 잇는다 — 즉 **장 모델이 3D에 그대로 맞는다**(sliceBytes = rowPitch × height, z = 장 번호) |
| `CopyPartialToReadback` | "부분 영역 3"으로 세어 둔 자리. 셋을 다시 보니 전부 같은 목적이었다 — 그래프가 소비자 없는 패스를 걷어내므로 결과를 살려 두려고 8×1만 옮긴다. 크기를 인자로 받지 않는 이유가 그것이다(리드백을 8×1로 만든 것이 곧 "8×1만 뜬다") |
| `Elements<T>()` | 버퍼를 `At()`로 읽으면 모르는 포맷이라 '전부 0'이 조용히 나온다. 읽는 길을 타입으로 갈라 그 자리를 없앴다 |

★ **디바이스가 들고 있던 프레임 리드백도 자기 인터페이스로 돌렸다.**
`DX12DeviceResources`가 `m_readback`·`m_rowPitch`를 손으로 만들고, 쓰는 쪽
셋이 `GetReadbackBuffer` + `GetRowPitch`로 행 산술을 다시 했다. 그 산술이 네
곳에서 함께 사라졌다. 초기 상태가 COMMON → COPY_DEST로 바뀌는데, 옛 주석은
"COPY_DEST를 넘기면 검증 레이어가 경고를 쌓는다"고 적어 두었다 — b1 이후의
리드백 30여 곳이 전부 COPY_DEST로 조용하고, `dx12.selftest`가 검증 레이어
클린으로 통과한다. **그 주석의 전제가 지금 코드에서는 반증됐다.**

**검증 방법을 배치마다 같게 했다:** 변경 전후를 각각 빌드해 그 배치가 건드린
검사만 나란히 돌리고 판정 줄을 `Compare-Object`로 견줬다. 여섯 배치 전부 차이
0이다. 계측 검사(`*scale`)는 시간이 매번 다르므로 구조값을 따로 견줬다 —
넘친 타일 0/0/0/0/0/3446(최대 1/1/2/6/15/60)과 경계 판정이 글자까지 같다.
SSGI 스윕 표 12줄도 같다.

★ **이것이 R6의 선결 조건이었다.** §2.1 ②가 "인코더가 인터페이스면 가짜
백엔드로 갈아탈 수 있다"고 적었지만, 자가 검증이 리드백으로 픽셀을 읽으므로
그 경로에 인터페이스가 없으면 갈아탈 자리가 없었다. 이제 있다.

**R3 — 인코더 이관.** 렌더 상태·드로우·디스패치를 `RHIEncoder`로.
패스에서 `commandList->`가 사라진다.

★ **§3.2의 인코더 스케치가 실사용의 3분의 1을 빠뜨렸다(2026-08-08 정정).**
커맨드 호출을 다시 세니 256건이고, 스케치가 덮는 것은 173건이다. 빠진 83건:

| 건수 | 무엇 | 왜 빠졌나 |
|---|---|---|
| **39** | 컴퓨트 루트 바인딩 (Table 17 · CBV 12 · RootSig 9 · SRV 1) | `SetBindings`에 바인드 포인트가 없다 |
| **15** | `SetGraphicsRootSignature` | `SetPipeline`이 루트 시그니처를 안 든다 |
| **13** | `Set*RootShaderResourceView` | 테이블을 안 거치는 루트 직결 SRV — 대응 없음 |
| **9** | `ResourceBarrier` | 위 §3.2 정정 |
| **7** | `CopyResource` 3 · `CopyTextureRegion` 3 · `CopyBufferRegion` 1 | `CopyTexture` 하나뿐 |

셋을 더한다:

**① 바인드 포인트.** DX12는 그래픽스와 컴퓨트의 루트 상태가 완전히 별개다.
★ 중립화해도 사라지지 않는 구분이다 — Vulkan도 `VK_PIPELINE_BIND_POINT_*`로
같은 것을 요구한다. 백엔드 하나의 사정이 아니므로 인터페이스에 둔다.

```cpp
enum class RHIBindPoint : uint8_t { Graphics, Compute };
virtual void SetBindings(RHIBindPoint, uint32_t slot, RHIBindingHandle) = 0;
```

**② 루트 직결 버퍼.** 본 팔레트·라이트 목록·인스턴스 버퍼가 GPU 주소로
직접 걸린다. `SetConstants(slot, data, bytes)`는 업로드와 바인딩을 붙여
놓은 형태라 이 자리에 못 쓴다 — 실제 패스는 길이가 프레임마다 다르고
(`m_bonePalettes.size()`), 한 번 할당해 루프로 채운 뒤 건다. 업로드와
바인딩이 나뉘어 있어야 한다. `RHIFrame::UploadConstants`는 이미 슬라이스를
돌려주므로 받는 쪽만 있으면 된다:

```cpp
virtual void SetRootBuffer(RHIBindPoint, uint32_t slot, RHIBufferSlice) = 0;
```

**③ 루트 시그니처의 자리.** DX12는 PSO와 루트 시그니처가 별개 객체이고
패스가 둘을 따로 건다. Vulkan은 파이프라인 레이아웃이 파이프라인에
구워진다. **Vulkan 쪽 모양을 택한다** — `RHIPipelineHandle`이 레이아웃까지
들고, DX12 구현이 `SetPipeline` 하나에서 둘을 건다. `DX12GraphicsPipelineDesc`가
이미 `rootSignature`와 `rootSignatureId`를 함께 받고 있어 그 자리가 있다.

★ **인코더는 워커당 하나다.** `AddSplitPass`의 `ExecuteContext::commandList`는
워커 리스트일 수 있는데 `IRenderDeviceServices::GetCommandList()`는 프레임
전역 리스트다 — 지금 한 인터페이스에 스레드 규약이 다른 둘이 섞여 있고
타입은 그것을 말하지 않는다. 인코더를 `ExecuteContext`가 주는 것으로
고정하면 전역 리스트를 집을 길이 없어진다.

★ **여기서 `IRHIDeviceResources`의 첫 소비자가 생긴다.** D2에서 "소비자가
생긴다"는 예측이 빗나갔고, 그때 "R3의 인코더가 붙어야 생긴다"고 적었다.
그러려면 R3 설계에 그 접점이 있어야 하는데 §3.2에는 없다 — 인코더의 수명이
`BeginFrame`/`EndFrame`으로 정의된다는 관계를 타입으로 적는 것이 그 접점이다.

**R3-1 — 인코더 정의와 첫 패스 (2026-08-08, 완료).**
`RHIEncoder`(인터페이스) · `DX12Encoder`(구현) · `ExecuteContext::encoder` 배선.
위 정정 셋(바인드 포인트 · 루트 직결 버퍼 · 루트 시그니처 자리)을 그대로
반영했고, `UavBarrier`만 두어 전이는 여전히 부를 방법이 없다.

★ **`ClearUnorderedAccess`가 VolumetricFog의 둘째 힙을 없앨 자리다.** DX12는
같은 UAV의 셰이더 가시 GPU 핸들과 비가시 CPU 핸들을 짝으로 요구해서 그
패스가 힙을 하나 더 들고 같은 뷰를 두 번 만들고 있었다. 인코더가 안에서
짝을 맞추니 호출부는 "이 리소스를 이 값으로 지운다"만 적는다 — RHI가 DX12보다
단순해지는 자리다. 비가시 UAV는 `DX12TargetViewHeap`이 받도록 타입만 넓혔다
(성질이 RTV/DSV와 같다 — 기록 시점에 소비되므로 프레임 구간이 필요 없다).

★ **벤치 결과를 설계에 반영했다.** §5가 열어 둔 "드로우 루프만 비가상"
선택지를 쓰지 않는다. 가상 dispatch가 호출당 0.9ns이므로 특별 취급할 이유가
없고, 그 근거를 헤더에 적었다.

★ **이행은 패스 단위다.** `ExecuteContext`에 `encoder`를 `commandList`와
나란히 뒀다 — 둘이 같은 커맨드 리스트에 적으므로 한 프레임에 섞여도 되고,
그래서 패스 하나씩 A/B가 성립한다. 워커마다 인코더가 따로인 것이 중요하다:
조각들이 각자 다른 리스트에 적으므로 프레임 전역 인코더면 서로의 기록을 덮는다.

`Execute`의 서명은 바꾸지 않았다. 인코더가 디바이스 서비스를 알아야 하는 것은
`ClearUnorderedAccess` 하나뿐인데, 인자를 더하면 자가 검증 39곳이 함께
흔들린다 — 선택적 setter로 두고 안 주면 그 한 호출만 아무 일도 하지 않는다.

첫 패스로 Grid를 옮겼고 측정값이 그대로다(점등 9840(15.0%) · 원점 선 R 0.225).
남은 16종은 같은 형태로 하나씩 간다.

**R4 — 그래프 서명 교체.** §3.3의 표대로. 자가 검증 39곳이 함께 바뀐다 —
가장 넓게 퍼지는 슬라이스라 R2·R3로 패스가 이미 인코더를 쓰게 된 뒤에 한다.

**R5 — 구 RHI 은퇴. (완료 2026-08-09)** `RHI.{h,cpp}` · `RHIDevice.h` ·
`RHICommandContext.h` · `RHIDefinitions.h` · `DX11RHI.{h,cpp}` 일곱 파일을
지웠다. 미루던 이유였던 `IRenderPass`는 D4에서, `EffectManager`는 PHASE 10에서
이미 사라져 선행 조건이 저절로 풀렸다.

★ **은퇴가 아니라 확인이었다.** 지우기 전 소비자를 세니 include 셋뿐이고
심볼 사용이 0이었다 — `RHI::Initialize`를 부르는 코드조차 없어 백엔드가 등록된
적이 없다. 즉 `Device()`를 불렀다면 assert에 걸렸을 것이고, 이 계통은 이미
한동안 죽어 있었다. PHASE 3-1이 "3-5가 서면 말라 죽는다"고 적어 둔 대로 됐다.

지금 살아 있는 RHI 경계는 `IRHIDeviceResources.h`(디바이스·스왑체인 계약)와
`ScreenSizedResource.h`(화면 크기 버스) 둘이다. 이름만 같고 계보가 다르다.

### 4.2 D축 — 디바이스·프레젠트 (2026-08-07 추가)

★ **이 축이 §4의 R축에 빠져 있었다.** R1~R6은 "패스가 GPU에 말하는 방법"만
다루고 "디바이스를 누가 소유하고 화면에 누가 내보내는가"를 건드리지 않았다.
그런데 DX11이 아직 스왑체인을 쥐고 있고, 그것이 ImGui DX12 셸을 막고 있으며,
나아가 `Utility_Framework/DeviceResources`(DX11) 은퇴의 관문이다.

**측정한 사실:**

| | |
|---|---|
| DX11 스왑체인 | `Utility_Framework/DeviceResources`가 생성·소유, `Dx11Main.cpp:569`가 매 프레임 Present |
| DX12 라이브 렌더러 | 스왑체인 **없음** — 오프스크린 → 공유 텍스처 → ImGui::Image |
| DX12 셸 | `AttachSwapChain` 보유하나 호출자가 셸 하나뿐, 기본 꺼짐 |
| 막는 것 | DXGI가 한 HWND에 스왑체인 둘을 불허 — 켜면 DX11 것이 무효화되어 종료 크래시 |
| DX11 백버퍼 실사용 | **2곳**(`Dx11Main.cpp` 두 곳이 `g_backBufferRTV`에 대입), 그 전역의 실소비는 ImGui가 그리는 한 줄 |

즉 화면 출력 경로가 **ImGui 하나뿐**이라, 이관은 "두 렌더러가 화면을 나눠 갖는
문제"가 아니라 "ImGui를 누가 Present하느냐" 하나다.

**D1 — `IRHIDeviceResources` 추출. (완료)**
디바이스 수명·크기·프레임 경계·펜스 질의·프레젠트·진단을 백엔드 중립
인터페이스로 뺐다(`RHI/IRHIDeviceResources.h`). `DX12DeviceResources`가 구현한다.

★ **DX11을 공통 분모에 넣지 않는다.** 기존 RHI가 죽은 이유가 정확히 그것이었다
— 즉시 컨텍스트 모델로 인터페이스를 잡으니 DX12가 들어갈 자리가 없었다. 이
인터페이스는 명시적 API 둘(DX12·Vulkan)만 기준으로 잡는다. 넷이 대응한다:
BeginFrame/EndFrame ↔ 커맨드 버퍼 begin·end, 펜스 값 ↔ 타임라인 세마포어,
AttachSwapChain ↔ VkSwapchainKHR, WaitForGpu ↔ vkDeviceWaitIdle.

인터페이스에 넣지 않은 것과 이유: `Initialize`(백엔드마다 인자가 다르다 —
DX12는 어댑터 LUID·화면 추종, Vulkan은 인스턴스·확장), 디바이스/큐/커맨드
리스트 핸들(노출하면 인터페이스가 곧 DX12가 된다 — 그쪽은 `IRenderDeviceServices`
담당), 백버퍼 리소스·RTV(쓰는 것은 셸뿐이고 셸은 백엔드 전용이다).

`AttachSwapChain`의 창 핸들은 `void*`다 — 이 헤더가 플랫폼·그래픽 헤더를
끌어오지 않기 위해서다(기존 `RHIDefinitions.h`의 규약을 잇는다).

★ **D1에는 인터페이스를 통해 쓰는 소비자가 아직 없다.** 셸이 후보였으나
`GetDevice`·`GetCommandList` 같은 백엔드 고유 표면도 함께 써서 완전 전환이
안 된다(억지로 하면 캐스팅만 는다). 지금 계약을 검증하는 것은 컴파일러다
(`override`). "이 인터페이스가 Vulkan에 충분한가"는 두 번째 구현이 생겨야
답할 수 있는 질문이고, D1은 그것을 답하지 않는다 — 그 자리를 만들 뿐이다.

**D2 — 스왑체인 소유권 DX11 → DX12. (완료)**
DX11 초기화에서 스왑체인 생성을 건너뛰고(셸 모드일 때), 셸이 `AttachSwapChain`을
가져간다. 소유권은 `App::SetWindow`가 창 크기 의존 리소스를 만들기 전에
명시적으로 정한다(`SetPresentOwnedExternally`).

★ **여기서 예측이 하나 빗나갔다.** 위에 "D1의 소비자가 D2에서 생긴다"고
적었는데 생기지 않았다. 셸이 `AttachSwapChain`만 쓰는 것이 아니라
`GetDevice`·`GetCommandList` 같은 백엔드 고유 표면도 함께 써서, 인터페이스로만
잡으면 캐스팅이 늘 뿐이다. `IRHIDeviceResources`는 아직 컴파일러(`override`)
말고는 소비자가 없다 — 두 번째 백엔드나 R3의 인코더가 붙어야 생긴다.

**D3 — ImGui DX12 셸 기본 켜기. (완료)**
`EngineSetting`의 기본값을 켜짐으로 뒤집었다(2026-08-07). 실측: 셸을 켜고
590프레임 렌더 · 폴백 없음 · 종료 19단계 정상 · 검증 레이어 0건.

**D4 — `Utility_Framework/DeviceResources` 은퇴.**
스왑체인만이 아니라 DX11 디바이스를 쥐고 있어서, 그 소비자가 먼저 사라져야
한다.

★ **잔량이 줄었다(2026-08-08 재측정).** 원래 적어 둔 것은 `Texture.cpp`
23건 · **EffectSystem 약 50건**(파티클) · `RenderDebugManager`·
`TerrainMaterial` 11건이었다. 이 중 **EffectSystem과 `TerrainMaterial`이
통째로 사라졌다**(PHASE 10-0 · 11-0). 남은 것은 `Texture.cpp`의 SRV 생성,
`RenderDebugManager`, 그리고 UI·PSO·Shader 계통이다.

★ **D4가 네 부류로 갈려 대부분 끝났다(2026-08-08).** 착수하고 나서 "은퇴"가
한 덩어리가 아니라는 것이 드러났고, 소비자의 성격대로 나눴다:

| 부류 | 무엇 | 상태 |
|---|---|---|
| A·B | 화면 크기 판독을 버스로, 죽은 DX11 계통 제거(`RenderDebugManager` 포함) | 완료 |
| C | ImGui·폰트·공유 텍스처 검증의 DX11 절단 | 완료 |
| D | DX11 전역 상태 배선 제거 — 엔진 본체의 `DeviceStates` 소비 0 | 완료 |
| E | 남은 배선(`Camera.h` 상수 버퍼 선언 · `Mesh`의 `CreateBuffer` · `EditorImGuiTexture` 폴백 · UI 셋의 `g_ClientRect`) | 완료 |
| — | `DeviceResources` 자체 제거 | **남음** |

그 앞에 관문 커밋이 하나 있다(DX12를 DX11에서 떼어냄). **이 과정에서
`IRenderPass.h`가 사라졌고, 그것이 R5를 막고 있던 유일한 이유였다** — D축의
작업이 R축의 마지막 슬라이스를 열어 준 셈이다.

★ **"D4 완료"라고 적으면 안 된다(2026-08-09 실측).** 부류 A~E가 끝난 것이지
D4가 끝난 것이 아니다 — D4의 정의는 은퇴이고, 클래스는 아직 살아 있다.
**소비 14자리 · 9파일**이 남는다:

| 어디 | 무엇 |
|---|---|
| `App.cpp` · `GameApp.cpp` | `make_shared`로 만든다 — 소유자 |
| `Dx11Main.{h,cpp}` · `GameMain.{h,cpp}` | `shared_ptr`로 들고 다닌다 |
| `ImGuiRenderer.{h,cpp}` | `weak_ptr` — DX11 ImGui 백엔드의 디바이스 |
| `MenuBarWindow` · `ResourceCounterWindow` · `ConsoleCommandSystem` · `SceneManager` | `GetActive()` 질의 |

★ **남은 것의 성격이 A~E와 다르다.** A~E는 **엔진 라이브러리**에서 걷어낸
것이고, 남은 것은 **실행 파일과 에디터 셸**이다. 한 부류로 세면 "거의 다
됐다"로 읽히는데 실제로는 선행 조건이 하나 더 있다 — **ImGui가 DX11 백엔드를
완전히 놓아야 한다.** `ImGuiRenderer`가 마지막 실사용자이고, 나머지 넷은
질의(`GetActive()`)라 그 하나가 없어지면 함께 정리된다.

순서: `D1 ✔ → D2 ✔ → D3 ✔ → T6 ✔ → D4(A~E ✔ · 본체 제거 남음 — ImGui DX11 은퇴가 선행)`

### 4.3 T축 — 자산의 DX11 잔량 (2026-08-08 추가)

★ **T1만 있고 구조가 없었다.** D4(`Utility_Framework/DeviceResources` 은퇴)가
T축 뒤라고 적어 두었는데, 정작 T축이 무엇으로 이루어지는지는 어디에도 없었다.
실측해서 채운다.

**측정한 사실 — `Texture`의 DX11 표면을 밖에서 만지는 곳은 열넷이다:**

| 곳 | 건수 | 성격 |
|---|---|---|
| `EffectSystem/*` (4파일) | 7 | 파티클 — **PHASE 10**, 이 축 밖 |
| `Material::SetShaderPSO` | 5 | DX11 즉시 바인딩. 구 렌더러 은퇴로 소비자 확인 필요 |
| `DX12TextureCache` | 5 | T1의 **DX11 경유 폴백**. 실측이 `CPU 직결 8 · DX11 경유 0`이라 제거 후보 |
| `RenderPassData` | 3 | 구 DX11 그림자 경로(`CreateSRVForArraySlice`·`ClearRenderTargetView`) |
| `Terrain`·`TerrainMaterial` | 5 | 지형 |
| ImGui 셋(`ReflectionImGuiHelper`·`ImGuiDrawHelperTerrainComponent`·`UIProxyBridge` 등) | 4 | 대부분 **표시 여부 판정에만** 쓴다 |

★ **ImGui 쪽은 이미 위험하지 않다.** `EditorImGuiTexture::From`이 셸 활성
여부로 갈라 주고 있어 `ImTextureID`는 올바르게 나간다. 남은 `m_pSRV`는
`if (texture->m_pSRV)` 같은 **가드**다 — "DX11 뷰가 있나"를 묻는데 실제로
알고 싶은 것은 "보여 줄 텍스처가 있나"다. DX11 SRV 생성이 사라지는 순간
이 가드들이 통째로 거짓이 되어 **그림이 조용히 사라진다.** 질문을 바꾸는
것이 T축의 첫 일이다.

**슬라이스:**

- **T2 — 가드 질문 바꾸기. (2026-08-08, 완료)**
  `Texture::HasImage()`를 두고 `if (texture->m_pSRV)` 가드 둘을 바꿨다
  (`ReflectionImGuiHelper` 인스펙터 썸네일, `ImGuiDrawHelperTerrainComponent`
  레이어 미리보기).

  ★ **위에서 "넷"이라고 적었으나 실제 가드는 둘이었다.** 나머지 둘
  (`UIRenderProxy`의 `SpriteBatch::Draw`, `UIProxyBridge`의 스프라이트시트
  로드)은 가드가 아니라 DX11 실사용이라 성격이 다르다 — T5/T6에서 다룬다.

  ★ **크기 출처가 둘이라 술어가 둘을 다 본다.** 파일에서 읽은 텍스처는
  `m_size`만 채우고(로더 셋이 메타데이터에서 넣는다), 코드가 만든
  렌더 타깃·깊이는 `m_desc`만 채운다. `GetWidth()`가 `m_desc`만 보는 탓에
  파일 텍스처에서 0을 돌려주는 어긋남이 이미 있는데, 그것은 T2 범위가
  아니라 `HasImage`가 흡수했다.

  ★ **새 가드가 옛 가드보다 좁아지지 않음을 확인했다.** 로더 셋 모두
  SRV 생성(`ThrowIfFailed`)과 `m_size` 대입이 같은 함수에 조건 없이
  나란히 있다. 즉 `m_pSRV != nullptr`이면 반드시 `HasImage()`가 참이다.
- **T3 — 죽은 DX11 경로 확인·제거. (2026-08-08, 완료)**

  호출자를 세어 보니 예상보다 넓었다. 지운 것과 근거:

  | 지운 것 | 근거 |
  |---|---|
  | `Material::SetShaderPSO`의 SRV 바인딩 5 | `ShaderPSO::Apply`가 소비하는데 그 **Apply를 부르는 코드가 0곳** |
  | `Material::ApplyMaterialInfo` | 빈 함수, 호출자 0 |
  | `RenderPassData`의 그림자 맵 일습 | 카메라마다 R32_TYPELESS 3장 + 슬라이스 SRV 3 + DSV 3을 만들고 소멸자에서 놓는 것이 전부 — **읽는 코드 0** |
  | `RenderPassData::m_SSRPrevTexture` | 화면 크기 RGBA16F. 셰이더가 읽는 유일한 줄이 주석 처리돼 처음부터 닫힌 고리였다 |
  | `RenderPassData::m_ViewBuffer`·`m_ProjBuffer` + `BindFrameCameraBuffers` | 그 함수 하나만 썼고 함수 호출자가 0. §1.1이 이미 적어 둔 구 RHI의 마지막 표면이다 |
  | `RenderPassData::ClearRenderTarget` | 호출자 0 |
  | `DirectX11::CreateSRVForArraySlice` | 유일한 호출자가 위 슬라이스 SRV였다 |

  ★ **남긴 것:** `m_renderTarget`·`m_depthStencil`은 EffectSystem 셋이
  `OMSetRenderTargets`에 그대로 건다 — PHASE 10과 함께 간다.

  ★ **딸려 나온 발견:** `ShaderPSO`의 DX11 즉시 바인딩 기구
  (`Apply`·`BindShaderResource`·`BindUnorderedAccess`·`ResolveSrvUavHazards`·
  `m_shaderResources`·`m_unorderedAccessViews`)가 통째로 소비자를 잃었다.
  `Apply`를 부르는 코드가 어디에도 없다. Texture의 DX11 표면과는 무관해
  이 축에서 지우지 않았다 — ShaderSystem 정리로 따로 다룬다.
- **T4 — DX12TextureCache의 DX11 폴백 제거. (2026-08-08, 완료)**

  실측(`CPU 직결 8 · DX11 경유 0`)만으로는 "그 씬에서 안 돌았다"까지밖에
  못 말하므로, **소비자를 전부 세어 정적으로 확정했다.** `GetOrUpload`를
  부르는 곳은 일곱이고 전부 `Texture::LoadManagedFromPath` 계열이 만든
  텍스처를 넘긴다:

  | 호출부 | 텍스처 출처 |
  |---|---|
  | GBuffer · Forward+ | 재질 넷(base·normal·ORM·emissive) |
  | Decal · GizmoIcon · UI | 자산 텍스처 |
  | Live(2곳) | `blueNoise.dds` · 스카이박스 equirect |

  로더 셋 모두 `m_cpuPixels`를 채우므로 폴백은 **도달 불가**였다.

  걷은 것: `UploadFromDX11`(176줄) · `m_dx11Device`/`m_dx11Context` ·
  `Initialize`의 DX11 인자 둘(호출부 11곳) · `Stats::fromDX11` ·
  `.cpp`의 `#include <d3d11.h>`.

  ★ **폴백을 남기지 않는 이유가 '깔끔함'이 아니다.** 도달할 수 없는 경로는
  죽었는지 살았는지 알 수 없고, 무엇보다 **지형(T5)이 DX11로 만든 배열
  텍스처를 그대로 밀어 넣는 손쉬운 길**이 되어 T6을 막는다. 지금은 CPU
  픽셀이 없는 텍스처가 오면 흰색 + `failures` 증가이고, 그 수가 곧 T5·T6의
  남은 양이다.

  미검증: 빌드·런타임(사용자 신호 대기). 유니티 빌드에서 `d3d11.h` 전이
  include가 재배치될 수 있다 — 이 프로젝트에서 네 번 겪은 부류다.
- **T5 — 지형. (2026-08-08, 범위 정정 후 1차 완료)**

  ★ **착수하고 나서 이 항목이 두 가지를 섞고 있다는 것이 드러났다.**
  "`TerrainMaterial`이 DX11 디바이스를 쥔다"만 적어 두었는데, 실제로 세어
  보니 **DX12에는 지형 렌더 경로 자체가 없다**(3-6 노트에도 기록). 즉
  `TerrainMaterial`의 DX11 자원은 *렌더*가 아니라 *편집*(스플랫 페인팅·
  레이어 배열 생성)을 위해 살아 있고, 그것을 걷으려면 DX12 지형 패스가
  먼저 있어야 한다. 순서가 뒤집혀 있었다.

  그래서 이 축에서 지금 할 수 있는 것 — **죽은 DX11 드로우 경로 제거** —
  만 했다:

  | 지운 것 | 근거 |
  |---|---|
  | `PrimitiveRenderProxy::Draw`·`DrawShadow`·`DrawInstanced` | 호출자 0. DX11 렌더러 은퇴로 부르던 쪽이 사라졌고 DX12는 프록시를 그리지 않는다(`EnhancedDrawItem`으로 복사해 간다) |
  | `InitializeLODs`·`GetLODLevel`·`m_currLOD` | 호출자 0 / 독자 0 |
  | `TerrainMesh::Draw()` ×2 | 유일한 호출자가 위 `Draw`의 지형 분기였다 |
  | `Mesh`의 DX11 드로우 8종 | 유일한 소비자가 위 셋이었다 |

  **남긴 것과 이유:**

  - `m_EnableLOD`·`SetLODEnabled` — 게임 쪽(`ProxyCommand`)이 여전히 저작
    값으로 설정한다. 의도가 살아 있고 소비자는 DX12가 LOD를 붙일 때 생긴다.
  - `Mesh`·`TerrainMesh`의 정점·인덱스 버퍼 — EffectSystem의 `MeshModuleGPU`가
    `GetVertexBuffer`/`GetIndexBuffer`로 쓴다(PHASE 10). `TerrainMesh` 쪽은
    조각(sculpt) 결과를 받는 자리이기도 하다.
  - `TerrainMaterial` 전부 — 편집 기능이 여기 걸려 있다.

  ★ **DX12 지형 패스가 생기기 전에 풀어야 할 것 하나를 찾았다.**
  `TerrainMesh::UpdateVertexBufferPatch`가 **DX11 버퍼만 갱신하고
  `m_vertices`(CPU 배열)는 그대로 둔다.** DX12는 CPU 배열에서 업로드하므로
  (`DX12MeshCache`의 규약) 지금 구조로는 조각 결과가 화면에 반영되지 않는다.
  즉 지형 패스를 쓰기 전에 조각 경로가 CPU 배열을 진실로 삼도록 바꿔야 한다.

  **남은 것 = 새 항목:** DX12 지형 렌더 경로 신설(3-6급). 그 뒤에야
  `TerrainMaterial`의 DX11을 옮길 수 있다.

  ★ **2차 완료 — 순서가 다시 뒤집혔다(2026-08-08, 사용자 판단).**
  위에 "지형 패스가 먼저 있어야 DX11을 걷을 수 있다"고 적었는데, 세어 보니
  **틀린 전제였다.** `TerrainMaterial`의 GPU 자원을 읽는 코드가 하나도 없다
  (`GetSplatMapSRV`·`GetLayerSRV`·`GetLayerBuffer`의 호출자 0곳). 편집이
  그것을 *쓰기만* 하고 아무도 읽지 않았으므로, 걷어내도 잃는 기능이 없다.
  "편집 기능이 사라진다"는 걱정이 실측 앞에서 사라졌다.

  걷은 것: `TerrainMaterial`의 DX11 일습(스플랫맵 배열·레이어 Albedo 배열·
  UAV·SRV·상수 버퍼 둘·DX11 컴퓨트 셰이더), `TerrainMesh`의 정점/인덱스
  버퍼와 Map 경로, 브러시 마스크의 텍스처·SRV, 셰이더 둘, 지형·폴리지 에셋.

  ★ **그러면서 위 선결 결함이 함께 풀렸다.** `UpdateVertexBufferPatch`가
  이제 CPU 배열에 쓴다 — 쓸 곳이 거기밖에 없어서다. 스플랫 마스크도 같은
  형태로 CPU가 진실이고, 양쪽에 `m_revision`을 두어 지형 패스가 "올린 것이
  최신인가"를 판정할 수 있게 했다.

  ★ **잃은 것 하나:** 브러시 마스크 썸네일(인스펙터가 이미지 버튼에서 이름
  버튼이 됐다). CPU 마스크와 브러시 동작은 그대로다. PHASE 11-5에서 갚는다.
- **T6 — `Texture`에서 DX11 멤버 제거. (2026-08-08, 완료)**

  ★ **전제가 거의 다 풀린 뒤에 닫혔다.** 외부 소비처가 **열넷에서 셋으로**
  줄어 있었다 — 이펙트 계통 통째 제거(PHASE 10-0)가 일곱을, 지형 GPU
  제거(T5 2차 = PHASE 11-0)가 다섯을 가져갔다. 남은 전제는 UI 스프라이트
  경로(DirectXTK `SpriteBatch`) 하나였고, 그것이 DX12로 가면서 함께 닫혔다.

  **T축은 여기서 끝난다.** T1~T6 전부 완료이고 남은 항목이 없다.

**R6(선택) — 기록 검증용 가짜 백엔드.** §2.1 ②. 인코더 호출을 기록만 하는
구현으로 패스의 명령 순서를 픽셀 없이 단정한다. 여기까지 오면 두 번째
백엔드를 붙이는 비용도 드러난다. **R2c(리드백)가 선행이다** — 지금 자가
검증이 리드백으로 픽셀을 읽으므로, 그 경로에 인터페이스가 없으면 가짜
백엔드로 갈아탈 자리가 없다.

**R7 — 파이프라인·루트 시그니처 기술의 중립화 (2026-08-08 신설).**
패스 17종이 `D3DCompile`로 HLSL을 컴파일하고 `D3D12_ROOT_PARAMETER` ·
`D3D12_DESCRIPTOR_RANGE` · `D3D12_ROOT_SIGNATURE_DESC` · `D3D12_STATIC_SAMPLER` ·
`D3D12_INPUT_ELEMENT_DESC`를 손으로 채운다 — **184건.** `DX12GraphicsPipelineDesc`도
`D3D12_FILL_MODE` · `CULL_MODE` · `COMPARISON_FUNC` · `RENDER_TARGET_BLEND_DESC`를
그대로 노출하고 패스가 그것을 채운다.

★ **이 슬라이스가 없으면 §6.2를 만족할 수 없다.** 그리고 §2는 "패스 셰이더
재작성 없음, 루트 시그니처 레이아웃은 그대로다"로 이것을 범위 밖에 두었다.
두 문장이 양립하지 않는다 — §6을 함께 고친다(아래).

R2·R3와 성격이 다르다. 저쪽은 *반복되는 손코드*를 없애는 것이라 줄어드는
줄 수가 곧 이득이지만, 이쪽은 레이아웃 기술을 다른 어휘로 옮겨 적는 것이라
**줄 수는 그대로이고 이득은 백엔드 독립성 하나다.** §2.1이 세운 동기 셋 중
①(실수 자리 제거)·②(테스트 가능)에 거의 기여하지 않는다. 그래서 선택
슬라이스로 두고 두 번째 백엔드가 실제로 시작될 때 착수한다.

### 4.1 규모 예상

R0은 이동뿐(반나절). R1은 서명 교체와 전파(1~2일). R2·R3가 본체로 패스
17종을 훑는다(각 3~4일). R4는 넓지만 기계적(2일). R5는 삭제(반나절).

**슬라이스 순서 (2026-08-10 재편 — Vulkan 확정):**

```
R1 ✔ → R2a ✔ → R2b ✔ → R2c ✔ → R3 ✔ → R4-1·4-2 ✔ → R5 ✔
                                          │
                                          └→ §7 V1 → V2(R4-3 흡수) → V3 → V4
                                                → [Vulkan 골격] → V5 → V6 → V7 → V8
```

★ R4-3와 R7은 독립 슬라이스가 아니게 됐다 — 전자는 V2에, 후자는 V4·V6에
흡수된다. 근거는 §7.1의 실측이다(포인터로 한 번, Vulkan에서 또 한 번 바꾸는
것을 피한다).

★ **이 문서가 한동안 코드보다 뒤처져 있었다(2026-08-09 정정).** R3-1까지
적어 두고 그 뒤 서른 커밋이 문서 없이 나갔다 — R3-2(패스 17종 인코더 이관
완료) · R4-1 a·b·c(렌더 타깃·힙 바인딩을 인코더로) · R4-2 a·b(커맨드 리스트
별칭 0, 타일 버퍼·SSGI 히스토리를 그래프로) · T6 완료 · D4 부류 A~D · M1.
아래 항목 중 "예정"으로 읽히는 것이 있으면 그 시점의 글이다.

**R4의 남은 것:** §3.3 표의 서명 교체 자체다. `EnhancedRenderGraph.h`가 아직
`ImportTexture(ID3D12Resource*)` · `Compile(ID3D12Device*)` ·
`Execute(ID3D12GraphicsCommandList*)` · `ExecuteContext::commandList`를 든다.
`EnhancedRenderPass.h`는 이미 DX12 참조 0이다.

**R5는 끝났다(2026-08-09).** 예측대로 비용이 없었다 — 구 RHI 574줄의 소비자가
죽은 include 셋뿐이었다(`Dx11Main.cpp` 둘 · `RenderPassData.cpp` 하나, 심볼
사용 0). §4.1 R5 항목 참고.

R2b·R2c가 R2a에서 갈라져 나온 이유는 같다 — R2a는 *셰이더 가시 디스크립터 링*
하나만 다뤘고, RTV/DSV는 힙이 다르며(R2b), 리소스 생성과 리드백은 링이
아니라 디바이스다(R2c). 셋을 한 슬라이스로 묶으면 A/B 판정 단위가 너무
커진다.

★ R2·R3는 패스를 하나씩 옮길 수 있다 — 인코더와 원시 커맨드 리스트를 한
프레임에 섞어 쓸 수 있으므로(같은 리스트에 기록), 패스 단위로 A/B하며
진행한다. 이것이 이 계획의 안전장치다.

## 5. 위험

- ~~**추상화가 성능을 먹는다**~~ — **재 봤고, 먹지 않는다(2026-08-08 해소).**

  `dx12.encoderbench`(Release, 중앙값 9회, 순서 회전). 같은 기록을 세 경로로
  돌리고 CPU 기록 시간만 잰다:

  | 드로우 | 직접 | 가상 | 인라인 | 가상−직접 | 호출당 |
  |---|---|---|---|---|---|
  | 256 | 0.013 | 0.014 | 0.013 | +0.001 ms | +0.86 ns |
  | 1,024 | 0.046 | 0.050 | 0.046 | +0.004 ms | +0.88 ns |
  | 4,096 | 0.178 | 0.195 | 0.177 | +0.017 ms | +0.84 ns |
  | 16,384 | 0.706 | 0.783 | 0.705 | +0.077 ms | +0.94 ns |

  가상 dispatch는 **호출당 0.9ns 안팎**이고 드로우 수를 64배 늘려도 그 값이
  유지된다 — 잡음이 아니라 실제 per-call 비용이라는 뜻이다. 드로우 16,384개
  (호출 81,920)에서도 +0.077ms이고, 현실적인 1,000 드로우면 +0.004ms다.
  `dx12.live status`의 CPU 2~7ms에 견주면 0.1% 수준이다.

  ★ **그래서 R3는 드로우 루프 안쪽을 비가상으로 특별 취급하지 않는다.**
  계획이 미리 걱정해 둔 복잡도를 하나 덜었다 — 재지 않았으면 필요 없는
  최적화를 인터페이스에 새겨 넣었을 것이다.

  ★ **인라인 경로가 직접과 같은 수를 낸다**(0.013/0.013 · 0.706/0.705).
  이론이 그렇게 말하고 측정도 그렇다. 나중에 정말 필요해지면 비가상 래퍼가
  공짜라는 것도 이 표가 보증한다 — 탈출구는 있되 지금 쓸 이유가 없다.

  ★ **이 표를 얻기까지 방법론 결함 둘을 밟았고, 둘 다 측정이 알려 줬다:**

  ① 처음에는 세 경로를 늘 같은 순서로 돌려 첫 번째가 워밍업을 뒤집어썼다.
     그 판은 "인라인이 직접보다 13% 빠르다"는 **말이 안 되는 수**를 냈고 —
     둘은 같은 코드로 컴파일된다 — 그 이상이 결함을 알려 줬다. 순서를
     회전시키자 둘이 일치했다. 못 봤으면 가상−직접도 과소평가된 채였다.

  ② 루트 시그니처를 걸지 않고 `SetGraphicsRoot*`를 불러 Release에서 죽었다.
     Debug에서는 검증 레이어가 경고만 남기고 넘어가는 자리다. 이 벤치가
     Release 전용인 이유와 같은 뿌리다.

  ★ **Debug로 재면 안 된다.** `commandList->X()` 한 번이 검증 레이어를 지나
  vtable 한 번을 통째로 덮는다 — "가상 호출은 공짜"라는 거짓 음성이 나오고,
  그 답을 믿으면 Release에서 비싼 것을 지나친다.
- **작업 중 회귀가 조용히 쌓인다** — 슬라이스마다 패스 목록 문자 일치와 픽셀
  대조를 요구한다. 패스 단위 진행이므로 어느 패스에서 깨졌는지도 좁혀진다.
- **범위가 번진다** — 셰이더·루트 시그니처 레이아웃·PSO 캐시 구조는 이번
  범위 밖이다. 그것들은 이미 DX12 개념이되 검증이 끝난 자산이고, 백엔드
  구현 안에 남는 것이 옳다.
- ~~**PHASE 10과 겹친다**~~ — **해소됐다(2026-08-09).** 파티클·이펙트가 DX11
  `IRenderPass`에 있다는 것이 이 위험의 내용이었는데, PHASE 10-0이 그 계통을
  통째로 걷었고 `IRenderPass.h`는 D4에서 사라졌다. R5가 피해 갈 층이 없다.

## 6. 완료 기준 (2026-08-10 이후 §7.4가 대체)

★ **아래는 백엔드가 하나일 때의 기준이다.** Vulkan 확정으로 §7.4가 이것을
대체한다 — 특히 2번의 "`Initialize` 제외"가 무효가 됐다. 기록으로 남긴다.

★ **2번을 고쳤다(2026-08-08).** 원래 "패스 17종에 `ID3D12`·`D3D12_` 직접
참조 0건"이었는데, §2가 "패스 셰이더 재작성 없음 · 루트 시그니처 레이아웃은
그대로다"로 범위를 자른 것과 양립하지 않았다. 남는 184건(`D3DCompile` ·
루트 파라미터 · 디스크립터 레인지 · 정적 샘플러 · 입력 레이아웃)이 전부
그 절단 안쪽에 있으면서 동시에 이 기준의 위반이다. 기준을 범위에 맞춰
좁히고, 잘라낸 부분은 R7의 완료 기준으로 옮긴다.

1. `RenderEngine/Render/` 아래 어떤 파일도 `d3d12.h`를 include하지 않는다.
2. 패스 17종의 **`PrepareFrame`·`Declare`·`Record` 본문**에 `ID3D12`·`D3D12_`
   직접 참조 0건(grep으로 검증). `Initialize`의 파이프라인·루트 시그니처
   기술은 §2가 범위 밖으로 둔 것이므로 제외하고, 그쪽은 R7의 기준이다
   — **R7 완료 시 조건: `Initialize`까지 포함해 0건.**
3. `dx12.live status`의 패스 이름 목록이 착수 전과 문자 그대로 같다.
4. 자가 검증 전체 통과, D3D12 검증 레이어 메시지 0건.
5. `dx12.live status`의 CPU ms가 착수 전 대비 유의미하게 늘지 않는다(실측 기록).
6. 구 RHI(`RHI.h` · `RHIDevice.h` · `RHICommandContext.h` · `DX11RHI.*`) 제거.

### 6.1 현재 위치 (2026-08-09 실측)

| # | 기준 | 상태 |
|---|---|---|
| 1 | `Render/` 폴더에 `d3d12.h` 0건 | **미착수** — 폴더 자체가 없다(R0). 다만 `EnhancedRenderPass.h`는 이미 DX12 참조 0이라 옮길 대상의 절반은 준비됐다 |
| 2 | 패스 본문에 `ID3D12`·`D3D12_` 0건 | **거의** — 남은 것은 `Declare`의 `ID3D12Resource*`(그래프 `Resolve`의 반환 타입이라 R4-3이 가져간다)와 VolFog `PrepareFrame` 7건. `CreatePipelines`/`Initialize` 쪽은 §2가 범위 밖으로 둔 것이고 R7의 기준이다 |
| 3 | 패스 이름 목록 문자 일치 | **충족** — 슬라이스마다 확인해 왔다 |
| 4 | 자가 검증 전체 통과 · 검증 레이어 0건 | **거의** — 워밍업을 갖춘 전수 스윕(2026-08-10) **33종 중 28 통과 · 4 계측 · 1 실패**. 남은 하나(`dx12.scene`)는 그 검사가 명시한 전제조건이고 원인이 리소스 부재다(§6.2). stderr 33종 모두 0바이트 · 종료 코드 전부 0 |
| 5 | CPU ms 회귀 없음 | 미측정(슬라이스 판정은 판정 줄 대조로 해 왔다) |
| 6 | 구 RHI 제거 | **충족** — R5 완료(일곱 파일). §4 R5 참고 |

### 6.2 "실패 둘"의 정체 — 검사가 아니라 부르는 방법이었다

★ **`dx12.gizmoscene`을 결함으로 적었는데 틀렸다.** 이 검사는 `RenderPassData`의
프레임 카메라 스냅샷을 읽고, 그 게시자는 라이브 렌더러의 프레임 경계다(3-2).
`--exec dx12.gizmoscene`을 기동 직후 부르면 프레임이 한 번도 안 돌아 스냅샷이
영행렬이고, 그래서 **기하는 만들어지는데(라인 정점 38) 점등이 0**이 된다.
`wait`를 앞에 두어 프레임을 돌리고 부르면 **점등 512로 통과한다.**

착수 전 커밋에서 출력이 글자까지 같았던 것도 같은 이유다 — 코드가 같았던 게
아니라 **호출 방식이 같았다.** "선재 문제"라는 결론은 맞았지만 그 내용이 틀렸다.

`dx12.scene`은 씬을 로드해도 드로우가 0이다. 씬 파일(`DX12Validation.creator`)에는
`MeshRenderer`가 12개 있는데 메시가 이름(`Cube`)으로만 적혀 있고, **그 이름을
만들어 줄 코드도 에셋도 리포에 없다** — 즉 씬이 아니라 그 씬이 참조하는 리소스가
없다. 검사가 스스로 적은 전제("씬에 메시를 배치한 뒤 다시 실행할 것")가 정확하다.

★ **고치고 다시 쟀다(2026-08-10).** 스윕 스크립트가 검사마다 `wait 60`을 앞에
두고 부르도록 바꾸니 `dx12.gizmoscene`이 **점등 6600으로 통과**한다. 전수 결과가
**28 통과 · 4 계측 · 1 실패**가 됐고, stderr 33종 0바이트 · 종료 코드 전부 0이다.
남은 실패 하나는 `dx12.scene`이고 그것은 씬 리소스 문제다.

★ **배운 것:** 검사가 *무엇을 전제하는가*를 실행 방법이 만족시키는지 확인하지
않으면, 통과 못 한 것을 결함으로 잘못 적게 된다. R2b가 "검증 목록이 검증
대상보다 낡으면 통과가 아무것도 뜻하지 않는다"고 적어 둔 것과 같은 부류이고,
이번에 빠져 있던 것은 목록이 아니라 **실행 전제**다. 전수 스윕 스크립트는
검사 앞에 프레임 워밍업을 두어야 한다.

★ **이 판단이 2026-08-10에 뒤집혔다.** "설계 몫은 R4-3에서 끝난다"는 §6.2가
`Initialize`를 범위 밖으로 잘라낸 것을 전제로 한 문장이고, 그 절단이 Vulkan
확정으로 무효가 됐다. 실제로 남은 것은 §7이 정의한다 — R4-3는 그 안의 한
조각이다.

---

## 7. Vulkan 전제 재설계 (2026-08-10)

Vulkan 도입이 예정으로 확정됐다. 이 계획서는 지금까지 **"DX12를 가리는 것"**을
기준으로 잘라 왔는데, 목적지가 둘이면 기준이 달라진다 — **"두 API가 같은
어휘로 들어오는가"**다. 그래서 범위와 순서를 다시 잡는다.

### 7.1 전체 표면 — 어디에 몰려 있나 (실측)

패스 17종 · 경계 헤더 넷 · 그래프 구현을 범주별로 세면 **742건**이다:

| 덩어리 | 건수 | 지금까지의 분류 | Vulkan에서 무엇이 되나 |
|---|---|---|---|
| 루트 시그니처 기술 | 160 | R7(선택) | 파이프라인 레이아웃 + 디스크립터 셋 레이아웃 |
| 포맷 `DXGI_FORMAT` | 145 | **어디에도 없었다** | `VkFormat` |
| 셰이더 컴파일 `D3DCompile`·`ID3DBlob` | ~~93~~ → **66** | §2가 "범위 밖" | DXC → SPIR-V |
| 리소스 핸들 `ID3D12Resource*` | 90 | R4-3 + 핸들화 | `VkImage`/`VkBuffer` + 메모리 |
| 샘플러 | 77 | R3가 "별개"라 미룸 | `VkSampler` (불변 샘플러 or 셋) |
| 파이프라인·루트시그 객체 | 67 | R7(선택) | `VkPipeline`/`VkPipelineLayout` |
| 상태·배리어 | 45 | R4 | `VkImageLayout` + 파이프라인 배리어 |
| 래스터·블렌드·뎁스 | 30 | R7(선택) | 파이프라인 생성 정보 구조체들 |
| 나머지(입력 레이아웃·주소·뷰) | 35 | — | 정점 입력 기술 등 |

★ **읽어야 할 것: 필수로 분류돼 있던 R4-3는 90건짜리이고, "선택"으로 밀어 둔
것들이 443건이다.** 즉 지금 계획서의 필수/선택 구분은 백엔드가 하나일 때의
것이고, 둘이 되는 순간 뒤집힌다.

★ **포맷 145건이 어느 슬라이스에도 없었다는 것이 이 재조사의 최대 수확이다.**
`RHIFormat`은 §3.2 스케치에 열거만 있고 실제로 도입된 적이 없다 —
`RHIBindingDesc`·`RHIReadback`·`RHITextureDesc`가 전부 `DXGI_FORMAT`을 그대로
들고 다닌다. 다른 모든 슬라이스가 포맷을 인자로 받으므로 이것이 선행이다.

### 7.2 슬라이스와 순서

의존 관계가 순서를 정한다. 각 슬라이스는 지금까지와 같이 A/B 가능해야 하고
판정은 자가 검증 **35종**의 판정 줄 대조다(2026-08-11 실측 —
`ConsoleCommandSystem.cpp` 의 `dx12.*` 명령 수).

| # | 슬라이스 | 범위 | 왜 이 자리인가 |
|---|---|---|---|
| **V1** | 포맷 중립화 — `RHIFormat` | 145 | 나머지 전부가 포맷을 인자로 받는다. 가장 먼저여야 하고, 기계적이라 위험이 낮다 |
| **V2** | 리소스 핸들 — `RHITextureHandle`·`RHIBufferHandle` | 90 | R4-3(그래프 서명)를 흡수한다. `Resolve()`가 핸들을 돌려주면 `Declare` 본문의 33건이 함께 사라진다 |
| **V3** | 상태·배리어 중립화 — `RGResourceState`를 RHI로 | 45 | §3.3이 이미 예고한 이동. V2 뒤인 것은 상태가 핸들에 붙기 때문 |
| **V4** | 바인딩 레이아웃 — 루트 시그니처 → `RHIPipelineLayout` | 227 | 최대 덩어리. 디스크립터 셋 모델이 핸들(V2)과 상태(V3)를 전제한다 |
| **V5** | 셰이더 컴파일 — `RHIShaderCompiler` | ~~93~~ → **66** | 소스 파일화·컴파일 일원화는 §7.2.3 에서 끝났다. 남은 것은 `ID3DBlob` 이 패스를 타고 다니는 것. 리플렉션 몫은 **사라졌다**(폐기) |
| **V6** | 파이프라인 상태 기술 — 래스터·블렌드·뎁스·샘플러 | 107 | 나머지 기술. 여기까지 오면 패스에 `D3D12_`가 남지 않는다 ✔ 완료 §7.2.4 |
| **V7** | R0 — 상위 개념을 `RenderEngine/Render/`로 | 헤더 21 | 이동. 여기서 §6-1이 검증 가능해진다(`Render/`에 `d3d12.h` 0). ★ **순수 이동이 아니다** — 옮길 헤더 셋이 객체 핸들로 `d3d12.h`를 물고 있어(50건) 핸들화가 선행이고, 그 핸들화는 V8이 모양을 보여 줘야 한다(§7.2.4) |
| **V8** | Vulkan 백엔드 골격 | — | **여기서 처음으로 계약이 맞았는지 증명된다.** 골격 ✔ §7.2.2 · 삼각형이 패스 경로를 탄다 ✔ §7.2.5(V8-a). 남은 것은 그래프 |

★ **V8을 맨 뒤에 두는 것이 이 계획의 가장 큰 위험이다.** 구 RHI가 죽은 이유가
정확히 "소비자 없는 추상"이었고(§1.1), V1~V7은 전부 소비자가 하나(DX12)뿐인
상태로 쌓인다. 그래서 **V4 직후에 Vulkan 골격을 얇게 세워 계약을 때려 보는
것을 기본으로 한다** — 디바이스·스왑체인·삼각형 하나면 충분하다. V4까지가
"두 API가 갈리는 부분"의 대부분이므로, 거기서 어긋나면 V5·V6를 헛짓으로
쌓지 않는다.

  (도입 시점이 한참 뒤라면 V7까지 밀고 세워도 된다. 그 경우 V5·V6가 검증 없이
  쌓이는 것을 감수하는 것이고, 이 문단이 그 대가를 미리 적어 둔 것이다.)

★ **V5·V6는 MaterialPipelinePlan(PHASE 3.5)이 승계한다(2026-08-11).** 골격까지
서고 나서 다음 슬라이스를 재는 자리에서, V5를 "FXC 호출을 DXC 호출로 옮겨
적기"로 자르면 소비자가 하나뿐인 추상이 된다는 §1.1의 사인이 다시 보였다 —
컴파일 인터페이스는 그 진짜 소비자(머테리얼 · 퍼뮤테이션 파이프라인)와 함께
잘려야 한다. 대응은 M1=V5 · M3=V6이고, 실측(셰이더 컴파일 시스템이 둘이며
화면에 나오는 쪽은 애셋 파이프라인이 아니라는 것)과 설계는 그 문서에 있다.

### 7.2.1 V2 설계 — 리소스 핸들 (착수 전 기록)

V 중 설계 판단이 가장 무거운 자리라 착수 전에 적어 둔다. 실측한 현재 형태:

| 어디 | 지금 |
|---|---|
| 경계 서명 | `ID3D12Resource*` 31곳(`RHIBindingDesc` · 렌더 타깃 · 깊이 · 리드백 복사) |
| 그래프 | `Resolve() → ID3D12Resource*` · 소비 113곳 · 내부는 `external` 포인터와 `owned` ComPtr 둘로 보관 |
| 패스 소유 | `ComPtr<ID3D12Resource>` 10개(Forward 타일 · SSGI 히스토리 · VolFog 볼륨) |
| 생성 | `CreateBuffer`/`CreateTexture`가 `ComPtr&`로 돌려준다 |

**정한 것 넷:**

**① 핸들은 불투명 정수다** — `struct RHITextureHandle { uint32_t id; }`, 0이 무효.
포인터가 아닌 이유는 §3.2가 적어 둔 그대로다(백엔드가 재배치·풀링해도 상위가
모르고, 값 복사가 싸며, 잘못된 포인터를 역참조할 길이 없다). Vulkan에서는
`VkImage` + `VkDeviceMemory` + `VkImageView`가 한 덩어리라 포인터 하나로 못
가리킨다 — 핸들이 그 셋을 묶는 자리가 된다.

**② 표는 디바이스 서비스가 든다.** 패스도 그래프도 아니다. 그래프는 프레임마다
새로 서고 패스는 여럿인데, 핸들의 유효 범위는 그보다 길다(패스 소유 리소스는
프레임을 넘긴다). 등록은 두 경로뿐이다 — `CreateTexture`/`CreateBuffer`가
만들면서 등록하고, 그래프가 transient를 만들면서 등록한다.

**③ 수명은 지금 규약을 그대로 옮긴다.** ComPtr이 하던 일(참조 세기)을 표가
대신하되 규칙을 바꾸지 않는다: 패스 소유는 `Shutdown`까지, transient는 그래프
수명까지(인플라이트 보관 포함), 임포트는 소유하지 않는다. **수명 정책을 이
슬라이스에서 함께 바꾸지 않는 것이 중요하다** — 표현을 바꾸는 것과 정책을
바꾸는 것을 겹치면 회귀가 났을 때 어느 쪽인지 못 가른다.

**④ `Resolve()`는 핸들을 돌려준다.** 소비 113곳이 그대로 살아 있되 타입만
바뀐다. 실제 리소스는 백엔드만 본다.

**쪼갠다(넷):**

| | 무엇 | 판정 |
|---|---|---|
| V2-a | 핸들 타입 + 표(등록·조회) + `CreateTexture`/`CreateBuffer`가 핸들을 돌려준다 | 패스 소유 10개가 핸들로 바뀌고 자가 검증 33종 판정 불변 |
| V2-b | 경계 desc 31곳(`RHIBindingDesc` · 렌더 타깃 · 깊이 · 리드백)이 핸들을 받는다 | 〃 (V1이 남긴 `ToDXGI` 86곳 중 대부분이 여기서 사라진다) |
| V2-c | 그래프 — `ImportTexture`/`Resolve`가 핸들, 내부 보관도 핸들 | 〃 |
| V2-a ✔ | 완료 (2026-08-10) | 패스 소유 7개 · 헤더의 `ComPtr<ID3D12Resource>` 0 |
| V2-c ✔ | 완료 (2026-08-10, c1/c2로 갈림) | 자가 검증 20종 판정 불변 |
| V2-b ✔ | 완료 (2026-08-10) | 36파일 · 자가 검증 20종 바이트 동일 |
| V2-d ✔ | 완료 (2026-08-10) | `ExecuteContext::commandList` 38 → **0** |
| V3 ✔ | 완료 (2026-08-10) | 패스 쪽 손 배리어 20 → **0** |
| V4 ✔ | 완료 (2026-08-10) | 레이아웃·샘플러 DX12 심볼 379 → **18** |
| 골격 ✔ | 완료 (2026-08-11) | `vk.selftest` 통과 · `IRHIDeviceResources` 무수정 구현 |
| V2-d | `ExecuteContext::commandList` 제거(R4-3 잔여) | 〃 |

★ **순서를 뒤집는다(2026-08-10, V2-a 직후 실측).** 위 표는 b(경계 desc) → c(그래프)
로 적었는데 **의존이 반대 방향이다.** desc 팩토리의 `resource` 인자를 무엇이
채우는지 세니 **30곳이 `executeContext.Resolve(...)`**이고 패스 소유 핸들로
채우는 곳은 둘뿐이다 — 즉 desc가 핸들을 받으려면 **그래프가 먼저 핸들을
돌려줘야 한다.** 순서대로 하면 같은 팩토리에 포인터와 핸들을 함께 받는
과도기 오버로드가 생기고, 그 오버로드는 남는다.

바뀐 순서: **V2-a ✔ → V2-c(그래프) → V2-b(경계 desc) → V2-d(ExecuteContext).**

★ 이 부류의 실수가 이 계획서에서 반복된다 — R3 결산이 "호출의 종류만 세고 그
호출이 어느 실행 문맥에 있는지를 안 봤다"고 적어 둔 것과 같다. 이번에는 종류
(desc 31개)는 셌지만 **인자가 어디서 오는지**를 안 봤다. 세는 단위가 접촉면일
때는 늘 "누가 이것을 채우는가"를 함께 물어야 한다.

#### V2-c 완료 결과 (2026-08-10)

핸들이 표만으로는 안 됐다. **그래프가 프레임·뷰마다 새로 서기 때문이다**
(`EnhancedSceneRendererLive.cpp:1836`이 매 프레임 `make_unique`). 그래프
리소스를 표에 넣는 순간 표가 60fps로 자라는데, `DX12ResourceTable`은
`Clear` 호출자가 0인 채 한 번도 안 비워지고 있었다. 그래서 슬라이스가
둘로 갈렸다.

| | 한 일 | 결과 |
|---|---|---|
| V2-c1 | 표에 `Release` + 빈칸 재사용 + **세대** | 동작 불변(호출자 0), 자가 검증 6종 동일 |
| V2-c2 | 그래프 내부 보관을 핸들로 | 자가 검증 **20종** 전부 기준선과 동일 |

★ **V2-a가 미룬 세대(generation)를 V2-c1이 넣었다.** V2-a는 안 넣는 이유를
"놓는 시점이 셋뿐이고 전부 그 핸들을 든 쪽이 함께 죽으니 잡을 사고가 없다"로
적으면서 조건도 함께 적어 뒀다 — "재사용을 시작하는 순간 필요해지므로, 그때
id의 상위 비트로 넣는다." **그 순간이 정확히 V2-c였다.** 미루면서 조건을 적어
둔 것이 값을 했다: 다시 판단하지 않고 조건 충족만 확인하면 됐다.

id = 세대(상위 16비트) | 칸 번호+1(하위 16비트). 세대가 어긋난 핸들은
`nullptr`로 풀린다 — 지난 프레임 핸들이 이번 프레임의 **남의 리소스**로 조용히
풀리는 것이 이 검사가 막는 사고다.

**남긴 것 하나.** `ExecuteContext::Resolve(RGHandle) -> ID3D12Resource*`를
안 지웠다. 지우면 소비처 113곳이 전부 `resources->Resolve(ctx.ResolveHandle(h))`로
부풀었다가 V2-b가 30곳을 되돌린다 — 옮겼다 되돌리는 변경은 회귀 위험만 있고
얻는 것이 없다. **소멸 조건을 헤더에 적었다**: V2-b·V3·V4가 소비처를 걷어내
호출자가 0이 되면 지운다. (조건 없이 남기는 것과 조건을 적고 남기는 것은
다르다 — R3-1의 `SetDeviceServices`가 조건 없이 남아 32종을 한꺼번에 깨뜨렸다.)

**곁들여 잡힌 것.** `Reset()`이 등록을 안 놓고 `m_resources`를 비웠다. 지금은
호출자가 0이라 안 드러나지만, V2-c2로 표 등록이 걸리면서 **한 번 부르면 표가
새는 API**가 된다. 소멸자와 같은 `ReleaseResources()`를 부르게 했다 —
호출자가 없다는 이유로 반쪽만 고치면 나중에 부른 사람이 알 길이 없다.

**아직 포인터인 자리 둘.** Forward+의 타일 버퍼가 `ImportTexture`의 포인터
오버로드를 탄다. 그래프에 **버퍼 개념이 없어서** `RHIBufferHandle`을 못 받기
때문이다. 이것은 V2가 아니라 그래프의 리소스 모델 문제이므로 V3/V4에서 잡는다 —
여기서 텍스처인 척 끼워 넣으면 Vulkan에서 `VkImage`/`VkBuffer`로 갈릴 때 터진다.

#### V2-b 완료 (2026-08-10) — 크기 실측이 계획의 4배였다

**계획이 "desc 31곳"이라고 적은 것이 틀렸다.** 실제로 해 보니 **131곳 · 19파일**,
최종 36파일이 바뀌었다. 그래도 한 커밋에 끝냈다 — 중간이 전부 빌드 실패인
덩어리라 나눌 이음매가 없었다.

무엇을 잘못 셌나 — 또 **"누가 이것을 채우는가"**다. V2-c 순서를 뒤집을 때
같은 반성을 적었는데, 그때는 desc의 인자가 그래프에서 온다는 것까지만 봤다.
그 인자들이 **중간 운반 변수를 거친다는 것**은 안 봤다:

| 인자 출처 | 수 | 비고 |
|---|---|---|
| `executeContext.Resolve(...)` | 34 | `ResolveHandle`로 바꾸면 끝 |
| `.Get()` (자가 검증의 손 ComPtr) | 8 | `RegisterExternalTexture` 필요 |
| **운반 변수·멤버** | **나머지** | `t.resources[i]` · `m_iblIrradiance` · `m_cubeMap` · `raw` · `fallback` · `resourceA` … |

셋째 줄이 전부였다. `ID3D12Resource*` 선언이 패스 파일에만 62곳 있고 그중
29곳이 desc를 먹인다 — 선언 타입을 바꾸면 그것을 채우는 자리와 읽는 자리로
연쇄한다. **컴파일러가 이것을 바로 말해 줬다**: 캐시 핸들화(V2-b1)만 하려
했는데 오류가 곧장 desc 팩토리까지 번졌다. b1과 b2는 나눌 수 있는 이음매가
아니다.

결과 — 자가 검증 20종이 판정·수치까지 기준선과 **바이트 동일**:

| | 전 | 후 |
|---|---|---|
| 패스 파일의 `ID3D12Resource*` | 62 | **33** |
| desc 호출 중 `.Get()` | 8 | **2** |

`Dim::Buffer`만 `bufferResource` 칸을 보게 텍스처/버퍼를 갈라 뒀다 — 한 칸에
몰면 "버퍼를 큐브맵으로 봤다"가 컴파일된다. 핸들을 내게 된 것: 텍스처 캐시
(업로드 때 빌려주고 은퇴 때 놓는다), IBL 생성기(네 산출물, Shutdown이 놓을 수
있게 서비스를 기억한다), 머티리얼 운반 배열, `m_ibl*`·`m_cubeMap`·`m_shadowMap`.

★ **곁들여 잡힌 이중 인자.** 인코더의 `ClearUnorderedAccess`가 리소스와 desc를
함께 받고 있었는데, desc가 핸들을 들게 되면서 같은 것을 두 번 받는 자리가 됐다.
둘이 어긋나면 어느 쪽이 이기는지 **서명만 봐서는 알 수 없다** — 인자를 없앴다.

★ **"V2-b가 V1의 청구서"라는 아래 예상은 틀렸다.** `ToDXGI` 실측 86 → **87**.
포맷 인자는 리소스 포맷을 그대로 쓰려고 있는 것이 아니라 **갈아 보려고**
(깊이→색, 밉별) 있는 것이라 핸들과 무관했다. 그 청구는 V4·V6의 몫이다.

★ V2-b가 V1의 청구서다. `ToDXGI` 86곳이 남은 이유가 "desc가 아직 DXGI를
받아서"였고, 그 서명이 핸들을 받게 되면 포맷 인자 자체가 대부분 사라진다 —
리소스가 자기 포맷을 알기 때문이다. **즉 V1과 V2는 따로 센 것이 아니라 한
덩어리의 앞뒤다.**

#### V2-d 완료 (2026-08-10) — V2 끝

R3가 "다 옮기고 나면 `commandList`가 사라지고 인코더 자리만 남는다"고 적어 둔
(§3.3) 그 지점이다. **마지막까지 원시 커맨드 리스트를 붙들던 것은 자가 검증의
리드백 복사 35곳**이었다:

```
resources.CopyToReadback(executeContext.commandList, readback, ...)
```

인자 하나 때문에 `ExecuteContext`가 커맨드 리스트를 계속 내보내야 했다.

★ **'검증용이니까 원시로 둔다'가 안 되는 이유.** 검증이 도는 경로와 실제로
그리는 경로가 같아야 검증이 뜻을 갖는다. 검증만 다른 통로를 쓰면 **그 통로에서만
나는 버그를 못 잡는다.** 이 원칙 때문에 리드백 복사가 전부 인코더로 왔다.

인코더가 받은 것: `CopyToReadback` · `CopyVolumeToReadback` ·
`CopyPartialToReadback` · `CopyBufferToReadback` · `CopyTexture` ·
`ClearRenderTargetRect`. 전부 핸들을 받는다.

`ClearRenderTargetRect`가 생긴 이유는 병렬 기록 검증이다 — '띠마다 다른 패스가
지운다'로 덮임을 재는데 전체 클리어만 있어서 **손으로 RTV 힙을 만들어** 원시
커맨드 리스트로 내려가 있었다. 그 힙이 통째로 사라졌다.

곁들여 렌더 타깃도 핸들이 됐다(V2-b 잔여):
`CreateRenderTargets(std::span<const RHITextureHandle>, ...)`.

| | V2 시작 | 지금 |
|---|---|---|
| 패스 파일의 `ID3D12Resource*` | 62 | **19** |
| `ExecuteContext::commandList` | 38 | **0** |

**V2 완료.** 다음은 V3(상태·배리어 중립화 45곳).

#### V2-c2 회귀와 그 교훈 (2026-08-10)

**transient 풀이 절반만 돌고 있었다.** V2-c2가 반납 조건을 `ownsRegistration`으로
두었는데, 풀에서 **빌려 오는 경로가 그 플래그를 세우지 않았다**:

```
프레임 N    풀 비었음 → 전부 CreateCommittedResource → 반납됨
프레임 N+1  풀에서 빌림 → ownsRegistration=false → 반납 안 됨, 풀이 다시 빔
프레임 N+2  전부 다시 생성 ...
```

한 프레임 걸러 전 transient를 새로 만들었다. PHASE 3-9가 "상시 실행에서
프레임당 수십 ms"라고 실측해서 풀을 넣은 바로 그 비용이 되살아난 것이고,
빌린 것을 안 놓으므로 리소스도 샜다.

**어떻게 드러났나: 에디터 카메라가 뚝뚝 끊긴다는 제보로.** 자가 검증 20종은
전부 통과하고 있었다.

★ **검사가 픽셀만 보면 성능 회귀를 못 잡는다.** 이 결함은 그리는 결과를 한
비트도 바꾸지 않는다 — 같은 그림을, 훨씬 비싸게 그릴 뿐이다. "20종 바이트
동일"이라는 근거가 그래서 이 결함 앞에서 무력했다. 픽셀 대조는 정확성의
증거이지 비용의 증거가 아니다.

**고친 방식: 플래그를 없앴다.** transient는 '그래프가 만들었든 빌렸든 그래프가
끝나면 내놓는다'가 예외 없는 규칙이므로, 그것을 기억하는 플래그가 있으면 안
된다. 세우는 자리가 둘이면 언젠가 한쪽을 빠뜨린다 — 실제로 빠뜨렸다.
`ownsRegistration`은 이제 임포트 한 가지만 뜻한다.

**넣은 검사: `dx12.rendergraph [6/6]`.** 같은 풀로 그래프를 두 번 세우고
`transientCreated`가 1 → 0인지 본다. 결함을 일부러 되살려 이 검사가 실패하는
것(2회차 생성 1)을 확인한 뒤 고쳤다 — 검사가 무엇을 잡는지 모르는 채로 넣으면
그것은 검사가 아니다.

#### V3 완료 (2026-08-10) — 상태·배리어 중립화

`RGResourceState` → **`RHIResourceState`**, `RenderEngine/RHI/` 로 올라갔다.
그래프가 배리어를 유도하려고 만든 어휘인데 그래프 **밖**에서도 같은 말이
필요했다 — 업로드 직후의 전이, 캐시 텍스처를 넓히는 자리, IBL 생성 체인의
RT→SRV 전이. **이름이 `RG-` 로 남아 있으니 "그래프 안에서만 쓰는 말"로 읽혔고,
실제로 그렇게 읽혀서 그래프 밖이 원시 상수로 갈라져 나가 있었다.**

```
IRenderDeviceServices::TransitionResources(span<const RHITransition>)
```

`before` 를 받는다. 백엔드가 현재 상태를 추적하지 않기 때문이다 — 추적하려면
표가 상태를 들어야 하고, 그러면 그래프의 상태 추적과 두 벌이 되어 어긋날
자리가 생긴다. **아는 쪽이 말한다.**

`ToD3D12` 도 디바이스에 한 벌만 뒀다. 그래프가 자기 것을 따로 들면 두 벌이
어긋나는 날 배리어의 `before` 와 `after` 가 다른 규칙으로 만들어진다.

★ **`PixelShaderResource` 를 어휘에 넣을지 한참 고민했다.** 상태를 잘게
노출할수록 호출부가 배리어를 손으로 짜는 쪽에 가까워진다. 그런데 이것은
'규칙'이 아니라 코드가 실제로 쓰는 '상태'였다 — 텍스처 캐시가 업로드를 그
상태로 끝내고, IBL 생성기와 스카이박스가 그것을 계약으로 적어 두었다. 빼면
그 자리들이 다시 원시 상수로 갈라진다. Vulkan 에서도 FRAGMENT 스테이지만
기다리는 것과 모든 셰이더 스테이지를 기다리는 것은 다른 배리어다.

★ **옮기면서 불일치 하나가 드러났다.** Forward+ 와 SSGI 의 자가 검증 블록이
`NON_PIXEL_SHADER_RESOURCE` 로 전이해 놓고 그래프에는 `ShaderResource`(=ALL)
라고 말하고 있었다. 그래프의 첫 usage 도 `ShaderResource` 라 전이가 아예 안
나와서 여태 드러나지 않았다 — 배리어가 한 번이라도 나왔으면 `before` 가
실제와 어긋난다. 중립 어휘로 옮기면서 선언을 참으로 만들었다.

★ **비용도 함께 봤다**(직전 회귀의 교훈). `fogCloudNeutral` 이 프레임마다
`ImportTexture` 의 포인터 오버로드를 타서 표에 등록하고 그래프가 죽을 때
놓기를 반복하고 있었다 — 그림은 같고 비용만 드는 왕복이다. 한 번 등록해 두고
프레임마다 핸들만 넘긴다.

| | 전 | 후 |
|---|---|---|
| 패스·라이브·IBL 생성기의 `D3D12_RESOURCE_BARRIER` | 20 | **0** |

**의도적으로 남긴 것: 자가 검증 파일의 설정 배리어 43곳.**

그 파일들은 `DX12DeviceResources` 를 직접 세운다 — 구조상 **DX12 백엔드의
자기 검사**이지 백엔드 중립 코드가 아니다. Vulkan 이 들어오면 그쪽 백엔드는
자기 검사를 따로 갖고, 이 파일들은 DX12 검사로 남는다. 중립 API 로 바꿔도
백엔드 중립성이 늘지 않는다.

★ 이것을 "적당히 남긴다"와 구분하는 것은 **소멸 조건이 아니라 소속**이다.
V2-c 가 남긴 `ExecuteContext::Resolve` 는 "호출자가 0이 되면 지운다"는 조건이
붙었지만, 이쪽은 지울 대상이 아니라 원래 DX12 쪽에 속한 코드다. 한 번
자동 변환을 시도했다가 `std::swap` 으로 재사용되는 배리어를 깨뜨려 되돌렸고,
그때 이 구분이 분명해졌다.

**다음은 V4(바인딩 레이아웃 227곳).**

#### 비유니티 빌드 점검 (2026-08-10)

유니티 빌드는 `.cpp` 열댓 개를 한 번역 단위로 묶는다. 그래서 **어떤 파일이
include 를 빠뜨려도 같은 묶음의 옆 파일이 대신 넣어 주면 빌드가 통과한다.**
묶음 구성은 파일 추가·이름 변경만으로 바뀌므로, 그렇게 가려진 결함은 무관한
커밋에서 갑자기 터진다.

```
MSBuild CreatorEngine.sln -p:Configuration=Debug -p:Platform=x64 \
        -p:EnableUnitySupport=false -t:Rebuild
```

잡힌 것 셋:

| 자리 | 빠진 것 | 원인 |
|---|---|---|
| 패스·자가 검증 24곳 | `RHIEncoder` 정의 | **V2-d** — 리드백 복사 35곳을 `executeContext.encoder->` 로 옮기면서 |
| `EnhancedRenderGraph.cpp` 7곳 | `DX12DeviceResources` 정의 | **V2-c2·V3** — 그래프가 표와 변환을 쓰게 되면서 |
| `GpuDiagnostics.cpp` | `<dxgi1_3.h>` | 기존 (`dxgidebug.h` 는 타입만 주고 `DXGIGetDebugInterface1` 은 안 준다) |

앞의 둘은 **이번 V 작업이 직접 만든 것**이다. 유니티 빌드가 계속 초록이라
자가 검증 21종으로는 알 방법이 없었다.

★ **고친 방식이 자리마다 다르다.** 인코더는 24곳에 include 를 뿌리지 않고
`EnhancedRenderGraph.h` 가 제공하게 했다 — `ExecuteContext` 가 `RHIEncoder*` 를
내주는데 그것을 받는 쪽은 **예외 없이 역참조한다**(패스는 인코더로만 기록한다,
R3 계약). 전방 선언만 두는 것은 "받았는데 못 쓰는" 헤더를 만드는 일이다.
반면 `DX12DeviceResources` 는 그래프 `.cpp` 안에서만 필요하므로 `.cpp` 에 넣었다.

★ **이 점검은 주기적으로 돌아야 한다.** 픽셀 대조가 비용을 못 재듯이(직전
회귀), 유니티 빌드는 include 위생을 못 잰다. 둘 다 "초록이었다"가 근거가 되지
못하는 경우다.

#### V4 완료 (2026-08-10) — 바인딩 레이아웃

루트 시그니처 설명이 **`RHIPipelineLayoutDesc`** 로 바뀌었다. 경계는 이미
서 있었다(`IRenderRootSignatureCache::GetOrCreate`, PHASE 3-4) — 다만 그
경계를 **지나가는 인자**가 `D3D12_ROOT_SIGNATURE_DESC` 였다. 인터페이스만
중립이고 인자가 DX12 면 Vulkan 백엔드는 그 인자를 읽을 수 없으므로, 경계가
있으나 없으나 같다. V4 가 고친 것은 그 부분이다.

**샘플러 설명도 함께 옮겼다.** R3-2 가 `RHISamplerTable`(거는 동작)만
중립화하면서 "만드는 쪽의 중립화는 필터·주소 모드·비교 함수를 전부 옮기는
별개의 몫"이라고 적어 둔 그 몫이다. 정적 샘플러(루트 시그니처 안)와 힙
샘플러(디스크립터 힙)가 **같은 상태**를 서로 다른 DX12 구조체로 적고 있어서,
한쪽만 옮기면 어휘가 두 벌이 된다.

**어휘를 실사용에서 뽑았다**(V1 과 같은 규칙). 25곳이 쓰는 값이 놀랄 만큼
적다 — 필터 3종 · 주소 모드 3종 · 가시성 3종 · 비교 함수 1종 · 경계색 1종 ·
루트 플래그 1종.

★ **필터에서 비교 여부를 꺼냈고 min/mag 와 mip 을 갈랐다.** D3D12_FILTER 는
셋을 한 값에 접어 넣는다(`COMPARISON_MIN_MAG_LINEAR_MIP_POINT`). Vulkan 은
`magFilter`·`minFilter`·`mipmapMode`·`compareEnable` 이 각각이다. 접힌 쪽을
그대로 쓰면 Vulkan 백엔드가 매번 그것을 펴야 하고, 필터 종류가 늘 때마다
'COMPARISON_ 가 붙은 변종'이 함께 늘어난다. 펴진 형태로 들고 DX12 백엔드가
접는다 — 실제로 쓰이는 값 하나가 min/mag 와 mip 이 다른 경우라, 합쳐 들었으면
그 하나를 표현할 수 없었다.

★ **정적 샘플러와 힙 샘플러를 타입으로 갈랐다.** 상태(`RHISamplerDesc`)는
공유하고, 거는 자리(레지스터·가시성)만 `RHIStaticSamplerDesc` 가 더한다.
한 구조체에 넣고 "힙 쪽에서는 무시한다"고 적어 두면 무시되는 필드를 채우는
호출부가 반드시 생긴다. Vulkan 에서도 이 구분이 그대로다 — 상태는
`VkSampler`, 거는 자리는 디스크립터 레이아웃의 `pImmutableSamplers` 다.

★ **대응표를 별도 파일에 한 벌로 뒀다**(`DX12PipelineLayoutTranslate.h`).
쓰는 곳이 둘이기 때문이다 — 캐시가 정적 샘플러를, 디스크립터 힙이 힙
샘플러를 만든다. 각자 두면 필터 하나를 더할 때 한쪽만 고치는 날이 오고,
그때 증상은 "샘플러가 자리에 따라 다르게 동작한다"가 된다.

★ **옮기면서 불일치 하나가 드러났다.** 안개 패스의 s0·s2 만 `MaxLOD` 가
0 이다(다른 자리는 전부 `FLOAT32_MAX`). 둘 다 밉이 하나뿐인 대상(격자·그림자
맵)이라 여태 드러나지 않았다. 중립 기본값은 `kMaxLod` 라 그냥 두면 값이
바뀐다 — **리팩터에서 고치지 않고 명시해서 지금 값을 유지했다.** 그림이
바뀌었을 때 무엇 때문인지 가려지면 안 되기 때문이다. 고칠 것이면 별도로 재고
바꾼다.

| | 전 | 후 |
|---|---|---|
| 패스·자가 검증의 레이아웃·샘플러 DX12 심볼 | 379 | **18** |

남은 18 중 실코드는 17이다(아래) — 나머지 1은 `desc.depthFunc` 로 V6 의 몫이다.

**의도적으로 남긴 것 ①: 벤치 둘의 `D3D12SerializeRootSignature` 직접 호출 17곳.**

`EnhancedApiOverheadBench`·`EnhancedEncoderBench` 는 캐시를 거치지 않고 루트
시그니처를 직접 만든다. V3 가 자가 검증 배리어 43곳을 남긴 것과 같은 이유다 —
**소속**이 DX12 백엔드의 자기 검사이지 백엔드 중립 코드가 아니다.

**의도적으로 남긴 것 ②: 돌려받는 `ID3D12RootSignature*`.**

★ 이쪽은 소속이 아니라 **짝** 때문이다. 그 포인터를 쓰는 자리를 전부 세어
보면 예외 없이 PSO 포인터와 함께 간다 — `SetPipeline(pso, rootSignature)`
이거나 `DX12*PipelineDesc::rootSignature` 다. 둘 중 하나만 핸들로 바꾸면
서명에서 DX12 타입이 하나 줄 뿐 경계는 그대로 있다. **V6(파이프라인 상태
기술)이 둘을 함께 준다.**

  (이때 `rootSignatureId` 도 사라진다. 그 필드가 있는 이유가 "포인터는 실행마다
  주소가 달라 디스크 캐시의 키가 못 된다"이고, 핸들이 곧 안정된 식별자다.)

**검증**

- 자가 검증 20종 **판정 줄 기준선과 동일**
- 비유니티 빌드 **그린**(V3 뒤로 규칙이 된 점검)
- 해시 알고리즘이 바뀌어 `rootSignatureId` 가 갈렸고, 그래서 PSO 캐시 판정
  줄이 한 번 어긋났다. `dx12_pso_selftest.cache` 를 지우고 다시 돌려
  **기준선과 같은 값(컴파일 2 · 히트 0)** 이 나오는 것을 확인했다 — 회귀가
  아니라 캐시 상태다. V2 때와 같은 확인 방법이다.

**다음은 V5(셰이더 컴파일) 또는 [Vulkan 골격]** — §7.2 가 "V4 직후에 골격을
세워 계약을 때려 보는 것을 기본으로 한다"고 적어 둔 자리가 여기다.

### 7.2.2 Vulkan 골격 설계 (착수 전 기록, 2026-08-10)

§7.2 가 "V4 직후에 골격을 얇게 세워 계약을 때려 보는 것을 기본으로 한다"고
적어 둔 자리다. **산출물은 '통과'가 아니라 '어디서 안 맞는가'다.** 구 RHI 가
죽은 이유가 소비자 없는 추상이었으므로(§1.1), 두 번째 소비자가 못 들어가는
자리를 세는 것이 이 작업의 값이다.

#### 환경 실측 (2026-08-10)

| | 상태 |
|---|---|
| `vulkan-1.dll` 로더 · 인스턴스 1.4.321 · RTX 2080 Ti (1.4.329) | 있음 (드라이버가 설치) |
| `vulkan.h` · `vulkan-1.lib` | **없음** (SDK 미설치) |
| SPIR-V 컴파일러 | **없음** (Windows SDK 의 `dxc` 는 `SPIR-V CodeGen not available`) |
| 검증 레이어 | **없음** (레이어 매니페스트 조회 실패) |

검증 레이어가 없는 채로 세우면 안 된다. `dx12.selftest` 가 "검증 레이어 메시지
0건"을 통과 조건으로 삼는 것과 같은 규율이 없으면, 잘못된 계약이 조용히
통과하고 골격이 "맞다"고 거짓 보고한다. → **LunarG SDK 설치를 전제로 한다.**

#### SDK 없이 먼저 잰 것 — 인터페이스별 DX12 잔량

주석을 뺀 **코드 줄**만 셌다.

| 인터페이스 | DX12 가 남은 코드 줄 | Vulkan 이 구현할 수 있나 |
|---|---|---|
| `IRHIDeviceResources` (D1) | **0** | **가능** |
| `RHIEncoder` (R3) | 8 서명 | 불가 |
| `IRenderDeviceServices` | 약 20 서명 | 불가 |
| `IRenderPipelineCache` · `IRenderRootSignatureCache` | 반환형이 DX12 | 불가 (V6) |
| 경계 구조체 (`RHIBindingDesc` · `RHITextureDesc` · `RHIBindingTable` …) | `DXGI_FORMAT` · `D3D12_RESOURCE_STATES` · `D3D12_GPU_DESCRIPTOR_HANDLE` | 불가 |

★ **지금 Vulkan 이 구현할 수 있는 인터페이스는 `IRHIDeviceResources` 하나뿐이다.**
D1 이 "이 인터페이스가 Vulkan 에 충분한가는 두 번째 구현이 생겨야 답할 수 있다 —
D1 은 그 자리를 만들 뿐이다"라고 적어 둔 청구서가 여기서 처음 청구된다.

★ **`ToDXGI` 잔량 83곳의 주인을 여기서 갈랐다.** V2-b 가 이것을 두고 "그 청구는
V4·V6 의 몫이다"라고만 적고 어느 쪽인지 나누지 않았다. 골격을 앞두고 실제로
어디로 가는 호출인지 분류했다:

| 곳 | 어디로 | 주인 |
|---|---|---|
| 36 | `desc.rtvFormats[0]` · `desc.dsvFormat` — PSO 기술 | **V6** |
| 24 | `D3D12_RESOURCE_DESC.Format` · clear value 등 원시 구조체를 직접 세우는 자리 | 패스 12 + 자가 검증 12 |
| 16 | `RHIBindingDesc::Srv2D/Uav2D(…, format)` — 뷰 포맷 | 아래 |
| 7 | `RHIDepthTargetDesc::Depth(…, format)` | 아래 |

**대부분이 V6 이거나 원시 구조체를 직접 세우는 자리다.** V4 라는 이름이 걸릴
수 있는 것은 뒤의 23곳뿐이고, 그것도 V4 행의 표현은 "루트 시그니처 →
`RHIPipelineLayout`" 이다 — 바인딩 **레이아웃**이지 바인딩 **디스크립터**가
아니다. V4 는 정의된 대로 끝났고, 이 23곳은 **V2-b 가 뭉뚱그려 단 채로 주인이
없던 자리**다.

★ 그래도 모양은 짚어 둔다. 이 23곳은 V1 이 **선언**은 옮겼지만 **경계 서명**은
안 옮겨서 남은 것이고, **V4 착수 전의 루트 시그니처 캐시와 정확히 같은
모양이다** — 인터페이스는 중립인데 인자가 DX12 다.
`RHITextureDesc::initialState` 가 `D3D12_RESOURCE_STATES` 인 것(V3 가 상태
어휘를 만들었는데 생성 desc 는 안 갔다)도 같은 부류다. 골격이 이 자리를 실제로
때려 보고, 그 뒤에 주인을 확정한다.

#### 범위

디바이스 · 스왑체인 · 삼각형 하나. 그리고 **삼각형은 `RHIEncoder` 를 타지
않는다** — 탈 수 없기 때문이다(위 표).

★ 이것을 타협이 아니라 **측정 결과**로 적어 둔다. §7.3 의 완료 조건 6번이
"Vulkan 백엔드가 **같은 패스 코드로** 삼각형 하나를 그린다"인데, 골격은 그
조건을 **만족하지 못한 채** 세워진다. 그 거리가 얼마인지가 이 작업의 산출물이다.

- `RenderEngine/RHI/Vulkan/VulkanDeviceResources.{h,cpp}` — `IRHIDeviceResources` 구현
- 삼각형은 자기 경로(자체 파이프라인 · SPIR-V)로 그린다
- 판정은 **오프스크린 + 리드백**이다. `dx12.*` 검사 20종이 전부 그 모양이라
  판정 줄을 같은 방식으로 대조할 수 있다
- 스왑체인은 **숨김 창을 따로 만들어** 확인한다. ★ 한 HWND 에 두 백엔드의
  스왑체인을 함께 둘 수 없다 — §4 가 적은 DXGI 제약(한 창에 스왑체인 하나)과
  같은 부류이고, D2 가 스왑체인 소유권을 DX12 로 옮겨 둔 상태다
- CLI 는 `vk.selftest` 로 붙인다(`dx12.selftest` 와 같은 배선)

#### 미리 적어 두는 예상 (나중에 맞았는지 대조한다)

1. `IRHIDeviceResources` 는 **고칠 것 없이** 구현된다 — `Initialize` 를 인터페이스
   밖으로 잘라낸 판단(§4 D1)이 옳았다면.
2. `GetFenceValue` / `WaitForGpu` 는 타임라인 세마포어로 그대로 대응된다.
3. `AttachSwapChain(void* hwnd)` 는 대응되지만, **프레임 인덱스의 의미가 갈린다** —
   DX12 는 백버퍼 인덱스를 앱이 고르고 Vulkan 은 `vkAcquireNextImageKHR` 가
   준다. 이것이 인터페이스에 드러나면 첫 번째 불일치가 된다.
4. `GetVideoMemoryInfo` 의 `byType`(`"ID3D12Resource" -> 47`)은 Vulkan 에서
   의미가 없다 — 진단 인터페이스가 백엔드 어휘를 들고 있다.

#### Vulkan 골격 완료 (2026-08-11)

`vk.selftest` 가 선다. 판정 줄은 `dx12.*` 20종과 같은 모양이다:

```
[1/5] 디바이스 생성 통과 — NVIDIA GeForce RTX 2080 Ti (Vulkan 1.4.329) · 검증 레이어 켬
[2/5] 프레임 경계·타임라인 세마포어 통과 (완료값 0 → 1 · 서명 1)
[3/5] 동적 렌더링 삼각형 기록 통과 (렌더 패스 객체 없이)
[4/5] 픽셀 검증·PNG 저장 통과 — 중앙(128,64,63) 구석(13,13,38)
[5/5] 스왑체인 통과 — 붙임·획득·표시 2프레임·크기 변경
Vulkan 골격 검증 통과 — 검증 레이어 클린
```

**예상 대조** (§7.2.2 에 착수 전 적어 둔 것):

| # | 예상 | 결과 |
|---|---|---|
| 1 | `IRHIDeviceResources` 를 고칠 것 없이 구현한다 | **맞음.** 인터페이스를 한 글자도 안 고쳤다 |
| 2 | 펜스 ↔ 타임라인 세마포어가 대응된다 | **맞음.** `GetCompletedFenceValue` 가 `vkGetSemaphoreCounterValue` 그대로다 |
| 3 | 프레임 인덱스의 의미가 갈린다 | **맞음, 그리고 예상보다 무겁다** — 아래 ★ |
| 4 | `byType`(`"ID3D12Resource"`)이 Vulkan 에서 무의미하다 | **맞음.** `available=false` 로 둘 수밖에 없다 |

★ **예상 3이 실물로 확인됐다.** DX12 의 `GetBackBufferIndex()` 는
`IDXGISwapChain3::GetCurrentBackBufferIndex()` 라 아무 때나 물을 수 있는
**질의**다. Vulkan 은 `vkAcquireNextImageKHR` 을 불러야 알 수 있고 그것은
세마포어를 요구하며 블록될 수 있다 — 같은 이름의 함수가 한쪽에서는 질의고
다른 쪽에서는 **획득**이다. 골격은 `BeginFrame` 안에서 획득해 이 차이를
가뒀지만, 그것은 "프레임을 열 때 반드시 백버퍼를 잡는다"를 계약에 못 박는
일이기도 하다. 오프스크린만 그리는 프레임에서는 헛일이다.

  같은 이유로 획득·표시용 **이진 세마포어**도 구현 안에 가뒀다. 타임라인으로
  대체할 수 없고(두 API 가 이진만 받는다) DX12 에는 대응 개념이 없다.

★ **골격의 삼각형은 `RHIEncoder` 를 타지 않는다.** §7.2.2 가 적은 대로이고,
그것이 이 작업의 주된 산출물이다 — §7.3 완료 조건 6번("같은 패스 코드로
삼각형을 그린다")까지의 거리가 그만큼이다. 지금 Vulkan 이 구현할 수 있는
인터페이스는 `IRHIDeviceResources` 하나뿐이다.

**덤으로 드러난 것 둘:**

★ `QueryVideoMemory` 의 `usedMB` 를 채울 수 없다. DXGI 는 `QueryVideoMemoryInfo`
로 '지금 얼마나 쓰는가'를 **코어 기능**으로 주는데 Vulkan 은
`VK_EXT_memory_budget` 확장이 있어야 한다. 인터페이스가 한쪽에만 코어인 값을
요구하고 있다.

★ **판정 기준을 잘못 잡았다가 고쳤다.** 처음에는 검증 레이어 메시지를 심각도만
보고 셌는데, 그러면 로더가 사용자 기계 설정에 대해 하는 말(`GENERAL` 종류 —
실측 예: OBS 후크 레이어가 32/64비트로 중복 설치돼 있다는 경고)까지 결함으로
센다. `ID3D12InfoQueue` 에는 그런 부류가 없으므로 두 백엔드의 판정 줄이 같은
뜻이 아니게 된다. `VALIDATION`·`PERFORMANCE` 만 세도록 고쳤다 — **코드를
재야지 기계를 재면 안 된다.**

**설계 판단 셋:**

★ **`vulkan-1.lib` 를 링크하지 않는다.** 링크하면 로더가 없는 기계에서 실행
파일이 아예 뜨지 못한다(프로세스 시작 시점의 임포트 해석 실패라 코드로 막을
수 없다). DX12 는 Windows 10+ 에 보장되지만 Vulkan 로더는 드라이버가 깔아 주는
것이라 보장이 아니다. 백엔드가 둘이 되는 순간 "둘 중 하나가 없는 기계"가
정상 상황이 되므로, 없으면 조용히 DX12 로 가야지 못 뜨면 안 된다.

★ **동적 렌더링(1.3)을 쓰고 `VkRenderPass` 를 만들지 않는다.** 그것을 쓰면
상위 계약에 **DX12 에 대응물이 없는 개념**이 생긴다 — DX12 는 렌더 타깃을
커맨드에 직접 걸고 렌더 패스 객체가 없다. 한쪽에만 있는 객체를 계약에 들이지
않는다.

★ **셰이더는 HLSL 을 유지하고 SPIR-V 를 미리 뽑아 박는다.** DX12 는
`D3DCompiler_47.dll` 이 Windows 에 있어 런타임 컴파일이 되지만 Vulkan 은 OS 가
주는 컴파일러가 없다. 이 비대칭이 V5(셰이더 컴파일 중립화)가 다뤄야 할 것의
실물이다. SDK 의 `dxc` 는 SPIR-V 를 내지만 Windows SDK 에 딸린 `dxc` 는 그
기능이 꺼져 있다.

**다음은 V5(셰이더 컴파일) 또는 V6(파이프라인 상태 107).** 골격이 계약을
때려 본 결과 V1~V4 를 되돌릴 이유는 나오지 않았다 — 어긋난 자리는 전부 아직
중립화하지 않은 곳(`RHIEncoder` · `IRenderDeviceServices` · 캐시 반환형)이다.

### 7.2.3 V5 전반 — 셰이더 소스 파일화와 옛 시스템 폐기 (2026-08-11)

V5 를 착수하려고 범위를 재다가, **먼저 해야 할 것이 앞에 있다**는 것이
드러났다. 컴파일을 인터페이스로 빼기 전에 소스가 파일이어야 한다.

#### 왜 소스 파일화가 V5 앞에 오는가

패스 24곳이 HLSL 을 `R"(...)"` 문자열로 들고 있었다(실측 45블록 · 3,792줄).
그 형태가 막고 있던 것이 셋인데, 셋째가 V5 를 직접 막는다:

- 개발자가 셰이더를 편집기로 못 연다. 콘텐츠 브라우저는 `.hlsl` 을 이미
  `FileType::Shader` 로 알아보는데 정작 엔진 셰이더가 파일이 아니었다.
- `#include` 가 안 된다. 그래서 공통 조각이 필요한 자리는 문자열을 손으로
  이어 붙이고 있었다(SSAO · 포스트체인 · 포그 셋).
- **컴파일 시점을 고를 수 없다.** 문자열은 런타임 컴파일이 전제다. DX12 는
  `D3DCompiler_47.dll` 이 Windows 에 있어 그것이 되지만, §7.2.2 가 실측했듯이
  **Vulkan 은 OS 가 주는 컴파일러가 없다.** 소스가 파일이어야 빌드 때 굽는
  길이 열린다 — 즉 이것을 안 하면 V5 가 "런타임 컴파일을 중립화"하는
  모양이 되고, 그것은 Vulkan 에서 성립하지 않는 계약이다.

#### 갈라 둔 두 조각

| | |
|---|---|
| `RHIShaderSource` | 소스를 읽는다. 백엔드와 무관하므로 **중립** |
| `DX12ShaderCompiler` | 컴파일한다. **여기만 백엔드에 묶인다** |

★ '읽는 일'은 백엔드와 무관하고 '컴파일하는 일'만 갈린다. V5 가 갈아 끼울
  것은 컴파일 쪽이고 읽는 쪽은 그대로 남는다.

#### 실측 — 같은 자로 잰 전후

§7.1 의 93 은 그 표를 쓸 당시 값이라 이미 낡아 있었다. 패스 범위
(`Enhanced*{Pass,Generator,Shaders}.{cpp,h}` 40파일)로 다시 잰다:

| | 착수 전 `98ef2993` | 지금 |
|---|---|---|
| `D3DCompile(` | 19 | **0** |
| `ID3DBlob` | 85 | 66 |
| `D3D_SHADER_MACRO` | 10 | 10 |
| `D3DReflect`·`ID3D11ShaderReflection`(전 범위) | 19 | **0** |

★ **V5 의 성격이 바뀌었다.** 표의 설명은 "컴파일과 리플렉션만 인터페이스로"
  였는데, 컴파일 호출은 패스에서 0 이 됐고(전부 `DX12ShaderCompiler` 한 곳으로
  모였다) 리플렉션은 폐기와 함께 통째로 사라졌다. **V5 에 남은 실물은
  `ID3DBlob` 66 건** — 컴파일 결과가 DX12 타입인 채로 패스를 타고 다니는 것이다.

  좋은 소식 하나: 바이트코드가 PSO 로 넘어가는 자리는 이미 `const void* +
  size` 라서 중립이다(`DX12PSOManager`). 즉 남은 66 건은 **패스가 blob 을
  들고 있는 구간**뿐이고, 인계 지점은 손댈 것이 없다.

#### 함께 폐기한 것 — 옛 셰이더 시스템

자산 셰이더 경로 전체를 걷어냈다(소스 17파일 · 자산 136파일 · 약 14,800줄).
`ShaderResourceSystem` · `ShaderPSO` · `VisualShaderPSO` · `ShaderDSL` ·
`VisualShaderDSL` · `Shader.h` · `PSO` · `HLSLCompiler` · `ShaderSelectionWindow`.

**이미 죽어 있었다는 것을 실측으로 확정한 뒤에 지웠다:**

| | |
|---|---|
| `PrimitiveRenderProxy::m_customPSO` | 쓰기 3곳 · **읽기 0곳** |
| `UIRenderProxy::GetCustomPixelShader` | 소비자 **0곳** |
| `EnhancedUIPass` 가 읽는 커스텀 상태 | **0개** |
| `Material::ApplyShaderParams` | 호출자 **0곳** |
| `ShaderPSO::Apply` | 호출자 **0곳** |
| `.cso` 를 읽는 코드 | **0곳** |

즉 에디터에서 셰이더를 고르면 이름이 저장되고 리플렉션이 돌고 CPU 버퍼가
채워지지만, 그 뒤 GPU 로 올리는 코드가 저장소에 없었다.

★ **폐기 경계를 "`ShaderPSO` 의존"으로 긋고 "이음매"까지 넓히지 않았다.**
  `Material::TrySetValue` 계열은 게임 스크립트 8곳과 C# 인터롭이 실제로
  호출한다. 그 호출들도 지금 아무 그림을 바꾸지 않지만, **이것이 다음 셰이더
  저작 언어가 다시 물릴 자리**다. `MaterialParameters.h` 에 중립 타입을 새로
  두어 갈아 끼웠다(옛 타입은 `D3D_SHADER_VARIABLE_TYPE` 을 들고 있어 헤더가
  DX11 을 끌고 왔다). 넓혔으면 게임 스크립트 프로젝트가 깨졌고, 그것은 셰이더
  시스템 폐기가 살 값이 아니다.

#### 배치

`Assets/Shaders/DefaultPassShader/` 아래에 둔다. `PakHelper` 가 `Assets/` 를
통째로 재귀 수집하므로 플레이어 배포가 자동으로 따라온다 — 실행 파일 옆에
두면 그 경로를 따로 챙겨야 한다. 자가검증 픽스처 6개는 `SelfTest/` 로 갈랐다.

#### 검증

- 빌드 경고 0 · 오류 0
- 자가 검증 **28 통과 · 4 계측**. 돌린 것은 33종이고 그중 `dx12.live` 는
  판정 줄을 내지 않는다. 뺀 둘은 실패 사유가 이 작업과 무관하다 —
  `dx12.scene` 은 열린 씬에 메시가 0개(`dx12.live` 도 `드로우 — 풀 0개`),
  `dx12.bench11` 은 `_DEBUG` 에서 설계된 거부다.
- 중간 커밋(소스 파일화만)으로 체크아웃해 **독립 빌드 확인** — 나누기만 하고
  중간 상태가 안 서면 bisect 가 못 지나가서 나눈 의미가 없다

★ **판정 줄의 의미가 넓어졌다.** "셰이더 컴파일 통과"가 이제 "파일을 찾았고
  컴파일된다"이다 — 배포가 빠지면 여기서 잡힌다. 검사가 더 많은 것을 재게 된
  것이고, §7.4 완료 기준 4번의 기준선을 읽을 때 이 차이를 알고 봐야 한다.

미검증 하나: `SelfTest/Bench.hlsl`. `dx12.bench11` 이 `_DEBUG` 에서 셰이더
컴파일 **전에** 막혀서(`Release 로 재야 한다`) 이 경로로는 닿지 않는다.
`fxc` 로 VS/PS 를 직접 컴파일해 확인했으나 C++ 배선까지 도는지는 Release
실행이 있어야 안다.

#### 이 절에서 한 번 틀린 것

★ 처음에 "§7.1 의 93 은 폐기된 자산 셰이더를 센 값"이라고 적었는데 **틀렸다.**
  §7.1 표의 정의는 `D3DCompile`·`ID3DBlob` 참조 수이고 자산 셰이더와 무관하다.
  같은 자로 재 보니 착수 전이 104(19+85)였다 — 93 은 표를 쓸 당시의 값이라
  패스가 늘면서 낡은 것이지, 잘못 센 것이 아니었다. 숫자가 안 맞을 때
  "세는 대상이 틀렸다"로 먼저 넘겨짚으면 안 된다는 자리다.

### 7.2.4 V1 잔여 · V5 잔여 · V6 완료 (2026-08-11)

세 슬라이스를 연달아 갈았다. 상세는 커밋 본문에 있고 여기는 결과와 그 뒤에
드러난 순서 문제만 적는다.

| 슬라이스 | 무엇 | 커밋 |
|---|---|---|
| V1 잔여 | 경계 서명(`RHIBindingDesc`·`RHIDepthTargetDesc`·`RHITextureDesc`)의 `format` 을 `RHIFormat` 으로 | `23c93ee0` |
| V5 잔여 | 컴파일 결과를 `RHIShaderBlob`(중립)으로. `ID3DBlob` 66 → **0** · `d3dcompiler.h` 18 → **0** | `d40fe3c0` |
| V6 | 파이프라인 상태 기술을 `RHIPipelineState.h` 어휘로 | `3bf03ec6` |

셋 다 솔루션 Debug 빌드 오류 0 · 경고 0, 자가 검증 28 통과 · 4 계측 · 실패 0.

★ **경계를 중립화할 때마다 코드가 줄었다.** 호출부가 자기 `RHIFormat` 상수를
`ToDXGI()` 로 감싸 넘기고 있었는데, 경계가 중립이 되니 그 래퍼가 사라졌다 —
V1 잔여에서 21곳, V6 에서 20곳. 되돌리는 작업이 아니라 낭비를 걷는 작업이었다.

★ **타입을 좁히는 변경은 컴파일러가 양방향으로 잡는다.** 덜 바꾸면 '변환 없음',
더 바꾸면 '반대 방향 변환 없음' 이 난다. 세 슬라이스에서 여섯 번 잡혔고
(과잉 변환 넷 · 이름 충돌 하나 · 가드 오류 하나), **빌드 통과가 곧 증명**이었다.

#### 남은 표면 실측 (2026-08-11, §7.1 과 같은 범위 40파일)

**177건.** 성격이 §7.1 시점과 바뀌었다:

| | 건수 |
|---|---|
| 패스 17종 | 99 |
| 경계 헤더 + 그래프 구현 | 78 |
| — 그중 **객체 핸들** (`PipelineState` 46 · `Resource` 23 · `RootSignature` 21 · `CommandList` 11 · `Device` 6) | **111** |
| — 그중 기술·어휘 (`ResourceStates` 5 · `ResourceDesc` 5 · 배리어 4 …) | 66 |

**V1~V6 이 '어휘'를 끝냈고 남은 것의 63% 가 '객체'다.** 기술 어휘 66 은 대부분
`EnhancedRenderGraph.cpp`(27) — 백엔드 구현이라 DX12 타입이 있는 것이 정당한
자리다.

★ **세는 도구를 한 번 잘못 읽었다.** PowerShell 의 `(Select-String -AllMatches).Matches.Count`
는 **일치한 줄 수**를 주지 일치 건수를 주지 않는다 — 한 줄에 둘 이상 있으면
어긋난다. 190 과 177 이 엇갈려서 파이썬으로 다시 세어 177 로 확정했다.
재측정이 새 오차가 되지 않으려면 자를 먼저 검증해야 한다.

#### V7 이 '이동'이 아니라는 발견

§7.4 완료 기준 1번은 "`Render/` 아래 어떤 파일도 `d3d12.h` 를 include 하지
않는다"인데, **옮길 헤더 셋이 전부 객체 핸들 때문에 `d3d12.h` 를 물고 있다** —
`RenderFrameServices.h` 32 · `RHIEncoder.h` 10 · `EnhancedRenderGraph.h` 8 = **50건**.

★ **이 50 은 주석을 포함한 수다(2026-08-11 재측정).** 코드만 세면 **41**
이고, 차이 9는 전부 `RenderFrameServices.h` 의 주석이다(32 → 23). 나머지 둘은
주석에 DX12 토큰이 없어 그대로다.

  가르는 이유: **주석은 `#include <d3d12.h>` 를 강제하지 않는다.** 완료 기준
  1번이 재는 것은 include 이므로 **막고 있는 것은 41 이고**, 9는 함께 낡을
  문서다. 둘을 한 수로 묶으면 "고쳤는데 수가 안 준다"가 나온다.
  §7.2.4 가 "자를 먼저 검증해야 한다"고 적어 둔 것이 바로 이 부류다 —
  이번에는 자가 아니라 **자의 눈금 범위**가 안 적혀 있었다.

즉 V7 은 폴더 이동이 아니라 **객체 핸들화를 선행으로 요구한다.** 그런데 그
핸들화는 V6 이 "두 번째 백엔드의 모양을 보고 정할 일"로 미뤄 둔 바로 그것이다
(`RHIPipelineState.h` 머리말). 순환처럼 보이지만 아니다 — **끊는 자리는 V8 이다.**

★ **그래서 다음은 A(객체 핸들화 50건)가 아니라 C(V8 을 한 걸음 더)다.**
A 는 지금 당장 해치울 수 있는 크기지만, 그러면 **소비자가 하나뿐인 채로 핸들
모양을 정하는 것**이고 §1.1 이 구 RHI 의 사인으로 지목한 자리다. V2(리소스
핸들)가 성공했던 것은 그때 소비 지점이 이미 실측돼 있었기 때문이다.
골격은 이미 서 있으므로(§7.2.2), 거기서 **삼각형이 패스 경로를 타게** 만들면
`VkPipeline`/`VkPipelineLayout` 의 수명·소유가 `ID3D12PipelineState` 와 어디서
갈리는지가 드러나고, 그 모양대로 A 를 한 번에 자를 수 있다. 어긋나면 50건을
두 번 고치는 대신 한 번에 끝난다.

**확정 순서: C(V8 진전) → A(객체 핸들화) → B(V7 이동).**

★ **C 는 §7.2.5 에서 끝났다(V8-a).** A 가 지켜야 할 자 넷이 거기서 나왔고,
그중 하나는 위 50건의 셈을 고친다 — `RHIRenderTargetBinding` 은 핸들화가
아니라 모델 교체라 같이 세면 안 된다.

★ **A 의 바깥에도 표면이 있다(§8).** 위 50건과 §7.2.5 의 41건은 둘 다
**V7 을 막는 헤더**를 센 값이다. 패스 쪽 프로덕션 구간을 따로 세면 224건이고,
그중 38건(업로드 링)은 이 계획서의 어느 표에도 없었다. 그래프 실행과 씬
러너도 마찬가지다 — §8 이 그 셋을 채우고 진척 지표를 바꾼다.

#### 남아 있는 부채 (독립적, 작다)

- `dx12.scene` 카메라 0 · 드로우 후보 0 — `scene.switch` 가 검사의 입력
  (`RenderPassData` 스냅샷)을 채우지 않는다. FT 자산은 `62e0557a` 로 들어왔고
  `dx12.live` 는 드로우 8 · 메시 8 · 텍스처 9 를 본다. 검사 배선 문제다
- `SelfTest/Bench.hlsl` 배선 미검증 (§7.2.3 의 미검증 하나, Release 필요)

### 7.2.5 V8-a 설계 — 삼각형을 패스 경로에 올린다 (착수 전 기록, 2026-08-11)

§7.2.4 가 확정한 순서 **C → A → B** 의 C 다. A(객체 핸들화 50건)를 지금
해치우면 **소비자가 하나뿐인 채로 핸들 모양을 정하는 것**이고, §1.1 이 구 RHI
의 사인으로 지목한 자리다. 그래서 먼저 두 번째 소비자를 그 자리까지 끌고 온다.

**산출물은 삼각형이 아니라 자다.** `VkPipeline`·`VkPipelineLayout` 의 수명과
소유가 `ID3D12PipelineState` 와 어디서 갈리는지를 실물로 재고, 그 모양대로
A 를 한 번에 자른다.

#### 지금 재 둔 값 — 인코더의 DX12 잔량

§7.2.2 가 "`RHIEncoder`(R3) — 8 서명"이라고만 적고 그 자가 무엇인지 안 적었다.
V1~V6 뒤에 같은 자로 다시 세고, 간접(구조체를 통해 묻어 오는 것)을 갈랐다:

| | 메서드 수 | 무엇 |
|---|---|---|
| 서명에 **DX12 토큰이 직접** 있다 | **8** | `SetPipeline` · `SetConstantBuffer` · `SetRootBuffer` · `SetVertexBuffer` · `SetIndexBuffer` · `UavBarrier` · `CopyResource` · `ClearRenderTargetRect` |
| **구조체를 통해** 묻어 온다 | 6 | `RHIBindingTable`·`RHISamplerTable`(`D3D12_GPU_DESCRIPTOR_HANDLE`) 2 · `RHIReadback`(`ComPtr<ID3D12Resource>`) 4 |
| 타입은 중립이다 | 10 | 뷰포트 · 토폴로지 · 드로우 3 · 렌더타깃 3 · `ClearUnorderedAccess` · `CopyTexture` |

★ **"8" 은 V1~V6 을 거치고도 그대로다.** 그 여섯 슬라이스가 '기술'을 갈았고
이 여덟은 전부 '객체'이기 때문이다 — §7.2.4 의 "남은 것의 63% 가 객체"와
같은 사실을 인코더 한 파일에서 본 것이다.

#### 무엇을 만드는가

골격의 삼각형은 지금 검사 파일 안에서 `vkCmd*` 를 직접 부른다(§7.2.2).
그것을 **패스가 하는 일 넷**으로 갈라 옮긴다. 넷은 `EnhancedGridPass` 에서
그대로 뽑았다 — 지금 실재하는 가장 단순한 그래픽 패스다:

| 패스가 하는 일 | DX12 쪽 | Vulkan 쪽에 새로 세우는 것 |
|---|---|---|
| ① 레이아웃을 캐시에서 받는다 | `IRenderRootSignatureCache::GetOrCreate(RHIPipelineLayoutDesc)` | `VulkanPipelineCache::GetOrCreateLayout` |
| ② 파이프라인을 캐시에서 받아 **멤버로 든다** | `IRenderPipelineCache::GetOrCreate(DX12GraphicsPipelineDesc)` | `VulkanPipelineCache::GetOrCreate` |
| ③ 인코더에만 커맨드를 적는다 | `RHIEncoder` | `VulkanEncoder` |
| ④ 상수를 슬롯에 건다 | `encoder.SetConstantBuffer(…, GPU 주소)` | 아래 ★ |

★ **삼각형에 상수 버퍼를 하나 준다.** 이것이 이번 설계의 유일한 '기능 추가'
이고, 이유는 하나다 — **빈 레이아웃은 레이아웃을 재지 않는다.** §7.2.2 의
골격은 `VkPipelineLayoutCreateInfo{}` 를 그대로 넘겼으므로 V4 가 만든
`RHIPipelineLayoutDesc` 가 Vulkan 에서 성립하는지는 아직 아무도 모른다.
`RHILayout::Cbv(0)` 하나를 걸면 그리드 패스와 **글자 그대로 같은 레이아웃**이
되고, 그때 비로소 ①②④가 재진다.

  검증은 그 상수가 셰이더에 실제로 닿았는지까지 본다: `tint` 를 (0,1,0) 으로
  주고 중앙 픽셀의 R·B 가 0 인지 잰다. 안 닿으면 정점 색이 그대로 살아 R·B 가
  85 근처로 나온다 — **디스크립터가 안 걸려도 삼각형은 그려지므로**, 픽셀이
  '삼각형이 있다'만 보면 이 경로는 조용히 틀린 채로 통과한다.

#### 범위 밖 — 못 하는 것과 그 이유

- **그래프는 안 탄다.** `EnhancedRenderGraph` 는 `RHI/DX12/` 에 있고 `d3d12.h`
  를 문다(§7.2.4 실측 8건). 패스의 `Declare`/`Record` 분리는 배리어 유도와
  병렬 기록을 위한 것이라(`EnhancedRenderPass.h`) 골격이 흉내 내면 흉내만
  남는다. **V8-a 가 재는 것은 `Record` 안쪽이다.**
- `IRenderDeviceServices` 를 구현하지 않는다. 서명 약 20이 DX12 라 그대로다.
  렌더 타깃 묶음도 그래서 검사가 직접 세운다.
- `SetBindings`·`SetSamplers`·`SetVertexBuffer`·`Dispatch` 는 소비자가 없다.
  삼각형이 안 쓰는 것을 구현하면 **틀려도 아무도 모르는 코드**가 된다
  (`RHIPipelineState.h` 가 D3D12 열거 전체를 옮겨 적지 않은 것과 같은 규칙).
  레이아웃 종류도 `ConstantBuffer` 하나만 옮긴다 — 다섯 중 하나다.

#### 미리 적어 두는 예상 (나중에 맞았는지 대조한다)

1. **`RHIPipelineLayoutDesc`(V4)는 고칠 것 없이 Vulkan 레이아웃이 된다.**
   §7.2.2 의 예상 1이 `IRHIDeviceResources` 에 대해 맞았던 것과 같은 부류다.

2. **`SetPipeline(bindPoint, pipeline, rootSignature)` 의 두 인자는 맞고,
   근거는 기록된 것과 반대다.** `RHIEncoder.h` ③은 "Vulkan 은 파이프라인
   레이아웃이 파이프라인에 구워지므로 SetPipeline 하나가 둘을 건다"고 적었다.
   그러나 `vkCmdBindDescriptorSets` 와 `vkCmdPushConstants` 가 **레이아웃을
   다시 요구한다** — 인코더는 레이아웃을 기억해야 한다. 즉 서명은 맞았는데
   이유가 틀렸고, 그 차이가 A 를 가른다: **핸들 둘이 아니라 짝 하나여야 한다.**

3. **수명이 갈린다.** `ID3D12PipelineState*` 는 참조 계수라 캐시가 `ComPtr` 로
   들고 패스가 원시 포인터를 멤버로 둬도 안전하다. `VkPipeline` 은 계수가
   없고, 제출된 커맨드 버퍼가 참조하는 동안 `vkDestroyPipeline` 을 부르면
   미정의다. → A 의 핸들은 **표를 통해 풀려야 하고 파괴를 미룰 수 있어야
   한다**(V2 의 리소스 표와 같은 요구). 원시 포인터를 `uint32_t` 로 바꾸는
   것으로는 모자란다.

4. **`BindRenderTargets` → `ClearRenderTargets` 순서가 Vulkan 에서 비싼 길을
   강제한다.** Vulkan 의 클리어는 렌더링을 **여는 시점의 load op** 이라 공짜에
   가깝다. 그런데 중립 계약은 '걸고 나서 지운다'라 `vkCmdClearAttachments` 를
   써야 한다. → 계약이 '무엇을 지울지'를 **거는 시점에** 알아야 한다.

5. **`RHIRenderTargetBinding` 이 파이프라인 핸들보다 큰 장애다.** 그것은
   힙 인덱스 셋(`rtvIndex`·`colorCount`·`dsvIndex`)이고, Vulkan 은 이미지 뷰와
   포맷과 크기를 요구한다. 필드 3 → 10 쯤으로 는다고 본다.

6. **`BindRenderTargets` 가 여는 것을 누가 닫는지가 새 계약 문제다.** DX12 의
   `OMSetRenderTargets` 는 여닫이가 없고 `vkCmdBeginRendering` 은 반드시
   `vkCmdEndRendering` 으로 닫힌다. 인코더 수명이 패스 하나라는 R3 의 결정
   (`RHIEncoder.h`)이 여기서 값을 한다고 본다 — 닫는 자리가 이미 있다.

7. **`SetConstantBuffer` 의 셋째 인자는 살릴 수 없다.** `D3D12_GPU_VIRTUAL_
   ADDRESS` 는 8바이트 주소이고 Vulkan 에는 대응이 없다(디스크립터 셋을
   거친다). 그리고 Vulkan 에서는 `SetBindings`·`SetConstantBuffer`·
   `SetRootBuffer` 셋이 **같은 호출**(`vkCmdBindDescriptorSets`)로 접힌다 —
   DX12 에서 셋인 이유는 루트 시그니처가 세 종류의 파라미터를 갖기 때문이다.

8. **`shaderRegister` 의 뜻이 백엔드마다 다르다.** DX12 는 `b`·`t`·`u`·`s` 가
   각각 별개 이름공간인데 Vulkan(SPIR-V)은 binding 하나뿐이다. 지금은 `b0`
   하나뿐이라 안 부딪히지만, 부딪히는 순간 dxc 의 `-fvk-*-shift` 규약이
   **경계의 일부**가 된다. 이번에는 드러나지 않을 것으로 본다 — 적어 두고
   다음에 확인한다.

**판정**: `vk.selftest` 5/5 · 검증 레이어 클린 · 솔루션 Debug 오류 0 경고 0 ·
자가 검증 35종이 기준선(28 통과 · 4 계측 · 2 실패 · 1 무판정)과 같을 것.
Vulkan 쪽만 건드리므로 `dx12.*` 는 한 줄도 달라지면 안 된다 — **달라지면
그것이 결함이다.**

#### V8-a 완료 (2026-08-11)

```
[1/5] 디바이스 생성 통과 — NVIDIA GeForce RTX 2080 Ti (Vulkan 1.4.329) · 검증 레이어 켬
[2/5] 프레임 경계·타임라인 세마포어 통과 (완료값 0 → 1 · 서명 1)
[3/5] 패스 경로 삼각형 기록 통과 — 레이아웃·파이프라인 캐시 경유 (구움 1 · 재사용 0)
[4/5] 픽셀 검증·PNG 저장 통과 — 중앙(0,64,0) 구석(13,13,38)
[5/5] 스왑체인 통과 — 붙임·획득·표시 2프레임·크기 변경
Vulkan 골격 검증 통과 — 검증 레이어 클린
```

솔루션 Debug 오류 0 · 경고 0. 자가 검증 35종 **28 통과 · 4 계측 · 2 실패 ·
1 무판정** — 기준선과 글자까지 같다(실패 둘은 여전히 `dx12.bench11` ·
`dx12.scene`). `RHI/DX12/` 아래는 한 파일도 건드리지 않았다.

★ **중앙이 (0,64,0) 인 것이 이번 판정의 요점이다.** R·B 가 0 이라는 것은
상수 버퍼가 셰이더에 실제로 닿았다는 뜻이다. 음성 대조도 재 두었다 —
`SetConstantBuffer` 한 줄을 빼고 돌리면 **중앙(0,0,0)** 으로 떨어져 [4/5] 가
실패한다. 안 걸린 디스크립터를 읽는 것은 미정의이고 이 드라이버는 0 을 준다.

  이것을 안 재고 지나갈 뻔했다: **디스크립터가 안 걸려도 삼각형은 그려진다.**
  예전 판정("중앙이 밝다")이었다면 바인딩 경로가 통째로 죽어도 5/5 였다.

**예상 대조** (착수 전 적어 둔 것):

| # | 예상 | 결과 |
|---|---|---|
| 1 | `RHIPipelineLayoutDesc`(V4)를 고칠 것 없이 쓴다 | **맞음.** 레이아웃 세 줄을 그리드 패스에서 그대로 복사했다 |
| 2 | `SetPipeline` 의 두 인자는 맞고 근거는 반대다 | **맞음.** 인코더가 `m_boundLayout` 을 든다 |
| 3 | 수명이 갈린다 | **맞음, 그리고 더 나왔다** — 아래 ★ |
| 4 | Bind → Clear 순서가 비싼 길을 강제한다 | **맞음.** `LOAD_OP_LOAD` + `vkCmdClearAttachments` |
| 5 | 렌더 타깃 묶음이 파이프라인 핸들보다 큰 장애다 · 필드 3 → 10 쯤 | **방향은 맞고 수가 틀렸다: 3 → 5** — 아래 ★ |
| 6 | 여는 것을 누가 닫는지가 새 계약 문제가 된다 | **맞음.** `EndRenderTargets` 가 생겼고 DX12 에 대응이 없다 |
| 7 | `SetConstantBuffer` 의 셋째 인자는 살릴 수 없다 | **맞음.** 그리고 객체 수가 **0 대 3** 이다(풀·셋·갱신) |
| 8 | `shaderRegister` 충돌은 이번에 안 드러난다 | **맞음.** `spirv-dis` 로 확인 — `b0` → set 0 · binding 0 |

★ **예상 3 이 예상보다 무겁다 — 놓는 순서가 계약이 됐다.** 검사가 자원을
놓는 순서가 `대기 → 패스 → 캐시 → 타깃` 이어야 하고, 그 셋 다 필연이다:
캐시가 파이프라인을 소유하므로 패스가 먼저 이름을 버려야 하고, 파괴는 GPU 가
그 파이프라인을 안 쓸 때여야 하므로 대기가 앞선다. **DX12 에는 이 순서 제약이
없다** — 참조 계수가 대신 지킨다.

  그래서 두 백엔드의 `Shutdown` 이 **같은 코드인데 다른 뜻**이다:

  | | 코드 | 뜻 |
  |---|---|---|
  | DX12 패스 | `m_pso = nullptr;` | 소유 지분을 던진다(마지막이면 파괴된다) |
  | Vulkan 패스 | `m_pipeline = VK_NULL_HANDLE;` | 이름만 버린다(파괴는 캐시의 몫) |

  → **A 의 핸들은 표를 통해 풀려야 하고 파괴를 미룰 수 있어야 한다**(V2 의
  리소스 표와 같은 요구). 원시 포인터를 `uint32_t` 로 바꾸는 것으로는 모자란다.

★ **예상 5 가 틀린 방향이 유익하다.** 포맷을 들 필요가 없었다 — 동적 렌더링은
포맷을 **파이프라인 굽는 시점에만** 요구하고, 거는 시점에는 이미지 뷰만 받는다.
V6 이 `rtvFormats` 를 파이프라인 기술에 둔 것이 그대로 맞았다는 뜻이다.
남은 다섯 필드는 뷰 배열 · 개수 · 깊이 뷰 · 너비 · 높이다.

**예상하지 않았는데 나온 것 넷:**

★ **"타입이 중립인 10" 중 셋은 실은 중립이 아니다.** 착수 전 표가
`BindRenderTargets` · `ClearRenderTargets` · `ClearDepthTarget` 을 중립으로
셌는데, 셋 다 `RHIRenderTargetBinding` 을 받고 그것은 **DX12 디스크립터 힙
인덱스**다. 타입에 `D3D12_` 가 없을 뿐 모델이 백엔드 것이다.

  → 인코더의 실질 잔량은 `8(직접) + 6(구조체) = 14` 가 아니라 **17/24** 다.
  **타입을 세는 자로는 안 잡히는 부류가 있다** — V1~V6 이 타입을 갈아 온
  방식으로는 이 셋에 닿지 못한다.

★ **이미 중립인 어휘가 DX12 헤더 안에 갇혀 있다.** `RHIBindPoint` 와
`RHIPrimitiveTopology` 는 백엔드 어휘가 한 글자도 없는데, `RHIEncoder.h` 가
`d3d12.h` 를 물어서 Vulkan 쪽이 재사용할 수 없다. 열거 둘을 베껴 적었고
(`VulkanBindPoint` · `VulkanPrimitiveTopology`) **그 베낌 자체가 V7(이동)이
왜 필요한지의 증거다** — V7 은 정리가 아니라 중복 제거다.

★ **V5 의 경계는 값에서 맞고 동작에서 갈린다.** `RHIShaderBlob` 은 그대로
성립한다 — Vulkan 쪽도 같은 타입에 담아 캐시에 넘긴다. 그런데 그것을 채우는
`DX12ShaderCompiler::CompileFile` 은 성립하지 않는다. Vulkan 에는 OS 가 주는
컴파일러가 없어 **컴파일이 아니라 조회**다. MaterialPipelinePlan 의 M1 이 이
자리를 받는다.

★ **파이프라인 기술은 21 필드 중 1 필드만 갈린다.**
`DX12GraphicsPipelineDesc` 와 `VulkanGraphicsPipelineDesc` 를 나란히 두고 세면
20 개가 타입까지 같고, 다른 것은 `rootSignature`(`ID3D12RootSignature*`) ↔
`layout`(`VkPipelineLayout`) 하나다. **V6 이 '기술'만 갈고 멈춘 판단이 여기서
값을 한다 — 남은 거리가 1/21 이다.**

#### A(객체 핸들화)의 자 — V8-a 가 정한 것

50건을 자르기 전에 지켜야 할 것 넷이 나왔다:

1. **파이프라인 핸들은 짝이다.** `{파이프라인, 레이아웃}` 을 함께 푼다.
   따로 두면 Vulkan 쪽이 `vkCmdBindDescriptorSets` 에서 레이아웃을 되찾을
   길이 없다 — Vulkan 은 파이프라인에게 레이아웃을 물을 방법을 주지 않는다.
2. **핸들은 표를 통해 풀리고 파괴를 미룰 수 있어야 한다.** 위 ★.
3. **`RHIRenderTargetBinding` 은 핸들화가 아니라 모델 교체다.** 인덱스 셋을
   핸들로 바꾸는 것이 아니라 뷰 목록으로 바꾸는 것이고, 이것은 A 의 다른
   부류다. 50건을 셀 때 함께 세면 안 된다.
4. **`SetBindings`·`SetConstantBuffer`·`SetRootBuffer` 셋은 접힐 후보지만
   지금 접으면 안 된다.** Vulkan 에서는 셋이 한 호출인데, V8-a 가 때린 것은
   `SetConstantBuffer` 하나뿐이다. 나머지 둘은 소비자가 아직 없다.

#### 패스가 둘인 것은 결과가 아니라 계측기다 — A 의 완료 조건

★ **`VulkanTrianglePass` 가 있는 상태는 틀린 구조다.** §7.4 완료 조건 6이
"Vulkan 백엔드가 **같은 패스 코드로** 삼각형을 그린다"이므로, 패스가 백엔드마다
있으면 그것이 곧 실패 상태다. 목적지는 **중립 패스 하나가 소비 시점에
백엔드로 컨버트되는 것**이고, 그 모델은 이미 작동한다 — V1~V6 이 '기술'에
대해 그렇게 했고 이번에 실측됐다(파이프라인 기술 21필드 중 20이 손 안 대고
건너간다).

  **컨버트가 안 되는 것은 객체 하나뿐이다.** `ToVulkan(RHIFormat)` 은 쓸 수
  있어도 `ToVulkan(ID3D12PipelineState*)` 는 쓸 수 없다. 컨버트는 값에만
  성립하고, 소비 시점에 컨버트하려면 그 전까지 중립이어야 하는데 패스 경로의
  일곱 자리가 DX12 객체를 들고 있다(`EnhancedGridPass` 실측):

  | 자리 | 지금 |
  |---|---|
  | `m_pso` · `m_rootSignature` | `ID3D12PipelineState*` · `ID3D12RootSignature*` |
  | `rootSignatures->GetOrCreate` | 반환형 `DX12RootSignatureEntry` |
  | `psoManager->GetOrCreate` | 인자·반환형이 DX12 |
  | `encoder.SetPipeline` | 인자가 DX12 객체 |
  | `encoder.SetConstantBuffer` | `D3D12_GPU_VIRTUAL_ADDRESS` |
  | `resources->GetUploadRing` | 반환형 `DX12UploadRing&` |

  즉 **"패스 하나"는 A 의 대안이 아니라 A 의 결과다.** 그리고 A 를 먼저 했다면
  `ID3D12PipelineState*` 를 `uint32_t` 핸들 하나로 갈았을 것이고 그것은
  틀렸다 — 위에서 나온 '짝'과 '파괴 지연'이 DX12 만 보고는 안 나온다.

**그래서 A 의 완료 조건에 이것을 못 박는다.** A 가 끝나면:

1. `VulkanTrianglePass` · `VulkanFrameContext` 가 **삭제된다.** 그리드 패스가
   두 백엔드에서 그대로 돌아야 한다. 남아 있으면 A 가 실패한 것이다.
2. `VulkanGraphicsPipelineDesc` 가 `DX12GraphicsPipelineDesc` 와
   `RHIGraphicsPipelineDesc` 하나로 **접힌다**(남은 거리 1/21).
3. `VulkanEncoder` 는 **살아남아** `RHIEncoder` 를 상속한다. 백엔드마다 인코더
   구현이 있는 것은 마땅하다 — 다만 `VulkanBindPoint` ·
   `VulkanPrimitiveTopology` · `VulkanRenderTargetBinding` 은 죽는다.
4. `VulkanPipelineCache` 도 **살아남아** `IRenderPipelineCache` 를 상속한다.

  ★ 살 것과 죽을 것을 갈라 적는 이유: 섞어 두면 다음 사람이 백엔드 구현까지
    걷어내려 하거나, 반대로 계측기를 구조로 읽는다. 뒤엣것이 더 위험하다 —
    "백엔드마다 패스를 쓰는 구조"가 굳으면 그것이 정확히 RHI 가 막으려던
    것이다.

#### 안 잰 것 — 다음 사람이 착각하지 않도록

- 레이아웃 파라미터 **다섯 중 하나**(`ConstantBuffer`)만 옮겼다. 나머지 넷과
  정적 샘플러는 부르면 **실패로 멈춘다**(조용히 건너뛰지 않는다).
- 정점 입력 · 컴퓨트 · 디스크립터 테이블 · 샘플러 · 리드백 복사 · 인덱스
  드로우 — 전부 소비자가 없어 안 옮겼다.
- **그래프를 안 탔다.** §7.3 완료 조건 6("같은 패스 코드로 삼각형을 그린다")은
  아직 아니다. V8-a 가 좁힌 것은 `Record` 안쪽이고, `Declare`(배리어 유도 ·
  병렬 기록)는 그대로 남아 있다. 골격의 레이아웃 전이 두 줄이 있는 자리가
  곧 '그래프가 없는 자리'다.

#### A 의 범위 실측 (2026-08-11) — 한 덩어리가 아니라 여섯이다

V7 을 막는 41건(주석 제외, 위 §7.2.4 의 ★ 참고)을 **파일이 아니라 부류로**
묶었다. 자르는 단위가 부류이기 때문이다:

| 부류 | 건수 | 무엇 | V8-a 가 자를 줬나 |
|---|---|---|---|
| **A-1** 파이프라인 짝 | **4** | `IRenderPipelineCache` 반환형 2 · `SetPipeline` 인자 2 (+ `IRenderRootSignatureCache` 반환형 · `DX12GraphicsPipelineDesc::rootSignature`) | **줬다** — 짝 · 표 · 파괴 지연 |
| **A-2** 상태 어휘 잔여 | 5 | `RHIBufferDesc`·`RHITextureDesc` 의 `initialState` · 그래프 1 | 필요 없다 — V3 가 이미 `RHIResourceState` 를 만들어 뒀다 |
| **A-3** 커맨드 리스트 | 8 | `GetCommandList` · `ClearUnorderedAccess` · `CopyXxxToReadback` 4 · 큐 | 아니오 — 통로 문제이고 인코더에 같은 이름이 이미 있다 |
| **A-4** 리소스 포인터 | 13 | `Resolve` 2 · `RegisterExternalTexture` · `RHIReadback::buffer` · `UavBarrier` · `CopyResource` … | 부분 — V2 가 세운 표를 쓰면 된다 |
| **A-5** 디스크립터 핸들 | 4 | `RHIBindingTable`·`RHISamplerTable` 의 GPU 핸들 2 · `SetConstantBuffer`·`SetRootBuffer` 의 GPU 주소 2 | **아니다 — 자가 없다.** 아래 ★ |
| **A-6** 원시 구조체·디바이스 | 7 | 정점/인덱스 뷰 · `D3D12_RECT` · 배리어 · `ID3D12Device` 3 | 아니오 |

★ **A-5 를 지금 자르면 안 된다.** `RenderFrameServices.h` 가 그 자리를 두고
"디스크립터 힙 모델이 갈리는 자리라 백엔드 골격(V8)이 볼 몫"이라고 적어 뒀는데,
**V8-a 는 그 자리를 안 때렸다** — 삼각형이 테이블도 샘플러도 안 쓴다.
`SetConstantBuffer` 하나만 때렸고 그것도 소비자가 하나다.

  즉 A-5 는 지금 **소비자 하나로 설계하는 자리**이고, §1.1 이 구 RHI 의 사인으로
  지목한 바로 그 조건이다. A-1 을 V8-a 뒤로 미룬 것과 같은 이유로 A-5 는
  **V8-b 뒤로 미룬다** — 삼각형에 텍스처 하나와 샘플러 하나를 물리면 자가 선다.

**그래서 다음은 A 전부가 아니라 A-1 이다.** V8-a 가 실측으로 모양을 정해 준
유일한 자리이고, 자 넷(짝 핸들 · 표를 통한 해소 · 파괴 지연 · 렌더 타깃은
따로)이 전부 여기에 걸린다. A-2 는 기계적이라(V3 어휘를 적용할 뿐) 곁들여도
되고, 나머지 넷은 각자 자가 설 때까지 센 채로 둔다.

그다음이 B(V7 이동)이고, V7 은 §7.2.4 가 적은 대로 이동이 아니라 위 ★ 의
중복 제거다.

### 7.2.6 A-1 설계 — 파이프라인 짝을 핸들로 (착수 전 기록, 2026-08-11)

§7.2.5 가 자를 줬으므로 그 자로 자른다. V8-a 가 정한 넷 중 **셋이 여기 걸린다**
(짝 · 표를 통한 해소 · 파괴 지연). 넷째(렌더 타깃은 모델 교체)는 A-1 이 아니다.

#### 크기 실측 — 헤더 4건인데 호출부가 약 200이다

§7.2.5 의 "A-1 4건"은 **V7 을 막는 헤더 토큰** 수다. 실제로 손대는 자리는
그 열 배가 넘는다(2026-08-11, 주석 제외):

| 무엇 | 건수 |
|---|---|
| `ID3D12PipelineState*` 선언 | **46** |
| `ID3D12RootSignature*` 선언 | 27 |
| `desc.rootSignature =` · `desc.rootSignatureId =` | 34 + 34 |
| `SetPipeline(...)` 호출부 | 31 (DX12 30 · Vulkan 1) |
| `psoManager->GetOrCreate` · `…Compute` | 16 + 10 |
| `rootSignatures->GetOrCreate` | 20 |

★ **V2-b·V4 와 같은 부류다** — V2-b 가 "크기 실측이 계획의 4배였다"고 적었고
V4 가 227 이었다. **헤더의 토큰 수는 이행 비용이 아니다.** 다음에 A-3~A-6 을
잡을 때도 부류별 토큰 수(8·13·4·7)를 일정으로 읽으면 안 된다.

#### 설계 판단 다섯

**① 핸들은 하나다 — `RHIPipelineHandle` 이 짝을 푼다.**

V8-a 가 "핸들은 짝이다"라고 했으므로 둘로 나누지 않는다. 파이프라인 캐시는
자기가 어떤 레이아웃으로 구웠는지 이미 안다(desc 에 들어 있다). 그러니 표가
그 짝을 들면 된다:

```
RHIPipelineHandle  →  { ID3D12PipelineState*, ID3D12RootSignature* }
                   →  { VkPipeline,           VkPipelineLayout      }
```

그러면 `SetPipeline(bindPoint, pipeline, rootSignature)` 이 인자 둘로 준다.
★ **"어긋난 루트 시그니처를 걸었다"가 표현 불가능해진다** — R3 가 "루트
시그니처를 안 걸고 루트를 건드린다"를 없앤 것과 같은 수인데, 그때는 인자를
**더해서** 막았고 이번에는 **빼서** 막는다.

  실측이 이것을 받쳐 준다: 패스의 `m_rootSignature` 는 `SetPipeline` 과
  `desc.rootSignature` **밖에서 쓰이는 자리가 하나도 없다.** 그래서 멤버 27 개가
  통째로 사라진다(원시 경로 벤치 둘은 제외 — 아래 ⑤).

**② `rootSignatureId` 를 없앤다 — 그런데 여기가 이 슬라이스의 함정이다.**

레이아웃 캐시는 지금 `{signature, id}` 를 주고 그 `id` 는 **설명의 내용 해시**다.
`DX12GraphicsPipelineDesc` 가 그것을 받는 이유가 명시돼 있다:

> "루트 시그니처는 포인터가 실행마다 달라 해시에 못 쓴다."

★ **핸들도 실행마다 다르다.** 슬롯+세대라서 표에 넣는 순서에 달렸다. 그러니
`RHIPipelineLayoutHandle` 을 그대로 desc 해시에 넣으면 **PSO 디스크 캐시가 매
실행 논다** — `ID3D12PipelineLibrary` 의 키가 실행마다 바뀐다.

  → 표가 **안정 해시를 함께 든다**: `{ID3D12RootSignature*, uint64_t 내용해시}`.
    `ComputeHash` 가 핸들을 그 해시로 풀어서 쓴다. 그러면 desc 에서
    `rootSignatureId` 필드가 사라지고(채우는 줄 68 → 34), 안정성은 유지된다.

  ★ **`dx12.psocache` 가 이 판단의 판정 도구다.** 그 검사가 "2회차 컴파일
    0건"을 보므로, 여기를 틀리면 **바로 잡힌다.** 판정 줄이 이미 있는 자리를
    골라 설계한 것이지 우연이 아니다.

**③ 표는 `DX12DeviceResources` 에 두고 `IRenderDeviceServices` 에는 안 올린다.**

인코더가 `DX12DeviceResources*` 를 이미 들고 있다(그래프가 생성자로 준다).
표를 **구현 클래스**에 두면 인코더가 그대로 닿고, 인터페이스에는 DX12 반환형이
안 늘어난다 — 올리면 V7 이 멀어진다.

  ★ 전례가 있다. V2 의 `RegisterExternalTexture` 가 정확히 "만드는 쪽이 따로
    있고 표는 디바이스가 든다"이고, 그 주석이 "이쪽은 과도기가 아니다"라고
    적어 뒀다. 파이프라인도 같은 모양이다 — 캐시가 만들고 표에 올린다.

**④ 파괴 지연은 표가 가능하게만 하고 구현하지 않는다.**

V8-a 의 자 셋째가 "파괴를 미룰 수 있어야 한다"인데, 그것은 **핸들 모델이 그것을
허용해야 한다**는 뜻이다. 슬롯+세대 표가 그 조건을 만족한다. 실제 지연 해제
큐는 안 만든다 — 지금 캐시가 앱 수명이라 **놓는 호출자가 하나도 없다.**
호출자 없는 코드를 두지 않는다(`RHIEncoder::UavBarrier` 의 ★ 와 같은 규칙).
대신 세어 둔다: **놓는 경로 호출자 0.**

**⑤ 벤치 둘은 안 바꾼다.**

`EnhancedApiOverheadBench` · `EnhancedEncoderBench` 는 루트 시그니처를 손으로
만들어 원시 커맨드 리스트에 건다. 그것이 **인코더와 대조하는 기준선**이므로
원시로 있어야 한다 — 바꾸면 재는 대상과 자가 같아진다.

#### 쪼갬 — A-1a · A-1b

| | 무엇 | 판정 |
|---|---|---|
| **A-1a** | 핸들·표·중립 desc 를 세우고 DX12 쪽 호출부 약 200 을 옮긴다. `DX12GraphicsPipelineDesc` → `RHIGraphicsPipelineDesc`(`RHI/` 로) | 35종 + `dx12.psocache` |
| **A-1b** | `VulkanPipelineCache` 가 그 중립 인터페이스를 **상속**하고 `VulkanGraphicsPipelineDesc` 를 지운다 | `vk.selftest` 5/5 |

★ **A-1b 가 A-1 의 진짜 판정이다.** §7.2.5 가 "접히지 않으면 A 가 덜 된
것이다"라고 적어 뒀고, 두 번째 소비자가 실제로 들어가야 그 말이 증명된다.

#### 미리 적어 두는 예상

1. **`SetPipeline` 인자가 셋에서 둘로 준다.** 패스의 `ID3D12RootSignature*`
   멤버 27 중 벤치 몫을 뺀 전부가 사라진다.
2. **`rootSignatureId` 가 사라지고 desc 채우는 줄이 68 → 34 로 준다.**
   V1 잔여 21 · V6 20 과 같은 부류다 — 경계를 중립화하면 코드가 준다.
3. **`ID3D12PipelineState*` 선언 46 이 `RHIPipelineHandle` 로 바뀐다.**
   수는 그대로고 타입만 갈린다(멤버가 없어지지는 않는다 — 패스가 여전히
   파이프라인을 들어야 한다).
4. **컴파일러가 V1 잔여·V5 잔여·V6 때(여섯 번)보다 많이 잡는다.** 규모가
   네댓 배다. 그리고 그 잡힘이 곧 증명이다(§7.2.4).
5. **`dx12.psocache` 가 한 번은 깨질 것이다.** ② 의 함정이 실재하면 첫 시도에서
   2회차 컴파일이 0 이 아니게 나온다. 안 깨지면 그것대로 적어 둔다 — 함정이
   없었다는 뜻이므로.
6. **`IRenderDeviceServices` 의 DX12 토큰 수는 안 준다.** A-1 은 파이프라인만
   건드리고 그 인터페이스의 잔량은 리소스·커맨드 리스트(A-3·A-4)다.
   §7.2.4 의 41 중 **4만 준다**(23 → 19 · 10 → 8 · 8 → 8 을 예상).

**판정**: 솔루션 Debug 오류 0 경고 0 · 자가 검증 35종이 기준선(28 · 4 · 2 · 1)과
같을 것 · `vk.selftest` 5/5 · 검증 레이어 클린. `dx12.psocache` 는 통과만이
아니라 **"2회차 컴파일 0건"** 까지 본다.

#### A-1 완료 (2026-08-11)

솔루션 Debug 오류 0 · 경고 0. 자가 검증 35종 **28 통과 · 4 계측 · 2 실패 ·
1 무판정** — 기준선과 같다. `vk.selftest` 5/5 · 검증 레이어 클린.
`dx12.psocache` 의 일곱 줄이 글자까지 같다(2회차 컴파일 0 · 라이브러리 히트 3 ·
메모리 히트 3).

| | 착수 전 | 뒤 |
|---|---|---|
| `ID3D12PipelineState*` 선언 | 46 | **4** |
| `ID3D12RootSignature*` 선언 | 27 | **2** |
| `desc.rootSignature =` | 34 | **0** |
| `desc.rootSignatureId =` | 34 | **0** |
| V7 을 막는 헤더 셋 | 41 | **37** |

남은 6은 전부 **원시 경로 벤치 둘**이다(`EnhancedApiOverheadBench` ·
`EnhancedEncoderBench`). 설계 ⑤대로 안 바꿨다 — 인코더와 대조하는 기준선이라
원시로 있어야 한다.

**예상 대조:**

| # | 예상 | 결과 |
|---|---|---|
| 1 | `SetPipeline` 인자가 셋에서 둘로 · 루트 시그니처 멤버가 사라진다 | **맞음.** 27 → 2 |
| 2 | `rootSignatureId` 가 사라지고 desc 채우는 줄이 68 → 34 | **맞음, 더 갔다.** 68 → **0** — 아래 ★ |
| 3 | `ID3D12PipelineState*` 46 이 타입만 갈린다 | **맞음.** 46 → 4(벤치) |
| 4 | 컴파일러가 여섯 번보다 많이 잡는다 | **맞음. 369건이었다** — 아래 ★ |
| 5 | `dx12.psocache` 가 한 번은 깨진다 | **안 깨졌다** — 아래 ★ |
| 6 | `IRenderDeviceServices` 의 DX12 토큰이 안 준다 · 41 → 35 | **틀렸다. 41 → 37 이고, 인터페이스가 오히려 늘었다** — 아래 ★ |

★ **예상 2 가 예상보다 멀리 갔다.** `desc.layout = root;` 한 줄이면 되는데,
그 `root` 가 이미 desc 가 원하는 그 값이라 **대입 자체가 34줄에서 34줄로
유지되는 게 아니라 68줄이 34줄로 접혔다.** 그리고 `rootSignature` 대입도 0 이
됐다 — 필드가 없어졌기 때문이다. V1 잔여 21 · V6 20 과 같은 부류이고 규모가
가장 컸다.

★ **예상 4 — 369건.** V1 잔여·V5 잔여·V6 셋을 합쳐 여섯 번이었던 것이 한
슬라이스에서 369건이다. 그리고 그 전부가 기계적이었다 — **한 건도 설계를
되돌리지 않았다.** 다만 오탐도 나왔다: 정규식이 `unique_ptr<LivePipeline>
pipeline` 을 파이프라인 핸들로 착각해 7곳을 잘못 고쳤고, 컴파일러가 그것도
잡았다. **일괄 치환의 안전망이 컴파일러라는 것이 다시 확인된다.**

★ **예상 5 가 빗나갔고, 빗나간 이유가 요점이다.** 함정(핸들이 실행마다 달라
디스크 캐시 키가 흔들린다)은 실재했지만 §7.2.6 ②에서 미리 짚어 표에 안정
해시를 함께 넣었으므로 깨지지 않았다. **착수 전에 적어 두는 것이 값을 한
자리다** — 안 적었으면 "2회차 컴파일 3"을 보고 원인을 캐시에서부터 찾았을 것이다.

★ **예상 6 이 틀렸다. 두 가지로.**

  ① **수가 −6 이 아니라 −4 다.** `RenderFrameServices.h` 는 23 → 19 가 아니라
    23 → 21 이었다 — 그 헤더의 파이프라인 관련 토큰은 캐시 메서드 둘뿐이라
    애초에 −2 밖에 없었다. 계산을 틀린 것이다.

  ② **인터페이스가 오히려 메서드 하나 늘었다.** 설계 ③은 "표는 구현 클래스에
    두고 인터페이스에는 안 올린다 — 인코더가 이미 들고 있으니 충분하다"였는데,
    **소비자가 하나 더 있었다**: `EnhancedIBLGenerator` 가 그래프 밖에서 원시
    커맨드 리스트에 직접 파이프라인을 건다. 인코더를 안 타므로 인코더의 해소를
    못 쓴다. 그래서 `IRenderDeviceServices::Resolve(RHIPipelineHandle)` 을
    더했다 — `Resolve(RHITextureHandle)` 과 **같은 부류**이고, 그 주석이
    "이 함수가 인터페이스에 있는 것이 과도기의 표시다"라고 적어 둔 그것이다.

    **교훈**: 소비자를 셀 때 "인코더를 타는 자리"만 세면 모자란다. 원시 커맨드
    리스트를 쓰는 자리(A-3)가 언제나 두 번째 소비자다.

**예상하지 않았는데 나온 것 셋:**

★ **A-1 이 없앤 표현이 하나 있다 — "파이프라인 없이 루트만 건다".** 세 곳이
`SetPipeline(bindPoint, nullptr, m_rootSignature)` 로 루트 시그니처만 걸고
루트 인자를 세운 뒤, 루프가 배치마다 진짜 PSO 를 걸었다(데칼 · 그림자 ·
볼류메트릭 포그). 핸들이 짝을 들면서 그 상태가 계약에 없어졌다.

  **없어진 것이 맞다.** `RHIEncoder` ③이 "루트 시그니처를 안 걸고 루트를
  건드린다"를 막았는데 그 이웃에 이것이 남아 있었고, **Vulkan 에는 표현할
  방법이 아예 없다** — 디스크립터 셋을 걸려면 파이프라인이 먼저 걸려 있어야
  하고 레이아웃도 거기서 따라온다. 셋 다 "실제 파이프라인 하나를 먼저 건다"로
  고쳤고, 포그는 그 김에 바인딩 람다가 파이프라인을 인자로 받게 됐다 —
  **거는 순서가 계약에 드러난 것이다.**

★ **자가 검증 하나가 더 강해졌다.** `dx12.psocache` 가 `desc.ComputeHash()` 를
직접 불러 셋이 다른지 봤는데, 해시가 desc 의 멤버가 아니게 되면서 그 검사를
**관측 가능한 결과**로 바꿨다 — 상태만 다른 셋이 서로 다른 **핸들**을 받는가.
해시는 수단이고 판정 대상은 '구분되는가'다. 우연히 더 나은 검사가 됐다.

★ **A-1b 가 한 겹을 더 드러냈다.** 서명을 중립으로 갈고도
`VulkanPipelineCache` 는 여전히 상속하지 못했다 — 선언이
`RHI/DX12/RenderFrameServices.h` 에 있고 그 헤더가 `d3d12.h` 를 물기 때문이다.

  **서명이 중립인 것과 헤더가 중립인 것은 다른 문제다.** §7.2.2 의 문장
  ("반환형이 DX12 면 두 번째 백엔드는 그 캐시를 쓸 수 없다")에 한 겹이 더
  있었다. 두 인터페이스를 `RHI/IRenderPipelineCache.h` 로 옮기고 나서야
  상속이 됐다.

  → **V7 은 정리가 아니라 기능이다.** 그리고 조각으로 할 수 있다는 것이
  함께 확인됐다 — **중립화가 끝난 것만 옮긴다**가 그 규칙이고, 이 파일이 첫
  조각이다.

#### A-1 의 판정 — V8-a 가 건 조건 넷

| §7.2.5 가 건 조건 | 결과 |
|---|---|
| `VulkanTrianglePass` · `VulkanFrameContext` 삭제 | **아직이다** — 아래 ★ |
| `VulkanGraphicsPipelineDesc` 가 접힌다 | **접혔다.** `RHIGraphicsPipelineDesc` 하나다 |
| `VulkanEncoder` 가 산다 | **산다.** `RHIPipelineHandle` 을 받고 캐시로 푼다 |
| `VulkanPipelineCache` 가 인터페이스를 상속한다 | **상속한다.** 둘 다 |

★ **패스는 아직 하나가 안 됐고, 그것이 A-1 의 정직한 상태다.** A-1 은
파이프라인 짝만 갈았고 패스 경로에는 아직 다섯 부류가 남아 있다 —
`SetConstantBuffer` 의 GPU 주소(A-5) · `GetUploadRing`(A-3) ·
`RHIRenderTargetBinding` 의 인덱스 모델 · 바인딩 테이블(A-5) · 그래프.
그 다섯이 남아 있는 한 그리드 패스는 Vulkan 에서 안 돈다.

  즉 `VulkanTrianglePass` 의 폐기 조건은 **A 전체**이지 A-1 이 아니다.
  그 파일의 머리말이 "A 가 끝나면 지운다"라고 적은 것이 맞고, A-1 만으로
  지워질 것처럼 읽힌다면 그것이 오해다 — 여기 적어 둔다.

**남은 부류 (A-2 5 · A-3 8 · A-4 13 · A-5 4 · A-6 7 = 37).** 다음은
§7.2.5 가 정한 대로 **V8-b**(삼각형에 텍스처·샘플러를 물려 A-5 의 자를 만든다)
이거나, 자가 이미 선 **A-2**(V3 어휘를 생성 desc 에 적용 — 기계적)다.

### 7.2.7 A-2 완료 — 생성 desc 의 초기 상태 (2026-08-11)

기계적이라 예상대로 작았다. **5건 · 3파일:**

| 어디 | 무엇 |
|---|---|
| `RHIBufferDesc::initialState` · `RHITextureDesc::initialState` | `D3D12_RESOURCE_STATES` → **`RHIResourceState`** |
| `DX12DeviceResources::CreateBuffer` · `CreateTexture` | `CreateCommittedResource` 인자를 `ToD3D12(desc.initialState)` 로 |
| `EnhancedSSGIPass::EnsureHistory` | 유일한 명시 대입 — `D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE` → `RHIResourceState::ShaderResource` |

★ **미루면서 조건을 적어 둔 것이 또 값을 했다.** R2c-a 가 이 필드를 안 옮긴
이유를 "그 타입이 그래프 헤더에 있고 경계 헤더가 그래프를 끌어오는 것은 방향이
거꾸로다" 라고 적고 **해소 조건까지 함께 적어 두었다.** V3 가
`RHIResourceState` 를 `RHI/` 로 올리면서 그 조건이 사라졌으므로, 다시 판단하지
않고 충족만 확인하면 됐다 — V2-a 가 세대(generation)를 미룬 방식과 같다.

★ **SSGI 의 주석 다섯 줄이 두 줄로 줄었다.** 그 자리는 위쪽
`m_historyState.fill(RHIResourceState::ShaderResource)` 와 아래쪽
`desc.initialState = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE` 가 **같은 값을
서로 다른 어휘로** 적고 있었고, 주석이 "ShaderResource 가 ALL_SHADER_RESOURCE 로
매핑된다" 를 설명하고 있었다. 같은 어휘가 되니 두 줄을 눈으로 대조할 수 있다 —
**설명이 필요 없어진 것이지 설명을 지운 것이 아니다.**

★ **그래프의 `ToD3D12` 는 A-2 가 아니다.** §7.2.5 의 A-2 행이 "그래프 1" 을
포함했는데, 실제로 보니 그것은 `DX12DeviceResources::ToD3D12` 로 위임하는
얇은 래퍼이고(V3 가 두 벌을 만들지 않으려고 그렇게 뒀다) **그래프가 배리어를
직접 조립하는 한 필요하다.** 지우려면 배리어 조립 자체가 백엔드로 가야 하므로
그 몫은 §8.5 의 6번(그래프 실행 중립화)이다. A-2 의 실측이 5건인 것은 그래서다.

#### 검증 — 그리고 자가 하나 틀렸다

전후를 **각각 빌드해** 같은 하네스로 35종을 돌렸다. 솔루션 Debug 오류 0 ·
새 경고 0(남은 14건은 전부 C# 쪽이고 이 변경과 무관하다).

```
A-2 전  통과 27 · 완료 4 · 실패 2 · 어서션 1 · 무판정 1
A-2 후  통과 27 · 완료 4 · 실패 2 · 어서션 1 · 무판정 1   ← 판정 줄 차이 0
```

★ **그런데 이 수가 §7.4 의 기준선(28 통과)과 다르다.** 원인을 갈라 보니
**하네스의 자였다** — 검사마다 `<검사>` · `wait 10` · `quit` 만 줬는데,
`dx12.gizmoscene` 은 §6.2 가 적은 대로 **에디터 씬의 살아 있는 카메라**를 쓴다.
워밍업 없이 부르면 점등 0 으로 실패하고, `wait 240` 을 앞에 두면 **점등 6600 ·
통과**다. 즉 회귀가 아니라 재는 방법이 달랐다.

  ★ **이것이 이 계획서에서 세 번째로 같은 부류다.** ① 검사 목록을 CLI 도움말에서
  뽑아 35 중 26 만 돌던 것(R2c-a) ② 판정 어휘를 둘로만 읽어 계측을 실패로
  센 것(같은 자리) ③ 이번 — 워밍업 유무. **매번 원인은 하나다: 검사 하네스가
  저장소에 없어 세션마다 새로 짜고, 그때마다 자가 달라진다.**
  `Tools/dx12-validation/` 은 `dx12.selftest` 하나만 돌고 있었다.

  → **그래서 하네스를 저장소에 넣었다**(`Tools/dx12-validation/Invoke-Dx12Suite.ps1`).
  셋을 구조로 막는다: 목록을 소스의 `cmd == "dx12.*"` 에서 뽑고, 판정을
  `[CLI] <검사> <판정>` 형태로만 읽고, 워밍업을 기본 240 프레임 준다.
  그 하네스로 다시 재니 **통과 28 · 완료 4 · 실패 2 · 무판정 1** 로 §7.4
  기준선과 글자까지 같다. `dx12.scene` 의 어서션도 워밍업 부족이었다 —
  워밍업을 주면 자기 전제조건(메시 0개)으로 정상 실패한다.

  이번에 밟은 것도 적어 둔다 — 판정을 `통과|실패|완료` 로 문자열 검색했더니
  `dx12.scene` 의 **진행 마커**(`[1/4] 씬 입력 확보 완료`)가 판정으로 잡혔다.
  판정 줄은 `[CLI] <검사> <판정>` 하나뿐이므로 그 형태로만 읽어야 한다.

**A-2 판정이 이 자 차이에 흔들리지 않는 이유**: 전후를 **같은 하네스로** 쟀고
차이가 0 이다. 자가 문서와 다른 것은 절대값을 문서 기준선과 비교할 때만 문제가
된다.

### 7.2.8 V8-b 설계 — 삼각형에 텍스처와 샘플러를 물린다 (착수 전 기록, 2026-08-11)

§7.2.5 가 **A-5 를 여기 뒤로 미뤘다** — "지금 자르면 소비자 하나로 설계하는
자리이고, §1.1 이 구 RHI 의 사인으로 지목한 조건이다." 그 자를 세우는 것이
이 슬라이스의 전부다. 산출물은 여전히 **'통과'가 아니라 '어디서 안 맞는가'**다.

#### 왜 텍스처와 샘플러인가

A-5 의 네 자리는 `RHIBindingTable`·`RHISamplerTable` 의 GPU 핸들 둘과
`SetConstantBuffer`·`SetRootBuffer` 의 GPU 주소 둘이다. V8-a 가 때린 것은
마지막 부류 하나뿐이고, **테이블 둘은 소비자가 없다.** 삼각형이 텍스처 하나를
샘플러 하나로 읽으면 넷 중 셋이 실물이 된다.

#### 범위

- 셰이더에 `Texture2D t0` · `SamplerState s0` 를 더하고 색에 곱한다.
- 레이아웃에 `RHILayout::Table`(SRV 1) 과 정적 샘플러 하나를 더한다 —
  지금 `VulkanPipelineCache` 가 둘 다 **실패로 멈춘다**(V8-a 가 일부러 그렇게
  뒀다: "안 쓰는 종류를 옮기면 틀려도 아무도 모르는 대응표가 된다").
- 삼각형이 자기 텍스처를 만들어 올린다(체커보드). `IRenderTextureCache` 는
  반환형이 `DX12TextureEntry` 라 Vulkan 이 구현할 수 없다.
- 판정은 V8-a 와 같은 규율 — **음성 대조를 함께 잰다.** 텍스처를 안 걸어도
  삼각형은 그려지므로, 그것을 재지 않으면 이 경로가 조용히 틀린 채 통과한다.

**범위 밖**: 프레임마다 바뀌는 상수(§8.5 가 여기 묶었으나 뺀다). 그것은 업로드
링의 자이고 링은 A-5 본체에서 실물을 본다 — 여기서 흉내 내면 흉내만 남는다.

#### 미리 적어 두는 예상 (나중에 맞았는지 대조한다)

1. **`shaderRegister` 를 binding 번호로 그대로 쓰는 대입이 깨진다.** V8-a 가
   그 줄에 "b 하나뿐이라 성립한다 — 성립하는 이유를 적어 둔다"고 달아 두었고,
   §7.2.5 의 예상 8 이 "이번에 안 드러난다"고 미뤄 둔 자리다. `b0` 과 `t0` 이
   둘 다 0 이 되어 충돌한다. **이것이 이 슬라이스의 주된 산출물이 될 것이다** —
   DX12 는 b·t·u·s 가 각각 별개 이름공간이고 SPIR-V 는 binding 하나뿐이라,
   **레지스터 공간 매핑 규약이 계약에 있어야 한다.** dxc 의
   `-fvk-{b,t,u,s}-shift` 와 백엔드가 **같은 값**을 알아야 하고, 그 값이
   어디에 적히는지가 새 설계 문제다.
2. `RHIStaticSamplerDesc`(V4)를 고칠 것 없이 쓴다. V4 가 `D3D12_FILTER` 를
   펴서 `magFilter`·`minFilter`·`mipmapMode`·`compareEnable` 로 나눈 판단이
   옳았다면.
3. **정적 샘플러에서 수명 비대칭이 또 나온다.** DX12 는 루트 시그니처 안에
   값으로 들어가 객체가 없는데, Vulkan 은 `VkSampler` 객체를 만들어
   `pImmutableSamplers` 에 넘겨야 하고 그 객체를 누군가 파괴해야 한다.
   캐시가 소유하게 될 것이고, 그러면 A-1 이 파이프라인에서 겪은 것과 같은
   부류가 하나 더 는다.
4. 텍스처 업로드 경로가 통째로 없다 — 스테이징 버퍼 · 레이아웃 전이 두 번 ·
   복사. **그 자리가 A-4(리소스 포인터)의 자를 만든다**: V2 의 표는
   `DX12ResourceTable` 뿐이므로 `RHITextureHandle` 을 Vulkan 이 풀 수 없다.
5. 디스크립터 셋이 둘로 늘거나(셋 0=CBV, 셋 1=SRV) 한 셋에 바인딩이 둘이 된다.
   `slot` 의 뜻이 또 갈릴 것이다 — V8-a 가 "DX12 는 루트 파라미터 번호이고
   여기서는 셋 번호"라고 적어 둔 그 자리다.
6. 픽셀 판정에 축이 하나 는다. 지금은 중앙 한 점으로 상수가 닿았는지 보는데,
   텍스처는 **위치에 따라 달라야** 의미가 있다(체커보드를 쓰는 이유). 한 점만
   보면 텍스처가 단색으로 걸려도 통과한다.

#### V8-b 완료 (2026-08-11)

```
[3/5] 패스 경로 삼각형 기록 통과 — 레이아웃·파이프라인 캐시 경유 (구움 1 · 재사용 0)
[4/5] 픽셀 검증·PNG 저장 통과 — 중앙(0,8,0) 구석(13,13,38) 최명 255 텍스처 계단 88%
Vulkan 골격 검증 통과 — 검증 레이어 클린
```

솔루션 Debug 오류 0 · 새 경고 0. **음성 대조도 재 뒀다** — `SetBindTexture(false)`
로 돌리면 `최명 0 · 계단 0%` 로 떨어져 [4/5] 가 실패한다. 안 걸린
`SAMPLED_IMAGE` 를 읽으면 이 드라이버는 0 을 주므로 삼각형이 통째로 검어진다
(V8-a 의 상수 버퍼와 같은 모양이다).

**예상 대조** (§7.2.8 에 착수 전 적어 둔 것):

| # | 예상 | 결과 |
|---|---|---|
| 1 | `shaderRegister` 를 binding 에 그대로 쓰는 대입이 깨진다 | **맞음.** `b0`·`t0`·`s0` 가 전부 0 이 됐다. 규약을 `VulkanBindingModel.h` 한 벌로 두고 **빌드 스크립트가 그 헤더에서 읽어 간다** |
| 2 | `RHIStaticSamplerDesc`(V4)를 고칠 것 없이 쓴다 | **맞음, 그리고 예상보다 세게** — 아래 ★ |
| 3 | 정적 샘플러에서 수명 비대칭이 나온다 | **맞음, 그리고 하나 더 나왔다** — 예산 비대칭. 아래 ★ |
| 4 | 텍스처 업로드 경로가 통째로 없다 | **맞음.** 약 150줄(스테이징·전이 둘·복사·일회성 제출). DX12 는 `textureCache->GetOrUpload` 한 줄이다 |
| 5 | 셋이 둘로 늘거나 한 셋에 바인딩이 둘이 된다 · `slot` 의 뜻이 또 갈린다 | **앞은 맞고 뒤는 틀렸다.** 한 셋에 바인딩 셋이 됐고, `slot` 은 안 갈렸다 — 구간 시프트가 종류를 가르므로 셋 번호를 늘릴 이유가 없었다 |
| 6 | 픽셀 판정에 축이 하나 는다 | **맞음, 그리고 기존 판정을 흔들었다** — 아래 ★ |

★ **예상 2 — V4 가 어휘를 최소로 뽑아 둔 것이 여기서 값을 했다.** 대응표를
쓰다가 `RHIAddressMode::Mirror` · `RHICompareOp::Never` · `Equal` · `Greater` 를
적었는데 **전부 없는 이름이라 컴파일이 멈췄다.** 실제 어휘는 주소 모드 셋 ·
비교 함수 셋뿐이다(V4 가 "25곳이 쓰는 값이 놀랄 만큼 적다"고 적은 그 결과).
D3D12 열거를 통째로 옮겨 왔다면 절반이 소비자 없이 남았을 것이고, 그 절반은
**틀려도 아무도 모르는 대응표**가 된다.

★ **예상 3 — 검증 레이어가 내 가정을 반증했다.** "정적 샘플러는 셋 레이아웃에
구워져 있으니 풀에서 잘라 오지 않는다"고 적고 풀 예산에서 뺐는데:

```
binding 2 was created with VK_DESCRIPTOR_TYPE_SAMPLER but VkDescriptorPool
was not created with any VkDescriptorPoolSize::type with VK_DESCRIPTOR_TYPE_SAMPLER
```

불변 샘플러여도 셋 안의 **자리**는 차지한다. DX12 는 정적 샘플러가 디스크립터
힙을 아예 안 쓰므로 대응이 없다 — 수명 비대칭에 이어 **예산 비대칭**이다.

  ★ **이 드라이버는 할당을 성공시켰고 픽셀도 맞게 나왔다.** §7.2.2 가 "검증
  레이어가 없는 채로 세우면 안 된다 — 잘못된 계약이 조용히 통과하고 골격이
  '맞다'고 거짓 보고한다"고 적은 그 자리를 실제로 밟았다.

★ **예상 6 — 텍스처가 들어오자 기존 판정이 흔들렸다.** 중앙 픽셀이 체커보드의
어두운 칸에 걸려 G 가 64 → **8** 로 떨어졌고(32/255 를 곱한 값 그대로),
"그려지긴 했는가"(`center[1] > 40`)가 실패했다. 판정이 재려는 것과 무관한
**칸 배치**에 흔들린 것이다. 표본을 '중앙 한 점'에서 **'삼각형 안에서 가장
밝은 점'**으로 바꿨다 — 배치와 무관해진다.

  그리고 계단을 **절대값으로 재면 안 됐다.** 처음에 `차이 > 40` 으로 두었더니
  15 가 나와 실패했는데, 초록 보간이 약한 자리를 훑었기 때문이다. 체커 경계의
  비율은 자리와 무관하게 약 87% 이므로 **비율로 잰다**(실측 88%).

**A-5 의 자 — 이 슬라이스가 얻으려던 것:**

| 잰 것 | DX12 | Vulkan |
|---|---|---|
| 테이블 하나 | 루트 파라미터 **1개**(힙 안의 연속 구간을 가리키는 GPU 핸들 하나) | 셋 레이아웃의 binding **N개**. 한 파라미터가 N 으로 펼쳐진다 |
| 디스크립터 예산 | 힙 하나에 CBV/SRV/UAV 를 섞어 담는다 | **종류마다** 개수를 풀 만들 때 말해야 한다 |
| 정적 샘플러 | 루트 시그니처 안에 값. 객체도 예산도 없다 | `VkSampler` 객체 + 셋 예산 둘 다 |
| 레지스터 이름공간 | b·t·u·s 가 각각 별개 | 하나. 구간 시프트가 필요하다 |

★ **즉 `RHIBindingTable`(GPU 핸들 하나 + count)은 그대로 안 간다.** A-5 는
"핸들 타입을 바꾸는 일"이 아니라 **테이블 모델을 바꾸는 일**이고, 이것은
§7.2.5 가 `RHIRenderTargetBinding` 을 두고 "핸들화가 아니라 모델 교체"라고
가른 것과 같은 부류다. **자를 세우고 나서야 그것이 보였다** — 미룬 판단이 옳았다.

**덤으로 드러난 것:**

★ **로더가 수동이라 새 API 마다 등록이 필요하다.** `vkCreateSampler` ·
`vkDestroySampler` · `vkCmdCopyBufferToImage` 셋을 `VK_DEVICE_FUNCTIONS` 에
더했다. `vulkan-1.lib` 를 링크하지 않는 판단(§7.2.2)의 대가이고, 대가로는
싸다 — 빠뜨리면 **컴파일 오류**라 조용히 틀릴 자리가 없다.

★ **동기화 2 를 골격 전체가 쓴다.** 처음에 `vkCmdPipelineBarrier` ·
`vkQueueSubmit`(1 세대)을 썼다가 로더에 없어서 드러났다. 한 파일만 구 API 를
쓰면 스테이지·접근 마스크의 어휘가 두 벌이 된다 — 로더가 그것을 막은 셈이다.

### 7.3 지금까지의 R 슬라이스와의 관계

R1~R5는 버려지지 않는다. 그것들이 만든 것이 V의 토대다:

| 남긴 것 | V에서의 쓰임 |
|---|---|
| `RHIEncoder`(R3) | 명령 기록 어휘. Vulkan은 커맨드 버퍼로 구현이 갈릴 뿐 계약은 같다 |
| `RHIBindingTable`·`CreateBindings`(R2a) | 디스크립터 셋의 전신. V4가 이것을 레이아웃과 짝지어 완성한다 |
| `RHIRenderTargetBinding`(R2b) | 렌더 패스·프레임버퍼의 전신 |
| `RHIReadback` 계열(R2c) | 그대로 산다. 포맷만 V1에서 갈린다 |
| `IRHIDeviceResources`(D1) | Vulkan 구현이 들어올 자리. D1이 "그 자리를 만들 뿐"이라 적어 둔 것이 여기서 청구된다 |

**R4-3는 V2에 흡수된다.** 그래프 서명을 포인터로 바꿔 봐야 Vulkan에서 또 바꿔야
하므로, 한 번에 핸들로 간다.

### 7.4 완료 기준 (§6 대체)

1. `RenderEngine/Render/` 아래 어떤 파일도 `d3d12.h`·`vulkan.h`를 include하지 않는다.
2. **패스 17종 전체**(`Initialize` 포함)에 `ID3D12`·`D3D12_`·`DXGI_` 직접 참조 0건.
3. `dx12.live status`의 패스 이름 목록이 착수 전과 문자 그대로 같다.
4. 자가 검증 **35종** 판정이 착수 전과 같다.

   **기준선 (2026-08-11 실측): 28 통과 · 4 계측 · 2 실패 · 1 무판정.**

   ★ **이 값은 워밍업을 갖춘 스윕의 것이다(§7.2.7).** 검사 앞에 프레임을
   돌리지 않으면 `dx12.gizmoscene` 이 점등 0 으로 실패해 **27 통과**가 나온다 —
   그 검사는 에디터 씬의 살아 있는 카메라를 쓰기 때문이다(§6.2). 절대값을 이
   기준선과 견줄 때는 **자가 같은지 먼저 확인해야 한다.**

   | | |
   |---|---|
   | 실패 `dx12.scene` | 열린 씬에 메시가 0개다. `dx12.live` 도 `드로우 — 풀 0개` 로 같은 것을 본다 — 코드가 아니라 리소스 부재다(§6.2) |
   | 실패 `dx12.bench11` | `_DEBUG` 에서 설계된 거부다. "검증 레이어가 켜져 비교가 성립하지 않는다 · Release 로 재야 한다" — 셰이더 컴파일 **전에** 막힌다 |
   | 무판정 `dx12.live` | 상태를 찍는 것이라 통과/실패 줄을 내지 않는다 |

   ★ 이전 기준선 "33종 · 1 실패"는 R2c-a 시점(`fea20c71`) 값이고 그 뒤로
     낡아 있었다. 실측한 차이는 **+5 · −3** 이다:

     - 늘었다 — `dx12.bench11` · `dx12.encoderbench` · `dx12.fog` ·
       `dx12.live` · `dx12.ssr`
     - 없어졌다 — `dx12.compare` · `dx12.sharedtexture` · `dx12.skyscene`

     실패가 둘이 된 것은 그중 `dx12.bench11` 이 Debug 에서 항상 거부하기
     때문이다. **회귀가 아니다** — 기준선을 읽는 사람이 "실패 2건"을 보고
     놀라지 않도록 사유를 여기 적어 둔다.

   ★ 판정 줄의 의미가 §7.2.3 에서 넓어졌다. "셰이더 컴파일 통과"가 이제
     "파일을 찾았고 컴파일된다"이다 — 배포가 빠지면 여기서 잡힌다.
5. `dx12.live status`의 CPU ms가 유의미하게 늘지 않는다.
6. **Vulkan 백엔드가 같은 패스 코드로 삼각형 하나를 그린다** — 이것이 없으면
   나머지 다섯이 전부 "그럴듯한 추상"이다.

★ **6번이 나머지 다섯보다 크다는 것을 §8 이 지표로 승격시킨다(2026-08-11).**
1·2·4번은 전부 "DX12 가 얼마나 사라졌나"를 재고, 그것은 백엔드 **하나**를
가리는 진척이다. 목표가 멀티백엔드이므로 재는 자를 바꾼다 — §8.4.

---

## 8. 우선순위 재산출 (2026-08-11)

목표는 **멀티백엔드 RHI** 다. 그 자로 §7 의 남은 순서를 다시 재고, 이 계획서가
세지 않고 있던 표면을 채운다.

★ **이 절을 쓰는 동안 같은 트리에서 V8-a 와 A-1 이 나갔다(§7.2.5 · §7.2.6).**
`MaterialPipelinePlan` §0 이 겪은 것과 같은 일이고, 처리도 같게 한다 — 틀린
것은 정정하고 살아남은 것은 그대로 둔다. 갈린 자리:

| §8 이 적은 것 | 지금 |
|---|---|
| W0 — Vulkan 스텁을 컴파일시켜 '구현 불가 서명' 목록을 얻는다 | **소멸.** V8-a 가 스텁이 아니라 **실물**로 같은 것을 했고 더 많이 냈다(인코더 실질 잔량이 14 가 아니라 17/24 · 파이프라인 기술은 21필드 중 1필드만 갈린다) |
| W1 — 파이프라인 객체·기술 95 | **A-1 이 실행했다**(369건). 그리고 §8 이 "95" 라 센 것보다 컸다 |
| W4 — `EnhancedGridPass` 가 두 백엔드에서 돈다 | **A 의 완료 조건으로 승격됐다**(§7.2.5) — `VulkanTrianglePass` 가 삭제되는 것이 그 판정이다 |
| W2 — 업로드 링을 W1 다음에 | **자리가 틀렸다.** §7.2.5 가 A-5(디스크립터·GPU 주소)를 V8-b 뒤로 미룬 것과 같은 이유다 — 지금 자르면 소비자 하나로 모양을 정한다 |

**살아남은 것은 §8.3 의 표면 셋과 §8.4 의 지표다.** 셋 다 §7.2.5 의 A-1~A-6
부류에 자리가 없다 — 그 표는 **V7 을 막는 헤더 41건**을 부류로 묶은 것이라
헤더에 안 나오는 호출부는 세지 않는다. §8.5 를 그 결과로 갈아 끼웠다.

### 8.1 왜 다시 재는가 — 지표가 목표를 재고 있지 않다

V1~V6 이 끝나면서 계획서의 모든 표가 **"DX12 심볼이 몇 건 남았나"** 를 센다.
그 수가 0 이 돼도 답이 안 나오는 질문이 하나 있다 — **두 번째 백엔드가 도는가.**

지금 상태가 그 간극을 그대로 보여 준다:

| | §8 착수 시점 | V8-a · A-1 뒤 |
|---|---|---|
| Vulkan 백엔드 코드 | 2,023줄 | 약 3,700줄 |
| Vulkan 이 구현한 경계 인터페이스 | 7종 중 **1종** (`IRHIDeviceResources`) | 7종 중 **3종** (+`IRenderPipelineCache` · `IRenderRootSignatureCache`) |
| 두 백엔드가 공유하는 패스 | 17종 중 **0종** | **0종** (`VulkanTrianglePass` 는 계측기다 — §7.2.5) |
| 두 백엔드를 대조하는 검사 | **0개** | **0개** (`vk.selftest` 는 자기 픽셀만 본다) |

★ **V8-a 뒤에도 아래 둘이 0 이라는 것이 이 지표를 쓰는 이유다.** 인터페이스가
1 → 3 으로 는 것은 진짜 진척이지만, 그것만 보면 "3/7 만큼 왔다"로 읽힌다.
실제로 두 백엔드가 **같은 코드로** 그린 적은 아직 없고, `VulkanTrianglePass`
가 있는 상태는 §7.2.5 가 적은 대로 **틀린 구조**다. 그 판정을 지표가 스스로
말하게 두는 것이 §8.4 다.

### 8.2 벽 목록 — 패스가 두 번째 백엔드를 타려면 (실측)

패스 파일 41개(`Enhanced*{Pass,Generator,Shaders,LightPacking}.{h,cpp}`)를
**프로덕션 구간과 파일 내 자가 검증 블록으로 갈라** 셌다. 가르는 자는
`EnhancedSceneRenderer::Run*` 정의가 시작되는 줄이다.

**프로덕션 224건 · 자가 검증 블록 51건.**

★ **가르지 않으면 이 표가 통째로 틀린다.** 자가 검증은 `DX12DeviceResources`
를 직접 세우는 DX12 자기 검사이고(V3·V4 가 같은 근거로 43곳·17곳을 남겼다),
Vulkan 이 그것을 탈 이유가 없다. 안 가르고 세면 `GetDevice()` 가 17건으로
잡히는데 프로덕션은 **6건**이다.

| 벽 | 프로덕션 | 지금까지의 분류 | Vulkan 에서 |
|---|---|---|---|
| ~~파이프라인·루트시그 **객체**~~ (`ID3D12PipelineState` 49 · `ID3D12RootSignature` 21) | ~~70~~ | §7.2.4 의 A — 그러나 경계 헤더 50건만 셌다 | **A-1 이 걷었다**(369건 · §7.2.6) |
| 파이프라인 **기술 타입** (`DX12GraphicsPipelineDesc` 15 · `DX12ComputePipelineDesc` 10) | **25** | V6 가 *어휘*만 옮겼다 — 그것을 담는 구조체는 DX12 | 남은 거리 **1/21 필드**(§7.2.5) — A 완료에서 하나로 접힌다 |
| **업로드 링** (`GetUploadRing()`) | **38** | **어디에도 없다** | 스테이징 버퍼 + `VkDescriptorBufferInfo` |
| 구현 클래스 이름 (`DX12DeviceResources`) | 18 | — | 인터페이스여야 할 자리 |
| 기타 `D3D12_`·`DXGI_` 원시 | 41 | — | 33건이 `EnhancedIBLGenerator.cpp` 하나 |
| 셰이더 매크로 (`D3D_SHADER_MACRO`) | 10 | M2(머테리얼 계획) | DXC 정의 인자 |
| 리소스 포인터 (`ID3D12Resource`) | 8 | V2 잔여 | 핸들 |
| 원시 탈출구 (`GetDevice()` 6 · `GetCommandList()` 2) | 8 | R2c-a 가 "프로덕션 0" 이라 했다 | 없어야 한다 |
| 디스크립터·샘플러 힙 직결 (`GetDescriptorRing()` 3 · `GetSamplerHeap()` 3) | 6 | — | 디스크립터 풀 |

그 밖의 표면:

| 곳 | 건수 | 성격 |
|---|---|---|
| 경계 헤더 (`RenderFrameServices.h` 23 · `RHIEncoder.h` 10) | 33 | 서명이 DX12 — Vulkan 이 `override` 할 수 없다 |
| 그래프 (`EnhancedRenderGraph.{h,cpp}` 8 + 27) | 35 | 아래 §8.3 ② |
| 씬 러너 (`EnhancedSceneRenderer*.{h,cpp}`) | 94 | 아래 §8.3 ③ |

### 8.3 이 계획서가 세지 않고 있던 표면 셋

★ **① 업로드 링 38건.** §3.2 가 `RHIFrame::UploadConstants` / `RHIBufferSlice`
로 예고했으나 도입된 적이 없고, §7.1 의 742건 표에도 항목이 없다. 실제 형태는
이렇다:

```cpp
const auto cb = context.resources->GetUploadRing().Allocate(...);   // 패스 38곳
// Allocation { void* cpuAddress; D3D12_GPU_VIRTUAL_ADDRESS gpuAddress; ID3D12Resource*; ... }
```

인터페이스(`IRenderDeviceServices`)가 **구현 클래스 참조를 돌려주고**, 그
반환값이 DX12 주소 타입을 들었다. §1.4 가 "패스가 백엔드 구현 클래스를 안다"
고 지목한 그 상태가 형태만 바뀌어 남아 있다 — R1 이 `EnhancedFrameContext`
에서 걷어낸 다섯 중 셋(`UploadRing` · `DescriptorRing` · `SamplerHeap`)이
인터페이스의 게터로 되살아났다. **포맷 145건이 어느 슬라이스에도 없었던 것
(§7.1)과 같은 부류의 누락이고, 크기는 그때보다 작지만 자리는 더 나쁘다**
— 드로우 루프 안쪽이라 성능 계약이 걸린다.

★ **② 그래프는 "정당한 백엔드 자리"가 아니다.** §7.2.4 가 `EnhancedRenderGraph.cpp`
의 27건을 "백엔드 구현이라 DX12 타입이 있는 것이 정당" 이라고 적었는데,
**그래프는 §7.4 완료 기준 1번이 `Render/` 로 옮기라고 한 상위 개념이다.**
서명 넷이 DX12 를 받는다:

```
Compile(ID3D12Device*)                                  ← transient 생성
Execute(ID3D12GraphicsCommandList*)                     ← 배리어 기록
ExecuteParallel(DX12CommandListPool&, ID3D12CommandQueue*)
CreateTransients(ID3D12Device*)
```

내용을 보면 셋 다 이미 인터페이스가 있는 일이다 — transient 생성은
`IRenderDeviceServices::CreateTexture`(R2c-a), 배리어는 `TransitionResources`(V3).
**그래프만 그 인터페이스를 안 쓰고 손으로 한다.** 정당한 잔량이 아니라 아직
안 내려간 자리다. 이대로면 Vulkan 은 렌더 그래프 없이 패스를 돌려야 하고,
그것은 성립하지 않는다.

★ **③ 러너가 DX12 전용이다(94건).** 패스가 전부 중립이 돼도 **프레임을 도는
코드가 DX12 면 Vulkan 경로가 없다.** 그리고 백엔드 선택 배선이 지금 없다 —
`render.backend` 는 `dx11` 이 dead code 가 되면서 **dx12 고정**이 됐다
(`ConsoleCommandSystem.cpp:1887`). 멀티백엔드는 스위치와 폴백이 있어야 성립하고,
없는 기계에서 조용히 DX12 로 가야 한다는 규칙은 이미 골격이 적어 뒀다
(`vulkan-1.lib` 를 링크하지 않는 이유, §7.2.2).

### 8.4 지표 전환

| 지금 재는 것 | 문제 | 바꿀 것 |
|---|---|---|
| 패스의 DX12 심볼 수 | 백엔드 **하나**를 가리는 진척. 0 이 돼도 두 번째가 도는지 모른다 | ① Vulkan 이 구현한 경계 인터페이스 **3/7** |
| 자가 검증 35종 판정 불변 | DX12 회귀만 잡는다 | ② 두 백엔드가 공유하는 패스 **0/17** |
| `Render/` 에 `d3d12.h` 0 | 폴더 위치는 계약이 아니다 | ③ 두 백엔드 **픽셀 대조** 검사 수 **0** |

기존 지표를 버리지 않는다 — 회귀 방어는 그것이 한다. **다만 진척 보고는
새 셋으로 한다.** "DX12 심볼 177 → 100" 은 목표에 가까워졌다는 증거가 아니다.

### 8.5 재산출한 순서

방향(V8 먼저)은 §7.2.4 의 것을 유지한다. 바꾸는 것은 **단위**다 — 슬라이스를
"심볼 종류"가 아니라 **수직 슬라이스**(패스 하나가 두 백엔드에서 도는 것)로
자른다. 이유는 §8.1 이다: 종류별로 자르면 매번 소비자가 하나뿐인 채로
모양을 정하게 되고, 그 판단이 맞았는지는 맨 뒤에서야 드러난다.

★ **그리고 "소비자가 설 때까지 자르지 않는다"를 §7.2.5 와 같게 지킨다.**
A-5 를 V8-b 뒤로 미룬 판단이 §8 의 W2 에도 그대로 적용된다 — 업로드 링을
지금 자르면 소비자가 DX12 하나뿐인 채로 모양을 정한다.

**A-1 완료 시점의 순서** (§7.2.5 의 A-2~A-6 과 §8.3 의 표면 셋을 한 줄에 세운 것):

| # | 슬라이스 | 크기 | 판정 |
|---|---|---|---|
| ✔ | **V8-a**(§7.2.5) · **A-1**(§7.2.6) | 369 | 완료 |
| ✔ | **A-2** 상태 어휘 잔여 — `initialState` 를 `RHIResourceState` 로 | 5 | 완료(§7.2.7). 판정 줄 차이 0 |
| ✔ | **V8-b** — 삼각형에 텍스처 하나 · 샘플러 하나를 물린다 | — | 완료(§7.2.8). A-5 의 자가 섰다 — **`RHIBindingTable` 은 핸들화가 아니라 모델 교체다** |
| 3 | **A-5 + 업로드 링** — 디스크립터·GPU 주소 4 + **`GetUploadRing()` 38** | **42** | 아래 ★ (§8.3 ①). **성능 계약이 걸린 유일한 슬라이스** |
| 4 | **A-3 · A-4 · A-6** — 커맨드 리스트 8 · 리소스 포인터 13 · 원시 구조체 7 | 28 | 자가 이미 있다(인코더 · V2 의 표) |
| 5 | **A 완료** — `VulkanTrianglePass`·`VulkanFrameContext` 삭제, `EnhancedGridPass` 가 두 백엔드에서 돈다 | — | §7.2.5 가 못 박은 조건 넷. `vk.grid` 가 `dx12.grid` 와 같은 픽셀 판정 |
| 6 | **그래프 실행 중립화** — `Compile`·`Execute`·`ExecuteParallel`·`CreateTransients` | 35 | §8.3 ②. 완료 기준 6("같은 패스 코드")은 그래프를 타야 성립한다 |
| 7 | **나머지 패스 16종 + IBL 생성기** | ~130 | 패스마다 `vk.*` 대조 |
| 8 | **러너·배선** — 오케스트레이션 중립화 + `render.backend vulkan` + 로더 없음 폴백 | 94 | §8.3 ③. 에디터가 Vulkan 으로 뜬다 |
| 9 | **V7** — 상위 개념을 `RenderEngine/Render/` 로 | 헤더 37 | 이동이 아니라 중복 제거(§7.2.5 의 `VulkanBindPoint` 베낌) |

★ **3번이 §8 의 실질 기여다 — A-5 의 몸통은 헤더 4건이 아니라 42건이다.**
§7.2.5 는 A-5 를 "`RHIBindingTable`·`RHISamplerTable` 의 GPU 핸들 2 ·
`SetConstantBuffer`·`SetRootBuffer` 의 GPU 주소 2" 로 셌는데, 그 **인자를 채우는
자리**가 패스 38곳의 `GetUploadRing().Allocate()` 다. V2-b 가 "desc 31곳"을
131곳으로 다시 세면서 적어 둔 반성이 그대로 재발했다 — **세는 단위가 접촉면일
때는 늘 "누가 이것을 채우는가"를 함께 물어야 한다.** 헤더만 세면 4 이고
호출부까지 세면 42 다.

★ **그래서 3번은 성능 계약이 걸린다.** 드로우당 링 `Allocate` 가 ~175ns 라
루프 안에서 부르면 안 되고, 지금 코드는 조각당 블록 할당으로 DX11 대비 기록
5.5배 우위를 만들어 둔 상태다. 중립화가 그 패턴을 못 담으면 우위가 사라진다 —
**`dx12.encoderbench` · `dx12.bench11`(Release) 를 전후로 돌리는 것이 완료
조건이다.** 픽셀 대조는 비용을 못 잰다(V2-c2 회귀의 교훈).

★ **5번의 대상이 `EnhancedGridPass` 인 것은 실측이다.** 프로덕션 3건 · 헤더
2건으로 패스 17종 중 가장 얇고(다음이 SSR·SSS·SkyBox 각 5), 이미
`dx12.grid` 가 **리드백 픽셀 판정**을 낸다(점등 9840 · 15.0% · 원점 선 R 0.225,
R2c-b1 기준선). 즉 대조할 자가 이미 있다. §7.2.5 가 A 의 완료 조건으로 같은
패스를 지목한 것과 근거가 같다.

### 8.6 판정 — 두 백엔드 픽셀 대조

A 완료(§8.5 의 5번)부터의 판정은 **같은 패스 코드 · 같은 HLSL 로 두 백엔드가
같은 픽셀을 내는가** 다. 형태는 이미 있다 — `dx12.compare` 가 DX11↔DX12 로 하던 일이고
(해석기하 판정 · 범인 bitmask), 그 문제의식(정밀도 기전 차이를 회귀로 세지
않는 법)이 그대로 쓰인다.

두 백엔드가 **정당하게** 다를 수 있는 자리를 미리 적어 둔다 — 여기서 차이가
나면 결함이 아니다:

| 자리 | 왜 |
|---|---|
| 뷰포트 Y 방향 | Vulkan 은 클립 공간 Y 가 뒤집혀 있다(`VK_KHR_maintenance1` 음수 높이로 맞춘다) |
| 깊이 범위 | DX 는 [0,1], Vulkan 코어도 [0,1] — 다만 `VK_EXT_depth_clip_control` 이 없으면 클립 동작이 갈린다 |
| 부동소수 마지막 비트 | 드라이버 최적화 차이. `dx12.compare` 가 쓰는 허용 대역을 그대로 쓴다 |
| 밉 필터링 · 비등방 | 구현 정의. 대조 셰이더에서 밉을 고정한다 |

★ **이 목록을 착수 전에 적는 이유**: 안 적으면 첫 차이가 났을 때 "Vulkan 이
틀렸다"와 "원래 다르다"를 가를 근거가 없고, 그때 가서 만든 근거는 결과에
맞춘 것이 된다.

### 8.7 §7.2.5 의 A 부류와의 관계

§8 이 A 부류를 대체하지 않는다. **부류는 그대로 두고 A-5 의 크기만 고치며,
부류 표에 자리가 없는 셋을 뒤에 세운다.**

| §7.2.5 의 부류 | §8 에서 |
|---|---|
| A-1 파이프라인 짝 | 완료(§7.2.6) |
| A-2 상태 어휘 · A-3 커맨드 리스트 · A-4 리소스 포인터 · A-6 원시 구조체 | 그대로. §8.5 의 1·4번 |
| **A-5 디스크립터·GPU 주소 4** | **+ 업로드 링 38 = 42.** 자를 시점은 §7.2.5 대로 V8-b 뒤 |
| — | **그래프 실행 35 · 러너 94 · 백엔드 선택 배선** 을 새로 세운다(§8.3) |

§7.4 완료 기준에 둘을 더한다:

7. **경계 인터페이스 7종을 Vulkan 이 전부 구현한다** — `IRHIDeviceResources` ✔ ·
   `IRenderPipelineCache` ✔ · `IRenderRootSignatureCache` ✔ ·
   `RHIEncoder` · `IRenderDeviceServices` · `IRenderMeshCache` · `IRenderTextureCache`.
8. **패스 17종이 두 백엔드에서 같은 픽셀을 낸다** — `vk.*` 대조 검사가
   `dx12.*` 와 짝을 이룬다.

★ 6번("같은 패스 코드로 삼각형 하나")은 §8.5 의 5번(= A 완료)에서, 7·8번은
7·8번 슬라이스에서 청구된다. 1~5번은 **회귀 방어**로 남고 진척 보고에서는
빠진다(§8.4).

### 8.8 이 재산출이 넘겨받지 않는 것

- **V5·V6 잔여(셰이더 컴파일·퍼뮤테이션)** 는 `MaterialPipelinePlan`(PHASE 3.5)
  이 M1·M2·M3 으로 승계한 그대로다. **바이트코드 인계 지점이 이미 중립**이고
  (`const void* + size`, §7.2.3) V8-a 가 `RHIShaderBlob` 이 Vulkan 쪽에서도
  성립함을 실측했으므로(갈리는 것은 그것을 *채우는* 동작이다 — §7.2.5)
  두 축이 부딪히지 않는다. 부딪히는 유일한 자리는 `D3D_SHADER_MACRO` 10건이고
  그것은 M2 의 몫이다.
- **자가 검증 하네스의 DX12 잔량**(패스 파일 안 51건 · 배리어 43곳 · 벤치 17곳).
  소속이 DX12 백엔드의 자기 검사다(V3·V4 의 판단). Vulkan 은 자기 검사를
  따로 갖는다 — `vk.selftest` 가 그 첫 번째다.
- **D4 잔여**(`Utility_Framework/DeviceResources` 본체 제거). ImGui DX11 은퇴가
  선행이고 이 축과 독립이다.
