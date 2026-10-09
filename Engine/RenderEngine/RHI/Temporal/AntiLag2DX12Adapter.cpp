#include "AntiLag2DX12Adapter.h"
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <mutex>
#ifndef CREATOR_ENABLE_ANTILAG2_SDK
#define CREATOR_ENABLE_ANTILAG2_SDK 0
#endif
#if CREATOR_ENABLE_ANTILAG2_SDK
#include <ffx_antilag2_dx12.h>
#endif
struct AntiLag2DX12Adapter::State
{
    std::mutex mutex;
    Microsoft::WRL::ComPtr<IDXGISwapChain> swapchain;
#if CREATOR_ENABLE_ANTILAG2_SDK
    AMD::AntiLag2DX12::Context context{};
#endif
    bool initialized{ false };
};
namespace
{
    // Official Anti-Lag2 SDK README ABI, consumed by FSR >=3.1.1 DX12 proxy.
    const GUID kAntiLagContext{ 0x5083ae5b, 0x8070, 0x4fca, { 0x8e, 0xe5, 0x35, 0x82, 0xdd, 0x36, 0x7d, 0x13 } };
    TemporalResult Result(HRESULT hr)
    { return { hr == S_OK ? TemporalStatus::Success : TemporalStatus::FeatureUnsupported, hr }; }
}
AntiLag2DX12Adapter::AntiLag2DX12Adapter() : m_state(std::make_unique<State>()) {}
AntiLag2DX12Adapter::~AntiLag2DX12Adapter()
{
    // Shutdown is explicit after FSR final consumption. Preserve a context
    // still installed in a proxy instead of leaving a dangling callback pointer.
    if (m_state && m_state->swapchain) (void)m_state.release();
    else Shutdown();
}
TemporalResult AntiLag2DX12Adapter::Initialize(ID3D12Device* device)
{
    std::lock_guard lock(m_state->mutex);
#if CREATOR_ENABLE_ANTILAG2_SDK
    if (m_state->initialized) return { TemporalStatus::AlreadyInitialized };
    const auto result = Result(AMD::AntiLag2DX12::Initialize(&m_state->context, device));
    m_state->initialized = result.IsSuccess();
    return result;
#else
    return { TemporalStatus::SdkNotBuilt };
#endif
}
TemporalResult AntiLag2DX12Adapter::BeforeInput()
{
    std::lock_guard lock(m_state->mutex);
#if CREATOR_ENABLE_ANTILAG2_SDK
    if (!m_state->initialized) return { TemporalStatus::NotInitialized };
    return Result(AMD::AntiLag2DX12::Update(&m_state->context, true, 0));
#else
    return { TemporalStatus::SdkNotBuilt };
#endif
}
TemporalResult AntiLag2DX12Adapter::EndRendering()
{
    std::lock_guard lock(m_state->mutex);
#if CREATOR_ENABLE_ANTILAG2_SDK
    if (!m_state->initialized) return { TemporalStatus::NotInitialized };
    return Result(AMD::AntiLag2DX12::MarkEndOfFrameRendering(&m_state->context));
#else
    return { TemporalStatus::SdkNotBuilt };
#endif
}
TemporalResult AntiLag2DX12Adapter::BindFsrSwapchain(IDXGISwapChain* swapchain)
{
    std::lock_guard lock(m_state->mutex);
#if CREATOR_ENABLE_ANTILAG2_SDK
    if (!m_state->initialized || !swapchain) return { TemporalStatus::NotInitialized };
    struct PrivateData { AMD::AntiLag2DX12::Context* context; bool enabled; } data{ &m_state->context, true };
    const auto result = Result(swapchain->SetPrivateData(kAntiLagContext, sizeof(data), &data));
    if (result.IsSuccess()) m_state->swapchain = swapchain;
    return result;
#else
    return { TemporalStatus::SdkNotBuilt };
#endif
}
TemporalResult AntiLag2DX12Adapter::Shutdown()
{
    if (!m_state) return { TemporalStatus::Success };
    std::lock_guard lock(m_state->mutex);
#if CREATOR_ENABLE_ANTILAG2_SDK
    if (m_state->swapchain)
    {
        const auto result = Result(m_state->swapchain->SetPrivateData(kAntiLagContext, 0, nullptr));
        if (!result.IsSuccess()) return result;
        m_state->swapchain.Reset();
    }
    if (m_state->initialized) AMD::AntiLag2DX12::DeInitialize(&m_state->context);
    m_state->initialized = false;
#endif
    return { TemporalStatus::Success };
}
