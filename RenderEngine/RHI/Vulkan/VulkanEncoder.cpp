#ifndef DYNAMICCPP_EXPORTS
#include "VulkanEncoder.h"
#include "VulkanPipelineCache.h"
#include "VulkanResourceTable.h"
#include "VulkanFrameAllocators.h"
#include "VulkanBindingTable.h"

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
    const size_t index = static_cast<size_t>(bindPoint);
    if (m_boundLayoutHandle[index].id != entry.layoutHandle.id)
    {
        // 서로 다른 셋 레이아웃의 디스크립터 상태를 섞지 않는다. 같은
        // 레이아웃의 다른 PSO라면 Vulkan에서도 셋이 호환되므로 보존한다.
        m_pending[index].clear();
        m_descriptorsDirty[index] = false;
    }
    m_boundLayout[index] = entry.layout;
    m_boundSetLayout[index] = entry.setLayout;
    m_boundLayoutHandle[index] = entry.layoutHandle;
}

void VulkanEncoder::SetConstantBuffer(RHIBindPoint bindPoint, uint32_t slot,
    const RHIBufferSlice& slice)
{
    if (VK_NULL_HANDLE == m_commandBuffer || nullptr == m_resources) return;

    if (nullptr == m_descriptors || VK_NULL_HANDLE == m_device)
    {
        NoteUnimplemented("SetConstantBuffer(디스크립터 풀 없음)");
        return;
    }

    const VulkanBufferEntry entry = m_resources->Resolve(slice.buffer);
    if (!entry.IsValid()) return;

    PendingBinding pending{};
    pending.param = slot;
    pending.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    pending.buffer.buffer = entry.buffer;
    pending.buffer.offset = slice.offset;

    // ★ 크기가 0 이면 버퍼 전체다(`RHIBufferSlice::Whole`). Vulkan 은 0 을
    //   허용하지 않으므로 `VK_WHOLE_SIZE` 로 옮긴다 — 같은 뜻을 다른 값으로
    //   적는 자리이고, 안 옮기면 검증 레이어가 잡는다.
    pending.buffer.range = (0 != slice.size) ? slice.size : VK_WHOLE_SIZE;

    UpsertBinding(static_cast<size_t>(bindPoint), pending);
}

void VulkanEncoder::UpsertBinding(size_t bindPointIndex, const PendingBinding& binding)
{
    std::vector<PendingBinding>& current = m_pending[bindPointIndex];
    for (PendingBinding& item : current)
    {
        const bool sameParam = UINT32_MAX != binding.param && item.param == binding.param;
        const bool sameBinding = UINT32_MAX == binding.param && UINT32_MAX == item.param &&
            item.binding == binding.binding;
        if (sameParam || sameBinding)
        {
            item = binding;
            m_descriptorsDirty[bindPointIndex] = true;
            return;
        }
    }

    current.push_back(binding);
    m_descriptorsDirty[bindPointIndex] = true;
}

