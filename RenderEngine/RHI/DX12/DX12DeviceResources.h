#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "RenderFrameServices.h"
#include "../IRHIDeviceResources.h"
#include <cstdint>
#include <string>
#include <array>
#include <wrl/client.h>
#include <d3d12.h>

#include "../ScreenSizedResource.h"
#include <dxgi1_6.h>

#include "DX12UploadRing.h"
#include "DX12DescriptorHeaps.h"
#include "DX12ResourceTable.h"

// DX12 디바이스 기반(PHASE 3-3, EnhancedSceneRenderer의 토대).
//
// 브링업 단계에서는 스왑체인을 만들지 않는다 — 창은 DX11 스왑체인이 점유 중이고,
// 병존 기간의 출력 경로(공유 텍스처로 DX11에 합성 vs 교체 시점에 스왑체인 이관)는
// 3-9에서 결정한다. 대신 오프스크린 타깃 + 리드백으로 렌더 결과를 파일로 꺼내
// 검증한다 — 픽셀이 곧 증거다.
//
// DX11이 암묵으로 해 주던 것들이 여기서 전부 명시 책임이 된다:
//   - 프레임 인플라이트: 얼로케이터 3개를 펜스로 회전(프레임당 하나, GPU가 그
//     프레임을 끝냈음을 펜스로 확인한 뒤에만 Reset)
//   - 리소스 상태: RT ↔ COPY_SOURCE 전이를 배리어로 직접 선언
// 두 인터페이스를 함께 구현한다. 역할이 다르기 때문이다:
//
//   IRHIDeviceResources   앱·셸이 쓰는 디바이스 수명 · 프레임 경계 · 프레젠트.
//                         백엔드 중립이고, Vulkan 구현이 들어올 자리다(D1).
//   IRenderDeviceServices 패스가 프레임 안에서 쓰는 서비스(디바이스 핸들 · 링 ·
//                         바인딩). 아직 DX12 타입을 노출하며 R2·R3에서 중립화된다.
//
// 한 클래스가 둘을 겸하는 것은 지금 이 객체가 실제로 둘 다이기 때문이다.
// 쪼갤 근거가 생기면(예: 서비스만 가짜로 바꿔 패스를 검증할 때) 그때 나눈다.
class DX12DeviceResources : public IRHIDeviceResources, public IRenderDeviceServices
{
public:
    static constexpr uint32_t kFrameCount = 3;

    // 렌더 타깃의 최적화 클리어 값. 생성 힌트와 실제 클리어가 일치해야 검증
    // 레이어가 조용하다 — 힌트를 안 주는 쪽도, 다른 값으로 지우는 쪽도 경고를 쌓는다
    // (둘 다 실측). 그래서 클리어 색은 여기 상수 하나로 고정한다.
    static constexpr float kClearColor[4] = { 0.1f, 0.1f, 0.15f, 1.f };

    // 실패 사유를 문자열로 돌려준다 — 브링업 단계의 실패는 전부 진단 가능해야 한다.
    //
    // matchAdapterLuid가 0이 아니면 그 LUID의 어댑터에 디바이스를 만든다.
    //
    // 이게 필요한 이유: 병존 기간에 DX12가 그린 텍스처를 DX11이 SRV로 열어
    // 화면에 올린다(에디터의 씬 뷰는 ImGui::Image로 SRV를 표시한다). 공유는
    // 같은 어댑터일 때만 성립하는데, DX11은 기본 어댑터(D3D11CreateDevice에
    // nullptr)를, DX12는 고성능 우선을 골라서 두 정책이 갈릴 수 있다.
    // iGPU+dGPU 노트북에서는 실제로 갈린다. 정책을 맞추는 것으로는 부족하고
    // 같은 물리 어댑터임을 LUID로 확인해야 한다.
    ///
    /// followScreenSize가 참이면 창 크기 변경을 구독한다. 기본은 거짓이다 —
    /// 자가 검증은 256x256처럼 화면과 무관한 크기로 디바이스를 세우고, 그것이
    /// 창을 따라가면 검증이 창 크기에 좌우된다. DX11 Texture의 기본값을
    /// '따라가지 않음'으로 둔 것과 같은 이유다.
    bool Initialize(uint32_t width, uint32_t height, std::string& outError,
        LUID matchAdapterLuid = LUID{ 0, 0 }, bool followScreenSize = false);
    void Shutdown() override;

