#pragma once
#ifndef DYNAMICCPP_EXPORTS

#include "EnhancedSceneRenderer.h"
#include "../../RHI/RHIHandle.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class IImGuiHost;
class IRHIGpuProfiler;
class IRenderDeviceServices;
class IRenderMeshCache;
class IRenderPipelineCache;
class IRenderRootSignatureCache;
class IRenderTextureCache;

/// EnhancedSceneRenderer의 공용 라이브 러너와 DX12 구현 사이의 경계.
///
/// 헤더에는 D3D12/DXGI/HANDLE/ComPtr가 없다. 공유 텍스처와 프로파일러의
/// native 접근은 구현 파일 안에만 있고, 공용 러너는 RHI 핸들과 불투명 토큰만
/// 보관한다. 파이프라인을 재구축해도 외부 소비자가 열어 둔 공유 리소스는
/// ShutdownInterop까지 격리 보관한다.
class EnhancedSceneRendererLiveDX12Adapter
{
public:
    using DisplayToken = uint64_t;
    static constexpr DisplayToken kInvalidDisplayToken = 0;
    static constexpr uint32_t kFrameCount = 3;

    EnhancedSceneRendererLiveDX12Adapter();
    ~EnhancedSceneRendererLiveDX12Adapter();

    EnhancedSceneRendererLiveDX12Adapter(
        const EnhancedSceneRendererLiveDX12Adapter&) = delete;
    EnhancedSceneRendererLiveDX12Adapter& operator=(
        const EnhancedSceneRendererLiveDX12Adapter&) = delete;

    bool Initialize(uint32_t width, uint32_t height, std::string& outError);
    bool Resize(uint32_t width, uint32_t height, std::string& outError);
    void ShutdownPipeline();
    void ShutdownInterop();

    bool IsInitialized() const;
    bool BeginFrame(std::string& outError);
    void AbortFrame();
    bool EndFrame(std::string& outError);
    void WaitForGpu();
    uint64_t GetCompletedFenceValue() const;
    uint64_t GetLastSignaledFenceValue() const;

    IRenderDeviceServices& Resources();
    IRenderPipelineCache& Pipelines();
    IRenderRootSignatureCache& RootSignatures();
    IRenderMeshCache& MeshCache();
    IRenderTextureCache& TextureCache();
    IRHIGpuProfiler* Profiler();

    bool CreateDisplayTexture(uint32_t width, uint32_t height,
        RHITextureHandle& outTexture, DisplayToken& outToken,
        std::string& outError);
    void RetireDisplayTexture(DisplayToken token);
    uint64_t OpenDisplayTexture(IImGuiHost& host, DisplayToken token) const;
    size_t GetRetiredDisplayCount() const;

    bool CreateFogCloudNeutral(RHITextureHandle& outTexture,
        std::string& outError);
    void ReleaseFogCloudNeutral(RHITextureHandle& texture);

    void BeginProfilerFrame(uint32_t frameIndex);
    void ResolveProfilerFrame();
    bool CollectProfiler(std::vector<EnhancedLivePassTiming>& outTimings,
        double& outTotalMilliseconds, std::string& outError);

    void MaintainAssetCaches(uint64_t frameIndex);
    uint32_t DrainDebugMessages(std::string& outMessages);
    uint32_t GetAssetGraveyardCount() const;

    std::string FormatUploadStatus() const;
    std::string FormatAssetStatus() const;
    std::string FormatTargetHeapStatus() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

#endif
