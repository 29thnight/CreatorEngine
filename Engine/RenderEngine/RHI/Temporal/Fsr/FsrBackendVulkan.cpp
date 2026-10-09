#include "FsrBackendVulkan.h"
#if CREATOR_ENABLE_FSR_SDK && CREATOR_ENABLE_FSR_VULKAN_SDK
#include "../../Vulkan/VulkanDeviceResources.h"
#include "../../Vulkan/VulkanEncoder.h"
#include "../../Vulkan/VulkanFormat.h"

namespace
{
TemporalResult Invalid() { return { TemporalStatus::InvalidInput, 0 }; }

FfxResource Resolve(VulkanDeviceResources& resources, RHITextureHandle handle, bool output = false)
{
    if (!handle.IsValid()) return {};
    const auto image = resources.GetResourceTable().Resolve(handle);
    if (!image.IsValid() || image.is3D || image.depthOrArraySize != 1) return {};
    const auto format = ToVulkan(image.format);
    // SDK creates its own views. Do not reinterpret a combined stencil image
    // as a single-aspect R32 texture without an explicit supported view mapping.
    if (format == VK_FORMAT_D24_UNORM_S8_UINT || format == VK_FORMAT_D32_SFLOAT_S8_UINT) return {};
    FfxResourceDescription description{};
    description.type = FFX_RESOURCE_TYPE_TEXTURE2D;
    description.format = ffxGetSurfaceFormatVK(format);
    if (description.format == FFX_SURFACE_FORMAT_UNKNOWN) return {};
    description.width = image.width;
    description.height = image.height;
    description.depth = 1;
    description.mipCount = image.mipLevels;
    description.usage = output ? FFX_RESOURCE_USAGE_UAV : FFX_RESOURCE_USAGE_READ_ONLY;
    if (format == VK_FORMAT_D32_SFLOAT || format == VK_FORMAT_D16_UNORM)
        description.usage = FFX_RESOURCE_USAGE_DEPTHTARGET;
    // Inputs are promised ShaderResource and output UAV by the graph boundary.
    // The SDK unregister path returns every external image to its input state.
    return ffxGetResourceVK(reinterpret_cast<void*>(image.image), description, nullptr,
        output ? FFX_RESOURCE_STATE_UNORDERED_ACCESS : FFX_RESOURCE_STATE_COMPUTE_READ);
}

class FsrUpscalerVulkan final : public ITemporalUpscaler
{
public:
    explicit FsrUpscalerVulkan(VulkanDeviceResources& resources)
        : m_resources(resources), m_upscaler(std::make_unique<FsrUpscaler>())
    {
        *m_deviceContext = { resources.GetDevice(), resources.GetPhysicalDevice(), VulkanApi::vkGetDeviceProcAddr };
    }
    ~FsrUpscalerVulkan() override
    {
        if (!Shutdown().IsSuccess())
        {
            m_upscaler.release();
            m_deviceContext.release();
        }
    }
    TemporalResult Initialize(const FsrContextDescription& description, FsrGpuSynchronization synchronization)
    {
        if (!VulkanApi::IsLoaderReady()) return { TemporalStatus::RuntimeUnavailable, 0 };
        return m_upscaler->Initialize(MakeFsrBackendDeviceVulkan(*m_deviceContext), description, synchronization);
    }
    TemporalResult QueryRenderExtent(TemporalQuality quality, TemporalExtent display, TemporalExtent& extent) const override
    { return FsrUpscaler::GetRenderExtent(display, quality, extent); }
    TemporalCapabilities GetCapabilities() const override
    {
        auto result = QueryFsrBuildAvailability(TemporalBackend::Vulkan);
        result.upscaling = { m_upscaler->IsInitialized() ? TemporalStatus::Success : TemporalStatus::NotInitialized, 0 };
        result.upscalerImplementation = "AMD FSR 3.1.4 source / Vulkan";
        return result;
    }
    TemporalResult Evaluate(const TemporalUpscaleInputs& inputs, RHIEncoder& encoder) override
    {
        auto result = ValidateTemporalUpscaleInputs(inputs);
        if (!result.IsSuccess()) return result;
        if (inputs.responsiveMask.IsValid()) return { TemporalStatus::FeatureUnsupported, 0 };
        auto* native = dynamic_cast<VulkanEncoder*>(&encoder);
        if (!native || !native->UsesResources(&m_resources.GetResourceTable()) || !native->GetCommandBuffer())
            return Invalid();
        FsrUpscaleResources resources;
        resources.commandList = ffxGetCommandListVK(native->GetCommandBuffer());
        resources.color = Resolve(m_resources, inputs.color);
        resources.depth = Resolve(m_resources, inputs.depth);
        resources.motionVectors = Resolve(m_resources, inputs.motionVectors);
        resources.exposure = Resolve(m_resources, inputs.exposure);
        resources.reactiveMask = Resolve(m_resources, inputs.reactiveMask);
        resources.transparencyMask = Resolve(m_resources, inputs.transparencyMask);
        resources.output = Resolve(m_resources, inputs.output, true);
        if ((inputs.exposure.IsValid() && !resources.exposure.resource) ||
            (inputs.reactiveMask.IsValid() && !resources.reactiveMask.resource) ||
            (inputs.transparencyMask.IsValid() && !resources.transparencyMask.resource)) return Invalid();
        native->EndRenderTargets();
        result = m_upscaler->Dispatch(inputs.frame, resources);
        native->InvalidateExternalBindings();
        return result;
    }
    TemporalResult Shutdown() override { return m_upscaler->Shutdown(); }
private:
    VulkanDeviceResources& m_resources;
    std::unique_ptr<VkDeviceContext> m_deviceContext{ std::make_unique<VkDeviceContext>() };
    std::unique_ptr<FsrUpscaler> m_upscaler;
};
}

