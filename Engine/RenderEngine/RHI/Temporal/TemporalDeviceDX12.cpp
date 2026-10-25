#if defined(CE_CONTRACT_STANDALONE)
#include "../../../../Tools/build/ModuleAdapters/Creator.Core.Ownership.h"
#else
import Creator.Core.Ownership;
#endif
#include "TemporalDeviceDX12.h"
#include "../../Render/Temporal/TemporalRuntimeControl.h"
#include "../../../Utility_Framework/Foundation/StandardLibrary.h"

namespace
{
    std::mutex g_mutex;
    own::weak_owner<TemporalDeviceDX12> g_device;
}
TemporalDeviceDX12::~TemporalDeviceDX12()
{
    // Every resource owner drains its queues before releasing this owner. SDK
    // contexts must be shut down first. A failed shutdown quarantines ownership.
    const auto shutdown = session && device && FAILED(device->GetDeviceRemovedReason())
        ? TemporalResult{ TemporalStatus::SdkFailure, device->GetDeviceRemovedReason() }
        : session ? session->ShutdownAfterGpuIdle() : TemporalResult{ TemporalStatus::Success };
    if (!shutdown.IsSuccess() && shutdown.status != TemporalStatus::NotInitialized &&
        shutdown.status != TemporalStatus::SdkNotBuilt)
    {
        auto* retained = new TemporalDeviceDX12;
        retained->session = std::move(session);
        retained->nativeFactory = std::move(nativeFactory);
        retained->factory = std::move(factory);
        retained->adapter = std::move(adapter);
        retained->nativeDevice = std::move(nativeDevice);
        retained->device = std::move(device);
        return;
    }
    device = nullptr;
    nativeDevice = nullptr;
    adapter = nullptr;
    factory = nullptr;
    nativeFactory = nullptr;
    if (session) session->UnloadAfterNativeObjectsDestroyed();
}
bool HasTemporalDeviceDX12()
{
    std::lock_guard lock(g_mutex);
    return !g_device.expired();
}
own::shared_owner<TemporalDeviceDX12> AcquireTemporalDeviceDX12(uint32_t flags,
    LUID adapterLuid, TemporalResult& result)
{
    std::lock_guard lock(g_mutex);
    if (auto existing = g_device.lock())
    {
        const auto actual = existing->device->GetAdapterLuid();
        if ((adapterLuid.LowPart || adapterLuid.HighPart) &&
            (actual.LowPart != adapterLuid.LowPart || actual.HighPart != adapterLuid.HighPart))
        { result = { TemporalStatus::BackendUnsupported }; return {}; }
        result = { TemporalStatus::Success };
        return existing;
    }
    const auto control = TemporalRuntimeControl::Get().Snapshot();
    const auto& settings = control.settings;
    const bool editorDlss = CREATOR_ENABLE_DLSS_STREAMLINE && control.editorUpscalingHostRegistered &&
        !settings.runtimeDirectory.empty() && !settings.dlssProjectId.empty();
    const bool requestReflex = control.playerLatencyHostRegistered && settings.reflexMode != TemporalLatencyMode::Off &&
        !(settings.enabled && settings.requestedFrameGenerator == TemporalProvider::XeSS);
    const bool requestSpatial = settings.spatialPost.nisMode != SpatialScalingMode::Off ||
        settings.spatialPost.deepDvcEnabled;
    if (!editorDlss && !requestReflex && !requestSpatial && (!settings.enabled ||
        (settings.requestedUpscaler == TemporalProvider::None &&
            settings.requestedFrameGenerator == TemporalProvider::None)))
    { result = { TemporalStatus::NotQueried }; return {}; }
    auto owner = own::make_shared<TemporalDeviceDX12>();
    const bool useDlss = editorDlss || requestReflex || requestSpatial || (settings.enabled &&
        (settings.requestedUpscaler == TemporalProvider::Dlss ||
            (control.playerLatencyHostRegistered && settings.requestedFrameGenerator == TemporalProvider::Dlss)));
    if (!useDlss)
    {
        auto hr = CreateDXGIFactory2(flags, IID_PPV_ARGS(owner->nativeFactory.put()));
        if (FAILED(hr)) { result = { TemporalStatus::SdkFailure, hr }; return {}; }
        owner->factory = owner->nativeFactory;
        hr = adapterLuid.LowPart || adapterLuid.HighPart
            ? owner->factory->EnumAdapterByLuid(adapterLuid, IID_PPV_ARGS(owner->adapter.put()))
            : owner->factory->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                IID_PPV_ARGS(owner->adapter.put()));
        if (FAILED(hr)) { result = { TemporalStatus::SdkFailure, hr }; return {}; }
        hr = D3D12CreateDevice(owner->adapter.get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(owner->nativeDevice.put()));
        if (FAILED(hr)) { result = { TemporalStatus::SdkFailure, hr }; return {}; }
        owner->device = owner->nativeDevice;
        result = { TemporalStatus::Success };
        g_device = owner;
        return owner;
    }
    owner->session = own::make_shared<DlssTemporalAdapter>();
    DlssInitialization init;
    init.pluginDirectory = settings.runtimeDirectory;
    init.projectId = settings.dlssProjectId;
    init.engineVersion = "CreatorEngine";
    // Do not make SR depend on the optional FG/Reflex DLL set, or vice versa.
    // Adding the other axis later requires a fresh pre-device SDK session.
    init.loadUpscaling = editorDlss || (settings.enabled && settings.requestedUpscaler == TemporalProvider::Dlss);
    init.loadFrameGeneration = control.playerLatencyHostRegistered && settings.enabled &&
        settings.requestedFrameGenerator == TemporalProvider::Dlss;
    init.loadReflex = requestReflex;
    init.loadNis = settings.spatialPost.nisMode != SpatialScalingMode::Off;
    init.loadDeepDvc = settings.spatialPost.deepDvcEnabled;
    result = owner->session->Initialize(init);
    if (!result.IsSuccess()) return {};
    auto hr = CreateDXGIFactory2(flags, IID_PPV_ARGS(owner->nativeFactory.put()));
    if (FAILED(hr)) { result = { TemporalStatus::SdkFailure, hr }; return {}; }
    void* proxy = owner->nativeFactory.get();
    result = owner->session->UpgradeDX12Interface(&proxy, DlssDX12ProxyKind::Factory);
    if (!result.IsSuccess()) return {};
    auto* proxyFactory = static_cast<IDXGIFactory*>(proxy);
    hr = proxyFactory->QueryInterface(IID_PPV_ARGS(owner->factory.put()));
    proxyFactory->Release();
    if (FAILED(hr)) { result = { TemporalStatus::SdkFailure, hr }; return {}; }
    hr = adapterLuid.LowPart || adapterLuid.HighPart
        ? owner->factory->EnumAdapterByLuid(adapterLuid, IID_PPV_ARGS(owner->adapter.put()))
        : owner->factory->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
            IID_PPV_ARGS(owner->adapter.put()));
    if (FAILED(hr)) { result = { TemporalStatus::SdkFailure, hr }; return {}; }
    hr = D3D12CreateDevice(owner->adapter.get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(owner->nativeDevice.put()));
    if (FAILED(hr)) { result = { TemporalStatus::SdkFailure, hr }; return {}; }
    proxy = owner->nativeDevice.get();
    result = owner->session->UpgradeDX12Interface(&proxy, DlssDX12ProxyKind::Device);
    if (!result.IsSuccess()) return {};
    owner->device.attach(static_cast<ID3D12Device*>(proxy));
    result = owner->session->BindDX12Device(owner->device.get());
    if (!result.IsSuccess()) return {};
    g_device = owner;
    return owner;
}
