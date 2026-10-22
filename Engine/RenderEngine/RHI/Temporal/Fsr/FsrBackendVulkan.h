#pragma once
#include "../../../../Utility_Framework/LifetimeToken.h"
#include "../../../../Utility_Framework/Ownership.h"
#include "FsrBackend.h"
#if CREATOR_ENABLE_FSR_SDK && CREATOR_ENABLE_FSR_VULKAN_SDK
#include "../../Vulkan/VulkanLoader.h"
#include "../../../Render/Temporal/ITemporalUpscaler.h"
#include "../../../Render/Temporal/ITemporalFrameGenerator.h"
#include <FidelityFX/host/backends/vk/ffx_vk.h>

class VulkanDeviceResources;

// deviceContext storage must outlive all contexts made with this descriptor.
FsrBackendDevice MakeFsrBackendDeviceVulkan(VkDeviceContext& deviceContext);
TemporalResult CreateFsrUpscalerVulkan(VulkanDeviceResources&, const FsrContextDescription&,
    FsrGpuSynchronization, own::unique_owner<ITemporalUpscaler>& output);

struct FsrPlayerSwapchainDescriptionVulkan
{
    VkSwapchainCreateInfoKHR description{};
    // SDK requires a present queue reserved exclusively for its presenter.
    // This serial adapter rejects an async compute queue.
    VkFrameInterpolationInfoFFX interpolation{};
    bool uiPremultipliedAlpha{ false };
};

class FsrFrameGeneratorVulkan final : public ITemporalFrameGenerator
{
public:
    ~FsrFrameGeneratorVulkan() override;
    TemporalResult Initialize(VulkanDeviceResources&, const FsrContextDescription&,
        const TemporalFrameGenerationConfig&, const FsrPlayerSwapchainDescriptionVulkan&, FsrGpuSynchronization);
    TemporalCapabilities GetCapabilities() const override;
    TemporalResult Prepare(const TemporalFrameGenerationInputs&, RHIEncoder&) override;
    // Caller submits prepared commands before Present. Use replacement entry
    // points for image enumeration/acquisition, not native vk* on the proxy.
    TemporalResult Present(const VkPresentInfoKHR& info);
    TemporalResult Shutdown() override;
    VkSwapchainKHR GetSwapchain() const;
    const FfxSwapchainReplacementFunctions& GetFunctions() const { return m_functions; }

private:
    VulkanDeviceResources* m_resources{ nullptr };
    own::unique_owner<VkDeviceContext> m_deviceContext{ own::make_unique<VkDeviceContext>() };
    own::unique_owner<FsrFrameGenerator> m_generator{ own::make_unique<FsrFrameGenerator>() };
    FfxSwapchain m_swapchain{ nullptr };
    FfxSwapchainReplacementFunctions m_functions{};
    VkQueue m_gameQueue{ VK_NULL_HANDLE };
    const VkAllocationCallbacks* m_allocator{ nullptr };
    own::unique_owner<ce::lifetime_token> m_lifetime;
    bool m_pending{ false };
    bool m_canPresent{ false };
    uint32_t m_uiFlags{ 0 };
};

TemporalResult CreateFsrFrameGeneratorVulkan(VulkanDeviceResources&, const FsrContextDescription&,
    const TemporalFrameGenerationConfig&, const FsrPlayerSwapchainDescriptionVulkan&, FsrGpuSynchronization,
    own::unique_owner<FsrFrameGeneratorVulkan>& output);
#endif
