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
#include <memory>

/// A-3. 즉시 인코더가 이 타입이다. 헤더를 물지 않는 것은 방향 때문이다 —
/// 인코더가 이 클래스를 알아야지 그 반대가 아니다.
class DX12Encoder;

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
    /// 정의는 둘 다 .cpp 에 있다 — 즉시 인코더를 불완전 타입으로 들기
    /// 때문이다(A-3).
    ///
    /// ★ 소멸자만으로는 모자란다. **생성자도** `unique_ptr` 의 소멸자를
    ///   인스턴스화한다 — 생성 중 예외가 나면 이미 만든 멤버를 되돌려야
    ///   하기 때문이고, 그래서 암묵 생성자가 만들어지는 모든 번역 단위에서
    ///   "불완전 타입을 delete 할 수 없다"가 난다(실측).
    DX12DeviceResources();
    ~DX12DeviceResources();

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

    /// BeginFrame 뒤에 기록을 포기한다 — 닫기만 하고 제출하지 않는다.
    ///
    /// 프레임 기록이 중간에 실패했을 때 필요하다. 그냥 빠져나가면 커맨드
    /// 리스트가 열린 채 남아, 다음 BeginFrame의 `allocator->Reset()`이
    /// E_FAIL로 죽는다 — 원래 실패의 사유가 그 2차 오류로 덮인다(실측).
    ///
    /// 제출하지 않으므로 새 펜스도 걸지 않는다. 이 슬롯의 펜스 값은 이미
    /// 완료된 값 그대로라, 다음 BeginFrame의 대기는 그냥 통과한다.
    /// 프레임 인덱스도 올리지 않는다 — 같은 슬롯을 다시 쓰는 편이 낫다.
    /// 업로드 링은 그 슬롯을 되감아 온전한 구간으로 다시 시작한다.
    void AbortFrame() override;

