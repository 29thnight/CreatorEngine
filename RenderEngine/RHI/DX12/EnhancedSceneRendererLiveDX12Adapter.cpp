#ifndef DYNAMICCPP_EXPORTS
#include "../../Render/Scene/EnhancedSceneRendererLiveDX12Adapter.h"

#include "../../GpuDiagnostics.h"
#include "../IDisplayPresentationSink.h"
#include "../../RHI/IRHIDeviceResources.h"
#include "../../RHI/RHIAssetEvictionPolicy.h"
#include "../../RHI/DX12/DX12DeviceResources.h"
#include "../../RHI/DX12/DX12GpuProfiler.h"
#include "../../RHI/DX12/DX12MeshCache.h"
#include "../../RHI/DX12/DX12PSOManager.h"
#include "../../RHI/DX12/DX12RootSignatureCache.h"
#include "../../RHI/DX12/DX12TextureCache.h"

#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>
#include <wrl/client.h>

struct EnhancedSceneRendererLiveDX12Adapter::Impl
{
    template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    struct DisplayResource
    {
        DisplayToken token{ kInvalidDisplayToken };
        ComPtr<ID3D12Resource> texture;
        HANDLE sharedHandle{ nullptr };
    };

    DX12DeviceResources resources;
    DX12PSOManager pipelines;
    DX12RootSignatureCache rootSignatures;
    DX12MeshCache meshCache;
    DX12TextureCache textureCache;
    DX12GpuProfiler profiler;

    ComPtr<ID3D12Resource> fogCloudNeutral;
    std::vector<DisplayResource> activeDisplays;
    std::vector<DisplayResource> retiredDisplays;
    DisplayToken nextDisplayToken{ 1 };
};

EnhancedSceneRendererLiveDX12Adapter::EnhancedSceneRendererLiveDX12Adapter()
    : m_impl(std::make_unique<Impl>())
{
}

EnhancedSceneRendererLiveDX12Adapter::~EnhancedSceneRendererLiveDX12Adapter()
{
    ShutdownPipeline();
    ShutdownInterop();
}

bool EnhancedSceneRendererLiveDX12Adapter::Initialize(
    uint32_t width, uint32_t height, std::string& outError)
{
    Impl& impl = *m_impl;
    if (!impl.resources.Initialize(width, height, outError)) return false;
    if (!impl.pipelines.Initialize(&impl.resources, L"dx12_live.cache", outError) ||
        !impl.rootSignatures.Initialize(&impl.resources, outError) ||
        !impl.meshCache.Initialize(&impl.resources, outError) ||
        !impl.textureCache.Initialize(&impl.resources, outError) ||
        !impl.profiler.Initialize(impl.resources.GetDevice(),
            impl.resources.GetCommandQueue(), 64, kFrameCount, outError))
    {
        return false;
    }

    SetDiagnosticsDeviceResources(&impl.resources);
    return true;
}

bool EnhancedSceneRendererLiveDX12Adapter::Resize(
    uint32_t width, uint32_t height, std::string& outError)
{
    return m_impl->resources.Resize(width, height, outError);
}

void EnhancedSceneRendererLiveDX12Adapter::ShutdownPipeline()
{
    Impl& impl = *m_impl;
    if (impl.resources.IsInitialized())
    {
        std::string lifecycleError;
        impl.resources.DrainForLifecycle(
            RHILifecycleCommand::BackendShutdown, lifecycleError);
        if (!lifecycleError.empty())
        {
            OutputDebugStringA(("[DX12 live] backend shutdown drain 실패: " +
                lifecycleError + "\n").c_str());
        }
    }

    // active는 공용 파이프라인 해체가 RetireDisplayTexture로 비워야 한다.
    // 부분 초기화 실패에서도 누락되지 않도록 여기서 남은 항목을 격리한다.
    for (Impl::DisplayResource& display : impl.activeDisplays)
    {
        impl.retiredDisplays.push_back(std::move(display));
    }
    impl.activeDisplays.clear();
    impl.fogCloudNeutral.Reset();

    if (impl.resources.IsInitialized())
    {
        GpuDiagnostics::LogCensus("라이브 파이프라인 해체 직전", true);
    }
    SetDiagnosticsDeviceResources(nullptr);

    impl.profiler.Shutdown();
    impl.textureCache.Shutdown();
    impl.meshCache.Shutdown();
    impl.rootSignatures.Shutdown();
    impl.pipelines.Shutdown();
    impl.resources.Shutdown();
}

