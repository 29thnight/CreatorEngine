#include "../Engine/RenderEngine/RenderAllocator.h"
#pragma once
#if defined(CE_CONTRACT_STANDALONE)
#include "../Tools/build/ModuleAdapters/Creator.Core.Ownership.h"
#else
import Creator.Core.Ownership;
#endif

#include "RHI/RHIDisplayFrame.h"
#if defined(CE_CONTRACT_STANDALONE)
#include "../Engine/Platform/WindowsSDK/Creator.WinAPI.h"
#else
import "Creator.WinAPI.h";
#endif
#include "../Engine/Utility_Framework/Foundation/StandardLibrary.h"

class DX12DeviceResources;
struct IDXGISwapChain3;
struct ID3D12Resource;

namespace Player
{
// Player-only native SDK owner. Renderer has no access to this proxy, pacing,
// native fences or the host's imported RHI handles.
class TemporalDX12 final
{
public:
    explicit TemporalDX12(DX12DeviceResources& resources);
    ~TemporalDX12();
    bool Configure(HWND window, uint32_t width, uint32_t height, std::string& error);
    bool HasProxy() const;
    bool RequiresLatencyMarkers() const;
    void CommitConfiguration();
    IDXGISwapChain3* GetSwapchain() const; // Borrowed; never cache an owning COM ref.
    bool BeginSimulationFrame(uint64_t realFrameId, std::string& error);
    void Mark(uint64_t realFrameId, RHITemporalLatencyMarker marker);
    void OpenRealFrame(const TemporalMeasurementProvenance& provenance);
    bool PresentRealFrame(std::string& error, bool sourceComposed);
    void StopSimulationFrames();
    void Discard(uint64_t realFrameId);
    bool SuspendProxy(std::string& error);
    bool Open(const RHITemporalDisplayPacket& packet, std::string& error);
    bool Prepare(std::string& error);
    bool Present(std::string& error);
    // Called after final-consumption drain. Records read->COPY_DEST on the same
    // imported resources, submits and waits BEFORE releasing the producer lease.
    bool RestoreAndRelease(std::string& error);
    bool Drain(std::string& error);
    bool Shutdown(std::string& error, bool stopGameLoop = true);
    void LatchFailure(const std::string& reason, bool recordingAborted);
    bool HasPendingInputs() const;
    bool RequiresReconfigure();
    uint64_t RealFrameId() const;
    ID3D12Resource* GetSdkComposedRealFrameColor() const;

private:
    struct State;
    own::unique_owner<State> m_state;
};
}
