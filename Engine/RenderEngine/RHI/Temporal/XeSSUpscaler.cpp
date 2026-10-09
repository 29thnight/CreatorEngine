#include "XeSSUpscaler.h"

#include <cmath>
#include <utility>

#if defined(_WIN32) && (CREATOR_ENABLE_XESS_SDK || CREATOR_ENABLE_XESS_VULKAN_SDK)
#include <xess/xess.h>
#if CREATOR_ENABLE_XESS_SDK
#include <xess/xess_d3d12.h>
#include "../DX12/DX12DeviceResources.h"
#include "../DX12/DX12Encoder.h"
#endif
#if CREATOR_ENABLE_XESS_VULKAN_SDK
#include <xess/xess_vk.h>
#include "../Vulkan/VulkanDeviceResources.h"
#include "../Vulkan/VulkanEncoder.h"
#include "../Vulkan/VulkanFormat.h"
#endif

namespace
{
TemporalResult XeSSResult(xess_result_t result)
{
    if (result >= XESS_RESULT_SUCCESS) return { TemporalStatus::Success, result };
    switch (result)
    {
    case XESS_RESULT_ERROR_UNSUPPORTED_DEVICE:
    case XESS_RESULT_ERROR_UNSUPPORTED_DRIVER:
    case XESS_RESULT_ERROR_UNSUPPORTED:
    case XESS_RESULT_ERROR_NOT_IMPLEMENTED: return { TemporalStatus::FeatureUnsupported, result };
    case XESS_RESULT_ERROR_CANT_LOAD_LIBRARY: return { TemporalStatus::RuntimeUnavailable, result };
    case XESS_RESULT_ERROR_UNINITIALIZED:
    case XESS_RESULT_ERROR_INVALID_CONTEXT: return { TemporalStatus::NotInitialized, result };
    case XESS_RESULT_ERROR_INVALID_ARGUMENT: return { TemporalStatus::InvalidInput, result };
    default: return { TemporalStatus::SdkFailure, result };
    }
}

bool GetQuality(TemporalQuality quality, xess_quality_settings_t& result)
{
    switch (quality)
    {
    case TemporalQuality::NativeAA: result = XESS_QUALITY_SETTING_AA; return true;
    case TemporalQuality::Quality: result = XESS_QUALITY_SETTING_QUALITY; return true;
    case TemporalQuality::Balanced: result = XESS_QUALITY_SETTING_BALANCED; return true;
    case TemporalQuality::Performance: result = XESS_QUALITY_SETTING_PERFORMANCE; return true;
    case TemporalQuality::UltraPerformance: result = XESS_QUALITY_SETTING_ULTRA_PERFORMANCE; return true;
    }
    return false;
}

uint32_t GetFlags(const XeSSUpscalerConfig& config)
{
    uint32_t flags = XESS_INIT_FLAG_NONE;
    if (config.depthInverted) flags |= XESS_INIT_FLAG_INVERTED_DEPTH;
    if (!config.highDynamicRange) flags |= XESS_INIT_FLAG_LDR_INPUT_COLOR;
    if (config.motionVectorsAtDisplayResolution) flags |= XESS_INIT_FLAG_HIGH_RES_MV;
    if (config.exposureTexture) flags |= XESS_INIT_FLAG_EXPOSURE_SCALE_TEXTURE;
    if (config.responsiveMask) flags |= XESS_INIT_FLAG_RESPONSIVE_PIXEL_MASK;
    if (config.autoExposure) flags |= XESS_INIT_FLAG_ENABLE_AUTOEXPOSURE;
    return flags;
}

struct UpscalerCore
{
    XeSSModule m_module;
    xess_context_handle_t m_context{ nullptr };
    decltype(&xessGetVersion) m_getVersion{ nullptr };
    decltype(&xessDestroyContext) m_destroy{ nullptr };
    decltype(&xessGetOptimalInputResolution) m_getResolution{ nullptr };
    decltype(&xessSetVelocityScale) m_setVelocityScale{ nullptr };
    decltype(&xessSetExposureMultiplier) m_setExposureMultiplier{ nullptr };
    XeSSUpscalerConfig m_config;
    xess_quality_settings_t m_quality{};
    xess_version_t m_runtimeVersion{};
    bool m_initialized{ false };
    bool m_hasHistory{ false };
    bool m_forceReset{ true };
    uint64_t m_lastFrameId{ 0 };
    uint64_t m_historyRevision{ 0 };
    TemporalExtent m_renderExtent;

