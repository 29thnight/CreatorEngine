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

**슬라이스 순서 (2026-08-09 현재 상태):**

```
R1 ✔ → R2a ✔ → R2b ✔ → R2c ✔ → R3 ✔ → R4(진행 중) → R5 ✔
                                  └ R6(선택, 선행 조건 충족) · R7(선택, 두 번째 백엔드 착수 시)
```

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

### 6.1 현재 위치 (2026-08-09 실측)

| # | 기준 | 상태 |
|---|---|---|
| 1 | `Render/` 폴더에 `d3d12.h` 0건 | **미착수** — 폴더 자체가 없다(R0). 다만 `EnhancedRenderPass.h`는 이미 DX12 참조 0이라 옮길 대상의 절반은 준비됐다 |
| 2 | 패스 본문에 `ID3D12`·`D3D12_` 0건 | **거의** — 남은 것은 `Declare`의 `ID3D12Resource*`(그래프 `Resolve`의 반환 타입이라 R4-3이 가져간다)와 VolFog `PrepareFrame` 7건. `CreatePipelines`/`Initialize` 쪽은 §2가 범위 밖으로 둔 것이고 R7의 기준이다 |
| 3 | 패스 이름 목록 문자 일치 | **충족** — 슬라이스마다 확인해 왔다 |
| 4 | 자가 검증 전체 통과 · 검증 레이어 0건 | **부분** — 33종 중 27 통과 · 4 계측 · 2 실패. ★ **둘 다 결함이 아니라 실행 전제였다(2026-08-09 확인, §6.2)**. stderr 33종 모두 0바이트 |
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

★ **배운 것:** 검사가 *무엇을 전제하는가*를 실행 방법이 만족시키는지 확인하지
않으면, 통과 못 한 것을 결함으로 잘못 적게 된다. R2b가 "검증 목록이 검증
대상보다 낡으면 통과가 아무것도 뜻하지 않는다"고 적어 둔 것과 같은 부류이고,
이번에 빠져 있던 것은 목록이 아니라 **실행 전제**다. 전수 스윕 스크립트는
검사 앞에 프레임 워밍업을 두어야 한다.

★ **남은 셋 중 하나(1)가 사실상 이동 작업이고, 실제 설계 몫은 2의 잔여**
**(그래프 서명 = R4-3) 하나다.** 5는 재기만 하면 되고, 4의 실패 둘은 이 축 밖의
선재 문제다 — 즉 이 계획서가 남긴 설계 판단은 R4-3에서 끝난다.
