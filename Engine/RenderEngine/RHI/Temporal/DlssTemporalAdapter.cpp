#include "DlssTemporalAdapter.h"

#ifndef CREATOR_ENABLE_DLSS_STREAMLINE
#define CREATOR_ENABLE_DLSS_STREAMLINE 0
#endif

#include <cassert>

#if CREATOR_ENABLE_DLSS_STREAMLINE && defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include "../DX12/DX12DeviceResources.h"
#include "../DX12/DX12Encoder.h"

// External, pinned Streamline SDK headers; no runtime is vendored or downloaded
// by this adapter. Do not use the header helpers' static function caches: they
// survive slShutdown and would retain pointers into a subsequently unloaded DLL.
#include <sl.h>
#include <sl_dlss.h>
#include <sl_dlss_g.h>
#include <sl_reflex.h>
#include <sl_security.h>

#include <atomic>
#include <cmath>
#include <filesystem>
#include <iterator>
#include <mutex>
#include <unordered_map>

static_assert(SL_VERSION_MAJOR == 2 && SL_VERSION_MINOR == 14 && SL_VERSION_PATCH == 1,
    "CreatorEngine DLSS adapter requires the pinned Streamline 2.14.1 SDK");

namespace
{
    std::atomic<bool> g_runtimeClaimed{ false };
    std::atomic<int64_t> g_presentationError{ 0 };

    TemporalResult ToResult(sl::Result result)
    {
        if (result == sl::Result::eOk) return { TemporalStatus::Success };
        switch (result)
        {
        case sl::Result::eErrorNoSupportedAdapterFound:
        case sl::Result::eErrorAdapterNotSupported:
        case sl::Result::eErrorFeatureNotSupported:
        case sl::Result::eErrorFeatureMissing:
        case sl::Result::eErrorFeatureFailedToLoad:
        case sl::Result::eErrorFeatureMissingDependency:
        case sl::Result::eErrorDriverOutOfDate:
        case sl::Result::eErrorOSOutOfDate:
        case sl::Result::eErrorOSDisabledHWS:
            return { TemporalStatus::FeatureUnsupported, static_cast<int64_t>(result) };
        default:
            return { TemporalStatus::SdkFailure, static_cast<int64_t>(result) };
        }
    }

    void OnPresentationError(const sl::APIError& error)
    {
        // Called by the SDK's asynchronous Present thread. No locks/logging or
        // host callbacks here; hand the native failure to the shell for recovery.
        if (FAILED(error.hres)) g_presentationError.store(error.hres, std::memory_order_relaxed);
    }

    sl::DLSSMode ToMode(TemporalQuality quality)
    {
        switch (quality)
        {
        case TemporalQuality::NativeAA: return sl::DLSSMode::eDLAA;
        case TemporalQuality::Quality: return sl::DLSSMode::eMaxQuality;
        case TemporalQuality::Balanced: return sl::DLSSMode::eBalanced;
        case TemporalQuality::Performance: return sl::DLSSMode::eMaxPerformance;
        case TemporalQuality::UltraPerformance: return sl::DLSSMode::eUltraPerformance;
        default: return sl::DLSSMode::eOff;
        }
    }

    sl::float4x4 ToMatrix(const std::array<float, 16>& values)
    {
        sl::float4x4 result;
        for (uint32_t row = 0; row < 4; ++row)
            result[row] = { values[row * 4], values[row * 4 + 1], values[row * 4 + 2], values[row * 4 + 3] };
        return result;
    }

    sl::float3 ToVector(const std::array<float, 3>& values)
    {
        return { values[0], values[1], values[2] };
    }

    bool SameFrame(const TemporalFrame& left, const TemporalFrame& right)
    {
        const auto& a = left.camera;
        const auto& b = right.camera;
        return left.realFrameId == right.realFrameId && left.historyRevision == right.historyRevision &&
            left.renderExtent == right.renderExtent && left.displayExtent == right.displayExtent &&
            left.jitterX == right.jitterX && left.jitterY == right.jitterY &&
            left.motionVectorScaleX == right.motionVectorScaleX && left.motionVectorScaleY == right.motionVectorScaleY &&
            left.frameTimeMilliseconds == right.frameTimeMilliseconds && left.cameraNear == right.cameraNear &&
            left.cameraFar == right.cameraFar && left.cameraVerticalFov == right.cameraVerticalFov &&
            left.viewSpaceToMeters == right.viewSpaceToMeters && left.preExposure == right.preExposure &&
            left.reset == right.reset && left.depthInverted == right.depthInverted &&
            left.depthInfinite == right.depthInfinite && left.highDynamicRange == right.highDynamicRange &&
            left.motionVectorsAtDisplayResolution == right.motionVectorsAtDisplayResolution &&
            left.motionVectorsDilated == right.motionVectorsDilated && a.viewMatrix == b.viewMatrix &&
            a.projectionMatrix == b.projectionMatrix && a.cameraViewToClip == b.cameraViewToClip &&
            a.clipToCameraView == b.clipToCameraView && a.clipToPreviousClip == b.clipToPreviousClip &&
            a.previousClipToClip == b.previousClipToClip && a.position == b.position && a.up == b.up &&
            a.right == b.right && a.forward == b.forward && a.aspectRatio == b.aspectRatio && a.valid == b.valid &&
            a.orthographicProjection == b.orthographicProjection;
    }

    bool IsTextureValid(const DlssDX12Texture& texture, TemporalExtent extent, bool output = false)
    {
        if (!texture.resource || texture.state == UINT32_MAX || !extent.IsValid()) return false;
        const auto description = texture.resource->GetDesc();
        return description.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D &&
            description.Width >= extent.width && description.Height >= extent.height &&
            description.DepthOrArraySize == 1 && description.SampleDesc.Count == 1 &&
            description.Format != DXGI_FORMAT_UNKNOWN &&
            (output || (description.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) == 0) &&
            (!output || (description.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0);
    }

    bool IsColorFormat(DXGI_FORMAT format, bool hdr)
    {
        // Deliberately limited to the engine's typed scene-color contract. This
        // is not a claim that the SDK rejects every other native color format.
        return hdr ? format == DXGI_FORMAT_R16G16B16A16_FLOAT || format == DXGI_FORMAT_R32G32B32A32_FLOAT :
            format == DXGI_FORMAT_R8G8B8A8_UNORM || format == DXGI_FORMAT_B8G8R8A8_UNORM;
    }

    bool IsDepthFormat(DXGI_FORMAT format)
    {
        return format == DXGI_FORMAT_R32_FLOAT || format == DXGI_FORMAT_R32_TYPELESS || format == DXGI_FORMAT_D32_FLOAT;
    }

    bool IsMotionFormat(DXGI_FORMAT format)
    {
        return format == DXGI_FORMAT_R16G16_FLOAT || format == DXGI_FORMAT_R32G32_FLOAT;
    }

    sl::Resource ToResource(const DlssDX12Texture& texture)
    {
        sl::Resource result{ sl::ResourceType::eTex2d, texture.resource, texture.state };
        result.flags = 0;
        if (texture.resource)
        {
            const auto description = texture.resource->GetDesc();
            result.width = static_cast<uint32_t>(description.Width);
            result.height = description.Height;
            result.nativeFormat = static_cast<uint32_t>(description.Format);
            result.mipLevels = description.MipLevels;
            result.arrayLayers = description.DepthOrArraySize;
        }
        return result;
    }

    sl::Extent ToExtent(TemporalExtent extent)
    {
        return { 0, 0, extent.width, extent.height };
    }
}

struct DlssTemporalAdapter::Implementation
{
    struct Frame
    {
        sl::FrameToken* token{ nullptr };
        uint64_t allocationSerial{ 0 };
        std::unordered_map<uint32_t, TemporalFrame> viewports;
        bool presented{ false };
    };
    struct Viewport
    {
        uint64_t historyRevision{ 0 };
        uint64_t lastFrameId{ 0 };
        TemporalExtent renderExtent;
        TemporalExtent displayExtent;
        bool upscaleResources{ false };
        bool upscaleFaulted{ false };
        bool forceReset{ false };
        uint64_t lastUpscaleFrame{ 0 };
        uint64_t lastFrameGenerationFrame{ 0 };
    };

    std::mutex mutex;
    HMODULE module{ nullptr };
    bool initialized{ false };
    bool initializationFailed{ false };
    bool permanentlyUnloaded{ false };
    bool deviceBound{ false };
    bool deviceProxyUpgraded{ false };
    bool factoryProxyUpgraded{ false };
    bool frameGenerationEnabled{ false };
    bool playerFrameTokenOwnership{ false };
    bool frameGenerationConfigured{ false };
    bool frameGenerationFaulted{ false };
    uint32_t frameGenerationViewport{ 0 };
    uint64_t lastRealFrameId{ 0 };
    uint64_t nextTokenSerial{ 0 };
    ID3D12Device* nativeDevice{ nullptr };
    LUID adapterLuid{};
    DlssInitialization configuration;
    TemporalCapabilities capabilities;
    IDXGISwapChain3* swapchain{ nullptr }; // Borrowed, valid through SDK shutdown.
    TemporalFrameGenerationConfig presentation;
    TemporalExtent dynamicResolutionTarget;
    TemporalExtent lastFrameGenerationExtent;
    std::shared_ptr<const void> pendingFrameGenerationOwner;
    uint64_t pendingPresentedFrames{ 0 };
    DlssPacingOwner pacingOwner{ DlssPacingOwner::Unspecified };
    std::unordered_map<uint64_t, Frame> frames;
    std::unordered_map<uint32_t, Viewport> viewports;