FsrBackendDevice MakeFsrBackendDeviceVulkan(VkDeviceContext& deviceContext)
{
    if (!VulkanApi::IsLoaderReady() || !deviceContext.vkDevice || !deviceContext.vkPhysicalDevice || !deviceContext.vkDeviceProcAddr) return {};
    // In pinned ffx_vk.cpp, ffxGetDeviceVK returns a pointer to a process-global
    // VkDeviceContext. Preserve identical published representation in caller-
    // owned storage instead, so another device query cannot overwrite ours.
    return { TemporalBackend::Vulkan, static_cast<FfxDevice>(&deviceContext),
        ffxGetScratchMemorySizeVK(deviceContext.vkPhysicalDevice, 1), ffxGetInterfaceVK, ffxWaitForPresents };
}

TemporalResult CreateFsrUpscalerVulkan(VulkanDeviceResources& resources, const FsrContextDescription& description,
    FsrGpuSynchronization synchronization, std::unique_ptr<ITemporalUpscaler>& output)
{
    if (output) return { TemporalStatus::AlreadyInitialized, 0 };
    auto candidate = std::make_unique<FsrUpscalerVulkan>(resources);
    const auto result = candidate->Initialize(description, synchronization);
    if (result.IsSuccess()) output = std::move(candidate);
    return result;
}

FsrFrameGeneratorVulkan::~FsrFrameGeneratorVulkan()
{
    if (!Shutdown().IsSuccess())
    {
        // Keep SDK callback owner, resources, proxy and caller lease alive.
        m_generator.release();
        m_deviceContext.release();
        m_lifetime.release();
        m_swapchain = nullptr;
    }
}