void EnhancedSceneRendererLiveDX12Adapter::ShutdownInterop()
{
    Impl& impl = *m_impl;
    const auto closeAll = [](std::vector<Impl::DisplayResource>& displays)
    {
        for (Impl::DisplayResource& display : displays)
        {
            if (nullptr != display.sharedHandle) ::CloseHandle(display.sharedHandle);
            display.sharedHandle = nullptr;
            display.texture.Reset();
        }
        displays.clear();
    };
    closeAll(impl.activeDisplays);
    closeAll(impl.retiredDisplays);
}

bool EnhancedSceneRendererLiveDX12Adapter::IsInitialized() const
{
    return m_impl->resources.IsInitialized();
}

bool EnhancedSceneRendererLiveDX12Adapter::BeginFrame(std::string& outError)
{
    return m_impl->resources.BeginFrame(outError);
}

void EnhancedSceneRendererLiveDX12Adapter::AbortFrame()
{
    m_impl->resources.AbortFrame();
}

bool EnhancedSceneRendererLiveDX12Adapter::EndFrame(std::string& outError)
{
    return m_impl->resources.EndFrame(outError);
}

void EnhancedSceneRendererLiveDX12Adapter::WaitForGpu()
{
    if (IsInitialized()) m_impl->resources.WaitForGpu();
}

bool EnhancedSceneRendererLiveDX12Adapter::DrainForLifecycle(
    RHILifecycleCommand command, std::string& outError)
{
    return !IsInitialized() ||
        m_impl->resources.DrainForLifecycle(command, outError);
}

uint64_t EnhancedSceneRendererLiveDX12Adapter::GetCompletedFenceValue() const
{
    return m_impl->resources.GetCompletedFenceValue();
}

uint64_t EnhancedSceneRendererLiveDX12Adapter::GetLastSignaledFenceValue() const
{
    return m_impl->resources.GetLastSignaledFenceValue();
}

IRenderDeviceServices& EnhancedSceneRendererLiveDX12Adapter::Resources()
{
    return m_impl->resources;
}

IRenderPipelineCache& EnhancedSceneRendererLiveDX12Adapter::Pipelines()
{
    return m_impl->pipelines;
}

IRenderRootSignatureCache& EnhancedSceneRendererLiveDX12Adapter::RootSignatures()
{
    return m_impl->rootSignatures;
}

IRenderMeshCache& EnhancedSceneRendererLiveDX12Adapter::MeshCache()
{
    return m_impl->meshCache;
}

IRenderTextureCache& EnhancedSceneRendererLiveDX12Adapter::TextureCache()
{
    return m_impl->textureCache;
}

IRHIGpuProfiler* EnhancedSceneRendererLiveDX12Adapter::Profiler()
{
    return &m_impl->profiler;
}

bool EnhancedSceneRendererLiveDX12Adapter::CreateDisplayTexture(
    uint32_t width, uint32_t height, RHITextureHandle& outTexture,
    DisplayToken& outToken, std::string& outError)
{
    outTexture = {};
    outToken = kInvalidDisplayToken;
    Impl& impl = *m_impl;

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    Impl::DisplayResource display;
    if (FAILED(impl.resources.GetDevice()->CreateCommittedResource(&heap,
        D3D12_HEAP_FLAG_SHARED, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr, IID_PPV_ARGS(&display.texture))))
    {
        outError = "공유 텍스처 생성 실패";
        return false;
    }

    outTexture = impl.resources.RegisterExternalTexture(display.texture.Get());
    if (!outTexture.IsValid())
    {
        outError = "공유 텍스처 RHI 등록 실패";
        return false;
    }

    if (FAILED(impl.resources.GetDevice()->CreateSharedHandle(display.texture.Get(),
        nullptr, GENERIC_ALL, nullptr, &display.sharedHandle)) ||
        nullptr == display.sharedHandle)
    {
        impl.resources.ReleaseTexture(outTexture);
        outTexture = {};
        outError = "공유 핸들 생성 실패";
        return false;
    }

    display.token = impl.nextDisplayToken++;
    if (kInvalidDisplayToken == impl.nextDisplayToken) ++impl.nextDisplayToken;
    outToken = display.token;
    impl.activeDisplays.push_back(std::move(display));
    return true;
}