    PFun_slInit* init{ nullptr };
    PFun_slShutdown* shutdown{ nullptr };
    PFun_slSetD3DDevice* setDevice{ nullptr };
    PFun_slIsFeatureSupported* isSupported{ nullptr };
    PFun_slIsFeatureLoaded* isLoaded{ nullptr };
    PFun_slGetFeatureRequirements* getRequirements{ nullptr };
    PFun_slGetFeatureVersion* getVersion{ nullptr };
    PFun_slGetFeatureFunction* getFunction{ nullptr };
    PFun_slUpgradeInterface* upgradeInterface{ nullptr };
    PFun_slGetNativeInterface* getNativeInterface{ nullptr };
    PFun_slGetNewFrameToken* getFrameToken{ nullptr };
    PFun_slSetConstants* setConstants{ nullptr };
    PFun_slSetTagForFrame* setTags{ nullptr };
    PFun_slEvaluateFeature* evaluate{ nullptr };
    PFun_slFreeResources* freeResources{ nullptr };
    PFun_slDLSSGetOptimalSettings* getOptimalSettings{ nullptr };
    PFun_slDLSSSetOptions* setUpscaleOptions{ nullptr };
    PFun_slDLSSGSetOptions* setFrameGenerationOptions{ nullptr };
    PFun_slDLSSGGetState* getFrameGenerationState{ nullptr };
    PFun_slReflexGetState* getReflexState{ nullptr };
    PFun_slReflexSetOptions* setReflexOptions{ nullptr };
    PFun_slReflexSleep* reflexSleep{ nullptr };
    PFun_slPCLSetMarker* setMarker{ nullptr };

    template<typename Function>
    bool Import(const char* name, Function*& destination)
    {
        destination = reinterpret_cast<Function*>(GetProcAddress(module, name));
        return destination != nullptr;
    }

    template<typename Function>
    TemporalResult ImportFeature(sl::Feature feature, const char* name, Function*& destination)
    {
        void* address = nullptr;
        const auto result = ToResult(getFunction(feature, name, address));
        if (!result.IsSuccess()) return result;
        destination = reinterpret_cast<Function*>(address);
        return destination ? TemporalResult{ TemporalStatus::Success } :
            TemporalResult{ TemporalStatus::RuntimeUnavailable };
    }

    TemporalResult Probe(sl::Feature feature)
    {
        sl::AdapterInfo adapter;
        adapter.deviceLUID = reinterpret_cast<uint8_t*>(&adapterLuid);
        adapter.deviceLUIDSizeInBytes = sizeof(adapterLuid);
        auto result = ToResult(isSupported(feature, adapter));
        if (!result.IsSuccess()) return result;
        bool loaded = false;
        result = ToResult(isLoaded(feature, loaded));
        if (!result.IsSuccess()) return result;
        if (!loaded) return { TemporalStatus::FeatureUnsupported };
        sl::FeatureRequirements requirements;
        result = ToResult(getRequirements(feature, requirements));
        if (!result.IsSuccess()) return result;
        if (!(requirements.flags & sl::FeatureRequirementFlags::eD3D12Supported))
            return { TemporalStatus::BackendUnsupported };
        sl::FeatureVersion version;
        result = ToResult(getVersion(feature, version));
        if (!result.IsSuccess()) return result;
        if (!(version.versionSL == sl::Version{ 2, 14, 1 })) return { TemporalStatus::SdkVersionMismatch };
        if (feature == sl::kFeatureDLSS)
            capabilities.upscalerImplementation = "DLSS Super Resolution, NGX " + version.versionNGX.toStr();
        if (feature == sl::kFeatureDLSS_G)
            capabilities.frameGeneratorImplementation = "DLSS Frame Generation, NGX " + version.versionNGX.toStr();
        return { TemporalStatus::Success };
    }

    TemporalResult ResolveUpscaling()
    {
        auto result = Probe(sl::kFeatureDLSS);
        if (!result.IsSuccess()) return result;
        result = ImportFeature(sl::kFeatureDLSS, "slDLSSGetOptimalSettings", getOptimalSettings);
        if (!result.IsSuccess()) return result;
        return ImportFeature(sl::kFeatureDLSS, "slDLSSSetOptions", setUpscaleOptions);
    }

    TemporalResult ResolveFrameGeneration()
    {
        for (auto feature : { sl::kFeatureDLSS_G, sl::kFeatureReflex, sl::kFeaturePCL })
        {
            const auto result = Probe(feature);
            if (!result.IsSuccess()) return result;
        }
#define DLSS_IMPORT_FEATURE(feature, name, member) \
        { const auto result = ImportFeature(feature, name, member); if (!result.IsSuccess()) return result; }
        DLSS_IMPORT_FEATURE(sl::kFeatureDLSS_G, "slDLSSGSetOptions", setFrameGenerationOptions);
        DLSS_IMPORT_FEATURE(sl::kFeatureDLSS_G, "slDLSSGGetState", getFrameGenerationState);
        DLSS_IMPORT_FEATURE(sl::kFeatureReflex, "slReflexGetState", getReflexState);
        DLSS_IMPORT_FEATURE(sl::kFeatureReflex, "slReflexSetOptions", setReflexOptions);
        DLSS_IMPORT_FEATURE(sl::kFeatureReflex, "slReflexSleep", reflexSleep);
        DLSS_IMPORT_FEATURE(sl::kFeaturePCL, "slPCLSetMarker", setMarker);
#undef DLSS_IMPORT_FEATURE
        sl::ReflexState reflexState;
        auto result = ToResult(getReflexState(reflexState));
        if (!result.IsSuccess()) return result;
        if (!reflexState.lowLatencyAvailable) return { TemporalStatus::FeatureUnsupported };
        sl::DLSSGState state;
        result = ToResult(getFrameGenerationState(sl::ViewportHandle{ 0u }, state, nullptr));
        if (!result.IsSuccess()) return result;
        capabilities.maxInterpolatedFrames = state.numFramesToGenerateMax;
        return state.numFramesToGenerateMax != 0 ? TemporalResult{ TemporalStatus::Success } :
            TemporalResult{ TemporalStatus::FeatureUnsupported };
    }

