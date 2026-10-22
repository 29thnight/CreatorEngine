#pragma once

#include "../RHIDisplayFrame.h"
#include "../../../Utility_Framework/Ownership.h"

class VulkanDeviceResources;

// A dedicated cross-API image. Vulkan writes the imported allocation and the
// presentation device opens the same D3D12 resource; no readback/upload occurs.
// Destroy only after producer GPU use has completed, while resources is alive.
// Published frame owners keep this object alive through OpenGpuFrame; consumer
// commands separately retain their opened native resource and consumer lease.
class VulkanSharedDisplayImage
{
public:
    VulkanSharedDisplayImage();
    ~VulkanSharedDisplayImage();
    VulkanSharedDisplayImage(const VulkanSharedDisplayImage&) = delete;
    VulkanSharedDisplayImage& operator=(const VulkanSharedDisplayImage&) = delete;

    bool Initialize(VulkanDeviceResources& resources, uint32_t width, uint32_t height,
        const RHIGpuPresentationCapabilities& consumer, std::string& outError);
    RHITextureHandle GetTexture() const;
    own::shared_owner<RHIDisplayConsumerLease> GetConsumerLease() const;
    bool CanReuse() const;
    RHIGpuDisplayImage DescribeCompletedFrame(const RHIDisplayFrameMetadata& metadata,
        RHICompletionPoint completion, uint64_t observedCompletion, ce::lifetime_token owner) const;

private:
    struct Impl;
    own::unique_owner<Impl> m_impl;
};
