#pragma once
#include "../../Render/Temporal/TemporalReconstruction.h"
#include <memory>
struct ID3D12Device;
struct IDXGISwapChain;
// Optional official Anti-Lag2 v2.0.0a source header. No bundled driver/DLL load.
class AntiLag2DX12Adapter
{
public:
    AntiLag2DX12Adapter();
    ~AntiLag2DX12Adapter();
    TemporalResult Initialize(ID3D12Device*);
    TemporalResult BeforeInput();
    TemporalResult EndRendering();
    TemporalResult BindFsrSwapchain(IDXGISwapChain*);
    // Caller drains all FSR presents and removes the proxy before destroying
    // its device. Shutdown clears the proxy private-data pointer first.
    TemporalResult Shutdown();
private:
    struct State;
    std::unique_ptr<State> m_state;
};
