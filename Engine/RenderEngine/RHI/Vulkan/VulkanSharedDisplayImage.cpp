#include "VulkanSharedDisplayImage.h"
#include "VulkanDeviceResources.h"
#include "VulkanResourceState.h"

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

using namespace VulkanApi;

struct VulkanSharedDisplayImage::Impl
{
    VulkanDeviceResources* resources{nullptr};
    RHITextureHandle texture;
    VulkanImageEntry native;
    RHIDisplayAdapterIdentity adapter;
    Microsoft::WRL::ComPtr<ID3D12Device> sharedDevice;
    Microsoft::WRL::ComPtr<ID3D12Resource> sharedResource;
    HANDLE sharedHandle{nullptr};
    own::shared_owner<RHIDisplayConsumerLease> consumerLease{own::make_shared<RHIDisplayConsumerLease>()};

    ~Impl()
    {
        if (resources)
        {
            if (texture.IsValid())
            {
                resources->ReleaseTexture(texture);
            }
            else
            {
                const VkDevice device = resources->GetDevice();
                if (native.view)
                {
                    vkDestroyImageView(device, native.view, nullptr);
                }
                if (native.image)
                {
                    vkDestroyImage(device, native.image, nullptr);
                }
                if (native.memory)
                {
                    vkFreeMemory(device, native.memory, nullptr);
                }
            }
        }
        if (sharedHandle)
        {
            CloseHandle(sharedHandle);
        }
    }
};

VulkanSharedDisplayImage::VulkanSharedDisplayImage() = default;
VulkanSharedDisplayImage::~VulkanSharedDisplayImage() = default;

