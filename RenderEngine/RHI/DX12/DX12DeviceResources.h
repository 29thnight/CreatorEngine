#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include <cstdint>
#include <string>
#include <array>
#include <wrl/client.h>
#include <d3d12.h>
#include <dxgi1_6.h>

#include "DX12UploadRing.h"

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
class DX12DeviceResources
{
public:
    static constexpr uint32_t kFrameCount = 3;

    // 렌더 타깃의 최적화 클리어 값. 생성 힌트와 실제 클리어가 일치해야 검증
    // 레이어가 조용하다 — 힌트를 안 주는 쪽도, 다른 값으로 지우는 쪽도 경고를 쌓는다
    // (둘 다 실측). 그래서 클리어 색은 여기 상수 하나로 고정한다.
    static constexpr float kClearColor[4] = { 0.1f, 0.1f, 0.15f, 1.f };

    // 실패 사유를 문자열로 돌려준다 — 브링업 단계의 실패는 전부 진단 가능해야 한다.
    bool Initialize(uint32_t width, uint32_t height, std::string& outError);
    void Shutdown();

    bool IsInitialized() const { return nullptr != m_device.Get(); }

    // 이번 프레임의 얼로케이터를 준비하고(필요 시 펜스 대기) 커맨드 리스트를 연다.
    bool BeginFrame(std::string& outError);
    // 커맨드 리스트를 닫고 제출한 뒤 펜스 신호를 건다.
    bool EndFrame(std::string& outError);
    // 모든 제출 완료까지 대기(리드백 읽기 전·종료 전).
    void WaitForGpu();

    // 검증 레이어 메시지를 비우고, 그중 '실제 문제'(WARNING 이상)의 수를 돌려준다.
    //
    // INFO/MESSAGE 등급은 세지 않는다 — 파이프라인 라이브러리의 첫 조회 실패
    // ("이 이름의 파이프라인이 없음")처럼 정상 경로가 남기는 안내가 여기 들어와서,
    // 전부 세면 캐시 미스라는 당연한 동작이 검증 실패로 둔갑한다(실측).
    // 대신 outMessages에는 등급을 붙여 전부 담는다 — 세지 않는 것과 감추는 것은 다르다.
    uint32_t DrainDebugMessages(std::string& outMessages);

    ID3D12Device* GetDevice() const { return m_device.Get(); }
    ID3D12GraphicsCommandList* GetCommandList() const { return m_commandList.Get(); }
    ID3D12Resource* GetRenderTarget() const { return m_renderTarget.Get(); }
    D3D12_CPU_DESCRIPTOR_HANDLE GetRtvHandle() const { return m_rtvHandle; }
    ID3D12Resource* GetReadbackBuffer() const { return m_readback.Get(); }
    uint32_t GetWidth() const { return m_width; }
    uint32_t GetHeight() const { return m_height; }
    uint32_t GetRowPitch() const { return m_rowPitch; }

    // 프레임 업로드 링. BeginFrame이 이 프레임 구간을 되감아 준다.
    //
    // 여기 묶어 두는 이유: 링의 반납 규칙이 "BeginFrame이 그 슬롯의 펜스를
    // 기다린 뒤에 되감는다"에 기대고 있다. 링을 따로 들고 다니면 그 계약이
    // 호출부 규율이 되고, 한 곳만 어겨도 GPU가 읽는 중인 데이터를 덮어쓴다.
    DX12UploadRing& GetUploadRing() { return m_uploadRing; }
    const DX12UploadRing& GetUploadRing() const { return m_uploadRing; }

private:
    template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

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
    ComPtr<ID3D12Resource>             m_readback;

    DX12UploadRing                     m_uploadRing;

    uint32_t m_width{ 0 };
    uint32_t m_height{ 0 };
    uint32_t m_rowPitch{ 0 };
};

#endif
