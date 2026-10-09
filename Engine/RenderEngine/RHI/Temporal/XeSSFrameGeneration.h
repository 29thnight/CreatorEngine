#pragma once

#include "XeSSUpscaler.h"
#include "../../Render/Temporal/ITemporalFrameGenerator.h"

#include <functional>

struct ID3D12CommandQueue;
struct ID3D12CommandList;
struct IDXGIFactory2;
struct IDXGISwapChain3;
struct DXGI_SWAP_CHAIN_DESC1;
class DX12DeviceResources;

enum class XeSSResourceLifetime : uint8_t { OnlyNow, UntilPresent };
enum class XeSSLatencyMarker : uint8_t { SimulationStart, SimulationEnd, RenderSubmitStart, RenderSubmitEnd, InputSample };

struct XeSSFrameGenerationDX12Bindings
{
    XeSSDX12Texture depth;
    XeSSDX12Texture motionVectors;
    XeSSDX12Texture hudlessColor;
    XeSSDX12Texture uiColor;
    XeSSResourceLifetime lifetime{ XeSSResourceLifetime::OnlyNow };
};

struct XeSSPresentStatus
{
    uint32_t framesPresented{ 0 };
    bool enabled{ false };
    TemporalResult interpolation;
};

struct XeSSFrameGenerationDX12State;

// Serial shell-owned adapter, independent of the chosen upscaler. XeLL Sleep
// must precede input sampling; the shell emits simulation/render markers and
// uses Present below. No existing engine swap chain is consumed or replaced.
class XeSSFrameGenerationDX12 final
{
public:
    XeSSFrameGenerationDX12();
    ~XeSSFrameGenerationDX12();
    XeSSFrameGenerationDX12(const XeSSFrameGenerationDX12&) = delete;
    XeSSFrameGenerationDX12& operator=(const XeSSFrameGenerationDX12&) = delete;

    static TemporalResult QuerySupport(ID3D12Device* device, const wchar_t* runtimeDirectory,
        uint32_t& maximumInterpolatedFrames);
    static TemporalResult QueryVulkanSupport() { return { TemporalStatus::BackendUnsupported }; }
    TemporalResult Initialize(ID3D12Device* device, ID3D12CommandQueue* queue, IDXGIFactory2* factory,
        void* window, const DXGI_SWAP_CHAIN_DESC1& description, const wchar_t* runtimeDirectory,
        const TemporalFrameGenerationConfig& config, bool depthInverted,
        std::function<TemporalResult()> drain, bool uiPremultiplied = true);
    // Initialization leaves generation disabled. The mandatory callback proves
    // GPU retirement before mode/count changes, resize, and shutdown.
    TemporalResult SetEnabled(bool enabled);
    TemporalResult SetInterpolatedFrames(uint32_t count);
    TemporalResult Sleep(uint64_t realFrameId);
    // CPU-only dropped frame: caller guarantees no further producer SDK calls
    // for this ID. No tagged/retained GPU input may be discarded by this method.
    TemporalResult DiscardRealFrame(uint64_t realFrameId);
    TemporalResult ReleaseInputsAfterGpuIdle();
    TemporalResult Mark(uint64_t realFrameId, XeSSLatencyMarker marker);
    // Serial baseline drains the previous frame before replacing its retained
    // owner. A failed/partially recorded tag keeps the owner until a successful drain.
    TemporalResult Prepare(ID3D12CommandList* commands, const TemporalFrame& frame,
        const XeSSFrameGenerationDX12Bindings& bindings, std::shared_ptr<const void> lifetimeToken);
    TemporalResult Present(uint64_t realFrameId, uint32_t syncInterval, uint32_t flags);
    TemporalResult GetLastPresentStatus(XeSSPresentStatus& status) const;
    TemporalResult Resize(TemporalExtent displayExtent);
    // Borrow only for buffers/metadata. Do not Present directly, cache extra COM
    // references, or Resize outside this adapter. Release every external reference
    // before Shutdown; the queue/device must outlive successful Shutdown.
    IDXGISwapChain3* GetSwapChain() const;
    uint32_t GetMaximumInterpolatedFrames() const;
    std::string GetRuntimeVersion() const;
    TemporalResult Shutdown();

private:
    std::unique_ptr<XeSSFrameGenerationDX12State> m_state;
};

// The drain callback must prove retirement of both engine tagging lists and SDK
// presentation work. A successful Present is NOT a producer-lease completion.
// This factory publishes no active engine feature; the shell chooses when to enable.
TemporalResult CreateXeSSFrameGeneratorDX12(DX12DeviceResources& resources,
    ID3D12CommandQueue* queue, IDXGIFactory2* factory, void* window,
    const DXGI_SWAP_CHAIN_DESC1& description, const wchar_t* runtimeDirectory,
    const TemporalFrameGenerationConfig& config, bool depthInverted,
    std::function<TemporalResult()> drain,
    std::unique_ptr<ITemporalFrameGenerator>& frameGenerator,
    XeSSFrameGenerationDX12*& presentation);