void VulkanEncoder::FlushDescriptors(RHIBindPoint bindPoint)
{
    const size_t index = static_cast<size_t>(bindPoint);
    std::vector<PendingBinding>& pending = m_pending[index];
    if (pending.empty() || !m_descriptorsDirty[index]) return;

    const VkPipelineLayout layout = m_boundLayout[index];
    const VkDescriptorSetLayout setLayout = m_boundSetLayout[index];
    if (VK_NULL_HANDLE == layout || VK_NULL_HANDLE == setLayout ||
        nullptr == m_pipelines || nullptr == m_descriptors || VK_NULL_HANDLE == m_device)
    {
        // 파이프라인을 안 걸고 상수를 건 것이다. 조용히 넘어가면 "그렸는데
        // 상수가 안 걸렸다"가 되므로 센다.
        NoteUnimplemented("FlushDescriptors(파이프라인 없음)");
        m_descriptorsDirty[index] = false;
        return;
    }

    const VkDescriptorSet set = m_descriptors->Allocate(m_device, setLayout);
    if (VK_NULL_HANDLE == set)
    {
        NoteUnimplemented("FlushDescriptors(디스크립터 예산 소진)");
        m_descriptorsDirty[index] = false;
        return;
    }

    std::vector<VkWriteDescriptorSet> writes;
    writes.reserve(pending.size());

    for (const PendingBinding& item : pending)
    {
        uint32_t binding = item.binding;
        VkDescriptorType type = item.type;
        if (UINT32_MAX != item.param)
        {
            // 슬롯 번호 → binding 번호. 표가 없으면 그대로 쓰지 않고 버린다.
            const VulkanLayoutSlot target =
                m_pipelines->ResolveParam(m_boundLayoutHandle[index], item.param);
            if (!target.IsValid())
            {
                NoteUnimplemented("FlushDescriptors(슬롯 번호표에 없다)");
                continue;
            }
            if (1 != target.count || item.type != target.type)
            {
                NoteUnimplemented("FlushDescriptors(루트 버퍼 종류 불일치)");
                continue;
            }
            binding = target.binding;
            type = target.type;
        }

        VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        write.dstSet = set;
        write.dstBinding = binding;
        write.descriptorCount = 1;
        write.descriptorType = type;
        if (PendingBinding::Value::Image == item.value)
            write.pImageInfo = &item.image;
        else
            write.pBufferInfo = &item.buffer;
        writes.push_back(write);
    }

    if (!writes.empty())
    {
        vkUpdateDescriptorSets(m_device,
            static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

        // ★ 셋 번호는 언제나 0 이다. `VulkanBindingModel` 이 "셋은 하나로 두고
        //   레지스터 번호만 옮긴다"고 정했기 때문이고, 그래야 계약의 `slot` 이
        //   백엔드마다 다른 뜻이 되지 않는다.
        vkCmdBindDescriptorSets(m_commandBuffer, VkEncoderBindPoint(bindPoint), layout,
            0, 1, &set, 0, nullptr);
    }

    m_descriptorsDirty[index] = false;
}

void VulkanEncoder::Draw(uint32_t vertexCount, uint32_t instanceCount,
    uint32_t firstVertex, uint32_t firstInstance)
{
    if (VK_NULL_HANDLE == m_commandBuffer) return;
    FlushDescriptors(RHIBindPoint::Graphics);
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
    FlushDescriptors(RHIBindPoint::Graphics);
    vkCmdDrawIndexed(m_commandBuffer, indexCount, instanceCount, firstIndex,
        baseVertex, firstInstance);
}

void VulkanEncoder::Dispatch(uint32_t x, uint32_t y, uint32_t z)
{
    if (VK_NULL_HANDLE == m_commandBuffer) return;
    FlushDescriptors(RHIBindPoint::Compute);
    vkCmdDispatch(m_commandBuffer, x, y, z);
}

// ── 아직 못 하는 것 ──
//
// 조용히 넘어가지 않고 세어진다(헤더 ★). 각 줄의 괄호가 무엇이 막고 있는지다.

void VulkanEncoder::SetBindings(RHIBindPoint bindPoint, uint32_t slot,
    const RHIBindingTable& table)
{
    if (VK_NULL_HANDLE == m_commandBuffer || nullptr == m_pipelines ||
        nullptr == m_resources || nullptr == m_bindingTables)
    {
        NoteUnimplemented("SetBindings(요청 표 없음)");
        return;
    }

    const size_t index = static_cast<size_t>(bindPoint);
    if (VK_NULL_HANDLE == m_boundLayout[index] ||
        !m_boundLayoutHandle[index].IsValid())
    {
        NoteUnimplemented("SetBindings(파이프라인 없음)");
        return;
    }

    const std::vector<RHIBindingDesc>* const descs = m_bindingTables->Resolve(table);
    if (nullptr == descs)
    {
        NoteUnimplemented("SetBindings(만료된 요청)");
        return;
    }

    const VulkanLayoutSlot target =
        m_pipelines->ResolveParam(m_boundLayoutHandle[index], slot);
    if (!target.IsValid() || target.count != descs->size())
    {
        NoteUnimplemented("SetBindings(레이아웃 개수 불일치)");
        return;
    }

    std::vector<PendingBinding> resolved;
    resolved.reserve(descs->size());
    for (size_t i = 0; i < descs->size(); ++i)
    {
        const RHIBindingDesc& desc = (*descs)[i];
        PendingBinding pending{};
        pending.binding = target.binding + static_cast<uint32_t>(i);
        pending.type = target.type;

        const VkDescriptorType expected =
            (RHIBindingDesc::Kind::ShaderResource == desc.kind)
            ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
            : (RHIBindingDesc::Dim::Buffer == desc.dim)
                ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
                : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        if (expected != target.type)
        {
            NoteUnimplemented("SetBindings(디스크립터 종류 불일치)");
            return;
        }

        if (RHIBindingDesc::Dim::Buffer == desc.dim)
        {
            const VulkanBufferEntry entry = m_resources->Resolve(desc.bufferResource);
            if (!entry.IsValid() || 0 == desc.structureByteStride ||
                0 == desc.numElements)
            {
                NoteUnimplemented("SetBindings(버퍼 범위 불완전)");
                return;
            }

            pending.value = PendingBinding::Value::Buffer;
            pending.buffer.buffer = entry.buffer;
            pending.buffer.offset = static_cast<VkDeviceSize>(desc.firstElement) *
                desc.structureByteStride;
            pending.buffer.range = static_cast<VkDeviceSize>(desc.numElements) *
                desc.structureByteStride;
        }
        else
        {
            const VulkanImageEntry entry = m_resources->Resolve(desc.resource);
            if (!entry.IsValid()) return;

            VkImageView view = entry.view;
            if (RHIBindingDesc::Dim::TextureCube == desc.dim)
            {
                view = m_resources->GetOrCreateCubeView(m_device, desc.resource,
                    desc.format, desc.mostDetailedMip, desc.mipLevels, desc.firstSlice);
            }
            else if (RHIBindingDesc::Dim::Default != desc.dim)
            {
                // 기본 2D/배열/3D 전체 뷰는 리소스 엔트리의 view와 같다.
                // 부분 밉·부분 배열 뷰는 다음 소비자가 청구할 때 캐시를 넓힌다.
                const RHIFormat format = (RHIFormat::Unknown == desc.format)
                    ? entry.format : desc.format;
                const bool wholeMip = 0 == desc.mostDetailedMip &&
                    desc.mipLevels == entry.mipLevels;
                const bool wholeSlice = (RHIBindingDesc::Dim::Texture2D == desc.dim) ||
                    (0 == desc.firstSlice && desc.sliceCount == entry.depthOrArraySize);
                if (format != entry.format || !wholeMip || !wholeSlice)
                {
                    NoteUnimplemented("SetBindings(부분 이미지 뷰 미지원)");
                    return;
                }
            }

            if (VK_NULL_HANDLE == view)
            {
                NoteUnimplemented("SetBindings(이미지 뷰 생성 실패)");
                return;
            }

            const bool sampledLayout = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE == target.type &&
                (VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL == entry.layout ||
                 VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL == entry.layout ||
                 VK_IMAGE_LAYOUT_GENERAL == entry.layout);
            const bool storageLayout = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE == target.type &&
                VK_IMAGE_LAYOUT_GENERAL == entry.layout;
            if (!sampledLayout && !storageLayout)
            {
                NoteUnimplemented("SetBindings(이미지 레이아웃 불일치)");
                return;
            }

            pending.value = PendingBinding::Value::Image;
            pending.image.imageView = view;
            pending.image.imageLayout = entry.layout;
        }
        resolved.push_back(pending);
    }

    // 하나라도 실패하면 기존 셋을 반쯤 바꾸지 않는다. 전부 검증된 뒤에만
    // 현재 바인딩 상태에 합친다.
    for (const PendingBinding& binding : resolved) UpsertBinding(index, binding);
}

void VulkanEncoder::SetSamplers(RHIBindPoint, uint32_t, const RHISamplerTable&)
{
    NoteUnimplemented("SetSamplers");            // 〃
}

void VulkanEncoder::SetRootBuffer(RHIBindPoint bindPoint, uint32_t slot,
    const RHIBufferSlice& slice)
{
    if (VK_NULL_HANDLE == m_commandBuffer || nullptr == m_resources ||
        nullptr == m_descriptors || VK_NULL_HANDLE == m_device)
    {
        NoteUnimplemented("SetRootBuffer(디스크립터 서비스 없음)");
        return;
    }

    const VulkanBufferEntry entry = m_resources->Resolve(slice.buffer);
    if (!entry.IsValid() || slice.offset >= entry.bytes)
    {
        NoteUnimplemented("SetRootBuffer(버퍼 범위 불완전)");
        return;
    }

    const VkDeviceSize range = (0 != slice.size)
        ? static_cast<VkDeviceSize>(slice.size)
        : entry.bytes - static_cast<VkDeviceSize>(slice.offset);
    if (0 == range || range > entry.bytes - static_cast<VkDeviceSize>(slice.offset))
    {
        NoteUnimplemented("SetRootBuffer(버퍼 범위 초과)");
        return;
    }

    PendingBinding pending{};
    pending.param = slot;
    pending.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    pending.buffer.buffer = entry.buffer;
    pending.buffer.offset = slice.offset;
    pending.buffer.range = range;
    UpsertBinding(static_cast<size_t>(bindPoint), pending);
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

void VulkanEncoder::CopyToReadback(const RHIReadback& readback, RHITextureHandle source,
    uint32_t slice, uint32_t sourceSubresource)
{
    // 실물이 됐다 (5d — vk.grid 가 청구했다). 원본은 COPY_SOURCE 상태여야
    // 한다는 계약 그대로이고, 그래프의 usage 선언이 그 상태를 만들어 준다.
    if (VK_NULL_HANDLE == m_commandBuffer || nullptr == m_resources) return;
    if (!readback.IsValid() || slice >= readback.sliceCount) return;

    const VulkanImageEntry image = m_resources->Resolve(source);
    const VulkanBufferEntry buffer = m_resources->Resolve(readback.buffer);
    if (!image.IsValid() || !buffer.IsValid()) return;

    // ★ 복사는 렌더링 밖이어야 한다. DX12 의 CopyTextureRegion 은 아무 데서나
    //   되지만 vkCmdCopyImageToBuffer 는 동적 렌더링 안에서 금지다.
    EndRenderTargets();

    VkBufferImageCopy copy{};
    copy.bufferOffset = static_cast<VkDeviceSize>(slice) * readback.sliceBytes;

    // 0 = 촘촘히. CreateReadback 이 rowPitch 를 width*bpp 로 적은 것과 짝이다.
    copy.bufferRowLength = 0;
    copy.bufferImageHeight = 0;

    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.mipLevel = sourceSubresource;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent = { readback.width, readback.height, 1 };

    vkCmdCopyImageToBuffer(m_commandBuffer, image.image,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer.buffer, 1, &copy);
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