    TemporalResult Load(const wchar_t* directory)
    {
        auto result = m_module.Load(directory, L"libxess.dll");
        if (!result.IsSuccess()) return result;
        if (!m_module.Resolve(m_getVersion, "xessGetVersion") ||
            !m_module.Resolve(m_destroy, "xessDestroyContext") ||
            !m_module.Resolve(m_getResolution, "xessGetOptimalInputResolution") ||
            !m_module.Resolve(m_setVelocityScale, "xessSetVelocityScale") ||
            !m_module.Resolve(m_setExposureMultiplier, "xessSetExposureMultiplier"))
            return XeSSMissingExport();
        result = XeSSResult(m_getVersion(&m_runtimeVersion));
        if (!result.IsSuccess()) return result;
        // SDK versions use a major ABI boundary; patch >= 90 is a development ABI.
        // The pinned package's SR component is 2.0.x, not package version 3.0.x.
        if (m_runtimeVersion.major != 2 || m_runtimeVersion.patch >= 90)
            return { TemporalStatus::SdkVersionMismatch };
        return result;
    }

    TemporalResult ValidateConfig(const XeSSUpscalerConfig& config)
    {
        if (!config.displayExtent.IsValid() || !GetQuality(config.quality, m_quality) ||
            (config.exposureTexture && config.autoExposure) ||
            (!config.highDynamicRange && config.autoExposure))
            return { TemporalStatus::InvalidInput };
        m_config = config;
        return { TemporalStatus::Success };
    }

    TemporalResult QueryResolution(XeSSInputResolution& resolution) const
    {
        resolution = {};
        if (!m_initialized) return { TemporalStatus::NotInitialized };
        const xess_2d_t output{ m_config.displayExtent.width, m_config.displayExtent.height };
        xess_2d_t optimal{}, minimum{}, maximum{};
        const auto result = XeSSResult(m_getResolution(m_context, &output, m_quality, &optimal, &minimum, &maximum));
        if (result.IsSuccess())
        {
            resolution.optimal = { optimal.x, optimal.y };
            resolution.minimum = { minimum.x, minimum.y };
            resolution.maximum = { maximum.x, maximum.y };
        }
        return result;
    }

    TemporalResult ValidateFrame(const TemporalFrame& frame, float exposure) const
    {
        if (!m_initialized) return { TemporalStatus::NotInitialized };
        const auto valid = ValidateTemporalFrame(frame);
        if (!valid.IsSuccess()) return valid;
        if (frame.displayExtent != m_config.displayExtent || frame.depthInverted != m_config.depthInverted ||
            frame.highDynamicRange != m_config.highDynamicRange ||
            frame.motionVectorsAtDisplayResolution != m_config.motionVectorsAtDisplayResolution ||
            (frame.motionVectorsAtDisplayResolution && !frame.motionVectorsDilated) ||
            (m_hasHistory && frame.realFrameId <= m_lastFrameId) || !std::isfinite(exposure) || exposure <= 0.0f)
            return { TemporalStatus::InvalidInput };
        XeSSInputResolution range;
        const auto queried = QueryResolution(range);
        if (!queried.IsSuccess()) return queried;
        if (frame.renderExtent.width < range.minimum.width || frame.renderExtent.width > range.maximum.width ||
            frame.renderExtent.height < range.minimum.height || frame.renderExtent.height > range.maximum.height)
            return { TemporalStatus::InvalidInput };
        return queried;
    }

    bool ShouldReset(const TemporalFrame& frame) const
    {
        return m_forceReset || !m_hasHistory || frame.reset || frame.historyRevision != m_historyRevision ||
            frame.renderExtent != m_renderExtent;
    }

    TemporalResult SetFrameScales(const TemporalFrame& frame)
    {
        auto result = XeSSResult(m_setVelocityScale(m_context, frame.motionVectorScaleX, frame.motionVectorScaleY));
        if (!result.IsSuccess()) return result;
        return XeSSResult(m_setExposureMultiplier(m_context, 1.0f / frame.preExposure));
    }

    void CommitFrame(const TemporalFrame& frame)
    {
        m_hasHistory = true;
        m_forceReset = false;
        m_lastFrameId = frame.realFrameId;
        m_historyRevision = frame.historyRevision;
        m_renderExtent = frame.renderExtent;
    }

    TemporalResult Destroy()
    {
        if (!m_context) return { TemporalStatus::Success };
        const auto result = XeSSResult(m_destroy(m_context));
        if (result.IsSuccess())
        {
            m_context = nullptr;
            m_initialized = false;
        }
        return result;
    }
};
}
#endif

struct XeSSUpscalerDX12State
{
#if defined(_WIN32) && CREATOR_ENABLE_XESS_SDK
    UpscalerCore m_core;
    decltype(&xessD3D12CreateContext) m_create{ nullptr };
    decltype(&xessD3D12Init) m_initialize{ nullptr };
    decltype(&xessD3D12Execute) m_execute{ nullptr };

    TemporalResult Load(ID3D12Device* device, const wchar_t* directory)
    {
        if (!device) return { TemporalStatus::InvalidInput };
        auto result = m_core.Load(directory);
        if (!result.IsSuccess()) return result;
        if (!m_core.m_module.Resolve(m_create, "xessD3D12CreateContext") ||
            !m_core.m_module.Resolve(m_initialize, "xessD3D12Init") ||
            !m_core.m_module.Resolve(m_execute, "xessD3D12Execute"))
            return XeSSMissingExport();
        return XeSSResult(m_create(device, &m_core.m_context));
    }
#endif
};

