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

**하지 않을 것**:

- **Vulkan/Metal 백엔드 구현 없음.** 두 번째 백엔드는 이 작업의 *동기*가
  아니라 *부산물*이다. 지금 동기는 다른 데 있다(§2.1).
- **기존 RHI(DX11 모델) 확장 없음.** 은퇴시킨다(§4, R5).
- **무비용 추상화 강박 없음.** 프레임당 수천 번 도는 자리(드로우 루프 안쪽)는
  인라인 가능한 얇은 래퍼로 두고, 가상 함수는 프레임당 수십 번 이하인
  자리에만 쓴다. 판단 근거는 실측이다 — 3-6의 API 오버헤드 벤치가 기준선이다.
- **패스 셰이더 재작성 없음.** HLSL과 루트 시그니처 레이아웃은 그대로다.

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

**R4 — 그래프 서명 교체.** §3.3의 표대로. 자가 검증 39곳이 함께 바뀐다 —
가장 넓게 퍼지는 슬라이스라 R2·R3로 패스가 이미 인코더를 쓰게 된 뒤에 한다.

**R5 — 구 RHI 은퇴.** `RHI.{h,cpp}` · `RHIDevice.h` · `RHICommandContext.h` ·
`DX11RHI.*` 제거. `IRenderPass`는 `EffectManager`가 상속하므로 PHASE 10
(파티클·이펙트 DX12 재설계)까지 남긴다 — 그때 함께 정리한다.

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

**D3 — ImGui DX12 셸 기본 켜기.** D2가 풀면 설정 기본값을 뒤집는다.

**D4 — `Utility_Framework/DeviceResources` 은퇴.**
스왑체인만이 아니라 DX11 디바이스를 쥐고 있어서, 그 소비자가 먼저 사라져야
한다. 실측한 잔량: `Texture.cpp` 23건(T축이 절반 해결, SRV 생성이 남음),
**EffectSystem 약 50건**(파티클 — PHASE 10), `RenderDebugManager`·
`TerrainMaterial` 11건. 즉 D4는 T축과 PHASE 10 뒤다.

순서: `D1 → D2 → D3 → (T축·PHASE 10) → D4`

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
- **T4 — DX12TextureCache의 DX11 폴백 제거.** 실측이 0경유이므로 폴백이
  도는 경우를 먼저 로그로 확인하고, 없으면 걷는다.
- **T5 — 지형.** `TerrainMaterial`이 DX11 디바이스를 직접 쥔다. 가장 크다.
- **T6 — `Texture`에서 DX11 멤버 제거.** 위가 끝나야 가능하다. 여기까지
  오면 D4의 전제 절반이 풀린다(나머지 절반은 PHASE 10).

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

**슬라이스 순서 (2026-08-08 재배치):**

```
R1 ✔ → R2a ✔ → R2b → R2c → R3 → R4 → R5
                        └ R6(선택, R2c 선행) · R7(선택, 두 번째 백엔드 착수 시)
```

R2b·R2c가 R2a에서 갈라져 나온 이유는 같다 — R2a는 *셰이더 가시 디스크립터 링*
하나만 다뤘고, RTV/DSV는 힙이 다르며(R2b), 리소스 생성과 리드백은 링이
아니라 디바이스다(R2c). 셋을 한 슬라이스로 묶으면 A/B 판정 단위가 너무
커진다.

★ R2·R3는 패스를 하나씩 옮길 수 있다 — 인코더와 원시 커맨드 리스트를 한
프레임에 섞어 쓸 수 있으므로(같은 리스트에 기록), 패스 단위로 A/B하며
진행한다. 이것이 이 계획의 안전장치다.

## 5. 위험

- **추상화가 성능을 먹는다** — 인코더 호출이 프레임당 수백~수천이다. 가상
  함수 오버헤드를 R3 시작 전에 실측으로 확인하고(마이크로벤치), 필요하면
  드로우 루프 안쪽만 비가상 인라인 경로로 둔다. 판정 기준은 3-6의 API
  오버헤드 벤치와 `dx12.live status`의 CPU ms다.
- **작업 중 회귀가 조용히 쌓인다** — 슬라이스마다 패스 목록 문자 일치와 픽셀
  대조를 요구한다. 패스 단위 진행이므로 어느 패스에서 깨졌는지도 좁혀진다.
- **범위가 번진다** — 셰이더·루트 시그니처 레이아웃·PSO 캐시 구조는 이번
  범위 밖이다. 그것들은 이미 DX12 개념이되 검증이 끝난 자산이고, 백엔드
  구현 안에 남는 것이 옳다.
- **PHASE 10과 겹친다** — 파티클·이펙트가 아직 DX11 `IRenderPass`에 있다.
  R5에서 그 층을 건드리지 않는 것으로 분리한다.

## 6. 완료 기준

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