TemporalResult FsrFrameGeneratorVulkan::Initialize(VulkanDeviceResources& resources,
    const FsrContextDescription& description, const TemporalFrameGenerationConfig& configuration,
    const FsrPlayerSwapchainDescriptionVulkan& native, FsrGpuSynchronization synchronization)
{
    if (m_swapchain || m_resources) return { TemporalStatus::AlreadyInitialized, 0 };
    auto result = ValidateTemporalFrameGenerationConfig(configuration);
    if (!result.IsSuccess()) return result;
    const auto& queues = native.interpolation;
    if (!VulkanApi::IsLoaderReady()) return { TemporalStatus::RuntimeUnavailable, 0 };
    if (!resources.GetDevice() || queues.device != resources.GetDevice() ||
        queues.physicalDevice != resources.GetPhysicalDevice() || !queues.gameQueue.queue ||
        !queues.presentQueue.queue || !queues.imageAcquireQueue.queue || queues.asyncComputeQueue.queue ||
        queues.presentQueue.queue == queues.gameQueue.queue || native.description.oldSwapchain ||
        native.description.imageExtent.width != configuration.displayExtent.width ||
        native.description.imageExtent.height != configuration.displayExtent.height ||
        native.description.imageArrayLayers != 1 || native.description.sType != VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR)
        return Invalid();
    VkColorSpaceKHR colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    if (configuration.transferFunction == TemporalTransferFunction::PQ)
        colorSpace = VK_COLOR_SPACE_HDR10_ST2084_EXT;
    else if (configuration.transferFunction == TemporalTransferFunction::Linear)
        colorSpace = VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT;
    if (native.description.imageColorSpace != colorSpace) return Invalid();
    uint32_t familyCount = 0;
    VulkanApi::vkGetPhysicalDeviceQueueFamilyProperties(queues.physicalDevice, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    VulkanApi::vkGetPhysicalDeviceQueueFamilyProperties(queues.physicalDevice, &familyCount, families.data());
    if (queues.gameQueue.familyIndex >= familyCount || queues.presentQueue.familyIndex >= familyCount ||
        queues.imageAcquireQueue.familyIndex >= familyCount ||
        (families[queues.gameQueue.familyIndex].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) !=
            (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) return Invalid();
    VkBool32 presentSupported = VK_FALSE;
    const auto support = VulkanApi::vkGetPhysicalDeviceSurfaceSupportKHR(queues.physicalDevice,
        queues.presentQueue.familyIndex, native.description.surface, &presentSupported);
    if (support != VK_SUCCESS) return { TemporalStatus::SdkFailure, support };
    if (!presentSupported) return { TemporalStatus::FeatureUnsupported, 0 };
    *m_deviceContext = { resources.GetDevice(), resources.GetPhysicalDevice(), VulkanApi::vkGetDeviceProcAddr };
    const auto device = MakeFsrBackendDeviceVulkan(*m_deviceContext);
    result = FsrResult(ffxGetSwapchainReplacementFunctionsVK(device.device, &m_functions));
    if (!result.IsSuccess()) return result;
    if (!m_functions.destroySwapchainKHR || !m_functions.queuePresentKHR || !m_functions.acquireNextImageKHR ||
        !m_functions.getSwapchainImagesKHR) return { TemporalStatus::SdkFailure, FFX_ERROR_INCOMPLETE_INTERFACE };
    result = m_generator->Initialize(device, description, configuration,
        ffxGetSurfaceFormatVK(native.description.imageFormat), synchronization);
    if (!result.IsSuccess()) return result;
    result = FsrResult(ffxReplaceSwapchainForFrameinterpolationVK(ffxGetCommandQueueVK(queues.gameQueue.queue),
        m_swapchain, &native.description, &queues));
    m_allocator = queues.pAllocator;
    if (!result.IsSuccess() || !m_swapchain)
    {
        const auto cleanup = Shutdown();
        return !cleanup.IsSuccess() ? cleanup : (result.IsSuccess() ? Invalid() : result);
    }
    m_resources = &resources;
    m_gameQueue = queues.gameQueue.queue;
    m_uiFlags = native.uiPremultipliedAlpha ? FFX_UI_COMPOSITION_FLAG_USE_PREMUL_ALPHA : 0;
    return { TemporalStatus::Success, 0 };
}

TemporalCapabilities FsrFrameGeneratorVulkan::GetCapabilities() const
{
    auto result = QueryFsrBuildAvailability(TemporalBackend::Vulkan);
    result.frameGeneration = { m_generator->IsInitialized() && m_swapchain ? TemporalStatus::Success
        : TemporalStatus::NotInitialized, 0 };
    result.maxInterpolatedFrames = result.frameGeneration.IsSuccess() ? 1 : 0;
    result.frameGeneratorImplementation = "AMD FSR 3.1.4 source / SDK Vulkan replacement swapchain";
    return result;
}

TemporalResult FsrFrameGeneratorVulkan::Prepare(const TemporalFrameGenerationInputs& inputs, RHIEncoder& encoder)
{
    if (!m_resources || !m_swapchain) return { TemporalStatus::NotInitialized, 0 };
    if (m_pending) return { TemporalStatus::IntegrationRequired, 0 };
    const auto validation = ValidateTemporalFrameGenerationInputs(inputs);
    if (!validation.IsSuccess()) return validation;
    auto* native = dynamic_cast<VulkanEncoder*>(&encoder);
    if (!native || !native->UsesResources(&m_resources->GetResourceTable()) || !native->GetCommandBuffer()) return Invalid();
    FsrFrameGenerationResources resources;
    resources.commandList = ffxGetCommandListVK(native->GetCommandBuffer());
    resources.depth = Resolve(*m_resources, inputs.depth);
    resources.motionVectors = Resolve(*m_resources, inputs.motionVectors);
    resources.hudlessColor = Resolve(*m_resources, inputs.hudlessColor);
    const auto ui = Resolve(*m_resources, inputs.uiColor);
    if (inputs.uiColor.IsValid() && (!ui.resource || ui.description.width != inputs.frame.displayExtent.width ||
        ui.description.height != inputs.frame.displayExtent.height || ui.resource == resources.hudlessColor.resource ||
        ui.resource == resources.depth.resource || ui.resource == resources.motionVectors.resource)) return Invalid();
    m_lifetime = std::make_unique<std::shared_ptr<const void>>(inputs.lifetimeToken);
    m_pending = true;
    m_canPresent = false;
    native->EndRenderTargets();
    auto result = m_generator->Prepare(inputs.frame, resources);
    native->InvalidateExternalBindings();
    if (!result.IsSuccess()) return result;
    result = FsrResult(ffxRegisterFrameinterpolationUiResourceVK(m_swapchain, ui, m_uiFlags));
    if (result.IsSuccess()) result = m_generator->Configure(m_swapchain);
    m_canPresent = result.IsSuccess();
    return result;
}

TemporalResult FsrFrameGeneratorVulkan::Present(const VkPresentInfoKHR& info)
{
    if (!m_swapchain || !m_pending || !m_canPresent) return { TemporalStatus::NotInitialized, 0 };
    if (info.sType != VK_STRUCTURE_TYPE_PRESENT_INFO_KHR || info.swapchainCount != 1 || !info.pSwapchains ||
        !info.pImageIndices || info.pSwapchains[0] != GetSwapchain()) return Invalid();
    m_canPresent = false;
    const auto present = m_functions.queuePresentKHR(m_gameQueue, &info);
    const auto complete = m_generator->FinishFrame();
    if (m_generator->HasPendingFrame()) return complete;
    m_lifetime.reset();
    m_pending = m_canPresent = false;
    if (present != VK_SUCCESS) return { TemporalStatus::SdkFailure, present };
    return complete;
}

VkSwapchainKHR FsrFrameGeneratorVulkan::GetSwapchain() const
{
    return m_swapchain ? ffxGetVKSwapchain(m_swapchain) : VK_NULL_HANDLE;
}

TemporalResult FsrFrameGeneratorVulkan::Shutdown()
{
    const auto result = m_generator->Shutdown();
    if (!result.IsSuccess()) return result;
    if (m_swapchain)
        m_functions.destroySwapchainKHR(m_deviceContext->vkDevice, GetSwapchain(), m_allocator);
    m_swapchain = nullptr;
    m_lifetime.reset();
    m_resources = nullptr;
    m_pending = m_canPresent = false;
    return result;
}

TemporalResult CreateFsrFrameGeneratorVulkan(VulkanDeviceResources& resources, const FsrContextDescription& description,
    const TemporalFrameGenerationConfig& configuration, const FsrPlayerSwapchainDescriptionVulkan& native,
    FsrGpuSynchronization synchronization, std::unique_ptr<FsrFrameGeneratorVulkan>& output)
{
    if (output) return { TemporalStatus::AlreadyInitialized, 0 };
    auto candidate = std::make_unique<FsrFrameGeneratorVulkan>();
    const auto result = candidate->Initialize(resources, description, configuration, native, synchronization);
    if (result.IsSuccess()) output = std::move(candidate);
    return result;
}
#endif