bool VulkanSharedDisplayImage::Initialize(VulkanDeviceResources& resources, uint32_t width, uint32_t height,
                                          const RHIGpuPresentationCapabilities& consumer, std::string& outError)
{
    outError.clear();
    if (m_impl || !width || !height || !resources.IsInitialized() || !consumer.sharedD3D12Resource ||
        !consumer.adapter.valid || !resources.SupportsWin32ExternalMemory() || !vkGetMemoryWin32HandlePropertiesKHR)
    {
        outError = "Vulkan shared display requires compatible Win32 external-memory and consumer capabilities.";
        return false;
    }
    VkPhysicalDeviceIDProperties identity{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
    VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    properties.pNext = &identity;
    vkGetPhysicalDeviceProperties2(resources.GetPhysicalDevice(), &properties);
    static_assert(VK_LUID_SIZE == sizeof(consumer.adapter.luid));
    if (!identity.deviceLUIDValid || identity.deviceNodeMask != consumer.adapter.nodeMask ||
        std::memcmp(identity.deviceLUID, consumer.adapter.luid.data(), VK_LUID_SIZE))
    {
        outError = "Vulkan producer and GPU presentation consumer are on different adapters.";
        return false;
    }

    constexpr VkImageUsageFlags usage =
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    VkPhysicalDeviceExternalImageFormatInfo externalFormat{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO};
    externalFormat.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT;
    VkPhysicalDeviceImageFormatInfo2 format{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2};
    format.pNext = &externalFormat;
    format.format = VK_FORMAT_R8G8B8A8_UNORM;
    format.type = VK_IMAGE_TYPE_2D;
    format.tiling = VK_IMAGE_TILING_OPTIMAL;
    format.usage = usage;
    VkExternalImageFormatProperties externalProperties{VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES};
    VkImageFormatProperties2 imageProperties{VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2};
    imageProperties.pNext = &externalProperties;
    const VkResult supported =
        vkGetPhysicalDeviceImageFormatProperties2(resources.GetPhysicalDevice(), &format, &imageProperties);
    if (supported != VK_SUCCESS ||
        !(externalProperties.externalMemoryProperties.externalMemoryFeatures &
          VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT) ||
        !(externalProperties.externalMemoryProperties.compatibleHandleTypes &
          VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT) ||
        width > imageProperties.imageFormatProperties.maxExtent.width ||
        height > imageProperties.imageFormatProperties.maxExtent.height ||
        !(imageProperties.imageFormatProperties.sampleCounts & VK_SAMPLE_COUNT_1_BIT))
    {
        outError = "The Vulkan adapter cannot import the requested RGBA8 D3D12 display image.";
        return false;
    }

    auto candidate = own::make_unique<Impl>();
    candidate->resources = &resources;
    candidate->adapter = consumer.adapter;
    Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    LUID luid{};
    std::memcpy(&luid, identity.deviceLUID, sizeof(luid));
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) ||
        FAILED(factory->EnumAdapterByLuid(luid, IID_PPV_ARGS(&adapter))) ||
        FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&candidate->sharedDevice))))
    {
        outError = "Could not create the D3D12 transport device on the Vulkan adapter.";
        return false;
    }
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap.CreationNodeMask = identity.deviceNodeMask;
    heap.VisibleNodeMask = identity.deviceNodeMask;
    D3D12_RESOURCE_DESC description{};
    description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    description.Width = width;
    description.Height = height;
    description.DepthOrArraySize = 1;
    description.MipLevels = 1;
    description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    description.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    if (FAILED(candidate->sharedDevice->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_SHARED, &description,
                                                                D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                                IID_PPV_ARGS(&candidate->sharedResource))) ||
        FAILED(candidate->sharedDevice->CreateSharedHandle(candidate->sharedResource.Get(), nullptr, GENERIC_ALL,
                                                           nullptr, &candidate->sharedHandle)))
    {
        outError = "Could not allocate the shared D3D12 display resource.";
        return false;
    }

    VkExternalMemoryImageCreateInfo externalImage{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};
    externalImage.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT;
    VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image.pNext = &externalImage;
    image.imageType = VK_IMAGE_TYPE_2D;
    image.format = format.format;
    image.extent = {width, height, 1};
    image.mipLevels = 1;
    image.arrayLayers = 1;
    image.samples = VK_SAMPLE_COUNT_1_BIT;
    image.tiling = format.tiling;
    image.usage = usage;
    image.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    const VkDevice device = resources.GetDevice();
    auto& native = candidate->native;
    VkResult result = vkCreateImage(device, &image, nullptr, &native.image);
    if (result != VK_SUCCESS)
    {
        outError = "Vulkan external display image creation failed: " + ResultToString(result);
        return false;
    }
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device, native.image, &requirements);
    VkMemoryWin32HandlePropertiesKHR handleProperties{VK_STRUCTURE_TYPE_MEMORY_WIN32_HANDLE_PROPERTIES_KHR};
    result = vkGetMemoryWin32HandlePropertiesKHR(device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT,
                                                 candidate->sharedHandle, &handleProperties);
    const uint32_t memoryType =
        result == VK_SUCCESS ? resources.FindMemoryType(requirements.memoryTypeBits & handleProperties.memoryTypeBits,
                                                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
                             : UINT32_MAX;
    if (memoryType == UINT32_MAX)
    {
        outError = "The shared display resource has no compatible Vulkan device-local memory type.";
        return false;
    }
    VkMemoryDedicatedAllocateInfo dedicated{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};
    dedicated.image = native.image;
    VkImportMemoryWin32HandleInfoKHR importMemory{VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR};
    importMemory.pNext = &dedicated;
    importMemory.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT;
    importMemory.handle = candidate->sharedHandle;
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.pNext = &importMemory;
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = memoryType;
    result = vkAllocateMemory(device, &allocation, nullptr, &native.memory);
    if (result == VK_SUCCESS)
    {
        result = vkBindImageMemory(device, native.image, native.memory, 0);
    }
    if (result != VK_SUCCESS)
    {
        outError = "Vulkan D3D12 display memory import failed: " + ResultToString(result);
        return false;
    }
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = native.image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = format.format;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    result = vkCreateImageView(device, &view, nullptr, &native.view);
    if (result != VK_SUCCESS)
    {
        outError = "Vulkan shared display image view creation failed: " + ResultToString(result);
        return false;
    }
    native.width = width;
    native.height = height;
    native.format = RHIFormat::RGBA8Unorm;
    // The only external acquisition permitted by the barrier contract discards
    // the prior contents and copies this complete single-mip image.
    native.externalShared = true;
    native.queueFamily = resources.GetQueueFamily();
    candidate->texture = resources.GetResourceTable().AddImage(native);
    if (!candidate->texture.IsValid())
    {
        outError = "Vulkan shared display image registry is full.";
        return false;
    }
    m_impl = std::move(candidate);
    return true;
}

RHITextureHandle VulkanSharedDisplayImage::GetTexture() const
{
    return m_impl ? m_impl->texture : RHITextureHandle{};
}

own::shared_owner<RHIDisplayConsumerLease> VulkanSharedDisplayImage::GetConsumerLease() const
{
    return m_impl ? m_impl->consumerLease : own::shared_owner<RHIDisplayConsumerLease>{};
}

bool VulkanSharedDisplayImage::CanReuse() const
{
    return m_impl && m_impl->consumerLease.use_count() == 1 &&
           !m_impl->consumerLease->m_completionLost.load(std::memory_order_acquire);
}

RHIGpuDisplayImage VulkanSharedDisplayImage::DescribeCompletedFrame(const RHIDisplayFrameMetadata& metadata,
                                                                    RHICompletionPoint completion,
                                                                    uint64_t observedCompletion,
                                                                    ce::lifetime_token owner) const
{
    if (!m_impl)
    {
        return {};
    }
    RHIGpuDisplayImage image;
    image.transport = RHISharedImageTransport::Win32D3D12Resource;
    image.adapter = m_impl->adapter;
    image.sharedHandle = m_impl->sharedHandle;
    image.width = m_impl->native.width;
    image.height = m_impl->native.height;
    image.format = m_impl->native.format;
    image.metadata = metadata;
    image.producerCompletion = completion;
    image.observedProducerCompletion = observedCompletion;
    image.consumerLease = m_impl->consumerLease;
    image.lifetimeToken = std::move(owner);
    return image.IsReady() ? std::move(image) : RHIGpuDisplayImage{};
}