    /// 창 크기가 바뀌었을 때 크기에 딸린 리소스를 다시 만든다.
    ///
    /// DX11 쪽 Texture와 같은 계약을 따른다(RHI/ScreenSizedResource.h). 교체
    /// 후 DX11 경로가 사라져도 크기 추종은 그대로 남아야 하므로, 버스는
    /// 백엔드 중립이고 이쪽은 그 구독자다.
    bool Resize(uint32_t width, uint32_t height, std::string& outError) override;

    bool IsInitialized() const override { return nullptr != m_device.Get(); }

    // 이번 프레임의 얼로케이터를 준비하고(필요 시 펜스 대기) 커맨드 리스트를 연다.
    bool BeginFrame(std::string& outError) override;
    // 커맨드 리스트를 닫고 제출한 뒤 펜스 신호를 건다.
    bool EndFrame(std::string& outError) override;

    /// 프레임 중간에 지금까지 기록한 것을 제출하고 리스트를 다시 연다.
    ///
    /// 병렬 기록에 필요하다. 업로드(PrepareFrame)는 이 리스트에 기록되는데,
    /// 그것이 워커 리스트보다 먼저 실행되어야 한다. EndFrame이 마지막에
    /// 제출하는 구조라 그대로 두면 순서가 뒤집힌다.
    ///
    /// 얼로케이터는 되돌리지 않는다 — GPU가 아직 그 메모리를 읽는 중이다.
    /// 리스트만 다시 여는 것은 제출 직후에도 허용된다.
    bool FlushCommandList(std::string& outError) override;
    // 모든 제출 완료까지 대기(리드백 읽기 전·종료 전).
    void WaitForGpu() override;

    /// 마지막으로 EndFrame이 서명한 펜스 값. 상시 러너의 비동기 표시가
    /// '이 프레임이 끝났는가'를 논블로킹으로 물을 때 GetCompletedFenceValue와
    /// 짝으로 쓴다 — 완료 확인이 CPU 대기 없이 되므로 WaitForGpu가 필요 없다.
    uint64_t GetLastSignaledFenceValue() const override { return m_nextFenceValue - 1; }
    uint64_t GetCompletedFenceValue() const override
    {
        return m_fence ? m_fence->GetCompletedValue() : 0;
    }

