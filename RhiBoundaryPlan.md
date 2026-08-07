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

★ **배리어가 없다.** 상태 전이는 그래프가 계획하고 백엔드가 실행한다 —
지금 구조 그대로다. 패스가 배리어를 부르지 않는 것이 3-5의 계약이고, RHI가
그 계약을 타입으로 굳힌다(부를 방법 자체가 없다).

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

**R2b — RTV/DSV 힙 통합.** 지금 패스 12종이 각자 `ID3D12DescriptorHeap`을
만들어 들고 있고(Initialize에서 생성·Shutdown에서 해제), 매 프레임 같은
자리에 뷰를 다시 만든다. 프레임 RTV 링 하나로 모으면 힙 12개와 그 수명
관리가 통째로 사라진다. R2a와 성격이 같지만 인터페이스가 달라 분리했다.

**R3 — 인코더 이관.** 렌더 상태·드로우·디스패치를 `RHIEncoder`로.
패스에서 `commandList->`가 사라진다.

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

- **T2 — 가드 질문 바꾸기.** `texture->m_pSRV` 가드를 백엔드 중립 술어로.
  넷뿐이고 위험이 없다. 이후 슬라이스가 이것에 안 걸린다.
- **T3 — 죽은 DX11 경로 확인·제거.** `Material::SetShaderPSO`의 바인딩 5건과
  `RenderPassData` 3건이 구 렌더러와 함께 죽었는지 호출자로 확인한다.
  (`Material::ApplyMaterialInfo`는 이미 빈 함수다.)
- **T4 — DX12TextureCache의 DX11 폴백 제거.** 실측이 0경유이므로 폴백이
  도는 경우를 먼저 로그로 확인하고, 없으면 걷는다.
- **T5 — 지형.** `TerrainMaterial`이 DX11 디바이스를 직접 쥔다. 가장 크다.
- **T6 — `Texture`에서 DX11 멤버 제거.** 위가 끝나야 가능하다. 여기까지
  오면 D4의 전제 절반이 풀린다(나머지 절반은 PHASE 10).

**R6(선택) — 기록 검증용 가짜 백엔드.** §2.1 ②. 인코더 호출을 기록만 하는
구현으로 패스의 명령 순서를 픽셀 없이 단정한다. 여기까지 오면 두 번째
백엔드를 붙이는 비용도 드러난다.

### 4.1 규모 예상

R0은 이동뿐(반나절). R1은 서명 교체와 전파(1~2일). R2·R3가 본체로 패스
17종을 훑는다(각 3~4일). R4는 넓지만 기계적(2일). R5는 삭제(반나절).

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

1. `RenderEngine/Render/` 아래 어떤 파일도 `d3d12.h`를 include하지 않는다.
2. 패스 17종에 `ID3D12`·`D3D12_` 직접 참조 0건(grep으로 검증).
3. `dx12.live status`의 패스 이름 목록이 착수 전과 문자 그대로 같다.
4. 자가 검증 전체 통과, D3D12 검증 레이어 메시지 0건.
5. `dx12.live status`의 CPU ms가 착수 전 대비 유의미하게 늘지 않는다(실측 기록).
6. 구 RHI(`RHI.h` · `RHIDevice.h` · `RHICommandContext.h` · `DX11RHI.*`) 제거.