XeSSUpscalerDX12::XeSSUpscalerDX12() = default;
XeSSUpscalerDX12::~XeSSUpscalerDX12()
{
    // A failed SDK destroy may still own live GPU objects. Keep the module and
    // context alive rather than unload code referenced by them. Explicit Shutdown
    // lets the host resolve the error and retry before destruction.
    if (!Shutdown().IsSuccess()) (void)m_state.release();
}

TemporalResult XeSSUpscalerDX12::QuerySupport(ID3D12Device* device, const wchar_t* runtimeDirectory)
{
#if defined(_WIN32) && CREATOR_ENABLE_XESS_SDK
    XeSSUpscalerDX12 probe;
    probe.m_state = std::make_unique<XeSSUpscalerDX12State>();
    const auto result = probe.m_state->Load(device, runtimeDirectory);
    const auto destroyed = probe.Shutdown();
    return result.IsSuccess() && !destroyed.IsSuccess() ? destroyed : result;
#else
    return { TemporalStatus::SdkNotBuilt };
#endif
}

TemporalResult XeSSUpscalerDX12::Initialize(ID3D12Device* device, const wchar_t* runtimeDirectory,
    const XeSSUpscalerConfig& config)
{
#if defined(_WIN32) && CREATOR_ENABLE_XESS_SDK
    if (m_state) return { TemporalStatus::AlreadyInitialized };
    m_state = std::make_unique<XeSSUpscalerDX12State>();
    auto result = m_state->m_core.ValidateConfig(config);
    if (result.IsSuccess()) result = m_state->Load(device, runtimeDirectory);
    if (result.IsSuccess())
    {
        xess_d3d12_init_params_t params{};
        params.outputResolution = { config.displayExtent.width, config.displayExtent.height };
        params.qualitySetting = m_state->m_core.m_quality;
        params.initFlags = GetFlags(config);
        result = XeSSResult(m_state->m_initialize(m_state->m_core.m_context, &params));
        m_state->m_core.m_initialized = result.IsSuccess();
    }
    if (!result.IsSuccess()) (void)Shutdown();
    return result;
#else
    return { TemporalStatus::SdkNotBuilt };
#endif
}

TemporalResult XeSSUpscalerDX12::QueryInputResolution(XeSSInputResolution& resolution) const
{
    resolution = {};
#if defined(_WIN32) && CREATOR_ENABLE_XESS_SDK
    return m_state ? m_state->m_core.QueryResolution(resolution) : TemporalResult{ TemporalStatus::NotInitialized };
#else
    return { TemporalStatus::SdkNotBuilt };
#endif
}

std::string XeSSUpscalerDX12::GetRuntimeVersion() const
{
#if defined(_WIN32) && CREATOR_ENABLE_XESS_SDK
    if (m_state)
    {
        const auto& version = m_state->m_core.m_runtimeVersion;
        return XeSSComponentVersion("XeSS-SR", version.major, version.minor, version.patch);
    }
#endif
    return {};
}