public:

    /// 프레임 중간에 지금까지 기록한 것을 제출하고 리스트를 다시 연다.
    ///
    /// 병렬 기록에 필요하다. 업로드(PrepareFrame)는 이 리스트에 기록되는데,
    /// 그것이 워커 리스트보다 먼저 실행되어야 한다. EndFrame이 마지막에
    /// 제출하는 구조라 그대로 두면 순서가 뒤집힌다.
    ///
    /// 얼로케이터는 되돌리지 않는다 — GPU가 아직 그 메모리를 읽는 중이다.
    /// 리스트만 다시 여는 것은 제출 직후에도 허용된다.
    bool FlushCommandList(std::string& outError) override;
    /// 이미 닫힌 backend command list 묶음을 제출하고 같은 queue fence로
    /// 현재 recording의 업로드 예약을 seal한다. 직접 queue 제출은 금지한다.
    bool SubmitCommandLists(std::span<ID3D12CommandList* const> lists,
        std::string& outError);
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

    ID3D12Device* GetDevice() const {  return m_device.Get(); }

    // 타임스탬프 주파수를 얻으려면 큐가 필요하다(큐마다 다를 수 있다).
    ID3D12CommandQueue* GetCommandQueue() const { return m_queue.Get(); }
    ID3D12GraphicsCommandList* GetCommandList() const {  return m_commandList.Get(); }
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
    /// A-5a. 링에서 잘라 중립 슬라이스로 돌려준다.
    ///
    /// ★ `Allocate` 호출 수가 그대로다 — 감싸기만 하고 자르는 횟수를 바꾸지
    ///   않는다. 그것이 성능 계약이다(호출당 ~175ns 원자 연산).
    using IRenderDeviceServices::AllocateUpload;

    bool ReserveUploadBatch(std::span<const RHIUploadRequest> requests,
        std::span<RHIBufferSlice> outSlices, std::string& outError) override
    {
        RHIBufferSlice slice{};
        return m_uploadAllocator.ReserveBatch(
            m_currentRecordingId, requests, outSlices, outError);
    }

    RHIBufferSlice AllocateUpload(const RHIUploadRequest& request) override
    {
        RHIBufferSlice slice{};
        std::string error;
        const std::span<const RHIUploadRequest> requests(&request, 1);
        const std::span<RHIBufferSlice> slices(&slice, 1);
        ReserveUploadBatch(requests, slices, error);
        return slice;
    }

    RHIBufferSlice UploadConstants(const void* data, size_t bytes) override
    {
        const RHIBufferSlice slice = AllocateUpload(
            RHIUploadRequest{ bytes, RHIUploadUsage::ConstantBuffer, 1 });
        if (slice.IsValid() && nullptr != data) std::memcpy(slice.cpuAddress, data, bytes);
        return slice;
    }

    RHIUploadStats GetUploadStats() const { return m_uploadAllocator.GetStats(); }
    uint64_t GetCurrentUploadRecordingId() const override
    {
        return m_currentRecordingId;
    }
    void RegisterUploadTransactionListener(
        IRHIUploadTransactionListener* listener) override;
    void UnregisterUploadTransactionListener(
        IRHIUploadTransactionListener* listener) override;
    uint64_t GetUploadUsedBytes() const { return m_uploadAllocator.GetRecordingUsedBytes(); }
    uint64_t GetRegularUploadSegmentBytes() const
    {
        return m_uploadAllocator.GetRegularSegmentBytes();
    }
    DX12UploadSegmentAllocator& GetUploadAllocator() { return m_uploadAllocator; }
    const DX12UploadSegmentAllocator& GetUploadAllocator() const { return m_uploadAllocator; }

    // 프레임 디스크립터 링. 업로드 링과 같은 이유로 여기 묶는다 — 되감기 시점이
    // 펜스 대기 뒤여야 한다는 계약이 호출부 규율이 되면 언젠가 어긋난다.
    DX12DescriptorRing& GetDescriptorRing() {  return m_descriptorRing; }
    const DX12DescriptorRing& GetDescriptorRing() const { return m_descriptorRing; }

    // 샘플러 힙은 프레임과 무관하다(설정이 바뀌지 않는다) — 되감지 않는다.
    //
    // ★ 인터페이스에서 빠졌다(A-4). 아래 CreateSamplers 가 그 자리를 덮고,
    //   힙 자체를 꺼내는 것은 백엔드 내부(인코더의 힙 바인딩·진단)만 한다.
    DX12SamplerHeap& GetSamplerHeap() {  return m_samplerHeap; }

    /// 샘플러 N개를 연속 테이블로 (A-4).
    ///
    /// ★ 하나짜리는 중복 제거 캐시로 흘린다. 계약에 없는 순수 최적화다 —
    ///   같은 설정이면 같은 디스크립터라 관측 가능한 차이가 없고, 패스마다
    ///   Initialize 에서 한 번씩 부르므로 힙 상한(2048)을 아끼는 값이 있다.
    RHISamplerTable CreateSamplers(std::span<const RHISamplerDesc> descs) override
    {
        if (descs.empty()) return RHISamplerTable{};
        const auto handle = (1 == descs.size())
            ? m_samplerHeap.GetOrCreate(descs[0])
            : m_samplerHeap.CreateRange(descs);
        return RHISamplerTable{ handle.ptr };
    }

    /// 지금 열린 커맨드 리스트에 붙은 인코더 (A-3). BeginFrame 이 다시 만든다.
    RHIEncoder& GetImmediateEncoder() override;

    /// 커맨드 리스트를 Reset 한 자리마다 부른다 — 셋뿐이다(생성 직후 ·
    /// BeginFrame · FlushCommandList).
    void ResetImmediateEncoder();

    // ── 바인딩(R2) — 구현은 .cpp에 ──
    RHIBindingTable CreateBindings(std::span<const RHIBindingDesc> descs) override;
    /// 인터페이스에서 빠졌다(R4-1b) — 인코더와, 그래프 밖에서 원시 경로를
    /// 쓰는 두 자리(인코더 벤치·SSGI 자가 검증)만 부른다.
    void BindDescriptorHeaps(ID3D12GraphicsCommandList* commandList,
        bool withSamplers = false);
    const DX12SamplerHeap& GetSamplerHeap() const { return m_samplerHeap; }

    // ── 렌더 타깃(R2b) — 구현은 .cpp에 ──
    RHIRenderTargetBinding CreateRenderTargets(std::span<const RHITextureHandle> colors,
        const RHIDepthTargetDesc* depth = nullptr) override;
    /// 거는 셋은 인터페이스에서 빠졌다(R4-1) — DX12Encoder만 부른다.
    /// 구현이 여기 남은 것은 뷰 힙이 여기 있기 때문이고, 인덱스에서 핸들을
    /// 얻는 산술을 두 곳에 두지 않으려는 것이다.
    void BindRenderTargets(ID3D12GraphicsCommandList* commandList,
        const RHIRenderTargetBinding& binding);
    void ClearRenderTargets(ID3D12GraphicsCommandList* commandList,
        const RHIRenderTargetBinding& binding, const float rgba[4]);
    /// rect가 널이면 전체를 지운다 — 위 함수가 이것의 얇은 별칭이다.
    void ClearRenderTargetsRect(ID3D12GraphicsCommandList* commandList,
        const RHIRenderTargetBinding& binding, const float rgba[4], const D3D12_RECT* rect);
    void ClearDepthTarget(ID3D12GraphicsCommandList* commandList,
        const RHIRenderTargetBinding& binding, float depth);

    /// 인터페이스에서 빠졌다(A-3) — DX12Encoder 만 부른다. 구현이 여기 남은
    /// 것은 셰이더 가시·비가시 디스크립터를 짝지어야 하고 두 힙이 여기 있기
    /// 때문이다(거는 셋을 R4-1 에서 내린 것과 같은 경계: 커맨드를 적는가).
    void ClearUnorderedAccess(ID3D12GraphicsCommandList* commandList,
        const RHIBindingDesc& view, const float rgba[4]);

    /// ClearUnorderedAccessViewFloat이 요구하는 비셰이더 가시 UAV 디스크립터.
    /// 인코더가 그 짝을 맞추는 데 쓴다 — 호출부는 이것을 몰라도 된다.
    D3D12_CPU_DESCRIPTOR_HANDLE CreateClearDescriptor(const RHIBindingDesc& desc);

    /// 설명 하나가 가리키는 실제 리소스. dim이 텍스처 칸과 버퍼 칸 중
    /// 어느 쪽을 보는지 정한다(V2-b).
    ID3D12Resource* ResolveBinding(const RHIBindingDesc& desc) const;

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
    /// 〃 버퍼 판 (A-4). 메시 캐시가 정점·인덱스 버퍼를 이렇게 올린다 —
    /// 소유는 캐시의 ComPtr 이 계속 들고 표는 빌려 볼 뿐이다.
    RHIBufferHandle RegisterExternalBuffer(ID3D12Resource* resource)
    {
        return m_resourceTable.AddExternalBuffer(resource);
    }

    /// 칸을 비운다. 부르는 쪽이 GPU 완료를 보장한 뒤여야 한다 — 표는 펜스를
    /// 보지 않는다(그래프 수명 규칙과 같은 계약).
    void ReleaseTexture(RHITextureHandle handle) override { m_resourceTable.Release(handle); }
    void TransitionResources(std::span<const RHITransition> transitions) override;
    void TransitionBuffers(std::span<const RHIBufferTransition> transitions) override;

    /// 중립 상태 → D3D12 상태. 그래프의 배리어 계획도 이것을 쓴다.
    static D3D12_RESOURCE_STATES ToD3D12(RHIResourceState state);
    void ReleaseBuffer(RHIBufferHandle handle) { m_resourceTable.Release(handle); }

    /// 살아 있는 칸 수 — 진단용. 프레임마다 늘면 누가 안 놓고 있다는 뜻이다.
    size_t GetLiveTextureCount() const { return m_resourceTable.LiveTextureCount(); }

    RHITextureInfo DescribeTexture(RHITextureHandle handle) const override;

    ID3D12Resource* Resolve(RHITextureHandle handle) const { return m_resourceTable.Resolve(handle); }
    ID3D12Resource* Resolve(RHIBufferHandle handle) const { return m_resourceTable.Resolve(handle); }

    /// 버퍼의 GPU 주소 (A-4). 인코더의 드로우 루프가 쓴다 — 근거는 표에 있다.
    D3D12_GPU_VIRTUAL_ADDRESS ResolveGpuAddress(RHIBufferHandle handle) const
    {
        return m_resourceTable.ResolveGpuAddress(handle);
    }

    // ── 파이프라인 핸들 표 (A-1) ──
    //
    // ★ **`IRenderDeviceServices` 에 올리지 않는다.** 인터페이스에 DX12 반환형을
    //   더하면 V7(§7.4 조건 1)이 멀어진다. 올릴 이유도 없다 — 이것을 푸는 것은
    //   `DX12Encoder` 뿐이고, 그쪽은 이 **구현 클래스**를 이미 들고 있다
    //   (그래프가 생성자로 준다).
    //
    // ★ 만드는 쪽과 표가 갈린 것은 V2 의 `RegisterExternalTexture` 와 같은
    //   모양이다 — 캐시가 만들어 오래 들고, 표는 소비처에 핸들을 주려고 든다.
    //   그 주석이 "이쪽은 과도기가 아니다"라고 적어 둔 부류다.
    RHIPipelineHandle RegisterPipeline(ID3D12PipelineState* pipeline,
        ID3D12RootSignature* signature)
    {
        return m_resourceTable.AddPipeline(pipeline, signature);
    }
    RHIPipelineLayoutHandle RegisterPipelineLayout(ID3D12RootSignature* signature,
        uint64_t stableHash)
    {
        return m_resourceTable.AddPipelineLayout(signature, stableHash);
    }

    DX12PipelineEntry Resolve(RHIPipelineHandle handle) const
    {
        return m_resourceTable.Resolve(handle);
    }
    DX12PipelineLayoutEntry Resolve(RHIPipelineLayoutHandle handle) const
    {
        return m_resourceTable.Resolve(handle);
    }

    // ── 패스 소유 리소스(R2c) — 구현은 .cpp에 ──
    bool CreateBuffer(const RHIBufferDesc& desc,
        RHIBufferHandle& outHandle, std::string& outError) override;
    bool CreateTexture(const RHITextureDesc& desc,
        RHITextureHandle& outHandle, std::string& outError) override;

    // ── 리드백(R2c-b) — 구현은 .cpp에 ──
    bool CreateReadback(uint32_t width, uint32_t height, RHIFormat format,
        uint32_t sliceCount, RHIReadback& outReadback, std::string& outError) override;
    void CopyToReadback(ID3D12GraphicsCommandList* commandList,
        const RHIReadback& readback, ID3D12Resource* source,
        uint32_t slice = 0, uint32_t sourceSubresource = 0);
    void CopyVolumeToReadback(ID3D12GraphicsCommandList* commandList,
        const RHIReadback& readback, ID3D12Resource* source,
        uint32_t sourceSubresource = 0);
    void CopyPartialToReadback(ID3D12GraphicsCommandList* commandList,
        const RHIReadback& readback, ID3D12Resource* source,
        uint32_t slice = 0, uint32_t sourceSubresource = 0);
    bool MapReadback(const RHIReadback& readback,
        RHIReadbackImage& outImage, std::string& outError) override;
    void ReleaseReadback(RHIReadback& readback) override
    {
        m_resourceTable.Release(readback.buffer);
        readback = RHIReadback{};
    }
    bool CreateBufferReadback(uint64_t bytes,
        RHIReadback& outReadback, std::string& outError) override;
    void CopyBufferToReadback(ID3D12GraphicsCommandList* commandList,
        const RHIReadback& readback, ID3D12Resource* source,
        uint64_t sourceOffset = 0, uint64_t bytes = 0);

private:
    template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    RHIUploadMemoryBudget QueryUploadMemoryBudget() const;
    void RefreshUploadBudget();

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

    /// 지금 열린 리스트에 붙은 인코더 (A-3). 리스트를 Reset 할 때마다 다시
    /// 만든다 — 인코더가 기억하는 디스크립터 힙 바인딩이 Reset 으로 풀리므로,
    /// 들고 있으면 '걸었다고 기억하는데 안 걸린' 상태가 된다.
    std::unique_ptr<DX12Encoder>       m_immediateEncoder;

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

    DX12UploadSegmentAllocator         m_uploadAllocator;
    bool                               m_uploadMemoryPressure{ false };
    uint64_t                           m_nextRecordingId{ 1 };
    uint64_t                           m_currentRecordingId{ 0 };
    std::vector<IRHIUploadTransactionListener*> m_uploadTransactionListeners;
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
