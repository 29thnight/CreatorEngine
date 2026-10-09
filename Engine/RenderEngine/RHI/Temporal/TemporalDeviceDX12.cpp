#include "TemporalDeviceDX12.h"
#include "../../Render/Temporal/TemporalRuntimeControl.h"
#include <mutex>

namespace
{
    std::mutex g_mutex;
    std::weak_ptr<TemporalDeviceDX12> g_device;
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
    device.Reset();
    nativeDevice.Reset();
    adapter.Reset();
    factory.Reset();
    nativeFactory.Reset();
    if (session) session->UnloadAfterNativeObjectsDestroyed();
}
bool HasTemporalDeviceDX12()
{
    std::lock_guard lock(g_mutex);
    return !g_device.expired();
}
std::shared_ptr<TemporalDeviceDX12> AcquireTemporalDeviceDX12(uint32_t flags,
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
    const bool requestReflex = control.playerLatencyHostRegistered && settings.reflexMode != TemporalLatencyMode::Off &&
        !(settings.enabled && settings.requestedFrameGenerator == TemporalProvider::XeSS);
    const bool requestSpatial = settings.spatialPost.nisMode != SpatialScalingMode::Off ||
        settings.spatialPost.deepDvcEnabled;
    if (!requestReflex && !requestSpatial && (!settings.enabled ||
        (settings.requestedUpscaler == TemporalProvider::None &&
            settings.requestedFrameGenerator == TemporalProvider::None)))
    { result = { TemporalStatus::NotQueried }; return {}; }
    auto owner = std::make_shared<TemporalDeviceDX12>();
    const bool useDlss = requestReflex || requestSpatial || (settings.enabled &&
        (settings.requestedUpscaler == TemporalProvider::Dlss ||
            (control.playerLatencyHostRegistered && settings.requestedFrameGenerator == TemporalProvider::Dlss)));
    if (!useDlss)
    {
        auto hr = CreateDXGIFactory2(flags, IID_PPV_ARGS(&owner->nativeFactory));
        if (FAILED(hr)) { result = { TemporalStatus::SdkFailure, hr }; return {}; }
        owner->factory = owner->nativeFactory;
        hr = adapterLuid.LowPart || adapterLuid.HighPart
            ? owner->factory->EnumAdapterByLuid(adapterLuid, IID_PPV_ARGS(&owner->adapter))
            : owner->factory->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                IID_PPV_ARGS(&owner->adapter));
        if (FAILED(hr)) { result = { TemporalStatus::SdkFailure, hr }; return {}; }
        hr = D3D12CreateDevice(owner->adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&owner->nativeDevice));
        if (FAILED(hr)) { result = { TemporalStatus::SdkFailure, hr }; return {}; }
        owner->device = owner->nativeDevice;
        result = { TemporalStatus::Success };
        g_device = owner;
        return owner;
    }
    owner->session = std::make_shared<DlssTemporalAdapter>();
    DlssInitialization init;
    init.pluginDirectory = settings.runtimeDirectory;
    init.projectId = settings.dlssProjectId;
    init.engineVersion = "CreatorEngine";
    // Do not make SR depend on the optional FG/Reflex DLL set, or vice versa.
    // Adding the other axis later requires a fresh pre-device SDK session.
    init.loadUpscaling = settings.enabled && settings.requestedUpscaler == TemporalProvider::Dlss;
    init.loadFrameGeneration = control.playerLatencyHostRegistered && settings.enabled &&
        settings.requestedFrameGenerator == TemporalProvider::Dlss;
    init.loadReflex = requestReflex;
    init.loadNis = settings.spatialPost.nisMode != SpatialScalingMode::Off;
    init.loadDeepDvc = settings.spatialPost.deepDvcEnabled;
    result = owner->session->Initialize(init);
    if (!result.IsSuccess()) return {};
    auto hr = CreateDXGIFactory2(flags, IID_PPV_ARGS(&owner->nativeFactory));
    if (FAILED(hr)) { result = { TemporalStatus::SdkFailure, hr }; return {}; }
    void* proxy = owner->nativeFactory.Get();
    result = owner->session->UpgradeDX12Interface(&proxy, DlssDX12ProxyKind::Factory);
    if (!result.IsSuccess()) return {};
    auto* proxyFactory = static_cast<IDXGIFactory*>(proxy);
    hr = proxyFactory->QueryInterface(IID_PPV_ARGS(&owner->factory));
    proxyFactory->Release();
    if (FAILED(hr)) { result = { TemporalStatus::SdkFailure, hr }; return {}; }
    hr = adapterLuid.LowPart || adapterLuid.HighPart
        ? owner->factory->EnumAdapterByLuid(adapterLuid, IID_PPV_ARGS(&owner->adapter))
        : owner->factory->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
            IID_PPV_ARGS(&owner->adapter));
    if (FAILED(hr)) { result = { TemporalStatus::SdkFailure, hr }; return {}; }
    hr = D3D12CreateDevice(owner->adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&owner->nativeDevice));
    if (FAILED(hr)) { result = { TemporalStatus::SdkFailure, hr }; return {}; }
    proxy = owner->nativeDevice.Get();
    result = owner->session->UpgradeDX12Interface(&proxy, DlssDX12ProxyKind::Device);
    if (!result.IsSuccess()) return {};
    owner->device.Attach(static_cast<ID3D12Device*>(proxy));
    result = owner->session->BindDX12Device(owner->device.Get());
    if (!result.IsSuccess()) return {};
    g_device = owner;
    return owner;
}
