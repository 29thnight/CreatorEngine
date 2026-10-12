#pragma once

#include "XeSSSdkPrivate.h"
#include "../../Render/Temporal/ITemporalUpscaler.h"

#include <functional>
#include <memory>

struct ID3D12Device;
struct ID3D12Resource;
struct ID3D12GraphicsCommandList;
class DX12DeviceResources;
class VulkanDeviceResources;

// Native bindings belong to the RHI adapter, never the neutral renderer contract.
// The caller transitions resources before Dispatch and retains them through GPU completion.
struct XeSSDX12Texture
{
    ID3D12Resource* resource{ nullptr };
    uint32_t state{ UINT32_MAX };
    uint32_t baseX{ 0 };
    uint32_t baseY{ 0 };
};

struct XeSSUpscalerConfig
{
    TemporalExtent displayExtent;
    TemporalQuality quality{ TemporalQuality::Quality };
    bool depthInverted{ false };
    bool highDynamicRange{ true };
    bool motionVectorsAtDisplayResolution{ false };
    bool exposureTexture{ false };
    bool responsiveMask{ false };
    bool autoExposure{ false };
};

struct XeSSInputResolution
{
    TemporalExtent optimal;
    TemporalExtent minimum;
    TemporalExtent maximum;
};

struct XeSSUpscaleDX12Bindings
{
    XeSSDX12Texture color;
    XeSSDX12Texture depth;
    XeSSDX12Texture motionVectors;
    XeSSDX12Texture exposure;
    XeSSDX12Texture responsiveMask;
    XeSSDX12Texture output;
    float exposureScale{ 1.0f };
};

struct XeSSUpscalerDX12State;

// Serial use only. The device and every dispatched GPU command must outlive this
// context. Explicitly Shutdown after GPU retirement, before device destruction.
class XeSSUpscalerDX12 final
{
public:
    XeSSUpscalerDX12();
    ~XeSSUpscalerDX12();
    XeSSUpscalerDX12(const XeSSUpscalerDX12&) = delete;
    XeSSUpscalerDX12& operator=(const XeSSUpscalerDX12&) = delete;

    static TemporalResult QuerySupport(ID3D12Device* device, const wchar_t* runtimeDirectory);
    TemporalResult Initialize(ID3D12Device* device, const wchar_t* runtimeDirectory,
        const XeSSUpscalerConfig& config);
    TemporalResult QueryInputResolution(XeSSInputResolution& resolution) const;
    std::string GetRuntimeVersion() const;
    TemporalResult Dispatch(ID3D12GraphicsCommandList* commands, const TemporalFrame& frame,
        const XeSSUpscaleDX12Bindings& bindings);
    TemporalResult Shutdown();

private:
    std::unique_ptr<XeSSUpscalerDX12State> m_state;
};

TemporalResult CreateXeSSUpscalerDX12(DX12DeviceResources& resources, const wchar_t* runtimeDirectory,
    const XeSSUpscalerConfig& config, std::function<TemporalResult()> drain,
    std::unique_ptr<ITemporalUpscaler>& upscaler);

#if CREATOR_ENABLE_XESS_VULKAN_SDK
#include "../Vulkan/VulkanLoader.h"

struct XeSSVulkanTexture
{
    VkImage image{ VK_NULL_HANDLE };
    VkImageView view{ VK_NULL_HANDLE };
    VkFormat format{ VK_FORMAT_UNDEFINED };
    VkImageSubresourceRange subresourceRange{};
    VkImageLayout layout{ VK_IMAGE_LAYOUT_UNDEFINED };
    TemporalExtent extent;
    uint32_t baseX{ 0 };
    uint32_t baseY{ 0 };
};

struct XeSSUpscaleVulkanBindings
{
    XeSSVulkanTexture color;
    XeSSVulkanTexture depth;
    XeSSVulkanTexture motionVectors;
    XeSSVulkanTexture exposure;
    XeSSVulkanTexture responsiveMask;
    XeSSVulkanTexture output;
    float exposureScale{ 1.0f };
};

struct XeSSUpscalerVulkanState;

class XeSSUpscalerVulkan final
{
public:
    XeSSUpscalerVulkan();
    ~XeSSUpscalerVulkan();
    XeSSUpscalerVulkan(const XeSSUpscalerVulkan&) = delete;
    XeSSUpscalerVulkan& operator=(const XeSSUpscalerVulkan&) = delete;

    // Load first, BEFORE vkCreateInstance/vkCreateDevice. Returned extension arrays
    // and feature-chain nodes are SDK-owned; retain this object through device creation.
    TemporalResult Load(const wchar_t* runtimeDirectory);
    TemporalResult QueryRequiredInstanceExtensions(uint32_t& count, const char* const*& names,
        uint32_t& minimumApiVersion) const;
    TemporalResult QueryRequiredDeviceExtensions(VkInstance instance, VkPhysicalDevice physicalDevice,
        uint32_t& count, const char* const*& names) const;
    TemporalResult PatchRequiredDeviceFeatures(VkInstance instance, VkPhysicalDevice physicalDevice,
        void*& features) const;
    // Use VkPhysicalDeviceFeatures2 (not pEnabledFeatures) and merge without duplicate sTypes.
    // The caller must enable all requirements above; context creation is the runtime probe.
    TemporalResult Initialize(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device,
        const XeSSUpscalerConfig& config);
    TemporalResult QueryInputResolution(XeSSInputResolution& resolution) const;
    std::string GetRuntimeVersion() const;
    TemporalResult Dispatch(VkCommandBuffer commands, const TemporalFrame& frame,
        const XeSSUpscaleVulkanBindings& bindings);
    TemporalResult Shutdown(); // After every submitted SDK command has retired.

private:
    std::unique_ptr<XeSSUpscalerVulkanState> m_state;
};

// preparedRuntime has already supplied requirements before device creation.
// On success ownership moves into the neutral adapter; failure retains it.
TemporalResult CreateXeSSUpscalerVulkan(VulkanDeviceResources& resources,
    std::unique_ptr<XeSSUpscalerVulkan>& preparedRuntime, const XeSSUpscalerConfig& config,
    std::function<TemporalResult()> drain, std::unique_ptr<ITemporalUpscaler>& upscaler);
#endif