    const TemporalFrame* FindFrame(uint64_t realFrameId, uint32_t viewportId)
    {
        const auto frame = frames.find(realFrameId);
        if (frame == frames.end()) return nullptr;
        const auto viewport = frame->second.viewports.find(viewportId);
        return viewport == frame->second.viewports.end() ? nullptr : &viewport->second;
    }
};

DlssTemporalAdapter::DlssTemporalAdapter() : m_implementation(std::make_unique<Implementation>()) {}

DlssTemporalAdapter::~DlssTemporalAdapter()
{
    // GPU idleness and external COM proxy lifetime cannot be inferred here.
    // A violated explicit shutdown contract must not unload live proxy vtables.
    assert(!m_implementation->module && "Explicit DLSS shutdown and proxy release are required");
    if (m_implementation->module) (void)m_implementation.release();
}

TemporalResult DlssTemporalAdapter::Initialize(const DlssInitialization& initialization)
{
    auto& state = *m_implementation;
    std::lock_guard lock(state.mutex);
    if (state.module || state.permanentlyUnloaded) return { TemporalStatus::AlreadyInitialized };
    state.configuration.backend = initialization.backend;
    if (initialization.backend != TemporalBackend::DX12) return { TemporalStatus::BackendUnsupported };
    if ((!initialization.loadUpscaling && !initialization.loadFrameGeneration) ||
        initialization.pluginDirectory.empty() ||
        (initialization.applicationId == 0 &&
            (initialization.engineVersion.empty() || initialization.projectId.empty())))
        return { TemporalStatus::InvalidInput };
    const std::filesystem::path directory{ initialization.pluginDirectory };
    if (!directory.is_absolute()) return { TemporalStatus::InvalidInput };
    bool unclaimed = false;
    if (!g_runtimeClaimed.compare_exchange_strong(unclaimed, true))
        return { TemporalStatus::AlreadyInitialized };
    state.configuration = initialization;
    const auto modulePath = directory / L"sl.interposer.dll";
    if (!sl::security::verifyEmbeddedSignature(modulePath.c_str()))
    {
        g_runtimeClaimed.store(false);
        return { TemporalStatus::RuntimeUnavailable };
    }
    state.module = LoadLibraryExW(modulePath.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!state.module)
    {
        const auto error = GetLastError();
        g_runtimeClaimed.store(false);
        return { TemporalStatus::RuntimeUnavailable, error };
    }
    const bool imports = state.Import("slInit", state.init) && state.Import("slShutdown", state.shutdown) &&
        state.Import("slSetD3DDevice", state.setDevice) &&
        state.Import("slIsFeatureSupported", state.isSupported) && state.Import("slIsFeatureLoaded", state.isLoaded) &&
        state.Import("slGetFeatureRequirements", state.getRequirements) &&
        state.Import("slGetFeatureVersion", state.getVersion) &&
        state.Import("slGetFeatureFunction", state.getFunction) &&
        state.Import("slUpgradeInterface", state.upgradeInterface) &&
        state.Import("slGetNativeInterface", state.getNativeInterface) &&
        state.Import("slGetNewFrameToken", state.getFrameToken) && state.Import("slSetConstants", state.setConstants) &&
        state.Import("slSetTagForFrame", state.setTags) && state.Import("slEvaluateFeature", state.evaluate) &&
        state.Import("slFreeResources", state.freeResources);
    if (!imports)
    {
        FreeLibrary(state.module);
        state.module = nullptr;
        g_runtimeClaimed.store(false);
        return { TemporalStatus::RuntimeUnavailable };
    }
    sl::Feature features[4];
    uint32_t count = 0;
    if (initialization.loadUpscaling) features[count++] = sl::kFeatureDLSS;
    if (initialization.loadFrameGeneration)
    {
        features[count++] = sl::kFeatureDLSS_G;
        features[count++] = sl::kFeatureReflex;
        features[count++] = sl::kFeaturePCL;
    }
    const wchar_t* pluginPath = state.configuration.pluginDirectory.c_str();
    sl::Preferences preferences;
    preferences.pathsToPlugins = &pluginPath;
    preferences.numPathsToPlugins = 1;
    preferences.featuresToLoad = features;
    preferences.numFeaturesToLoad = count;
    preferences.applicationId = initialization.applicationId;
    preferences.engine = sl::EngineType::eCustom;
    preferences.engineVersion = state.configuration.engineVersion.c_str();
    preferences.projectId = state.configuration.projectId.c_str();
    preferences.renderAPI = sl::RenderAPI::eD3D12;
    // Keep the pin meaningful: no OTA code download/loading and no OS bypass.
    preferences.flags = sl::PreferenceFlags::eDisableCLStateTracking |
        sl::PreferenceFlags::eUseManualHooking | sl::PreferenceFlags::eUseDXGIFactoryProxy |
        sl::PreferenceFlags::eUseFrameBasedResourceTagging;
    const auto result = ToResult(state.init(preferences, sl::kSDKVersion));
    if (!result.IsSuccess())
    {
        // slInit can have partially initialized plugins; tear those down while
        // the module remains loaded. No device/proxy exists at this stage.
        const auto shutdownResult = ToResult(state.shutdown());
        if (!shutdownResult.IsSuccess())
        {
            // Preserve a partially initialized runtime for explicit shutdown
            // retry; unloading after a failed cleanup could strand plugin work.
            state.initialized = true;
            state.initializationFailed = true;
            return result;
        }
        FreeLibrary(state.module);
        state.module = nullptr;
        g_runtimeClaimed.store(false);
        return result;
    }
    state.initialized = true;
    g_presentationError.store(0);
    state.capabilities = {};
    state.capabilities.provider = TemporalProvider::Dlss;
    state.capabilities.backend = TemporalBackend::DX12;
    state.capabilities.sdkVersion = "Streamline 2.14.1";
    state.capabilities.sdkRevision = "2122257e0fce486f91b385aa63b9a09b0a34b363";
    return { TemporalStatus::Success };
}

TemporalResult DlssTemporalAdapter::BindDX12Device(ID3D12Device* nativeDevice)
{
    auto& state = *m_implementation;
    std::lock_guard lock(state.mutex);
    if (!state.initialized) return { TemporalStatus::NotInitialized };
    if (state.initializationFailed) return { TemporalStatus::IntegrationRequired };
    if (state.deviceBound) return { TemporalStatus::AlreadyInitialized };
    if (!nativeDevice) return { TemporalStatus::InvalidInput };
    const auto result = ToResult(state.setDevice(nativeDevice));
    if (!result.IsSuccess()) return result;
    state.adapterLuid = nativeDevice->GetAdapterLuid();
    state.nativeDevice = nativeDevice;
    state.deviceBound = true;
    state.capabilities.upscaling = state.configuration.loadUpscaling ? state.ResolveUpscaling() :
        TemporalResult{ TemporalStatus::IntegrationRequired };
    state.capabilities.frameGeneration = state.configuration.loadFrameGeneration ? state.ResolveFrameGeneration() :
        TemporalResult{ TemporalStatus::IntegrationRequired };
    // Binding succeeded even if only one independently requested axis is usable.
    return { TemporalStatus::Success };
}

bool DlssTemporalAdapter::IsBoundToDX12Device(ID3D12Device* nativeDevice) const
{
    auto& state = *m_implementation;
    std::lock_guard lock(state.mutex);
    return state.deviceBound && nativeDevice && state.nativeDevice == nativeDevice;
}

bool DlssTemporalAdapter::MatchesRuntimeConfiguration(const std::wstring& directory,
    const std::string& projectId) const
{
    auto& state = *m_implementation;
    std::lock_guard lock(state.mutex);
    return state.initialized && directory == state.configuration.pluginDirectory &&
        projectId == state.configuration.projectId;
}

TemporalCapabilities DlssTemporalAdapter::QueryCapabilities()
{
    auto& state = *m_implementation;
    std::lock_guard lock(state.mutex);
    auto result = state.capabilities;
    result.provider = TemporalProvider::Dlss;
    result.backend = state.configuration.backend;
    result.sdkVersion = "Streamline 2.14.1";
    result.sdkRevision = "2122257e0fce486f91b385aa63b9a09b0a34b363";
    if (!state.deviceBound)
        result.upscaling = result.frameGeneration = { state.configuration.backend == TemporalBackend::DX12 ?
            TemporalStatus::NotInitialized : TemporalStatus::BackendUnsupported };
    return result;
}

TemporalResult DlssTemporalAdapter::UpgradeDX12Interface(void** interfacePointer, DlssDX12ProxyKind kind)
{
    auto& state = *m_implementation;
    std::lock_guard lock(state.mutex);
    if (!state.initialized) return { TemporalStatus::NotInitialized };
    if (state.initializationFailed) return { TemporalStatus::IntegrationRequired };
    if (!interfacePointer || !*interfacePointer) return { TemporalStatus::InvalidInput };
    const IID* expected = nullptr;
    switch (kind)
    {
    case DlssDX12ProxyKind::Device: expected = &__uuidof(ID3D12Device); break;
    case DlssDX12ProxyKind::Factory: expected = &__uuidof(IDXGIFactory); break;
    case DlssDX12ProxyKind::Swapchain: expected = &__uuidof(IDXGISwapChain); break;
    default: return { TemporalStatus::InvalidInput };
    }
    IUnknown* verified = nullptr;
    const auto hr = static_cast<IUnknown*>(*interfacePointer)->QueryInterface(*expected,
        reinterpret_cast<void**>(&verified));
    if (FAILED(hr)) return { TemporalStatus::InvalidInput, hr };
    verified->Release();
    void* original = *interfacePointer;
    const auto result = ToResult(state.upgradeInterface(interfacePointer));
    if (!result.IsSuccess()) return result;
    if (*interfacePointer == original) return { TemporalStatus::IntegrationRequired };
    if (kind == DlssDX12ProxyKind::Device) state.deviceProxyUpgraded = true;
    if (kind == DlssDX12ProxyKind::Factory) state.factoryProxyUpgraded = true;
    return result;
}

TemporalResult DlssTemporalAdapter::BindPlayerSwapchain(IDXGISwapChain3* proxySwapchain,
    const TemporalFrameGenerationConfig& configuration, DlssPacingOwner pacingOwner)
{
    const auto validation = ValidateTemporalFrameGenerationConfig(configuration);
    if (!validation.IsSuccess()) return validation;
    auto& state = *m_implementation;
    std::lock_guard lock(state.mutex);
    if (!state.deviceBound) return { TemporalStatus::NotInitialized };
    if (state.frameGenerationEnabled) return { TemporalStatus::IntegrationRequired };
    if (!state.capabilities.frameGeneration.IsSuccess()) return state.capabilities.frameGeneration;
    if (!proxySwapchain || !state.deviceProxyUpgraded || !state.factoryProxyUpgraded ||
        (pacingOwner != DlssPacingOwner::Streamline && pacingOwner != DlssPacingOwner::ApplicationWaitableObject))
        return { TemporalStatus::IntegrationRequired };
    if (configuration.transferFunction == TemporalTransferFunction::Linear)
        return { TemporalStatus::FeatureUnsupported }; // SDK 2.14.1 explicitly excludes FP16/scRGB FG.
    void* native = nullptr;
    const auto result = ToResult(state.getNativeInterface(proxySwapchain, &native));
    if (!result.IsSuccess()) return result;
    const bool isProxy = native && native != proxySwapchain;
    if (native) static_cast<IUnknown*>(native)->Release(); // API returns an AddRef'd native interface.
    if (!isProxy) return { TemporalStatus::IntegrationRequired };
    DXGI_SWAP_CHAIN_DESC1 description{};
    auto hr = proxySwapchain->GetDesc1(&description);
    if (FAILED(hr)) return { TemporalStatus::SdkFailure, hr };
    const bool applicationWaitable = (description.Flags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT) != 0;
    if (description.Width != configuration.displayExtent.width ||
        description.Height != configuration.displayExtent.height ||
        applicationWaitable != (pacingOwner == DlssPacingOwner::ApplicationWaitableObject))
        return { TemporalStatus::InvalidInput };
    if (configuration.transferFunction == TemporalTransferFunction::PQ &&
        description.Format != DXGI_FORMAT_R10G10B10A2_UNORM)
        return { TemporalStatus::FeatureUnsupported };
    if (configuration.transferFunction == TemporalTransferFunction::SRGB &&
        description.Format != DXGI_FORMAT_R8G8B8A8_UNORM && description.Format != DXGI_FORMAT_B8G8R8A8_UNORM)
        return { TemporalStatus::FeatureUnsupported };
    // The shell still owns HDR metadata. Match the SDK's interpretation of the
    // final color to the explicitly requested swapchain transfer function.
    hr = proxySwapchain->SetColorSpace1(configuration.transferFunction == TemporalTransferFunction::PQ ?
        DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 : DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
    if (FAILED(hr)) return { TemporalStatus::SdkFailure, hr };
    state.swapchain = proxySwapchain;
    state.presentation = configuration;
    state.pacingOwner = pacingOwner;
    return { TemporalStatus::Success };
}

TemporalResult DlssTemporalAdapter::ValidatePlayerSwapchainBinding(
    const TemporalFrameGenerationConfig& configuration) const
{
    const auto validation = ValidateTemporalFrameGenerationConfig(configuration);
    if (!validation.IsSuccess()) return validation;
    auto& state = *m_implementation;
    std::lock_guard lock(state.mutex);
    if (!state.deviceBound) return { TemporalStatus::NotInitialized };
    if (!state.swapchain || configuration.displayExtent != state.presentation.displayExtent ||
        configuration.transferFunction != state.presentation.transferFunction)
        return { TemporalStatus::IntegrationRequired };
    return { TemporalStatus::Success };
}

TemporalResult DlssTemporalAdapter::GetOptimalSettings(TemporalQuality quality, TemporalExtent displayExtent,
    DlssOptimalSettings& settings)
{
    settings = {};
    auto& state = *m_implementation;
    std::lock_guard lock(state.mutex);
    if (!state.deviceBound) return { TemporalStatus::NotInitialized };
    if (!state.capabilities.upscaling.IsSuccess()) return state.capabilities.upscaling;
    if (!displayExtent.IsValid() || ToMode(quality) == sl::DLSSMode::eOff)
        return { TemporalStatus::InvalidInput };
    sl::DLSSOptions options;
    options.mode = ToMode(quality);
    options.outputWidth = displayExtent.width;
    options.outputHeight = displayExtent.height;
    sl::DLSSOptimalSettings optimal;
    const auto result = ToResult(state.getOptimalSettings(options, optimal));
    if (!result.IsSuccess()) return result;
    settings.optimalRenderExtent = { optimal.optimalRenderWidth, optimal.optimalRenderHeight };
    settings.minimumRenderExtent = { optimal.renderWidthMin, optimal.renderHeightMin };
    settings.maximumRenderExtent = { optimal.renderWidthMax, optimal.renderHeightMax };
    return result;
}

TemporalResult DlssTemporalAdapter::BeginRealFrame(uint64_t realFrameId)
{
    auto& state = *m_implementation;
    std::lock_guard lock(state.mutex);
    if (!state.deviceBound) return { TemporalStatus::NotInitialized };
    if (realFrameId == 0 || realFrameId <= state.lastRealFrameId) return { TemporalStatus::InvalidInput };
    if (state.frames.size() >= sl::MAX_FRAMES_IN_FLIGHT) return { TemporalStatus::IntegrationRequired };
    for (const auto& [id, frame] : state.frames)
        if (state.nextTokenSerial - frame.allocationSerial >= sl::MAX_FRAMES_IN_FLIGHT)
            return { TemporalStatus::IntegrationRequired };
    sl::FrameToken* token = nullptr;
    // SDK owns its uint32_t token index. Do not truncate the engine's uint64_t ID.
    const auto result = ToResult(state.getFrameToken(token, nullptr));
    if (!result.IsSuccess()) return result;
    if (!token) return { TemporalStatus::SdkFailure };
    Implementation::Frame frame;
    frame.token = token;
    frame.allocationSerial = state.nextTokenSerial++;
    state.frames.emplace(realFrameId, std::move(frame));
    state.lastRealFrameId = realFrameId;
    return result;
}

TemporalResult DlssTemporalAdapter::EnsureRealFrame(uint64_t realFrameId)
{
    auto& state = *m_implementation;
    std::lock_guard lock(state.mutex);
    if (!state.deviceBound) return { TemporalStatus::NotInitialized };
    if (state.frames.contains(realFrameId)) return { TemporalStatus::Success };
    if (realFrameId == 0 || realFrameId <= state.lastRealFrameId) return { TemporalStatus::InvalidInput };
    // The render thread records all views for one real frame before advancing.
    // Retain one token across those views; retire it at the next frame boundary.
    if (!state.playerFrameTokenOwnership && !state.frameGenerationEnabled) state.frames.clear();
    if (state.frames.size() >= sl::MAX_FRAMES_IN_FLIGHT) return { TemporalStatus::IntegrationRequired };
    for (const auto& [id, frame] : state.frames)
        if (state.nextTokenSerial - frame.allocationSerial >= sl::MAX_FRAMES_IN_FLIGHT)
            return { TemporalStatus::IntegrationRequired };
    sl::FrameToken* token = nullptr;
    // SDK owns its uint32_t token index. Do not truncate the engine's uint64_t ID.
    const auto result = ToResult(state.getFrameToken(token, nullptr));
    if (!result.IsSuccess()) return result;
    if (!token) return { TemporalStatus::SdkFailure };
    Implementation::Frame frame;
    frame.token = token;
    frame.allocationSerial = state.nextTokenSerial++;
    state.frames.emplace(realFrameId, std::move(frame));
    state.lastRealFrameId = realFrameId;
    return result;
}

void DlssTemporalAdapter::SetPlayerFrameTokenOwnership(bool enabled)
{
    auto& state = *m_implementation;
    std::lock_guard lock(state.mutex);
    state.playerFrameTokenOwnership = enabled;
}
bool DlssTemporalAdapter::HasPlayerFrameTokenOwnership() const
{
    auto& state = *m_implementation;
    std::lock_guard lock(state.mutex);
    return state.playerFrameTokenOwnership;
}

TemporalResult DlssTemporalAdapter::SetFrameConstants(uint32_t viewportId, const TemporalFrame& frame)
{
    if (viewportId == UINT32_MAX) return { TemporalStatus::InvalidInput };
    auto result = ValidateTemporalFrame(frame);
    if (!result.IsSuccess()) return result;
    result = ValidateTemporalCamera(frame.camera);
    if (!result.IsSuccess()) return result;
    // Streamline has no depthInfinite flag: matrices describe the projection,
    // but cameraFar still needs an explicit finite camera range for constants.
    if (!std::isfinite(frame.cameraFar) || frame.cameraFar <= frame.cameraNear)
        return { TemporalStatus::InvalidInput };
    auto& state = *m_implementation;
    std::lock_guard lock(state.mutex);
    if (!state.deviceBound) return { TemporalStatus::NotInitialized };
    const auto activeFrame = state.frames.find(frame.realFrameId);
    if (activeFrame == state.frames.end()) return { TemporalStatus::InvalidInput };
    const auto existing = activeFrame->second.viewports.find(viewportId);
    if (existing != activeFrame->second.viewports.end())
        return { SameFrame(existing->second, frame) ? TemporalStatus::Success : TemporalStatus::InvalidInput };
    auto& viewport = state.viewports[viewportId];
    if (frame.realFrameId <= viewport.lastFrameId) return { TemporalStatus::InvalidInput };
    const bool reset = frame.reset || viewport.forceReset || viewport.lastFrameId == 0 ||
        frame.historyRevision != viewport.historyRevision || frame.renderExtent != viewport.renderExtent ||
        frame.displayExtent != viewport.displayExtent;
    sl::Constants constants;
    constants.cameraViewToClip = ToMatrix(frame.camera.cameraViewToClip);
    constants.clipToCameraView = ToMatrix(frame.camera.clipToCameraView);
    constants.clipToPrevClip = ToMatrix(frame.camera.clipToPreviousClip);
    constants.prevClipToClip = ToMatrix(frame.camera.previousClipToClip);
    constants.jitterOffset = { frame.jitterX, frame.jitterY };
    // The common contract converts stored vectors into render/display pixels.
    // Streamline consumes normalized screen displacement, not pixels.
    const auto motionExtent = frame.motionVectorsAtDisplayResolution ? frame.displayExtent : frame.renderExtent;
    constants.mvecScale = { frame.motionVectorScaleX / static_cast<float>(motionExtent.width),
        frame.motionVectorScaleY / static_cast<float>(motionExtent.height) };
    constants.cameraPinholeOffset = { 0.0f, 0.0f };
    constants.cameraPos = ToVector(frame.camera.position);
    constants.cameraUp = ToVector(frame.camera.up);
    constants.cameraRight = ToVector(frame.camera.right);
    constants.cameraFwd = ToVector(frame.camera.forward);
    constants.cameraNear = frame.cameraNear;
    constants.cameraFar = frame.cameraFar;
    constants.cameraFOV = frame.cameraVerticalFov;
    constants.cameraAspectRatio = frame.camera.aspectRatio;
    constants.depthInverted = frame.depthInverted ? sl::eTrue : sl::eFalse;
    constants.cameraMotionIncluded = sl::eTrue;
    constants.motionVectors3D = sl::eFalse;
    constants.reset = reset ? sl::eTrue : sl::eFalse;
    constants.orthographicProjection = frame.camera.orthographicProjection ? sl::eTrue : sl::eFalse;
    constants.motionVectorsDilated = frame.motionVectorsDilated ? sl::eTrue : sl::eFalse;
    constants.motionVectorsJittered = sl::eFalse;
    result = ToResult(state.setConstants(constants, *activeFrame->second.token, sl::ViewportHandle{ viewportId }));
    if (!result.IsSuccess())
    {
        viewport.forceReset = true;
        return result;
    }
    viewport.forceReset = false;
    activeFrame->second.viewports.emplace(viewportId, frame);
    viewport.lastFrameId = frame.realFrameId;
    viewport.historyRevision = frame.historyRevision;
    viewport.renderExtent = frame.renderExtent;
    viewport.displayExtent = frame.displayExtent;
    return result;
}

TemporalResult DlssTemporalAdapter::DispatchUpscaling(uint32_t viewportId, uint64_t realFrameId,
    TemporalQuality quality, const DlssUpscaleResources& resources)
{
    auto& state = *m_implementation;
    std::lock_guard lock(state.mutex);
    if (!state.deviceBound) return { TemporalStatus::NotInitialized };
    if (!state.capabilities.upscaling.IsSuccess()) return state.capabilities.upscaling;
    const auto* frame = state.FindFrame(realFrameId, viewportId);
    if (!frame || !resources.commandList || ToMode(quality) == sl::DLSSMode::eOff)
        return { TemporalStatus::InvalidInput };
    if (resources.commandList->GetType() != D3D12_COMMAND_LIST_TYPE_DIRECT &&
        resources.commandList->GetType() != D3D12_COMMAND_LIST_TYPE_COMPUTE)
        return { TemporalStatus::InvalidInput };
    auto& viewport = state.viewports[viewportId];
    if (viewport.upscaleFaulted) return { TemporalStatus::IntegrationRequired };
    if (viewport.lastUpscaleFrame >= realFrameId) return { TemporalStatus::InvalidInput };
    const auto motionExtent = frame->motionVectorsAtDisplayResolution ? frame->displayExtent : frame->renderExtent;
    if (!IsTextureValid(resources.color, frame->renderExtent) ||
        !IsTextureValid(resources.depth, frame->renderExtent) ||
        !IsTextureValid(resources.motionVectors, motionExtent) ||
        !IsTextureValid(resources.output, frame->displayExtent, true) ||
        (resources.exposure.resource && !IsTextureValid(resources.exposure, { 1, 1 })) ||
        resources.output.resource == resources.color.resource || resources.output.resource == resources.depth.resource ||
        resources.output.resource == resources.motionVectors.resource || resources.output.resource == resources.exposure.resource)
        return { TemporalStatus::InvalidInput };
    if (!IsColorFormat(resources.color.resource->GetDesc().Format, frame->highDynamicRange) ||
        !IsColorFormat(resources.output.resource->GetDesc().Format, frame->highDynamicRange) ||
        !IsDepthFormat(resources.depth.resource->GetDesc().Format) ||
        !IsMotionFormat(resources.motionVectors.resource->GetDesc().Format) ||
        (resources.exposure.resource && resources.exposure.resource->GetDesc().Format != DXGI_FORMAT_R32_FLOAT &&
            resources.exposure.resource->GetDesc().Format != DXGI_FORMAT_R16_FLOAT))
        return { TemporalStatus::FeatureUnsupported };
    sl::DLSSOptions options;
    options.mode = ToMode(quality);
    options.outputWidth = frame->displayExtent.width;
    options.outputHeight = frame->displayExtent.height;
    options.preExposure = frame->preExposure;
    options.colorBuffersHDR = frame->highDynamicRange ? sl::eTrue : sl::eFalse;
    options.useAutoExposure = resources.exposure.resource ? sl::eFalse : sl::eTrue;
    sl::DLSSOptimalSettings optimal;
    auto result = ToResult(state.getOptimalSettings(options, optimal));
    if (!result.IsSuccess()) return result;
    if (frame->renderExtent.width < optimal.renderWidthMin || frame->renderExtent.width > optimal.renderWidthMax ||
        frame->renderExtent.height < optimal.renderHeightMin || frame->renderExtent.height > optimal.renderHeightMax)
        return { TemporalStatus::InvalidInput };
    const sl::ViewportHandle handle{ viewportId };
    viewport.upscaleResources = true; // Failed option/evaluate calls can leave feature state behind.
    result = ToResult(state.setUpscaleOptions(handle, options));
    if (!result.IsSuccess())
    {
        viewport.upscaleFaulted = true;
        return result;
    }
    auto color = ToResource(resources.color);
    auto depth = ToResource(resources.depth);
    auto motion = ToResource(resources.motionVectors);
    auto exposure = ToResource(resources.exposure);
    auto output = ToResource(resources.output);
    const auto renderExtent = ToExtent(frame->renderExtent);
    const auto displayExtent = ToExtent(frame->displayExtent);
    const auto vectorsExtent = ToExtent(motionExtent);
    const sl::Extent exposureExtent{ 0, 0, 1, 1 };
    sl::ResourceTag tags[] = {
        { &color, sl::kBufferTypeScalingInputColor, sl::eValidUntilEvaluate, &renderExtent },
        { &depth, sl::kBufferTypeDepth, sl::eValidUntilEvaluate, &renderExtent },
        { &motion, sl::kBufferTypeMotionVectors, sl::eValidUntilEvaluate, &vectorsExtent },
        { resources.exposure.resource ? &exposure : nullptr, sl::kBufferTypeExposure,
            sl::eValidUntilEvaluate, &exposureExtent },
        { &output, sl::kBufferTypeScalingOutputColor, sl::eValidUntilEvaluate, &displayExtent }
    };
    // Local evaluation tags cannot overwrite FG's independently retained,
    // frame-based eValidUntilPresent tags for the same viewport and real frame.
    const sl::BaseStructure* inputs[] = { &handle, &tags[0], &tags[1], &tags[2], &tags[3], &tags[4] };
    viewport.upscaleResources = true; // Failed evaluate may still allocate SDK resources.
    result = ToResult(state.evaluate(sl::kFeatureDLSS, *state.frames.at(realFrameId).token,
        inputs, static_cast<uint32_t>(std::size(inputs)), resources.commandList));
    if (result.IsSuccess()) viewport.lastUpscaleFrame = realFrameId;
    else viewport.upscaleFaulted = true;
    // Caller restores root signature/descriptors/pipeline state invalidated by
    // SDK recording and retains inputs/output until submission GPU completion.
    return result;
}

TemporalResult DlssTemporalAdapter::SetFrameGenerationOptions(uint32_t viewportId,
    const TemporalFrameGenerationConfig& configuration, bool enabled, TemporalExtent dynamicResolutionTarget)
{
    const auto validation = ValidateTemporalFrameGenerationConfig(configuration);
    if (!validation.IsSuccess()) return validation;
    if (viewportId != 0 || (dynamicResolutionTarget.IsValid() &&
        (dynamicResolutionTarget.width > configuration.displayExtent.width ||
            dynamicResolutionTarget.height > configuration.displayExtent.height)) ||
        ((dynamicResolutionTarget.width == 0) != (dynamicResolutionTarget.height == 0)))
        return { TemporalStatus::InvalidInput };
    auto& state = *m_implementation;
    std::lock_guard lock(state.mutex);
    if (!state.deviceBound) return { TemporalStatus::NotInitialized };
    if (!state.capabilities.frameGeneration.IsSuccess()) return state.capabilities.frameGeneration;
    if (enabled && state.frameGenerationFaulted) return { TemporalStatus::IntegrationRequired };
    if (!state.swapchain || configuration.displayExtent != state.presentation.displayExtent ||
        configuration.transferFunction != state.presentation.transferFunction ||
        (state.frameGenerationConfigured && viewportId != state.frameGenerationViewport))
        return { TemporalStatus::IntegrationRequired };
    sl::DLSSGState current;
    const sl::ViewportHandle handle{ viewportId };
    auto result = ToResult(state.getFrameGenerationState(handle, current, nullptr));
    if (!result.IsSuccess()) return result;
    state.pendingPresentedFrames += current.numFramesActuallyPresented;
    if (enabled && (configuration.interpolatedFrameCount > current.numFramesToGenerateMax ||
        configuration.displayExtent.width < current.minWidthOrHeight ||
        configuration.displayExtent.height < current.minWidthOrHeight))
        return { TemporalStatus::FeatureUnsupported };
    if (enabled)
    {
        sl::ReflexOptions reflex;
        reflex.mode = sl::ReflexMode::eLowLatency;
        result = ToResult(state.setReflexOptions(reflex));
        if (!result.IsSuccess()) return result;
    }
    sl::DLSSGOptions options;
    options.mode = enabled ? sl::DLSSGMode::eOn : sl::DLSSGMode::eOff;
    options.numFramesToGenerate = configuration.interpolatedFrameCount;
    options.colorWidth = configuration.displayExtent.width;
    options.colorHeight = configuration.displayExtent.height;
    options.queueParallelismMode = sl::DLSSGQueueParallelismMode::eBlockPresentingClientQueue;
    options.onErrorCallback = OnPresentationError;
    // Disable first, then drain, then explicitly free. A failed drain must not
    // have already discarded feature allocations through the Off transition.
    options.flags |= sl::DLSSGFlags::eRetainResourcesWhenOff;
    options.enableUserInterfaceRecomposition = sl::eFalse;
    if (dynamicResolutionTarget.IsValid())
    {
        options.flags |= sl::DLSSGFlags::eDynamicResolutionEnabled;
        options.dynamicResWidth = dynamicResolutionTarget.width;
        options.dynamicResHeight = dynamicResolutionTarget.height;
    }
    result = ToResult(state.setFrameGenerationOptions(handle, options));
    if (result.IsSuccess())
    {
        state.frameGenerationEnabled = enabled;
        state.frameGenerationConfigured = true;
        state.frameGenerationViewport = viewportId;
        state.dynamicResolutionTarget = dynamicResolutionTarget;
        if (!enabled) state.lastFrameGenerationExtent = {};
    }
    else state.frameGenerationFaulted = true;
    return result;
}

TemporalResult DlssTemporalAdapter::PrepareFrameGeneration(uint32_t viewportId, uint64_t realFrameId,
    const DlssFrameGenerationResources& resources)
{
    auto& state = *m_implementation;
    std::lock_guard lock(state.mutex);
    if (!state.deviceBound) return { TemporalStatus::NotInitialized };
    if (!state.frameGenerationEnabled || viewportId != state.frameGenerationViewport)
        return { TemporalStatus::IntegrationRequired };
    if (state.frameGenerationFaulted) return { TemporalStatus::IntegrationRequired };
    if (!resources.lifetimeToken) return { TemporalStatus::InvalidInput };
    if (state.pendingFrameGenerationOwner) return { TemporalStatus::IntegrationRequired };
    const auto* frame = state.FindFrame(realFrameId, viewportId);
    if (!frame || frame->displayExtent != state.presentation.displayExtent) return { TemporalStatus::InvalidInput };
    if (!state.dynamicResolutionTarget.IsValid() && state.lastFrameGenerationExtent.IsValid() &&
        frame->renderExtent != state.lastFrameGenerationExtent)
        return { TemporalStatus::IntegrationRequired }; // Disable/reconfigure fixed-resolution FG first.
    auto& viewport = state.viewports[viewportId];
    if (viewport.lastFrameGenerationFrame >= realFrameId) return { TemporalStatus::InvalidInput };
    const auto motionExtent = frame->motionVectorsAtDisplayResolution ? frame->displayExtent : frame->renderExtent;
    // Mixed depth/vector extents are supported by SR but not declared by this
    // first FG route. Do not silently attach mismatched projection resources.
    if (motionExtent != frame->renderExtent) return { TemporalStatus::FeatureUnsupported };
    if (!IsTextureValid(resources.hudlessColor, frame->displayExtent) ||
        !IsTextureValid(resources.depth, frame->renderExtent) ||
        !IsTextureValid(resources.motionVectors, motionExtent) ||
        (resources.uiColor.resource && !IsTextureValid(resources.uiColor, frame->displayExtent)))
        return { TemporalStatus::InvalidInput };
    if (resources.hudlessColor.resource == resources.depth.resource ||
        resources.hudlessColor.resource == resources.motionVectors.resource ||
        resources.depth.resource == resources.motionVectors.resource ||
        (resources.uiColor.resource && (resources.uiColor.resource == resources.hudlessColor.resource ||
            resources.uiColor.resource == resources.depth.resource ||
            resources.uiColor.resource == resources.motionVectors.resource)))
        return { TemporalStatus::InvalidInput };
    if (!IsDepthFormat(resources.depth.resource->GetDesc().Format) ||
        !IsMotionFormat(resources.motionVectors.resource->GetDesc().Format))
        return { TemporalStatus::FeatureUnsupported };
    DXGI_SWAP_CHAIN_DESC1 swapchainDescription{};
    const auto hr = state.swapchain->GetDesc1(&swapchainDescription);
    if (FAILED(hr)) return { TemporalStatus::SdkFailure, hr };
    if (resources.hudlessColor.resource->GetDesc().Format != swapchainDescription.Format)
        return { TemporalStatus::FeatureUnsupported };
    // An RGBA10:2 UI texture cannot represent sufficient alpha precision.
    if (resources.uiColor.resource &&
        resources.uiColor.resource->GetDesc().Format == DXGI_FORMAT_R10G10B10A2_UNORM)
        return { TemporalStatus::FeatureUnsupported };
    auto hudless = ToResource(resources.hudlessColor);
    auto ui = ToResource(resources.uiColor);
    auto depth = ToResource(resources.depth);
    auto motion = ToResource(resources.motionVectors);
    const auto displayExtent = ToExtent(frame->displayExtent);
    const auto renderExtent = ToExtent(frame->renderExtent);
    sl::ResourceTag tags[] = {
        { &hudless, sl::kBufferTypeHUDLessColor, sl::eValidUntilPresent, &displayExtent },
        { resources.uiColor.resource ? &ui : nullptr, sl::kBufferTypeUIColorAndAlpha,
            sl::eValidUntilPresent, &displayExtent },
        { &depth, sl::kBufferTypeDepth, sl::eValidUntilPresent, &renderExtent },
        { &motion, sl::kBufferTypeMotionVectors, sl::eValidUntilPresent, &renderExtent }
    };
    // Retain before the first SDK operation that can capture the inputs. Even a
    // failed call may have partially installed tags. COM refs alone do not pin
    // a graph's placed-resource alias allocation or pooled texture slot.
    state.pendingFrameGenerationOwner = resources.lifetimeToken;
    const auto result = ToResult(state.setTags(*state.frames.at(realFrameId).token,
        sl::ViewportHandle{ viewportId }, tags, static_cast<uint32_t>(std::size(tags)), nullptr));
    if (result.IsSuccess())
    {
        viewport.lastFrameGenerationFrame = realFrameId;
        state.lastFrameGenerationExtent = frame->renderExtent;
    }
    else state.frameGenerationFaulted = true;
    return result;
}

TemporalResult DlssTemporalAdapter::GetFrameGenerationState(uint32_t viewportId, DlssFrameGenerationState& output)
{
    output = {};
    if (viewportId != 0) return { TemporalStatus::InvalidInput };
    auto& state = *m_implementation;
    std::lock_guard lock(state.mutex);
    if (!state.deviceBound) return { TemporalStatus::NotInitialized };
    if (!state.capabilities.frameGeneration.IsSuccess()) return state.capabilities.frameGeneration;
    sl::DLSSGState current;
    const auto result = ToResult(state.getFrameGenerationState(sl::ViewportHandle{ viewportId }, current, nullptr));
    if (!result.IsSuccess()) return result;
    output.statusFlags = static_cast<uint32_t>(current.status);
    output.presentedFramesSinceLastQuery = state.pendingPresentedFrames + current.numFramesActuallyPresented;
    state.pendingPresentedFrames = 0;
    output.maxInterpolatedFrames = current.numFramesToGenerateMax;
    output.minimumDimension = current.minWidthOrHeight;
    output.vsyncSupported = current.bIsVsyncSupportAvailable == sl::eTrue;
    output.inputsCompletionFence = current.inputsProcessingCompletionFence;
    output.inputsCompletionValue = current.lastPresentInputsProcessingCompletionFenceValue;
    if (current.status != sl::DLSSGStatus::eOk) state.frameGenerationFaulted = true;
    return current.status == sl::DLSSGStatus::eOk ? result :
        TemporalResult{ TemporalStatus::SdkFailure, output.statusFlags };
}

TemporalResult DlssTemporalAdapter::Sleep(uint64_t realFrameId)
{
    auto& state = *m_implementation;
    sl::FrameToken* token = nullptr;
    PFun_slReflexSleep* sleep = nullptr;
    {
        std::lock_guard lock(state.mutex);
        if (!state.deviceBound) return { TemporalStatus::NotInitialized };
        if (!state.reflexSleep) return { TemporalStatus::FeatureUnsupported };
        const auto frame = state.frames.find(realFrameId);
        if (frame == state.frames.end()) return { TemporalStatus::InvalidInput };
        token = frame->second.token;
        sleep = state.reflexSleep;
    }
    // Do not hold the adapter mutex while the driver waits for previous-frame
    // work: the rendering/presenting thread must remain able to make progress.
    return ToResult(sleep(*token));
}

TemporalResult DlssTemporalAdapter::MarkLatency(uint64_t realFrameId, DlssLatencyMarker marker)
{
    auto& state = *m_implementation;
    std::lock_guard lock(state.mutex);
    if (!state.deviceBound) return { TemporalStatus::NotInitialized };
    if (!state.setMarker) return { TemporalStatus::FeatureUnsupported };
    const auto frame = state.frames.find(realFrameId);
    if (frame == state.frames.end()) return { TemporalStatus::InvalidInput };
    sl::PCLMarker value;
    switch (marker)
    {
    case DlssLatencyMarker::SimulationStart: value = sl::PCLMarker::eSimulationStart; break;
    case DlssLatencyMarker::SimulationEnd: value = sl::PCLMarker::eSimulationEnd; break;
    case DlssLatencyMarker::RenderSubmitStart: value = sl::PCLMarker::eRenderSubmitStart; break;
    case DlssLatencyMarker::RenderSubmitEnd: value = sl::PCLMarker::eRenderSubmitEnd; break;
    case DlssLatencyMarker::PresentStart: value = sl::PCLMarker::ePresentStart; break;
    case DlssLatencyMarker::PresentEnd: value = sl::PCLMarker::ePresentEnd; break;
    case DlssLatencyMarker::InputSample: value = sl::PCLMarker::eControllerInputSample; break;
    case DlssLatencyMarker::LatencyPing: value = sl::PCLMarker::ePCLatencyPing; break;
    case DlssLatencyMarker::TriggerFlash: value = sl::PCLMarker::eTriggerFlash; break;
    default: return { TemporalStatus::InvalidInput };
    }
    const auto result = ToResult(state.setMarker(value, *frame->second.token));
    if (result.IsSuccess() && marker == DlssLatencyMarker::PresentEnd) frame->second.presented = true;
    return result;
}

TemporalResult DlssTemporalAdapter::DiscardRealFrame(uint64_t realFrameId)
{
    auto& state = *m_implementation;
    std::lock_guard lock(state.mutex);
    if (!state.deviceBound) return { TemporalStatus::NotInitialized };
    const auto frame = state.frames.find(realFrameId);
    if (frame == state.frames.end()) return { TemporalStatus::Success };
    for (auto& [viewportId, viewport] : state.viewports)
    {
        if (viewport.lastFrameGenerationFrame == realFrameId) return { TemporalStatus::IntegrationRequired };
    }
    for (const auto& [viewportId, constants] : frame->second.viewports)
        state.viewports[viewportId].forceReset = true;
    state.frames.erase(frame);
    return { TemporalStatus::Success };
}

TemporalResult DlssTemporalAdapter::EndRealFrame(uint64_t realFrameId)
{
    auto& state = *m_implementation;
    std::lock_guard lock(state.mutex);
    if (!state.deviceBound) return { TemporalStatus::NotInitialized };
    const auto frame = state.frames.find(realFrameId);
    if (frame == state.frames.end()) return { TemporalStatus::InvalidInput };
    if (state.frameGenerationEnabled && !frame->second.presented) return { TemporalStatus::IntegrationRequired };
    state.frames.erase(frame);
    return { TemporalStatus::Success };
}

TemporalResult DlssTemporalAdapter::TakePresentationError()
{
    const auto error = g_presentationError.exchange(0, std::memory_order_relaxed);
    if (error != 0)
    {
        auto& state = *m_implementation;
        std::lock_guard lock(state.mutex);
        state.frameGenerationFaulted = true;
    }
    return { error == 0 ? TemporalStatus::Success : TemporalStatus::SdkFailure, error };
}

TemporalResult DlssTemporalAdapter::FreeViewportAfterGpuIdle(uint32_t viewportId)
{
    const auto upscale = FreeUpscalingAfterGpuIdle(viewportId);
    if (!upscale.IsSuccess()) return upscale;
    if (viewportId != 0) return upscale;
    return FreeFrameGenerationAfterGpuIdle(viewportId);
}

TemporalResult DlssTemporalAdapter::FreeUpscalingAfterGpuIdle(uint32_t viewportId)
{
    auto& state = *m_implementation;
    std::lock_guard lock(state.mutex);
    if (!state.deviceBound) return { TemporalStatus::NotInitialized };
    // The caller has drained this viewport's GPU work. Retire its constants
    // from the multiview render-only token; other views retain their own data.
    if (!state.playerFrameTokenOwnership && !state.frameGenerationEnabled)
        for (auto& [id, frame] : state.frames) frame.viewports.erase(viewportId);
    for (const auto& [id, frame] : state.frames)
        if (frame.viewports.contains(viewportId)) return { TemporalStatus::IntegrationRequired };
    const sl::ViewportHandle handle{ viewportId };
    const auto viewport = state.viewports.find(viewportId);
    if (viewport != state.viewports.end() && viewport->second.upscaleResources)
    {
        const auto result = ToResult(state.freeResources(sl::kFeatureDLSS, handle));
        if (!result.IsSuccess()) return result;
        viewport->second.upscaleResources = false;
        viewport->second.upscaleFaulted = false;
        viewport->second.forceReset = true;
        viewport->second.lastUpscaleFrame = 0;
    }
    if (viewport != state.viewports.end() &&
        (!state.frameGenerationConfigured || viewportId != state.frameGenerationViewport))
        state.viewports.erase(viewport);
    return { TemporalStatus::Success };
}

TemporalResult DlssTemporalAdapter::FreeFrameGenerationAfterGpuIdle(uint32_t viewportId)
{
    if (viewportId != 0) return { TemporalStatus::InvalidInput };
    auto& state = *m_implementation;
    std::lock_guard lock(state.mutex);
    if (!state.deviceBound) return { TemporalStatus::NotInitialized };
    if (state.frameGenerationEnabled && viewportId == state.frameGenerationViewport)
        return { TemporalStatus::IntegrationRequired };
    for (const auto& [id, frame] : state.frames)
        if (frame.viewports.contains(viewportId)) return { TemporalStatus::IntegrationRequired };
    const sl::ViewportHandle handle{ viewportId };
    if (state.frameGenerationConfigured && viewportId == state.frameGenerationViewport)
    {
        const auto result = ToResult(state.freeResources(sl::kFeatureDLSS_G, handle));
        if (!result.IsSuccess()) return result;
        state.frameGenerationConfigured = false;
        state.frameGenerationFaulted = false;
        state.lastFrameGenerationExtent = {};
    }
    state.pendingFrameGenerationOwner.reset();
    const auto viewport = state.viewports.find(viewportId);
    if (viewport != state.viewports.end() && !viewport->second.upscaleResources) state.viewports.erase(viewport);
    return { TemporalStatus::Success };
}

TemporalResult DlssTemporalAdapter::ReleaseFrameGenerationInputsAfterGpuIdle()
{
    auto& state = *m_implementation;
    std::lock_guard lock(state.mutex);
    if (!state.deviceBound) return { TemporalStatus::NotInitialized };
    state.pendingFrameGenerationOwner.reset();
    return { TemporalStatus::Success };
}

TemporalResult DlssTemporalAdapter::ShutdownAfterGpuIdle()
{
    auto& state = *m_implementation;
    std::lock_guard lock(state.mutex);
    if (!state.initialized) return { TemporalStatus::NotInitialized };
    if (!state.playerFrameTokenOwnership && !state.frameGenerationEnabled) state.frames.clear();
    if (state.frameGenerationEnabled || !state.frames.empty()) return { TemporalStatus::IntegrationRequired };
    const auto result = ToResult(state.shutdown());
    if (!result.IsSuccess()) return result;
    state.initialized = false;
    state.deviceBound = false;
    state.nativeDevice = nullptr;
    state.swapchain = nullptr;
    state.viewports.clear();
    state.frameGenerationConfigured = false;
    state.pendingFrameGenerationOwner.reset();
    return result;
}

TemporalResult DlssTemporalAdapter::UnloadAfterNativeObjectsDestroyed()
{
    auto& state = *m_implementation;
    std::lock_guard lock(state.mutex);
    if (state.initialized) return { TemporalStatus::IntegrationRequired };
    if (!state.module) return { TemporalStatus::NotInitialized };
    if (!FreeLibrary(state.module)) return { TemporalStatus::SdkFailure, GetLastError() };
    state.module = nullptr;
    state.permanentlyUnloaded = true;
    g_runtimeClaimed.store(false);
    // A fresh object is required for a fresh device. No cached function pointer
    // can silently become valid against a different SDK/module generation.
    state.capabilities.upscaling = state.capabilities.frameGeneration = { TemporalStatus::NotInitialized };
    return { TemporalStatus::Success };
}

namespace
{
    constexpr uint32_t kShaderResourceState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

    DlssDX12Texture ResolveTexture(DX12DeviceResources& resources, RHITextureHandle handle, uint32_t state)
    {
        return { handle.IsValid() ? resources.Resolve(handle) : nullptr, state };
    }

    class DlssDX12Upscaler final : public ITemporalUpscaler
    {
    public:
        DlssDX12Upscaler(DX12DeviceResources& resources, std::shared_ptr<DlssTemporalAdapter> session,
            uint32_t viewportId, TemporalQuality quality, DlssFinalConsumptionDrain drain)
            : m_resources(resources), m_session(std::make_unique<std::shared_ptr<DlssTemporalAdapter>>(std::move(session))),
              m_viewportId(viewportId), m_quality(quality), m_drain(std::move(drain)) {}

        ~DlssDX12Upscaler() override
        {
            if (!Shutdown().IsSuccess()) (void)m_session.release();
        }

        TemporalResult QueryRenderExtent(TemporalQuality quality, TemporalExtent display, TemporalExtent& extent) const override
        {
            DlssOptimalSettings settings;
            const auto result = (*m_session)->GetOptimalSettings(quality, display, settings);
            extent = settings.optimalRenderExtent;
            return result;
        }
        TemporalCapabilities GetCapabilities() const override { return (*m_session)->QueryCapabilities(); }

        TemporalResult Evaluate(const TemporalUpscaleInputs& inputs, RHIEncoder& encoder) override
        {
            if (m_shutdown) return { TemporalStatus::NotInitialized };
            auto result = ValidateTemporalUpscaleInputs(inputs);
            if (!result.IsSuccess()) return result;
            if (!inputs.frame.highDynamicRange) return { TemporalStatus::FeatureUnsupported };
            // The common masks are FSR-style hints. Do not silently reinterpret
            // them as the semantically different DLSS transparency/bias inputs.
            if (inputs.reactiveMask.IsValid() || inputs.transparencyMask.IsValid() || inputs.responsiveMask.IsValid())
                return { TemporalStatus::FeatureUnsupported };
            auto* nativeEncoder = dynamic_cast<DX12Encoder*>(&encoder);
            if (!nativeEncoder || !nativeEncoder->UsesResources(&m_resources) ||
                !(*m_session)->IsBoundToDX12Device(m_resources.GetDevice()))
                return { TemporalStatus::InvalidInput };
            auto* commandList = nativeEncoder->GetCommandList();
            if (!commandList) return { TemporalStatus::InvalidInput };
            const auto listType = commandList->GetType();
            if (listType != D3D12_COMMAND_LIST_TYPE_DIRECT && listType != D3D12_COMMAND_LIST_TYPE_COMPUTE)
                return { TemporalStatus::InvalidInput };
            const uint32_t readState = listType == D3D12_COMMAND_LIST_TYPE_COMPUTE ?
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE : kShaderResourceState;
            DlssUpscaleResources native;
            native.commandList = commandList;
            native.color = ResolveTexture(m_resources, inputs.color, readState);
            native.depth = ResolveTexture(m_resources, inputs.depth, readState);
            native.motionVectors = ResolveTexture(m_resources, inputs.motionVectors, readState);
            native.exposure = ResolveTexture(m_resources, inputs.exposure, readState);
            native.output = ResolveTexture(m_resources, inputs.output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            if (inputs.exposure.IsValid() && !native.exposure.resource) return { TemporalStatus::InvalidInput };
            result = (*m_session)->SetFrameConstants(m_viewportId, inputs.frame);
            if (!result.IsSuccess()) return result;
            result = (*m_session)->DispatchUpscaling(m_viewportId, inputs.frame.realFrameId, m_quality, native);
            // Pinned sl.dlss source reverses resource transitions via ScopedTasks;
            // command binding caches still need invalidation, including failure
            // paths that may have partially recorded GPU commands.
            nativeEncoder->ResetState(commandList);
            return result;
        }

        TemporalResult Shutdown() override
        {
            if (m_shutdown) return { TemporalStatus::Success };
            const auto drained = m_drain();
            if (!drained.IsSuccess()) return drained;
            const auto result = (*m_session)->FreeUpscalingAfterGpuIdle(m_viewportId);
            if (result.IsSuccess()) m_shutdown = true;
            return result;
        }

    private:
        DX12DeviceResources& m_resources;
        std::unique_ptr<std::shared_ptr<DlssTemporalAdapter>> m_session;
        uint32_t m_viewportId;
        TemporalQuality m_quality;
        DlssFinalConsumptionDrain m_drain;
        bool m_shutdown{ false };
    };

    class DlssDX12FrameGenerator final : public ITemporalFrameGenerator
    {
    public:
        DlssDX12FrameGenerator(DX12DeviceResources& resources, std::shared_ptr<DlssTemporalAdapter> session,
            const TemporalFrameGenerationConfig& configuration, DlssFinalConsumptionDrain drain)
            : m_resources(resources), m_session(std::make_unique<std::shared_ptr<DlssTemporalAdapter>>(std::move(session))),
              m_configuration(configuration), m_drain(std::move(drain)) {}

        ~DlssDX12FrameGenerator() override
        {
            if (!Shutdown().IsSuccess()) (void)m_session.release();
        }

        TemporalCapabilities GetCapabilities() const override { return (*m_session)->QueryCapabilities(); }

        TemporalResult Prepare(const TemporalFrameGenerationInputs& inputs, RHIEncoder& encoder) override
        {
            if (m_shutdown) return { TemporalStatus::NotInitialized };
            const auto validation = ValidateTemporalFrameGenerationInputs(inputs);
            if (!validation.IsSuccess()) return validation;
            auto* nativeEncoder = dynamic_cast<DX12Encoder*>(&encoder);
            if (!nativeEncoder || !nativeEncoder->UsesResources(&m_resources) ||
                !(*m_session)->IsBoundToDX12Device(m_resources.GetDevice()))
                return { TemporalStatus::InvalidInput };
            auto* commandList = nativeEncoder->GetCommandList();
            if (!commandList) return { TemporalStatus::InvalidInput };
            if (commandList->GetType() != D3D12_COMMAND_LIST_TYPE_DIRECT)
                return { TemporalStatus::IntegrationRequired };
            if (m_pendingInputs)
            {
                // Deliberately serial until the shell provides per-present
                // fence attribution. Do not grow an unbounded owner queue or
                // equate CPU Present completion with resource availability.
                auto result = m_drain();
                if (!result.IsSuccess()) return result;
                result = (*m_session)->ReleaseFrameGenerationInputsAfterGpuIdle();
                if (!result.IsSuccess()) return result;
                m_pendingInputs = false;
            }
            DlssFrameGenerationResources native;
            native.hudlessColor = ResolveTexture(m_resources, inputs.hudlessColor, kShaderResourceState);
            native.uiColor = ResolveTexture(m_resources, inputs.uiColor, kShaderResourceState);
            native.depth = ResolveTexture(m_resources, inputs.depth, kShaderResourceState);
            native.motionVectors = ResolveTexture(m_resources, inputs.motionVectors, kShaderResourceState);
            native.lifetimeToken = inputs.lifetimeToken;
            if (inputs.uiColor.IsValid() && !native.uiColor.resource) return { TemporalStatus::InvalidInput };
            auto result = (*m_session)->SetFrameConstants(0, inputs.frame);
            if (!result.IsSuccess()) return result;
            m_pendingInputs = true;
            result = (*m_session)->PrepareFrameGeneration(0, inputs.frame.realFrameId, native);
            nativeEncoder->ResetState(commandList);
            return result;
        }

        TemporalResult Shutdown() override
        {
            if (m_shutdown) return { TemporalStatus::Success };
            // RetainResourcesWhenOff makes this an allocation-preserving pause.
            auto result = (*m_session)->SetFrameGenerationOptions(0, m_configuration, false);
            if (!result.IsSuccess()) return result;
            result = m_drain();
            if (!result.IsSuccess()) return result;
            result = (*m_session)->FreeFrameGenerationAfterGpuIdle(0);
            if (result.IsSuccess()) m_shutdown = true;
            return result;
        }

    private:
        DX12DeviceResources& m_resources;
        std::unique_ptr<std::shared_ptr<DlssTemporalAdapter>> m_session;
        TemporalFrameGenerationConfig m_configuration;
        DlssFinalConsumptionDrain m_drain;
        bool m_shutdown{ false };
        bool m_pendingInputs{ false };
    };
}

TemporalResult CreateDlssDX12Upscaler(DX12DeviceResources& resources,
    std::shared_ptr<DlssTemporalAdapter> session, uint32_t viewportId, TemporalQuality quality,
    DlssFinalConsumptionDrain drain, std::unique_ptr<ITemporalUpscaler>& upscaler)
{
    if (upscaler) return { TemporalStatus::AlreadyInitialized };
    if (!session || !drain || viewportId == UINT32_MAX || ToMode(quality) == sl::DLSSMode::eOff)
        return { TemporalStatus::InvalidInput };
    const auto capability = session->QueryCapabilities().upscaling;
    if (!capability.IsSuccess()) return capability;
    if (!session->IsBoundToDX12Device(resources.GetDevice())) return { TemporalStatus::InvalidInput };
    upscaler = std::make_unique<DlssDX12Upscaler>(resources, std::move(session), viewportId, quality, std::move(drain));
    return { TemporalStatus::Success };
}

TemporalResult CreateDlssDX12FrameGenerator(DX12DeviceResources& resources,
    std::shared_ptr<DlssTemporalAdapter> session, const TemporalFrameGenerationConfig& configuration,
    DlssFinalConsumptionDrain drain, std::unique_ptr<ITemporalFrameGenerator>& frameGenerator)
{
    if (frameGenerator) return { TemporalStatus::AlreadyInitialized };
    if (!session || !drain) return { TemporalStatus::InvalidInput };
    const auto validation = ValidateTemporalFrameGenerationConfig(configuration);
    if (!validation.IsSuccess()) return validation;
    const auto capability = session->QueryCapabilities().frameGeneration;
    if (!capability.IsSuccess()) return capability;
    if (!session->IsBoundToDX12Device(resources.GetDevice())) return { TemporalStatus::InvalidInput };
    const auto binding = session->ValidatePlayerSwapchainBinding(configuration);
    if (!binding.IsSuccess()) return binding;
    // Enabling remains the shell's explicit presenting-thread operation, after
    // a real proxy swapchain has been bound and pacing ownership has been chosen.
    frameGenerator = std::make_unique<DlssDX12FrameGenerator>(resources, std::move(session), configuration, std::move(drain));
    return { TemporalStatus::Success };
}

#else

struct DlssTemporalAdapter::Implementation
{
    TemporalBackend backend{ TemporalBackend::DX12 };
};

DlssTemporalAdapter::DlssTemporalAdapter() : m_implementation(std::make_unique<Implementation>()) {}
DlssTemporalAdapter::~DlssTemporalAdapter() = default;
TemporalResult DlssTemporalAdapter::Initialize(const DlssInitialization& initialization)
{
    m_implementation->backend = initialization.backend;
    return { initialization.backend == TemporalBackend::DX12 ?
        TemporalStatus::SdkNotBuilt : TemporalStatus::BackendUnsupported };
}
TemporalCapabilities DlssTemporalAdapter::QueryCapabilities()
{
    TemporalCapabilities result;
    result.provider = TemporalProvider::Dlss;
    result.backend = m_implementation->backend;
    result.upscaling = result.frameGeneration = { result.backend == TemporalBackend::DX12 ?
        TemporalStatus::SdkNotBuilt : TemporalStatus::BackendUnsupported };
    result.sdkVersion = "Streamline 2.14.1";
    result.sdkRevision = "2122257e0fce486f91b385aa63b9a09b0a34b363";
    return result;
}
TemporalResult DlssTemporalAdapter::BindDX12Device(ID3D12Device*) { return { TemporalStatus::SdkNotBuilt }; }
bool DlssTemporalAdapter::IsBoundToDX12Device(ID3D12Device*) const { return false; }
bool DlssTemporalAdapter::MatchesRuntimeConfiguration(const std::wstring&, const std::string&) const { return false; }
TemporalResult DlssTemporalAdapter::UpgradeDX12Interface(void**, DlssDX12ProxyKind)
{ return { TemporalStatus::SdkNotBuilt }; }
TemporalResult DlssTemporalAdapter::BindPlayerSwapchain(IDXGISwapChain3*,
    const TemporalFrameGenerationConfig& configuration, DlssPacingOwner)
{
    const auto result = ValidateTemporalFrameGenerationConfig(configuration);
    return result.IsSuccess() ? TemporalResult{ TemporalStatus::SdkNotBuilt } : result;
}
TemporalResult DlssTemporalAdapter::GetOptimalSettings(TemporalQuality, TemporalExtent, DlssOptimalSettings& settings)
{ settings = {}; return { TemporalStatus::SdkNotBuilt }; }
TemporalResult DlssTemporalAdapter::ValidatePlayerSwapchainBinding(const TemporalFrameGenerationConfig&) const
{ return { TemporalStatus::SdkNotBuilt }; }
TemporalResult DlssTemporalAdapter::BeginRealFrame(uint64_t) { return { TemporalStatus::SdkNotBuilt }; }
TemporalResult DlssTemporalAdapter::EnsureRealFrame(uint64_t) { return { TemporalStatus::SdkNotBuilt }; }
void DlssTemporalAdapter::SetPlayerFrameTokenOwnership(bool) {}
bool DlssTemporalAdapter::HasPlayerFrameTokenOwnership() const { return false; }
TemporalResult DlssTemporalAdapter::SetFrameConstants(uint32_t, const TemporalFrame&)
{ return { TemporalStatus::SdkNotBuilt }; }
TemporalResult DlssTemporalAdapter::DispatchUpscaling(uint32_t, uint64_t, TemporalQuality, const DlssUpscaleResources&)
{ return { TemporalStatus::SdkNotBuilt }; }
TemporalResult DlssTemporalAdapter::SetFrameGenerationOptions(uint32_t,
    const TemporalFrameGenerationConfig& configuration, bool, TemporalExtent)
{
    const auto result = ValidateTemporalFrameGenerationConfig(configuration);
    return result.IsSuccess() ? TemporalResult{ TemporalStatus::SdkNotBuilt } : result;
}
TemporalResult DlssTemporalAdapter::PrepareFrameGeneration(uint32_t, uint64_t, const DlssFrameGenerationResources&)
{ return { TemporalStatus::SdkNotBuilt }; }
TemporalResult DlssTemporalAdapter::GetFrameGenerationState(uint32_t, DlssFrameGenerationState& state)
{ state = {}; return { TemporalStatus::SdkNotBuilt }; }
TemporalResult DlssTemporalAdapter::Sleep(uint64_t) { return { TemporalStatus::SdkNotBuilt }; }
TemporalResult DlssTemporalAdapter::MarkLatency(uint64_t, DlssLatencyMarker) { return { TemporalStatus::SdkNotBuilt }; }
TemporalResult DlssTemporalAdapter::DiscardRealFrame(uint64_t) { return { TemporalStatus::SdkNotBuilt }; }
TemporalResult DlssTemporalAdapter::EndRealFrame(uint64_t) { return { TemporalStatus::SdkNotBuilt }; }
TemporalResult DlssTemporalAdapter::TakePresentationError() { return { TemporalStatus::SdkNotBuilt }; }
TemporalResult DlssTemporalAdapter::FreeViewportAfterGpuIdle(uint32_t) { return { TemporalStatus::SdkNotBuilt }; }
TemporalResult DlssTemporalAdapter::FreeUpscalingAfterGpuIdle(uint32_t) { return { TemporalStatus::SdkNotBuilt }; }
TemporalResult DlssTemporalAdapter::FreeFrameGenerationAfterGpuIdle(uint32_t) { return { TemporalStatus::SdkNotBuilt }; }
TemporalResult DlssTemporalAdapter::ReleaseFrameGenerationInputsAfterGpuIdle() { return { TemporalStatus::SdkNotBuilt }; }
TemporalResult DlssTemporalAdapter::ShutdownAfterGpuIdle() { return { TemporalStatus::SdkNotBuilt }; }
TemporalResult DlssTemporalAdapter::UnloadAfterNativeObjectsDestroyed() { return { TemporalStatus::SdkNotBuilt }; }
TemporalResult CreateDlssDX12Upscaler(DX12DeviceResources&,
    std::shared_ptr<DlssTemporalAdapter>, uint32_t, TemporalQuality, DlssFinalConsumptionDrain,
    std::unique_ptr<ITemporalUpscaler>& upscaler)
{ return { upscaler ? TemporalStatus::AlreadyInitialized : TemporalStatus::SdkNotBuilt }; }
TemporalResult CreateDlssDX12FrameGenerator(DX12DeviceResources&,
    std::shared_ptr<DlssTemporalAdapter>, const TemporalFrameGenerationConfig& configuration,
    DlssFinalConsumptionDrain, std::unique_ptr<ITemporalFrameGenerator>& frameGenerator)
{
    if (frameGenerator) return { TemporalStatus::AlreadyInitialized };
    const auto validation = ValidateTemporalFrameGenerationConfig(configuration);
    return validation.IsSuccess() ? TemporalResult{ TemporalStatus::SdkNotBuilt } : validation;
}

#endif