void EnhancedSceneRendererLiveDX12Adapter::RetireDisplayTexture(DisplayToken token)
{
    if (kInvalidDisplayToken == token) return;
    Impl& impl = *m_impl;
    for (auto it = impl.activeDisplays.begin(); it != impl.activeDisplays.end(); ++it)
    {
        if (it->token != token) continue;
        impl.retiredDisplays.push_back(std::move(*it));
        impl.activeDisplays.erase(it);
        return;
    }
}

uint64_t EnhancedSceneRendererLiveDX12Adapter::OpenDisplayTexture(
    IDisplayPresentationSink& sink, DisplayToken token) const
{
    if (kInvalidDisplayToken == token) return 0;
    for (const Impl::DisplayResource& display : m_impl->activeDisplays)
    {
        if (display.token == token)
            return sink.OpenSharedTexture(display.sharedHandle);
    }
    return 0;
}

size_t EnhancedSceneRendererLiveDX12Adapter::GetRetiredDisplayCount() const
{
    return m_impl->retiredDisplays.size();
}

bool EnhancedSceneRendererLiveDX12Adapter::CreateFogCloudNeutral(
    RHITextureHandle& outTexture, std::string& outError)
{
    Impl& impl = *m_impl;
    if (impl.fogCloudNeutral)
    {
        return outTexture.IsValid();
    }

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = 1;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;

    if (FAILED(impl.resources.GetDevice()->CreateCommittedResource(&heap,
        D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON,
        nullptr, IID_PPV_ARGS(&impl.fogCloudNeutral))))
    {
        outError = "포그 중립 구름 텍스처 생성 실패";
        return false;
    }
    impl.fogCloudNeutral->SetName(L"Fog.CloudNeutral");

    const RHIBufferSlice staging = impl.resources.AllocateUpload(
        RHIUploadRequest{ D3D12_TEXTURE_DATA_PITCH_ALIGNMENT,
            RHIUploadUsage::TextureCopy, 1 });
    if (!staging.IsValid())
    {
        impl.fogCloudNeutral.Reset();
        outError = "포그 중립 구름 업로드 링 할당 실패";
        return false;
    }
    const uint8_t white[4]{ 255, 255, 255, 255 };
    std::memcpy(staging.cpuAddress, white, sizeof(white));

    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource = impl.fogCloudNeutral.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;

    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource = impl.resources.Resolve(staging.buffer);
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint.Offset = staging.offset;
    src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    src.PlacedFootprint.Footprint.Width = 1;
    src.PlacedFootprint.Footprint.Height = 1;
    src.PlacedFootprint.Footprint.Depth = 1;
    src.PlacedFootprint.Footprint.RowPitch = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
    impl.resources.GetCommandList()->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    outTexture = impl.resources.RegisterExternalTexture(impl.fogCloudNeutral.Get());
    if (!outTexture.IsValid())
    {
        impl.fogCloudNeutral.Reset();
        outError = "포그 중립 구름 RHI 등록 실패";
        return false;
    }
    const RHITransition toSrv[] = {
        { outTexture, RHIResourceState::CopyDest, RHIResourceState::ShaderResource } };
    impl.resources.TransitionResources(toSrv);
    return true;
}

void EnhancedSceneRendererLiveDX12Adapter::ReleaseFogCloudNeutral(
    RHITextureHandle& texture)
{
    if (texture.IsValid()) m_impl->resources.ReleaseTexture(texture);
    texture = {};
    m_impl->fogCloudNeutral.Reset();
}

void EnhancedSceneRendererLiveDX12Adapter::BeginProfilerFrame(uint32_t frameIndex)
{
    m_impl->profiler.BeginFrame(frameIndex % kFrameCount);
}

void EnhancedSceneRendererLiveDX12Adapter::ResolveProfilerFrame()
{
    m_impl->profiler.ResolveFrame(m_impl->resources.GetCommandList());
}