#if defined(_WIN32) && CREATOR_ENABLE_XESS_SDK
namespace
{
bool ValidDX12Texture(const XeSSDX12Texture& texture, TemporalExtent extent, bool output = false)
{
    if (!texture.resource || !extent.IsValid()) return false;
    const auto description = texture.resource->GetDesc();
    const auto expectedState = output ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS
                                      : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    return description.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D && description.SampleDesc.Count == 1 &&
        description.DepthOrArraySize == 1 && (texture.state & expectedState) == expectedState &&
        texture.state != UINT32_MAX && texture.baseX <= description.Width &&
        extent.width <= description.Width - texture.baseX && texture.baseY <= description.Height &&
        extent.height <= description.Height - texture.baseY &&
        (!output || (description.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0);
}
}
#endif

TemporalResult XeSSUpscalerDX12::Dispatch(ID3D12GraphicsCommandList* commands, const TemporalFrame& frame,
    const XeSSUpscaleDX12Bindings& bindings)
{
#if defined(_WIN32) && CREATOR_ENABLE_XESS_SDK
    if (!m_state) return { TemporalStatus::NotInitialized };
    auto& core = m_state->m_core;
    auto result = core.ValidateFrame(frame, bindings.exposureScale);
    if (!result.IsSuccess()) return result;
    const auto motionExtent = frame.motionVectorsAtDisplayResolution ? frame.displayExtent : frame.renderExtent;
    if (!commands || !ValidDX12Texture(bindings.color, frame.renderExtent) ||
        !ValidDX12Texture(bindings.motionVectors, motionExtent) ||
        !ValidDX12Texture(bindings.output, frame.displayExtent, true) ||
        (!frame.motionVectorsAtDisplayResolution && !ValidDX12Texture(bindings.depth, frame.renderExtent)) ||
        (core.m_config.exposureTexture && !ValidDX12Texture(bindings.exposure, { 1, 1 })) ||
        (core.m_config.responsiveMask && !ValidDX12Texture(bindings.responsiveMask, frame.renderExtent)))
        return { TemporalStatus::InvalidInput };
    if (commands->GetType() != D3D12_COMMAND_LIST_TYPE_DIRECT && commands->GetType() != D3D12_COMMAND_LIST_TYPE_COMPUTE)
        return { TemporalStatus::InvalidInput };
    if (bindings.output.resource == bindings.color.resource ||
        bindings.output.resource == bindings.depth.resource ||
        bindings.output.resource == bindings.motionVectors.resource ||
        bindings.output.resource == bindings.exposure.resource ||
        bindings.output.resource == bindings.responsiveMask.resource)
        return { TemporalStatus::InvalidInput };
    // SDK performs role-specific typed-format validation. Preserve its exact
    // InvalidInput/native result rather than inventing a smaller format whitelist.
    result = core.SetFrameScales(frame);
    if (!result.IsSuccess()) return result;
    xess_d3d12_execute_params_t params{};
    params.pColorTexture = bindings.color.resource;
    params.pVelocityTexture = bindings.motionVectors.resource;
    params.pDepthTexture = frame.motionVectorsAtDisplayResolution ? nullptr : bindings.depth.resource;
    params.pExposureScaleTexture = core.m_config.exposureTexture ? bindings.exposure.resource : nullptr;
    params.pResponsivePixelMaskTexture = core.m_config.responsiveMask ? bindings.responsiveMask.resource : nullptr;
    params.pOutputTexture = bindings.output.resource;
    params.jitterOffsetX = frame.jitterX;
    params.jitterOffsetY = frame.jitterY;
    params.exposureScale = bindings.exposureScale;
    params.resetHistory = core.ShouldReset(frame) ? 1u : 0u;
    params.inputWidth = frame.renderExtent.width;
    params.inputHeight = frame.renderExtent.height;
    params.inputColorBase = { bindings.color.baseX, bindings.color.baseY };
    params.inputMotionVectorBase = { bindings.motionVectors.baseX, bindings.motionVectors.baseY };
    params.inputDepthBase = { bindings.depth.baseX, bindings.depth.baseY };
    params.inputResponsiveMaskBase = { bindings.responsiveMask.baseX, bindings.responsiveMask.baseY };
    params.outputColorBase = { bindings.output.baseX, bindings.output.baseY };
    result = XeSSResult(m_state->m_execute(core.m_context, commands, &params));
    if (result.IsSuccess()) core.CommitFrame(frame);
    else core.m_forceReset = true; // SDK may have partially recorded work or touched its history.
    return result;
#else
    return { TemporalStatus::SdkNotBuilt };
#endif
}

TemporalResult XeSSUpscalerDX12::Shutdown()
{
#if defined(_WIN32) && CREATOR_ENABLE_XESS_SDK
    if (m_state)
    {
        const auto result = m_state->m_core.Destroy();
        if (!result.IsSuccess()) return result;
    }
#endif
    m_state.reset();
    return { TemporalStatus::Success };
}

#if defined(_WIN32) && CREATOR_ENABLE_XESS_SDK
namespace
{
class XeSSUpscalerBridgeDX12 final : public ITemporalUpscaler
{
public:
    XeSSUpscalerBridgeDX12(DX12DeviceResources& resources, const XeSSUpscalerConfig& config,
        std::function<TemporalResult()> drain)
        : m_resources(resources), m_config(config), m_drain(std::move(drain)),
          m_adapter(std::make_unique<XeSSUpscalerDX12>()) {}
    ~XeSSUpscalerBridgeDX12() override
    {
        if (!Shutdown().IsSuccess()) (void)m_adapter.release();
    }
    XeSSUpscalerDX12& GetAdapter() { return *m_adapter; }
    TemporalResult QueryRenderExtent(TemporalQuality, TemporalExtent, TemporalExtent& extent) const override
    {
        if (!m_adapter) return { TemporalStatus::NotInitialized };
        XeSSInputResolution resolution;
        const auto result = m_adapter->QueryInputResolution(resolution);
        extent = resolution.optimal;
        return result;
    }
    TemporalCapabilities GetCapabilities() const override
    {
        TemporalCapabilities capabilities;
        capabilities.provider = TemporalProvider::XeSS;
        capabilities.backend = TemporalBackend::DX12;
        capabilities.sdkVersion = kXeSSSdkVersion;
        capabilities.sdkRevision = kXeSSSdkRevision;
        capabilities.upscaling = { m_adapter ? TemporalStatus::Success : TemporalStatus::NotInitialized };
        if (m_adapter) capabilities.upscalerImplementation = m_adapter->GetRuntimeVersion();
        return capabilities;
    }
    TemporalResult Evaluate(const TemporalUpscaleInputs& inputs, RHIEncoder& encoder) override
    {
        if (!m_adapter) return { TemporalStatus::NotInitialized };
        const auto valid = ValidateTemporalUpscaleInputs(inputs);
        if (!valid.IsSuccess()) return valid;
        if (inputs.reactiveMask.IsValid() || inputs.transparencyMask.IsValid())
            return { TemporalStatus::FeatureUnsupported };
        if (inputs.responsiveMask.IsValid() != m_config.responsiveMask ||
            inputs.exposure.IsValid() != m_config.exposureTexture)
            return { TemporalStatus::InvalidInput };
        auto* native = dynamic_cast<DX12Encoder*>(&encoder);
        if (!native || !native->UsesResources(&m_resources) || !native->GetCommandList())
            return { TemporalStatus::InvalidInput };
        const auto queueType = native->GetCommandList()->GetType();
        if (queueType != D3D12_COMMAND_LIST_TYPE_DIRECT && queueType != D3D12_COMMAND_LIST_TYPE_COMPUTE)
            return { TemporalStatus::InvalidInput };
        const auto texture = [this, queueType](RHITextureHandle handle, bool output = false)
        {
            uint32_t state = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            if (queueType == D3D12_COMMAND_LIST_TYPE_DIRECT) state |= D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            if (output) state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            return XeSSDX12Texture{ m_resources.Resolve(handle), state };
        };
        XeSSUpscaleDX12Bindings bindings;
        bindings.color = texture(inputs.color);
        bindings.depth = texture(inputs.depth);
        bindings.motionVectors = texture(inputs.motionVectors);
        bindings.exposure = texture(inputs.exposure);
        bindings.responsiveMask = texture(inputs.responsiveMask);
        bindings.output = texture(inputs.output, true);
        const auto result = m_adapter->Dispatch(native->GetCommandList(), inputs.frame, bindings);
        // XeSS keeps input/output states, but changes descriptor/root/PSO bindings.
        native->ResetState(native->GetCommandList());
        return result;
    }
    TemporalResult Shutdown() override
    {
        if (!m_adapter) return { TemporalStatus::Success };
        const auto drained = m_drain();
        if (!drained.IsSuccess()) return drained;
        const auto result = m_adapter->Shutdown();
        if (result.IsSuccess()) m_adapter.reset();
        return result;
    }
private:
    DX12DeviceResources& m_resources;
    XeSSUpscalerConfig m_config;
    std::function<TemporalResult()> m_drain;
    std::unique_ptr<XeSSUpscalerDX12> m_adapter;
};
}
#endif

TemporalResult CreateXeSSUpscalerDX12(DX12DeviceResources& resources, const wchar_t* runtimeDirectory,
    const XeSSUpscalerConfig& config, std::function<TemporalResult()> drain,
    std::unique_ptr<ITemporalUpscaler>& upscaler)
{
    if (upscaler) return { TemporalStatus::AlreadyInitialized };
    if (!drain) return { TemporalStatus::InvalidInput };
#if defined(_WIN32) && CREATOR_ENABLE_XESS_SDK
    auto bridge = std::make_unique<XeSSUpscalerBridgeDX12>(resources, config, std::move(drain));
    const auto result = bridge->GetAdapter().Initialize(resources.GetDevice(), runtimeDirectory, config);
    if (!result.IsSuccess()) return result;
    upscaler = std::move(bridge);
    return result;
#else
    return { TemporalStatus::SdkNotBuilt };
#endif
}

#if CREATOR_ENABLE_XESS_VULKAN_SDK
struct XeSSUpscalerVulkanState
{
#if defined(_WIN32)
    UpscalerCore m_core;
    decltype(&xessVKGetRequiredInstanceExtensions) m_instanceExtensions{ nullptr };
    decltype(&xessVKGetRequiredDeviceExtensions) m_deviceExtensions{ nullptr };
    decltype(&xessVKGetRequiredDeviceFeatures) m_deviceFeatures{ nullptr };
    decltype(&xessVKCreateContext) m_create{ nullptr };
    decltype(&xessVKInit) m_initialize{ nullptr };
    decltype(&xessVKExecute) m_execute{ nullptr };
#endif
};

XeSSUpscalerVulkan::XeSSUpscalerVulkan() = default;
XeSSUpscalerVulkan::~XeSSUpscalerVulkan()
{
    if (!Shutdown().IsSuccess()) (void)m_state.release();
}

TemporalResult XeSSUpscalerVulkan::Load(const wchar_t* runtimeDirectory)
{
#if defined(_WIN32)
    if (m_state) return { TemporalStatus::AlreadyInitialized };
    auto state = std::make_unique<XeSSUpscalerVulkanState>();
    auto result = state->m_core.Load(runtimeDirectory);
    if (!result.IsSuccess()) return result;
    auto& module = state->m_core.m_module;
    if (!module.Resolve(state->m_instanceExtensions, "xessVKGetRequiredInstanceExtensions") ||
        !module.Resolve(state->m_deviceExtensions, "xessVKGetRequiredDeviceExtensions") ||
        !module.Resolve(state->m_deviceFeatures, "xessVKGetRequiredDeviceFeatures") ||
        !module.Resolve(state->m_create, "xessVKCreateContext") ||
        !module.Resolve(state->m_initialize, "xessVKInit") ||
        !module.Resolve(state->m_execute, "xessVKExecute"))
        return XeSSMissingExport();
    m_state = std::move(state);
    return result;
#else
    return { TemporalStatus::BackendUnsupported };
#endif
}

TemporalResult XeSSUpscalerVulkan::QueryRequiredInstanceExtensions(uint32_t& count,
    const char* const*& names, uint32_t& minimumApiVersion) const
{
    count = 0; names = nullptr; minimumApiVersion = 0;
#if defined(_WIN32)
    if (!m_state) return { TemporalStatus::NotInitialized };
    return XeSSResult(m_state->m_instanceExtensions(&count, &names, &minimumApiVersion));
#else
    return { TemporalStatus::BackendUnsupported };
#endif
}

TemporalResult XeSSUpscalerVulkan::QueryRequiredDeviceExtensions(VkInstance instance, VkPhysicalDevice physicalDevice,
    uint32_t& count, const char* const*& names) const
{
    count = 0; names = nullptr;
#if defined(_WIN32)
    if (!m_state) return { TemporalStatus::NotInitialized };
    if (!instance || !physicalDevice) return { TemporalStatus::InvalidInput };
    return XeSSResult(m_state->m_deviceExtensions(instance, physicalDevice, &count, &names));
#else
    return { TemporalStatus::BackendUnsupported };
#endif
}

TemporalResult XeSSUpscalerVulkan::PatchRequiredDeviceFeatures(VkInstance instance, VkPhysicalDevice physicalDevice,
    void*& features) const
{
#if defined(_WIN32)
    if (!m_state) return { TemporalStatus::NotInitialized };
    if (!instance || !physicalDevice) return { TemporalStatus::InvalidInput };
    return XeSSResult(m_state->m_deviceFeatures(instance, physicalDevice, &features));
#else
    return { TemporalStatus::BackendUnsupported };
#endif
}

TemporalResult XeSSUpscalerVulkan::Initialize(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device,
    const XeSSUpscalerConfig& config)
{
#if defined(_WIN32)
    if (!m_state) return { TemporalStatus::NotInitialized };
    auto& core = m_state->m_core;
    if (core.m_context) return { TemporalStatus::AlreadyInitialized };
    if (!instance || !physicalDevice || !device) return { TemporalStatus::InvalidInput };
    auto result = core.ValidateConfig(config);
    if (!result.IsSuccess()) return result;
    result = XeSSResult(m_state->m_create(instance, physicalDevice, device, &core.m_context));
    if (result.IsSuccess())
    {
        xess_vk_init_params_t params{};
        params.outputResolution = { config.displayExtent.width, config.displayExtent.height };
        params.qualitySetting = core.m_quality;
        params.initFlags = GetFlags(config);
        result = XeSSResult(m_state->m_initialize(core.m_context, &params));
        core.m_initialized = result.IsSuccess();
    }
    if (!result.IsSuccess()) (void)core.Destroy();
    return result;
#else
    return { TemporalStatus::BackendUnsupported };
#endif
}

TemporalResult XeSSUpscalerVulkan::QueryInputResolution(XeSSInputResolution& resolution) const
{
    resolution = {};
#if defined(_WIN32)
    return m_state ? m_state->m_core.QueryResolution(resolution) : TemporalResult{ TemporalStatus::NotInitialized };
#else
    return { TemporalStatus::BackendUnsupported };
#endif
}

std::string XeSSUpscalerVulkan::GetRuntimeVersion() const
{
#if defined(_WIN32)
    if (m_state)
    {
        const auto& version = m_state->m_core.m_runtimeVersion;
        return XeSSComponentVersion("XeSS-SR", version.major, version.minor, version.patch);
    }
#endif
    return {};
}

#if defined(_WIN32)
namespace
{
bool ValidVulkanTexture(const XeSSVulkanTexture& texture, TemporalExtent extent, bool output = false)
{
    return texture.image && texture.view && texture.format != VK_FORMAT_UNDEFINED && extent.IsValid() &&
        texture.subresourceRange.aspectMask != 0 && texture.subresourceRange.levelCount == 1 &&
        texture.subresourceRange.layerCount == 1 &&
        texture.layout == (output ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) &&
        texture.baseX <= texture.extent.width && extent.width <= texture.extent.width - texture.baseX &&
        texture.baseY <= texture.extent.height && extent.height <= texture.extent.height - texture.baseY;
}

xess_vk_image_view_info GetImageInfo(const XeSSVulkanTexture& texture)
{
    return { texture.view, texture.image, texture.subresourceRange, texture.format,
        texture.extent.width, texture.extent.height };
}
}
#endif

TemporalResult XeSSUpscalerVulkan::Dispatch(VkCommandBuffer commands, const TemporalFrame& frame,
    const XeSSUpscaleVulkanBindings& bindings)
{
#if defined(_WIN32)
    if (!m_state) return { TemporalStatus::NotInitialized };
    auto& core = m_state->m_core;
    auto result = core.ValidateFrame(frame, bindings.exposureScale);
    if (!result.IsSuccess()) return result;
    const auto motionExtent = frame.motionVectorsAtDisplayResolution ? frame.displayExtent : frame.renderExtent;
    if (!commands || !ValidVulkanTexture(bindings.color, frame.renderExtent) ||
        !ValidVulkanTexture(bindings.motionVectors, motionExtent) ||
        !ValidVulkanTexture(bindings.output, frame.displayExtent, true) ||
        (!frame.motionVectorsAtDisplayResolution && !ValidVulkanTexture(bindings.depth, frame.renderExtent)) ||
        (core.m_config.exposureTexture && !ValidVulkanTexture(bindings.exposure, { 1, 1 })) ||
        (core.m_config.responsiveMask && !ValidVulkanTexture(bindings.responsiveMask, frame.renderExtent)))
        return { TemporalStatus::InvalidInput };
    if (bindings.output.image == bindings.color.image || bindings.output.image == bindings.depth.image ||
        bindings.output.image == bindings.motionVectors.image || bindings.output.image == bindings.exposure.image ||
        bindings.output.image == bindings.responsiveMask.image)
        return { TemporalStatus::InvalidInput };
    result = core.SetFrameScales(frame);
    if (!result.IsSuccess()) return result;
    xess_vk_execute_params_t params{};
    params.colorTexture = GetImageInfo(bindings.color);
    params.velocityTexture = GetImageInfo(bindings.motionVectors);
    if (!frame.motionVectorsAtDisplayResolution) params.depthTexture = GetImageInfo(bindings.depth);
    if (core.m_config.exposureTexture) params.exposureScaleTexture = GetImageInfo(bindings.exposure);
    if (core.m_config.responsiveMask) params.responsivePixelMaskTexture = GetImageInfo(bindings.responsiveMask);
    params.outputTexture = GetImageInfo(bindings.output);
    params.jitterOffsetX = frame.jitterX;
    params.jitterOffsetY = frame.jitterY;
    params.exposureScale = bindings.exposureScale;
    params.resetHistory = core.ShouldReset(frame) ? 1u : 0u;
    params.inputWidth = frame.renderExtent.width;
    params.inputHeight = frame.renderExtent.height;
    params.inputColorBase = { bindings.color.baseX, bindings.color.baseY };
    params.inputMotionVectorBase = { bindings.motionVectors.baseX, bindings.motionVectors.baseY };
    params.inputDepthBase = { bindings.depth.baseX, bindings.depth.baseY };
    params.inputResponsiveMaskBase = { bindings.responsiveMask.baseX, bindings.responsiveMask.baseY };
    params.outputColorBase = { bindings.output.baseX, bindings.output.baseY };
    result = XeSSResult(m_state->m_execute(core.m_context, commands, &params));
    if (result.IsSuccess()) core.CommitFrame(frame);
    else core.m_forceReset = true;
    return result;
#else
    return { TemporalStatus::BackendUnsupported };
#endif
}

TemporalResult XeSSUpscalerVulkan::Shutdown()
{
#if defined(_WIN32)
    if (m_state)
    {
        const auto result = m_state->m_core.Destroy();
        if (!result.IsSuccess()) return result;
    }
#endif
    m_state.reset();
    return { TemporalStatus::Success };
}

#if defined(_WIN32)
namespace
{
class XeSSUpscalerBridgeVulkan final : public ITemporalUpscaler
{
public:
    XeSSUpscalerBridgeVulkan(VulkanDeviceResources& resources, const XeSSUpscalerConfig& config,
        std::function<TemporalResult()> drain, std::unique_ptr<XeSSUpscalerVulkan> adapter)
        : m_resources(resources), m_config(config), m_drain(std::move(drain)), m_adapter(std::move(adapter)) {}
    ~XeSSUpscalerBridgeVulkan() override
    {
        if (!Shutdown().IsSuccess()) (void)m_adapter.release();
    }
    TemporalResult QueryRenderExtent(TemporalQuality, TemporalExtent, TemporalExtent& extent) const override
    {
        if (!m_adapter) return { TemporalStatus::NotInitialized };
        XeSSInputResolution resolution;
        const auto result = m_adapter->QueryInputResolution(resolution);
        extent = resolution.optimal;
        return result;
    }
    TemporalCapabilities GetCapabilities() const override
    {
        TemporalCapabilities capabilities;
        capabilities.provider = TemporalProvider::XeSS;
        capabilities.backend = TemporalBackend::Vulkan;
        capabilities.sdkVersion = kXeSSSdkVersion;
        capabilities.sdkRevision = kXeSSSdkRevision;
        capabilities.upscaling = { m_adapter ? TemporalStatus::Success : TemporalStatus::NotInitialized };
        capabilities.frameGeneration = { TemporalStatus::BackendUnsupported };
        if (m_adapter) capabilities.upscalerImplementation = m_adapter->GetRuntimeVersion();
        return capabilities;
    }
    TemporalResult Evaluate(const TemporalUpscaleInputs& inputs, RHIEncoder& encoder) override
    {
        if (!m_adapter) return { TemporalStatus::NotInitialized };
        const auto valid = ValidateTemporalUpscaleInputs(inputs);
        if (!valid.IsSuccess()) return valid;
        if (inputs.reactiveMask.IsValid() || inputs.transparencyMask.IsValid())
            return { TemporalStatus::FeatureUnsupported };
        if (inputs.responsiveMask.IsValid() != m_config.responsiveMask ||
            inputs.exposure.IsValid() != m_config.exposureTexture)
            return { TemporalStatus::InvalidInput };
        auto& table = m_resources.GetResourceTable();
        auto* native = dynamic_cast<VulkanEncoder*>(&encoder);
        if (!native || !native->UsesResources(&table)) return { TemporalStatus::InvalidInput };
        const auto texture = [&table](RHITextureHandle handle, bool output = false)
        {
            const auto entry = table.Resolve(handle);
            XeSSVulkanTexture binding;
            // Current RHI default views cover the whole resource, so do not
            // pretend an array/3D/mip-chain view is a single 2D SDK input.
            if (!entry.IsValid() || entry.is3D || entry.depthOrArraySize != 1 || entry.mipLevels != 1 ||
                entry.format == RHIFormat::D24UnormS8Uint || entry.format == RHIFormat::D32FloatS8Uint)
                return binding;
            binding.image = entry.image;
            binding.view = entry.view;
            binding.extent = { entry.width, entry.height };
            binding.format = ToVulkan(entry.format);
            const bool depth = entry.format == RHIFormat::D16Unorm || entry.format == RHIFormat::D24UnormS8Uint ||
                entry.format == RHIFormat::D32Float || entry.format == RHIFormat::D32FloatS8Uint;
            binding.subresourceRange = { depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            // Per-command graph state is authoritative, not the resource table's
            // last submitted layout. Evaluate requires ShaderResource/UAV usage.
            binding.layout = output ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            return binding;
        };
        XeSSUpscaleVulkanBindings bindings;
        bindings.color = texture(inputs.color);
        bindings.depth = texture(inputs.depth);
        bindings.motionVectors = texture(inputs.motionVectors);
        bindings.exposure = texture(inputs.exposure);
        bindings.responsiveMask = texture(inputs.responsiveMask);
        bindings.output = texture(inputs.output, true);
        native->EndRenderTargets();
        const auto result = m_adapter->Dispatch(native->GetCommandBuffer(), inputs.frame, bindings);
        native->InvalidateExternalBindings();
        return result;
    }
    TemporalResult Shutdown() override
    {
        if (!m_adapter) return { TemporalStatus::Success };
        const auto drained = m_drain();
        if (!drained.IsSuccess()) return drained;
        const auto result = m_adapter->Shutdown();
        if (result.IsSuccess()) m_adapter.reset();
        return result;
    }
private:
    VulkanDeviceResources& m_resources;
    XeSSUpscalerConfig m_config;
    std::function<TemporalResult()> m_drain;
    std::unique_ptr<XeSSUpscalerVulkan> m_adapter;
};
}
#endif

TemporalResult CreateXeSSUpscalerVulkan(VulkanDeviceResources& resources,
    std::unique_ptr<XeSSUpscalerVulkan>& preparedRuntime, const XeSSUpscalerConfig& config,
    std::function<TemporalResult()> drain, std::unique_ptr<ITemporalUpscaler>& upscaler)
{
    if (upscaler) return { TemporalStatus::AlreadyInitialized };
    if (!drain || !preparedRuntime) return { TemporalStatus::InvalidInput };
#if defined(_WIN32)
    const auto result = preparedRuntime->Initialize(resources.GetInstance(), resources.GetPhysicalDevice(),
        resources.GetDevice(), config);
    if (!result.IsSuccess()) return result;
    upscaler = std::make_unique<XeSSUpscalerBridgeVulkan>(resources, config, std::move(drain), std::move(preparedRuntime));
    return result;
#else
    return { TemporalStatus::BackendUnsupported };
#endif
}
#endif
