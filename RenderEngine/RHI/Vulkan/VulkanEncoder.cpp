#ifndef DYNAMICCPP_EXPORTS
#include "VulkanEncoder.h"
#include "VulkanPipelineCache.h"

using namespace VulkanApi;

namespace
{
    // 유니티 빌드에서 익명 네임스페이스가 파일 간 합쳐지므로 이름을 고유하게 둔다.
    VkPipelineBindPoint VkEncoderBindPoint(VulkanBindPoint point)
    {
        return (VulkanBindPoint::Compute == point)
            ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS;
    }

    VkPrimitiveTopology VkEncoderTopology(VulkanPrimitiveTopology topology)
    {
        switch (topology)
        {
        case VulkanPrimitiveTopology::TriangleStrip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        case VulkanPrimitiveTopology::LineList:      return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        case VulkanPrimitiveTopology::PointList:     return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        default:                                     return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        }
    }
}

void VulkanEncoder::SetViewportAndScissor(uint32_t width, uint32_t height)
{
    if (VK_NULL_HANDLE == m_commandBuffer) return;

    // 높이를 음수로 준다 — 헤더의 ★ 참고. 원점을 아래로 옮겨 뒤집는다.
    VkViewport viewport{};
    viewport.x = 0.f;
    viewport.y = static_cast<float>(height);
    viewport.width = static_cast<float>(width);
    viewport.height = -static_cast<float>(height);
    viewport.minDepth = 0.f;
    viewport.maxDepth = 1.f;
    vkCmdSetViewport(m_commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{ { 0, 0 }, { width, height } };
    vkCmdSetScissor(m_commandBuffer, 0, 1, &scissor);
}

void VulkanEncoder::SetPrimitiveTopology(VulkanPrimitiveTopology topology)
{
    if (VK_NULL_HANDLE == m_commandBuffer) return;
    vkCmdSetPrimitiveTopology(m_commandBuffer, VkEncoderTopology(topology));
}

void VulkanEncoder::SetPipeline(VulkanBindPoint bindPoint, RHIPipelineHandle pipeline)
{
    if (VK_NULL_HANDLE == m_commandBuffer || nullptr == m_pipelines) return;

    // 핸들이 짝을 푼다. 어긋난 조합이 만들어질 자리가 없다.
    const VulkanPipelineEntry entry = m_pipelines->Resolve(pipeline);
    if (!entry.IsValid()) return;

    vkCmdBindPipeline(m_commandBuffer, VkEncoderBindPoint(bindPoint), entry.pipeline);

    // 레이아웃을 기억한다. 파이프라인에 구워져 있어도 vkCmdBindDescriptorSets 가
    // 다시 요구하고, Vulkan 은 파이프라인에게 레이아웃을 되물을 방법을 주지
    // 않는다 — 그래서 '짝'이다(헤더 ★).
    m_boundLayout[static_cast<size_t>(bindPoint)] = entry.layout;
}

void VulkanEncoder::SetConstantBuffer(VulkanBindPoint bindPoint, uint32_t slot,
    VkDescriptorSet set)
{
    if (VK_NULL_HANDLE == m_commandBuffer || VK_NULL_HANDLE == set) return;

    const VkPipelineLayout layout = m_boundLayout[static_cast<size_t>(bindPoint)];
    if (VK_NULL_HANDLE == layout) return;   // 파이프라인을 안 걸고 부른 것이다

    vkCmdBindDescriptorSets(m_commandBuffer, VkEncoderBindPoint(bindPoint), layout,
        slot, 1, &set, 0, nullptr);
}

void VulkanEncoder::Draw(uint32_t vertexCount, uint32_t instanceCount,
    uint32_t firstVertex, uint32_t firstInstance)
{
    if (VK_NULL_HANDLE == m_commandBuffer) return;
    vkCmdDraw(m_commandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
}

void VulkanEncoder::BindRenderTargets(const VulkanRenderTargetBinding& binding)
{
    if (VK_NULL_HANDLE == m_commandBuffer || !binding.IsValid()) return;

    // 이미 열려 있으면 닫고 연다. DX12 는 OMSetRenderTargets 를 그냥 다시
    // 부르면 되는 자리다.
    EndRenderTargets();

    VkRenderingAttachmentInfo colors[VulkanRenderTargetBinding::kMaxColors]{};
    for (uint32_t i = 0; i < binding.colorCount; ++i)
    {
        colors[i].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colors[i].imageView = binding.colorViews[i];
        colors[i].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        // ★ LOAD 다. 중립 계약이 '걸고 나서 지운다'라, 거는 시점에는 지울지
        //   말지를 모른다 — 그래서 공짜인 CLEAR load op 을 쓸 수가 없다.
        colors[i].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        colors[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    }

    VkRenderingAttachmentInfo depth{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    depth.imageView = binding.depthView;
    depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingInfo rendering{ VK_STRUCTURE_TYPE_RENDERING_INFO };
    rendering.renderArea = { { 0, 0 }, { binding.width, binding.height } };
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = binding.colorCount;
    rendering.pColorAttachments = binding.HasColor() ? colors : nullptr;
    rendering.pDepthAttachment = binding.HasDepth() ? &depth : nullptr;

    vkCmdBeginRendering(m_commandBuffer, &rendering);
    m_renderingOpen = true;
}

void VulkanEncoder::ClearRenderTargets(const VulkanRenderTargetBinding& binding,
    const float rgba[4])
{
    if (VK_NULL_HANDLE == m_commandBuffer || !m_renderingOpen || !binding.HasColor()) return;

    VkClearAttachment attachments[VulkanRenderTargetBinding::kMaxColors]{};
    for (uint32_t i = 0; i < binding.colorCount; ++i)
    {
        attachments[i].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        attachments[i].colorAttachment = i;
        attachments[i].clearValue.color = { { rgba[0], rgba[1], rgba[2], rgba[3] } };
    }

    VkClearRect rect{};
    rect.rect = { { 0, 0 }, { binding.width, binding.height } };
    rect.baseArrayLayer = 0;
    rect.layerCount = 1;

    vkCmdClearAttachments(m_commandBuffer, binding.colorCount, attachments, 1, &rect);
}

void VulkanEncoder::ClearDepthTarget(const VulkanRenderTargetBinding& binding, float depth)
{
    if (VK_NULL_HANDLE == m_commandBuffer || !m_renderingOpen || !binding.HasDepth()) return;

    VkClearAttachment attachment{};
    attachment.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    attachment.clearValue.depthStencil = { depth, 0 };

    VkClearRect rect{};
    rect.rect = { { 0, 0 }, { binding.width, binding.height } };
    rect.baseArrayLayer = 0;
    rect.layerCount = 1;

    vkCmdClearAttachments(m_commandBuffer, 1, &attachment, 1, &rect);
}

void VulkanEncoder::EndRenderTargets()
{
    if (VK_NULL_HANDLE == m_commandBuffer || !m_renderingOpen) return;

    vkCmdEndRendering(m_commandBuffer);
    m_renderingOpen = false;
}

#endif