bool EnhancedSceneRendererLiveDX12Adapter::CollectProfiler(
    std::vector<EnhancedLivePassTiming>& outTimings,
    double& outTotalMilliseconds, std::string& outError)
{
    std::vector<DX12GpuProfiler::PassTiming> nativeTimings;
    if (!m_impl->profiler.Collect(nativeTimings, outError)) return false;

    outTimings.clear();
    outTimings.reserve(nativeTimings.size());
    for (DX12GpuProfiler::PassTiming& timing : nativeTimings)
    {
        outTimings.push_back({ std::move(timing.name), timing.milliseconds });
    }
    outTotalMilliseconds = m_impl->profiler.GetLastTotalMilliseconds();
    return true;
}

void EnhancedSceneRendererLiveDX12Adapter::MaintainAssetCaches(uint64_t frameIndex)
{
    Impl& impl = *m_impl;
    const uint64_t completedFence = impl.resources.GetCompletedFenceValue();
    const uint64_t lastFence = impl.resources.GetLastSignaledFenceValue();
    impl.textureCache.BeginFrame(frameIndex);
    impl.meshCache.BeginFrame(frameIndex);

    const RHIDeviceMemoryPressureInfo pressureInfo = impl.resources
        .GetPersistentMemoryBudgetCoordinator().GetMemoryPressureInfo();
    RHIAssetEvictionPass evictionPass = BeginRHIAssetEvictionPass(
        pressureInfo.memoryPressure, pressureInfo.targetReleaseBytes);
    impl.textureCache.RetireUnused(lastFence, &evictionPass);
    impl.meshCache.RetireUnused(lastFence, &evictionPass);
    impl.textureCache.SweepGraveyard(completedFence);
    impl.meshCache.SweepGraveyard(completedFence);
}

uint32_t EnhancedSceneRendererLiveDX12Adapter::DrainDebugMessages(
    std::string& outMessages)
{
    return m_impl->resources.DrainDebugMessages(outMessages);
}

uint32_t EnhancedSceneRendererLiveDX12Adapter::GetAssetGraveyardCount() const
{
    const auto texture = m_impl->textureCache.GetStats();
    const auto mesh = m_impl->meshCache.GetStats();
    return texture.graveyardCount + mesh.graveyardCount;
}

std::string EnhancedSceneRendererLiveDX12Adapter::FormatUploadStatus() const
{
    const auto stats = m_impl->resources.GetUploadStats();
    char line[192]{};
    std::snprintf(line, sizeof(line),
        "\n  업로드 링 — 세그먼트 %u개 %.1f MB(증설 %u회) · 최대 프레임 %.2f MB · 거절 %llu",
        stats.segmentCount,
        static_cast<double>(stats.segmentBytes) / (1024.0 * 1024.0),
        stats.growths,
        static_cast<double>(stats.peakFrameBytes) / (1024.0 * 1024.0),
        static_cast<unsigned long long>(stats.overflows));
    return line;
}