    // ── 스왑체인 (3-9 교체 — ImGui/셸이 DX12로 출력하는 경로) ──
    //
    // 브링업의 '스왑체인 없음' 원칙은 오프스크린 검증용이었고, 여기서 그
    // 결정(3-9)이 내려졌다: 셸이 스왑체인을 갖는다. Initialize 뒤에 한 번
    // 부른다. 백버퍼는 kFrameCount장, FLIP_DISCARD — DX11 쪽의 DISCARD와
    // 달리 FLIP 계열이 DX12의 유일한 선택지다.
    //
    // 백버퍼 상태 전이(PRESENT ↔ RENDER_TARGET)는 호출부 책임이다 —
    // 프레임 구조(어디서 그리고 어디서 제출하는가)를 여기가 모른다.
    bool AttachSwapChain(void* windowHandle, uint32_t width, uint32_t height,
        std::string& outError) override;
    bool ResizeSwapChain(uint32_t width, uint32_t height, std::string& outError) override;
    bool Present(std::string& outError) override;
    bool HasSwapChain() const override { return nullptr != m_swapChain.Get(); }
    uint32_t GetBackBufferIndex() const override;
    ID3D12Resource* GetBackBuffer(uint32_t index) const
    {
        return (index < kFrameCount) ? m_backBuffers[index].Get() : nullptr;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE GetBackBufferRtv(uint32_t index) const;

    // 검증 레이어 메시지를 비우고, 그중 '실제 문제'(WARNING 이상)의 수를 돌려준다.
    //
    // INFO/MESSAGE 등급은 세지 않는다 — 파이프라인 라이브러리의 첫 조회 실패
    // ("이 이름의 파이프라인이 없음")처럼 정상 경로가 남기는 안내가 여기 들어와서,
    // 전부 세면 캐시 미스라는 당연한 동작이 검증 실패로 둔갑한다(실측).
    // 대신 outMessages에는 등급을 붙여 전부 담는다 — 세지 않는 것과 감추는 것은 다르다.
    uint32_t DrainDebugMessages(std::string& outMessages) override;

    // ── GPU 진단 (DX11 DeviceResources에서 이관, 2026-08-10) ──
    //
    // VRAM은 어댑터(DXGI) 질의라 원래도 백엔드와 무관했다 — DX11 쪽 구현을
    // 그대로 옮긴 것이다. 라이브 객체 집계는 디버그 레이어 메시지를 파싱하는
    // 같은 형태인데 출처가 ID3D11Debug/InfoQueue에서 ID3D12DebugDevice/
    // ID3D12InfoQueue로 바뀐다.
    RHIVideoMemoryInfo QueryVideoMemory() const override;
    RHIGpuObjectCensus CaptureLiveObjectCensus(bool allowDeviceEnumeration) override;
    void ReportLiveObjectsToDebugOutput() override;

    /// 장치 제거가 감지되었을 때 DRED breadcrumb와 page-fault 정보를 붙인다.
    /// operationResult가 일반 실패이고 장치는 살아 있으면 아무것도 추가하지 않는다.
    void AppendDeviceRemovedReport(HRESULT operationResult, std::string& outError) const;

    ID3D12Device* GetDevice() const override {  return m_device.Get(); }

    // 타임스탬프 주파수를 얻으려면 큐가 필요하다(큐마다 다를 수 있다).
    ID3D12CommandQueue* GetCommandQueue() const { return m_queue.Get(); }
    ID3D12GraphicsCommandList* GetCommandList() const override {  return m_commandList.Get(); }
    ID3D12Resource* GetRenderTarget() const { return m_renderTarget.Get(); }
    D3D12_CPU_DESCRIPTOR_HANDLE GetRtvHandle() const { return m_rtvHandle; }
    /// 프레임 리드백. 오프스크린 결과를 픽셀로 꺼내 보는 자가 검증이 쓴다.
    const RHIReadback& GetFrameReadback() const { return m_frameReadback; }
    uint32_t GetWidth() const override { return m_width; }
    uint32_t GetHeight() const override { return m_height; }
    uint32_t GetRowPitch() const { return m_frameReadback.rowPitch; }

    // 프레임 업로드 링. BeginFrame이 이 프레임 구간을 되감아 준다.
    //
    // 여기 묶어 두는 이유: 링의 반납 규칙이 "BeginFrame이 그 슬롯의 펜스를
    // 기다린 뒤에 되감는다"에 기대고 있다. 링을 따로 들고 다니면 그 계약이
    // 호출부 규율이 되고, 한 곳만 어겨도 GPU가 읽는 중인 데이터를 덮어쓴다.
    DX12UploadRing& GetUploadRing() override {  return m_uploadRing; }
    const DX12UploadRing& GetUploadRing() const { return m_uploadRing; }

    // 프레임 디스크립터 링. 업로드 링과 같은 이유로 여기 묶는다 — 되감기 시점이
    // 펜스 대기 뒤여야 한다는 계약이 호출부 규율이 되면 언젠가 어긋난다.
    DX12DescriptorRing& GetDescriptorRing() override {  return m_descriptorRing; }
    const DX12DescriptorRing& GetDescriptorRing() const { return m_descriptorRing; }

    // 샘플러 힙은 프레임과 무관하다(설정이 바뀌지 않는다) — 되감지 않는다.
    DX12SamplerHeap& GetSamplerHeap() override {  return m_samplerHeap; }

    // ── 바인딩(R2) — 구현은 .cpp에 ──
    RHIBindingTable CreateBindings(std::span<const RHIBindingDesc> descs) override;
    /// 인터페이스에서 빠졌다(R4-1b) — 인코더와, 그래프 밖에서 원시 경로를
    /// 쓰는 두 자리(인코더 벤치·SSGI 자가 검증)만 부른다.
    void BindDescriptorHeaps(ID3D12GraphicsCommandList* commandList,
        bool withSamplers = false);
    const DX12SamplerHeap& GetSamplerHeap() const { return m_samplerHeap; }

    // ── 렌더 타깃(R2b) — 구현은 .cpp에 ──
    RHIRenderTargetBinding CreateRenderTargets(std::span<ID3D12Resource* const> colors,
        const RHIDepthTargetDesc* depth = nullptr) override;
    /// 거는 셋은 인터페이스에서 빠졌다(R4-1) — DX12Encoder만 부른다.
    /// 구현이 여기 남은 것은 뷰 힙이 여기 있기 때문이고, 인덱스에서 핸들을
    /// 얻는 산술을 두 곳에 두지 않으려는 것이다.
    void BindRenderTargets(ID3D12GraphicsCommandList* commandList,
        const RHIRenderTargetBinding& binding);
    void ClearRenderTargets(ID3D12GraphicsCommandList* commandList,
        const RHIRenderTargetBinding& binding, const float rgba[4]);
    void ClearDepthTarget(ID3D12GraphicsCommandList* commandList,
        const RHIRenderTargetBinding& binding, float depth);

    void ClearUnorderedAccess(ID3D12GraphicsCommandList* commandList,
        const RHIBindingDesc& view, const float rgba[4]) override;

    /// ClearUnorderedAccessViewFloat이 요구하는 비셰이더 가시 UAV 디스크립터.
    /// 인코더가 그 짝을 맞추는 데 쓴다 — 호출부는 이것을 몰라도 된다.
    D3D12_CPU_DESCRIPTOR_HANDLE CreateClearDescriptor(const RHIBindingDesc& desc);

    const DX12TargetViewHeap& GetRtvViewHeap() const { return m_rtvViewHeap; }
    const DX12TargetViewHeap& GetDsvViewHeap() const { return m_dsvViewHeap; }

    // ── 리소스 핸들 표 (V2-a) ──
    //
    // ★ 표를 디바이스가 드는 이유: 그래프는 프레임마다 새로 서고 패스는
    //   여럿인데, 핸들의 유효 범위는 그보다 길다(패스 소유 리소스는 프레임을
    //   넘긴다). 수명 정책은 바뀌지 않는다 — ComPtr이 하던 참조 세기를 표가
    //   그대로 든다(RhiBoundaryPlan.md §7.2.1 ③).
    RHITextureHandle RegisterTexture(Microsoft::WRL::ComPtr<ID3D12Resource> resource)
    {
        return m_resourceTable.AddTexture(std::move(resource));
    }
    RHIBufferHandle RegisterBuffer(Microsoft::WRL::ComPtr<ID3D12Resource> resource)
    {
        return m_resourceTable.AddBuffer(std::move(resource));
    }
    /// 소유하지 않고 등록한다 — 임포트(백버퍼·자가 검증이 만든 텍스처).
    RHITextureHandle RegisterExternalTexture(ID3D12Resource* resource)
    {
        return m_resourceTable.AddExternalTexture(resource);
    }

    ID3D12Resource* Resolve(RHITextureHandle handle) const override { return m_resourceTable.Resolve(handle); }
    ID3D12Resource* Resolve(RHIBufferHandle handle) const override { return m_resourceTable.Resolve(handle); }

    // ── 패스 소유 리소스(R2c) — 구현은 .cpp에 ──
    bool CreateBuffer(const RHIBufferDesc& desc,
        RHIBufferHandle& outHandle, std::string& outError) override;
    bool CreateTexture(const RHITextureDesc& desc,
        Microsoft::WRL::ComPtr<ID3D12Resource>& outResource, std::string& outError) override;

    // ── 리드백(R2c-b) — 구현은 .cpp에 ──
    bool CreateReadback(uint32_t width, uint32_t height, RHIFormat format,
        uint32_t sliceCount, RHIReadback& outReadback, std::string& outError) override;
    void CopyToReadback(ID3D12GraphicsCommandList* commandList,
        const RHIReadback& readback, ID3D12Resource* source,
        uint32_t slice = 0, uint32_t sourceSubresource = 0) override;
    void CopyVolumeToReadback(ID3D12GraphicsCommandList* commandList,
        const RHIReadback& readback, ID3D12Resource* source,
        uint32_t sourceSubresource = 0) override;
    void CopyPartialToReadback(ID3D12GraphicsCommandList* commandList,
        const RHIReadback& readback, ID3D12Resource* source,
        uint32_t slice = 0, uint32_t sourceSubresource = 0) override;
    bool MapReadback(const RHIReadback& readback,
        RHIReadbackImage& outImage, std::string& outError) override;
    bool CreateBufferReadback(uint64_t bytes,
        RHIReadback& outReadback, std::string& outError) override;
    void CopyBufferToReadback(ID3D12GraphicsCommandList* commandList,
        const RHIReadback& readback, ID3D12Resource* source,
        uint64_t sourceOffset = 0, uint64_t bytes = 0) override;

private:
    template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    // 핸들 → 리소스. 등록 경로는 둘뿐이다 — Create* 가 만들면서, 그래프가
    // transient 를 만들면서(V2-c).
    DX12ResourceTable m_resourceTable;

    // 크기에 딸린 것들. 초기화와 리사이즈가 같은 코드를 탄다 — 나뉘어 있으면
    // 한쪽만 고쳐 두 경로가 갈린다.
    bool CreateSizeDependentResources(uint32_t width, uint32_t height, std::string& outError);

    ComPtr<IDXGIFactory6>              m_factory;
    ComPtr<IDXGIAdapter1>              m_adapter;
    ComPtr<ID3D12Device>               m_device;
    ComPtr<ID3D12CommandQueue>         m_queue;
    std::array<ComPtr<ID3D12CommandAllocator>, kFrameCount> m_allocators;
    std::array<uint64_t, kFrameCount>  m_frameFenceValues{};
    ComPtr<ID3D12GraphicsCommandList>  m_commandList;
    ComPtr<ID3D12Fence>                m_fence;
    HANDLE                             m_fenceEvent{ nullptr };
    uint64_t                           m_nextFenceValue{ 1 };
    uint32_t                           m_frameIndex{ 0 };

    ComPtr<ID3D12DescriptorHeap>       m_rtvHeap;
    D3D12_CPU_DESCRIPTOR_HANDLE        m_rtvHandle{};
    ComPtr<ID3D12Resource>             m_renderTarget;
    RHIReadback                        m_frameReadback;

    // 스왑체인(셸 전용 — AttachSwapChain을 부른 인스턴스만 갖는다).
    ComPtr<IDXGISwapChain3>            m_swapChain;
    std::array<ComPtr<ID3D12Resource>, kFrameCount> m_backBuffers;
    ComPtr<ID3D12DescriptorHeap>       m_backBufferRtvHeap;
    uint32_t                           m_backBufferRtvSize{ 0 };

    DX12UploadRing                     m_uploadRing;
    DX12DescriptorRing                 m_descriptorRing;
    DX12SamplerHeap                    m_samplerHeap;

    // 패스가 매 프레임 다시 만드는 RTV/DSV(R2b). 링과 달리 프레임 구간으로
    // 나누지 않는다 — 이 디스크립터는 기록 시점에 소비된다(DX12DescriptorHeaps.h).
    DX12TargetViewHeap                 m_rtvViewHeap;
    DX12TargetViewHeap                 m_dsvViewHeap;

    // ClearUnorderedAccessViewFloat 전용 비가시 UAV 힙(R3).
    DX12TargetViewHeap                 m_clearViewHeap;

    uint32_t m_width{ 0 };
    uint32_t m_height{ 0 };

    ScreenResizeBus::Handle m_resizeSubscription{ ScreenResizeBus::kInvalidHandle };
};

#endif
