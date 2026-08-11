#ifndef DYNAMICCPP_EXPORTS
#include "VulkanEncoder.h"
#include "VulkanPipelineCache.h"
#include "VulkanResourceTable.h"

using namespace VulkanApi;

namespace
{
    // 유니티 빌드에서 익명 네임스페이스가 파일 간 합쳐지므로 이름을 고유하게 둔다.
    VkPipelineBindPoint VkEncoderBindPoint(RHIBindPoint point)
    {
        return (RHIBindPoint::Compute == point)
            ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS;
    }

    VkPrimitiveTopology VkEncoderTopology(RHIPrimitiveTopology topology)
    {
        switch (topology)
        {
        case RHIPrimitiveTopology::TriangleStrip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        case RHIPrimitiveTopology::LineList:      return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        case RHIPrimitiveTopology::PointList:     return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        default:                                  return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        }
    }

    /// 인덱스 폭.
    ///
    /// ★ **어휘에 16비트 인덱스가 없다**(`RHIFormat` 에 `R16Uint` 가 없다).
    ///   V4 가 "실사용에서 뽑은 최소 집합"으로 어휘를 정한 결과이고, 이
    ///   엔진의 인덱스는 전부 32비트다(`DX12MeshCache` 가 `R32Uint` 로 굽는다).
    ///
    ///   그래서 지금은 32비트 하나로 답한다. 16비트 소비자가 서면 그때
    ///   `R16Uint` 를 어휘에 더하고 여기 한 줄이 갈린다 — 미리 넣으면 아무도
    ///   안 쓰는 값이 대응표에 남는다(§1.1 의 부류).
    VkIndexType VkEncoderIndexType(RHIFormat)
    {
        return VK_INDEX_TYPE_UINT32;
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

void VulkanEncoder::SetPrimitiveTopology(RHIPrimitiveTopology topology)
{
    if (VK_NULL_HANDLE == m_commandBuffer) return;
    vkCmdSetPrimitiveTopology(m_commandBuffer, VkEncoderTopology(topology));
}

void VulkanEncoder::SetPipeline(RHIBindPoint bindPoint, RHIPipelineHandle pipeline)
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

void VulkanEncoder::SetConstantBuffer(RHIBindPoint bindPoint, uint32_t slot,
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


// ── 실물: 정점·인덱스 (5c-4a 의 표로 푼다) ──

void VulkanEncoder::SetVertexBuffer(const RHIBufferSlice& slice, uint32_t stride)
{
    // ★ 보폭을 여기서 쓰지 않는다. DX12 는 뷰가 보폭을 들지만 Vulkan 은
    //   **파이프라인의 정점 입력 기술**이 든다 — 같은 인자가 두 백엔드에서
    //   다른 시점에 소비되는 자리다. 계약에 남겨 두는 것은 DX12 가 요구하기
    //   때문이고, 여기서 버리는 것이 손실은 아니다(파이프라인이 이미 안다).
    (void)stride;

    if (VK_NULL_HANDLE == m_commandBuffer || nullptr == m_resources) return;

    const VulkanBufferEntry entry = m_resources->Resolve(slice.buffer);
    if (!entry.IsValid()) return;

    const VkDeviceSize offset = slice.offset;
    vkCmdBindVertexBuffers(m_commandBuffer, 0, 1, &entry.buffer, &offset);
}

void VulkanEncoder::SetIndexBuffer(const RHIBufferSlice& slice, RHIFormat format)
{
    if (VK_NULL_HANDLE == m_commandBuffer || nullptr == m_resources) return;

    const VulkanBufferEntry entry = m_resources->Resolve(slice.buffer);
    if (!entry.IsValid()) return;

    vkCmdBindIndexBuffer(m_commandBuffer, entry.buffer, slice.offset,
        VkEncoderIndexType(format));
}

void VulkanEncoder::DrawIndexed(uint32_t indexCount, uint32_t instanceCount,
    uint32_t firstIndex, int32_t baseVertex, uint32_t firstInstance)
{
    if (VK_NULL_HANDLE == m_commandBuffer) return;
    vkCmdDrawIndexed(m_commandBuffer, indexCount, instanceCount, firstIndex,
        baseVertex, firstInstance);
}

void VulkanEncoder::Dispatch(uint32_t x, uint32_t y, uint32_t z)
{
    if (VK_NULL_HANDLE == m_commandBuffer) return;
    vkCmdDispatch(m_commandBuffer, x, y, z);
}

// ── 아직 못 하는 것 ──
//
// 조용히 넘어가지 않고 세어진다(헤더 ★). 각 줄의 괄호가 무엇이 막고 있는지다.

void VulkanEncoder::SetBindings(RHIBindPoint, uint32_t, const RHIBindingTable&)
{
    NoteUnimplemented("SetBindings");            // 디스크립터 풀 (5c-4d)
}

void VulkanEncoder::SetSamplers(RHIBindPoint, uint32_t, const RHISamplerTable&)
{
    NoteUnimplemented("SetSamplers");            // 〃
}

void VulkanEncoder::SetConstantBuffer(RHIBindPoint, uint32_t, const RHIBufferSlice&)
{
    // ★ 여기가 DX12 와 가장 크게 갈리는 자리다. DX12 는 루트에 **주소를**
    //   직접 걸지만 Vulkan 은 버퍼를 디스크립터 셋에 써 넣고 그 셋을 건다 —
    //   즉 슬라이스 하나를 걸려면 셋 할당이 필요하다(5c-4d).
    NoteUnimplemented("SetConstantBuffer");
}

void VulkanEncoder::SetRootBuffer(RHIBindPoint, uint32_t, const RHIBufferSlice&)
{
    NoteUnimplemented("SetRootBuffer");          // 〃
}

void VulkanEncoder::BindRenderTargets(const RHIRenderTargetBinding& binding)
{
    BindRenderTargets(ResolveTargets(binding));
}

void VulkanEncoder::ClearRenderTargets(const RHIRenderTargetBinding& binding, const float rgba[4])
{
    ClearRenderTargets(ResolveTargets(binding), rgba);
}

void VulkanEncoder::ClearDepthTarget(const RHIRenderTargetBinding& binding, float depth)
{
    ClearDepthTarget(ResolveTargets(binding), depth);
}

/// 표가 없으면 무효 묶음이고, 무효 묶음은 아래 셋이 조용히 무시한다.
///
/// ★ 그런데 **조용히 무시하면 안 되는 자리**다 — 표를 안 받은 인코더로
///   렌더 타깃을 걸면 그림이 안 나오는데 아무도 말해 주지 않는다. 그래서
///   표가 없는 경우만 계수로 남긴다(슬롯이 범위 밖인 것은 호출부의 실수가
///   아니라 프레임이 넘어간 것이므로 세지 않는다).
VulkanRenderTargetBinding VulkanEncoder::ResolveTargets(const RHIRenderTargetBinding& binding)
{
    if (nullptr == m_renderTargets)
    {
        NoteUnimplemented("BindRenderTargets(표 없음)");
        return VulkanRenderTargetBinding{};
    }
    if (!binding.IsValid()) return VulkanRenderTargetBinding{};
    return m_renderTargets->Resolve(binding.backend);
}

void VulkanEncoder::ClearRenderTargetRect(const RHIRenderTargetBinding&,
    const float[4], const RHIRect&)
{
    NoteUnimplemented("ClearRenderTargetRect");  // 〃
}

void VulkanEncoder::UavBarrier(std::span<const RHITextureHandle>)
{
    // ★ 소비처가 DX12 에서도 0 이다(A-6). 어휘는 살아 있으므로 계약에 남고,
    //   실물은 G-2b 가 배리어 모델을 정할 때 함께 선다.
    NoteUnimplemented("UavBarrier");
}

void VulkanEncoder::CopyResource(RHITextureHandle, RHITextureHandle)
{
    NoteUnimplemented("CopyResource");           // 슬라이스 7
}

void VulkanEncoder::CopyTexture(RHITextureHandle, RHITextureHandle, uint32_t, uint32_t)
{
    NoteUnimplemented("CopyTexture");            // 〃
}

void VulkanEncoder::ClearUnorderedAccess(const RHIBindingDesc&, const float[4])
{
    NoteUnimplemented("ClearUnorderedAccess");   // 〃
}

void VulkanEncoder::CopyToReadback(const RHIReadback&, RHITextureHandle, uint32_t, uint32_t)
{
    NoteUnimplemented("CopyToReadback");         // vk.* 자가 검증이 설 때
}

void VulkanEncoder::CopyVolumeToReadback(const RHIReadback&, RHITextureHandle, uint32_t)
{
    NoteUnimplemented("CopyVolumeToReadback");   // 〃
}

void VulkanEncoder::CopyPartialToReadback(const RHIReadback&, RHITextureHandle, uint32_t, uint32_t)
{
    NoteUnimplemented("CopyPartialToReadback");  // 〃
}

void VulkanEncoder::CopyBufferToReadback(const RHIReadback&, RHIBufferHandle, uint64_t, uint64_t)
{
    NoteUnimplemented("CopyBufferToReadback");   // 〃
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

    // ★ `RHIDepthTargetDesc::readOnly` 가 여기로 온다 (5c-4c). DX12 는 DSV 에
    //   `READ_ONLY_DEPTH` 플래그를 굽는데 Vulkan 은 **렌더링 시작에 레이아웃**
    //   이다 — 같은 뜻이 뷰의 성질이냐 커맨드의 인자냐로 갈린다.
    //
    //   ★ `DEPTH_STENCIL_` 쪽을 쓴다. 깊이 전용 이미지에도 유효한 레이아웃이고,
    //     무엇보다 `VulkanResourceState.h` 의 `DepthWrite`/`DepthRead` 대응이
    //     그 값을 준다 — **전이가 옮겨 놓은 레이아웃과 렌더링이 선언하는
    //     레이아웃이 같아야 한다.** 어휘를 두 벌 쓰면 검증 레이어가 잡고,
    //     그 대조가 여기서 실제로 값을 했다(5c-4c 자가 검증).
    depth.imageLayout = binding.depthReadOnly
        ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
        : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
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