std::string EnhancedSceneRendererLiveDX12Adapter::FormatAssetStatus() const
{
    const auto texStats = m_impl->textureCache.GetStats();
    const auto meshStats = m_impl->meshCache.GetStats();
    constexpr double kBytesPerMB = 1024.0 * 1024.0;
    std::string status;

    char textureLine[160]{};
    std::snprintf(textureLine, sizeof(textureLine),
        "\n  텍스처 업로드 %u건 (CPU 직결 %u) · 히트 %u · 실패 %u",
        texStats.uploads, texStats.fromCpuPixels, texStats.hits, texStats.failures);
    status += textureLine;

    char residentLine[192]{};
    std::snprintf(residentLine, sizeof(residentLine),
        "\n  자산 상주 — 텍스처 %u개 %.1f MB · 메시 %u개 %.1f MB · 합계 %.1f MB",
        texStats.residentCount, texStats.residentBytes / kBytesPerMB,
        meshStats.residentCount, meshStats.residentBytes / kBytesPerMB,
        (texStats.residentBytes + meshStats.residentBytes) / kBytesPerMB);
    status += residentLine;

    const RHIPersistentHeapStats& bufferHeap = meshStats.persistentHeap;
    const RHIPersistentHeapStats& textureHeap = texStats.persistentHeap;
    const RHIPersistentHeapStats& budgetStats = 0 != textureHeap.budgetBytes
        ? textureHeap : bufferHeap;
    const RHIDeviceMemoryBudgetCoordinatorStats coordinatorStats =
        m_impl->resources.GetPersistentMemoryBudgetStats();
    char heapLine[768]{};
    std::snprintf(heapLine, sizeof(heapLine),
        "\n  Persistent heap — buffer %u segment %.1f/%.1f MB · texture %u segment %.1f/%.1f MB"
        " · dedicated live %u · trim %llu · fallback %llu"
        "\n  Device-local budget — %.1f/%.1f MB · DXGI LOCAL · pressure %s"
        "\n  Budget coordinator — owner %u · ticket grant/deny %llu/%llu"
        " · pending/committed %.1f/%.1f MB · snapshot %llu",
        bufferHeap.activeSegments, bufferHeap.allocatedBytes / kBytesPerMB,
        bufferHeap.segmentBytes / kBytesPerMB,
        textureHeap.activeSegments, textureHeap.allocatedBytes / kBytesPerMB,
        textureHeap.segmentBytes / kBytesPerMB,
        bufferHeap.liveDedicatedAllocations + textureHeap.liveDedicatedAllocations,
        static_cast<unsigned long long>(bufferHeap.trimmedSegments + textureHeap.trimmedSegments),
        static_cast<unsigned long long>(bufferHeap.dedicatedFallbacks + textureHeap.dedicatedFallbacks),
        budgetStats.budgetUsageBytes / kBytesPerMB,
        budgetStats.budgetBytes / kBytesPerMB,
        (bufferHeap.memoryPressure || textureHeap.memoryPressure) ? "ON" : "off",
        coordinatorStats.registeredOwners,
        static_cast<unsigned long long>(coordinatorStats.growthGrants),
        static_cast<unsigned long long>(coordinatorStats.growthDenials),
        coordinatorStats.reservedGrowthBytes / kBytesPerMB,
        coordinatorStats.committedSinceRefreshBytes / kBytesPerMB,
        static_cast<unsigned long long>(coordinatorStats.snapshotRefreshes));
    status += heapLine;

    char retireLine[224]{};
    std::snprintf(retireLine, sizeof(retireLine),
        "\n  자산 은퇴 — 누적 텍스처 %u개 %.1f MB · 메시 %u개 %.1f MB"
        " · 묘지 %u개 %.1f MB · 격리 %u개",
        texStats.retired, texStats.retiredBytes / kBytesPerMB,
        meshStats.retired, meshStats.retiredBytes / kBytesPerMB,
        texStats.graveyardCount + meshStats.graveyardCount,
        (texStats.graveyardBytes + meshStats.graveyardBytes) / kBytesPerMB,
        texStats.quarantinedCount + meshStats.quarantinedCount);
    status += retireLine;

    char evictionLine[256]{};
    std::snprintf(evictionLine, sizeof(evictionLine),
        "\n  Pressure eviction — pass %llu · 퇴출 %llu개 %.1f MB"
        " · recent 보호 %llu · upload-pending 보호 %llu",
        static_cast<unsigned long long>(texStats.eviction.pressurePasses +
            meshStats.eviction.pressurePasses),
        static_cast<unsigned long long>(texStats.eviction.pressureRetired +
            meshStats.eviction.pressureRetired),
        (texStats.eviction.pressureRetiredBytes +
            meshStats.eviction.pressureRetiredBytes) / kBytesPerMB,
        static_cast<unsigned long long>(texStats.eviction.pressureProtectedRecent +
            meshStats.eviction.pressureProtectedRecent),
        static_cast<unsigned long long>(texStats.eviction.pressureUploadPending +
            meshStats.eviction.pressureUploadPending));
    status += evictionLine;
    return status;
}

std::string EnhancedSceneRendererLiveDX12Adapter::FormatTargetHeapStatus() const
{
    const auto rtvStats = m_impl->resources.GetRtvViewHeap().GetStats();
    const auto dsvStats = m_impl->resources.GetDsvViewHeap().GetStats();
    char line[192]{};
    std::snprintf(line, sizeof(line),
        "\n  타깃 뷰 힙 — RTV 최대 %u/%u(넘침 %llu) · DSV 최대 %u/%u(넘침 %llu)",
        rtvStats.peakFrameDescriptors,
        m_impl->resources.GetRtvViewHeap().GetCapacity(),
        static_cast<unsigned long long>(rtvStats.overflows),
        dsvStats.peakFrameDescriptors,
        m_impl->resources.GetDsvViewHeap().GetCapacity(),
        static_cast<unsigned long long>(dsvStats.overflows));
    return line;
}

#endif
