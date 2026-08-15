#ifndef DYNAMICCPP_EXPORTS
#include "VulkanEncoder.h"
#include "VulkanPipelineCache.h"
#include "VulkanResourceTable.h"
#include "VulkanResourceState.h"
#include "VulkanFrameAllocators.h"
#include "VulkanBindingTable.h"

#include <algorithm>

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
    const VkDeviceSize size = (0 != slice.size) ? slice.size : VK_WHOLE_SIZE;
    const VkDeviceSize nativeStride = stride;
    // RHI는 stride를 바인딩 시점에 말한다. Vulkan 파이프라인의
    // VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE와 짝지어 같은 계약을 지킨다.
    vkCmdBindVertexBuffers2(m_commandBuffer, 0, 1, &entry.buffer, &offset,
        &size, &nativeStride);
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
            (RHIBindingDesc::Dim::Buffer == desc.dim)
            ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
            : (RHIBindingDesc::Kind::ShaderResource == desc.kind)
                ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
                : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        if (expected != target.type)
        {
            NoteUnimplemented("SetBindings(디스크립터 종류 불일치)");
            return;
        }

        if (RHIBindingDesc::Dim::Buffer == desc.dim)
        {
            const VulkanBufferEntry entry = m_resources->Resolve(desc.bufferResource);
            if (!entry.IsValid() && desc.allowNull)
            {
                pending.value = PendingBinding::Value::Buffer;
                pending.buffer.buffer = VK_NULL_HANDLE;
                pending.buffer.offset = 0;
                pending.buffer.range = VK_WHOLE_SIZE;
                resolved.push_back(pending);
                continue;
            }
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
            if (!entry.IsValid() && desc.allowNull)
            {
                pending.value = PendingBinding::Value::Image;
                pending.image.imageView = VK_NULL_HANDLE;
                pending.image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                resolved.push_back(pending);
                continue;
            }
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
                // DX12는 D32 자원을 R32_FLOAT SRV로 보는 어휘를 쓴다. 공용
                // RHIBindingDesc도 그 포맷을 그대로 전달하지만 Vulkan에서는
                // depth image의 기존 D32 view로 샘플하는 것이 같은 뜻이다.
                // R32 color view를 새로 만들려 하면 mutable-format 이미지가
                // 필요하고, 무엇보다 depth aspect를 잃는다.
                const bool depthSrvAlias = RHIFormat::D32Float == entry.format &&
                    RHIFormat::R32Float == format &&
                    RHIBindingDesc::Kind::ShaderResource == desc.kind;
                const bool wholeMip = 0 == desc.mostDetailedMip &&
                    desc.mipLevels == entry.mipLevels;
                const bool wholeSlice =
                    (RHIBindingDesc::Dim::Texture2D == desc.dim) ||
                    (RHIBindingDesc::Dim::Texture3D == desc.dim) ||
                    (0 == desc.firstSlice && desc.sliceCount == entry.depthOrArraySize);
                if ((!depthSrvAlias && format != entry.format) || !wholeMip || !wholeSlice)
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

            pending.value = PendingBinding::Value::Image;
            pending.image.imageView = view;
            // Descriptor가 요구하는 layout은 record thread가 관측한 전역
            // resource-table 상태가 아니라 이 command buffer에 먼저 기록된
            // barrier가 정한다.
            //
            // 병렬 그래프에서는 각 worker가 서로 다른 command buffer에 barrier를
            // 기록한다. 전역 값을 읽으면 graph 순서가 아닌 worker 실행 순서에
            // 따라 layout이 달라진다. 반대로 SRV/UAV 종류만 보면 깊이를 읽기
            // 전용 DSV와 동시에 샘플하는 DepthReadShaderResource를 구별하지
            // 못한다. 로컬 barrier 결과가 두 경우를 모두 정확히 보존한다.
            const auto recorded = m_recordedImageLayouts.find(desc.resource.id);
            if (recorded != m_recordedImageLayouts.end())
            {
                pending.image.imageLayout = recorded->second;
            }
            else if (VK_DESCRIPTOR_TYPE_STORAGE_IMAGE == target.type)
            {
                pending.image.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            }
            else
            {
                // 전이가 필요 없던 첫 바인딩의 안전한 기본값. 읽기 전용 깊이
                // 동시 바인딩은 위 로컬 전이에 잡힌다.
                pending.image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            }
        }
        resolved.push_back(pending);
    }

    // 하나라도 실패하면 기존 셋을 반쯤 바꾸지 않는다. 전부 검증된 뒤에만
    // 현재 바인딩 상태에 합친다.
    for (const PendingBinding& binding : resolved) UpsertBinding(index, binding);
}

void VulkanEncoder::SetSamplers(RHIBindPoint bindPoint, uint32_t slot,
    const RHISamplerTable& table)
{
    if (VK_NULL_HANDLE == m_commandBuffer || nullptr == m_pipelines ||
        nullptr == m_samplerTables)
    {
        NoteUnimplemented("SetSamplers(요청 표 없음)");
        return;
    }

    const size_t index = static_cast<size_t>(bindPoint);
    if (VK_NULL_HANDLE == m_boundLayout[index] ||
        !m_boundLayoutHandle[index].IsValid())
    {
        NoteUnimplemented("SetSamplers(파이프라인 없음)");
        return;
    }

    const std::vector<VkSampler>* const samplers = m_samplerTables->Resolve(table);
    const VulkanLayoutSlot target =
        m_pipelines->ResolveParam(m_boundLayoutHandle[index], slot);
    if (nullptr == samplers || !target.IsValid() ||
        VK_DESCRIPTOR_TYPE_SAMPLER != target.type ||
        target.count != samplers->size())
    {
        NoteUnimplemented("SetSamplers(레이아웃 종류·개수 불일치)");
        return;
    }

    std::vector<PendingBinding> resolved;
    resolved.reserve(samplers->size());
    for (size_t i = 0; i < samplers->size(); ++i)
    {
        if (VK_NULL_HANDLE == (*samplers)[i])
        {
            NoteUnimplemented("SetSamplers(샘플러 무효)");
            return;
        }

        PendingBinding pending{};
        pending.binding = target.binding + static_cast<uint32_t>(i);
        pending.type = VK_DESCRIPTOR_TYPE_SAMPLER;
        pending.value = PendingBinding::Value::Image;
        pending.image.sampler = (*samplers)[i];
        resolved.push_back(pending);
    }

    for (const PendingBinding& binding : resolved) UpsertBinding(index, binding);
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

void VulkanEncoder::ResourceBarriers(const RHIBarrierBatch& batch)
{
    if (VK_NULL_HANDLE == m_commandBuffer || nullptr == m_resources || batch.IsEmpty())
        return;
    EndRenderTargets();

    std::vector<VkImageMemoryBarrier2> imageBarriers;
    std::vector<VkBufferMemoryBarrier2> bufferBarriers;
    imageBarriers.reserve(batch.textureTransitions.size() + batch.uavTextures.size());
    bufferBarriers.reserve(batch.bufferTransitions.size() + batch.uavBuffers.size());

    for (const RHITransition& transition : batch.textureTransitions)
    {
        const VulkanImageEntry entry = m_resources->Resolve(transition.texture);
        if (!entry.IsValid()) continue;

        const VulkanBarrierState before = ToVulkan(transition.before, true);
        const VulkanBarrierState after = ToVulkan(transition.after, false);
        VkImageMemoryBarrier2 barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
        barrier.srcStageMask = before.stage;
        barrier.srcAccessMask = before.access;
        barrier.dstStageMask = after.stage;
        barrier.dstAccessMask = after.access;
        barrier.oldLayout = before.layout;
        barrier.newLayout = after.layout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = entry.image;
        barrier.subresourceRange.aspectMask = AspectOf(entry.format);
        barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
        barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
        imageBarriers.push_back(barrier);
        m_recordedImageLayouts[transition.texture.id] = after.layout;
    }

    for (const RHIBufferTransition& transition : batch.bufferTransitions)
    {
        const VulkanBufferEntry entry = m_resources->Resolve(transition.buffer);
        if (!entry.IsValid()) continue;

        const VulkanBarrierState before = ToVulkan(transition.before, true);
        const VulkanBarrierState after = ToVulkan(transition.after, false);
        VkBufferMemoryBarrier2 barrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
        barrier.srcStageMask = before.stage;
        barrier.srcAccessMask = before.access;
        barrier.dstStageMask = after.stage;
        barrier.dstAccessMask = after.access;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = entry.buffer;
        barrier.offset = 0;
        barrier.size = VK_WHOLE_SIZE;
        bufferBarriers.push_back(barrier);
    }

    for (RHITextureHandle handle : batch.uavTextures)
    {
        const VulkanImageEntry entry = m_resources->Resolve(handle);
        if (!entry.IsValid()) continue;
        VkImageMemoryBarrier2 barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = entry.image;
        barrier.subresourceRange.aspectMask = AspectOf(entry.format);
        barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
        barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
        imageBarriers.push_back(barrier);
    }

    for (RHIBufferHandle handle : batch.uavBuffers)
    {
        const VulkanBufferEntry entry = m_resources->Resolve(handle);
        if (!entry.IsValid()) continue;
        VkBufferMemoryBarrier2 barrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = entry.buffer;
        barrier.offset = 0;
        barrier.size = VK_WHOLE_SIZE;
        bufferBarriers.push_back(barrier);
    }

    if (imageBarriers.empty() && bufferBarriers.empty()) return;
    VkDependencyInfo dependency{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    dependency.imageMemoryBarrierCount = static_cast<uint32_t>(imageBarriers.size());
    dependency.pImageMemoryBarriers = imageBarriers.data();
    dependency.bufferMemoryBarrierCount = static_cast<uint32_t>(bufferBarriers.size());
    dependency.pBufferMemoryBarriers = bufferBarriers.data();
    vkCmdPipelineBarrier2(m_commandBuffer, &dependency);
}

void VulkanEncoder::UavBarrier(std::span<const RHITextureHandle> textures)
{
    RHIBarrierBatch batch{};
    batch.uavTextures = textures;
    ResourceBarriers(batch);
}

void VulkanEncoder::UavBarrierBuffers(std::span<const RHIBufferHandle> buffers)
{
    RHIBarrierBatch batch{};
    batch.uavBuffers = buffers;
    ResourceBarriers(batch);
}

void VulkanEncoder::CopyResource(RHITextureHandle destination, RHITextureHandle source)
{
    if (VK_NULL_HANDLE == m_commandBuffer || nullptr == m_resources) return;
    const VulkanImageEntry src = m_resources->Resolve(source);
    const VulkanImageEntry dst = m_resources->Resolve(destination);
    if (!src.IsValid() || !dst.IsValid()) return;

    // D3D12 CopyResource와 같은 계약: 크기·포맷·차원·밉 구조가 같은 두 이미지의
    // 전 subresource를 복사한다. 다른 구조를 부분 복사로 조용히 축소하면 패스가
    // 성공한 척하므로 진단 계수로 올린다.
    if (src.width != dst.width || src.height != dst.height ||
        src.depthOrArraySize != dst.depthOrArraySize ||
        src.mipLevels != dst.mipLevels || src.format != dst.format ||
        src.is3D != dst.is3D)
    {
        NoteUnimplemented("CopyResource(호환되지 않는 이미지)");
        return;
    }

    EndRenderTargets();
    std::vector<VkImageCopy> regions;
    regions.reserve(src.mipLevels);
    for (uint32_t mip = 0; mip < src.mipLevels; ++mip)
    {
        VkImageCopy copy{};
        copy.srcSubresource.aspectMask = AspectOf(src.format);
        copy.srcSubresource.mipLevel = mip;
        copy.srcSubresource.baseArrayLayer = 0;
        copy.srcSubresource.layerCount = src.is3D ? 1u : src.depthOrArraySize;
        copy.dstSubresource = copy.srcSubresource;
        copy.extent.width = (std::max)(1u, src.width >> mip);
        copy.extent.height = (std::max)(1u, src.height >> mip);
        copy.extent.depth = src.is3D
            ? (std::max)(1u, src.depthOrArraySize >> mip) : 1u;
        regions.push_back(copy);
    }
    vkCmdCopyImage(m_commandBuffer, src.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        dst.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        static_cast<uint32_t>(regions.size()), regions.data());
}

void VulkanEncoder::CopyTexture(RHITextureHandle, RHITextureHandle, uint32_t, uint32_t)
{
    NoteUnimplemented("CopyTexture");            // 〃
}

void VulkanEncoder::ClearUnorderedAccess(const RHIBindingDesc& desc, const float values[4])
{
    if (VK_NULL_HANDLE == m_commandBuffer || nullptr == m_resources ||
        nullptr == values || RHIBindingDesc::Dim::Buffer == desc.dim)
        return;

    const VulkanImageEntry image = m_resources->Resolve(desc.resource);
    if (!image.IsValid()) return;

    EndRenderTargets();

    VkClearColorValue clear{};
    for (uint32_t i = 0; i < 4; ++i) clear.float32[i] = values[i];

    VkImageSubresourceRange range{};
    range.aspectMask = AspectOf(image.format);
    range.baseMipLevel = desc.mostDetailedMip;
    range.levelCount = 1;
    range.baseArrayLayer = image.is3D ? 0u : desc.firstSlice;
    range.layerCount = image.is3D ? 1u :
        (0 != desc.sliceCount ? desc.sliceCount : 1u);

    vkCmdClearColorImage(m_commandBuffer, image.image, VK_IMAGE_LAYOUT_GENERAL,
        &clear, 1, &range);
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

    // RHI sourceSubresource는 DX12와 같은 mip-fastest 선형 번호다.
    // 단일 mip 배열인 shadow cascade에서는 곧 array layer 번호가 된다.
    const uint32_t mipLevels = (std::max)(image.mipLevels, 1u);
    const uint32_t sourceMip = sourceSubresource % mipLevels;
    const uint32_t sourceLayer = sourceSubresource / mipLevels;
    if (sourceMip >= image.mipLevels || sourceLayer >= image.depthOrArraySize) return;

    copy.imageSubresource.aspectMask = AspectOf(image.format);
    copy.imageSubresource.mipLevel = sourceMip;
    copy.imageSubresource.baseArrayLayer = sourceLayer;
    copy.imageSubresource.layerCount = 1;

    // 리드백 장은 큰 밉 기준 크기일 수 있다(IBL은 64x64 장에 밉5의 2x2를
    // 왼쪽 위에 넣는다). 복사 extent는 선택한 소스 밉을 넘지 않되, 다음 행과
    // 다음 장의 간격은 RHIReadback 배치를 그대로 유지한다.
    const uint32_t sourceWidth = (std::max)(1u, image.width >> sourceMip);
    const uint32_t sourceHeight = (std::max)(1u, image.height >> sourceMip);
    copy.imageExtent = {
        (std::min)(readback.width, sourceWidth),
        (std::min)(readback.height, sourceHeight), 1 };

    const uint32_t bytesPerPixel = RHIFormatBytes(readback.format);
    if (0 == bytesPerPixel || 0 != (readback.rowPitch % bytesPerPixel)) return;
    copy.bufferRowLength = readback.rowPitch / bytesPerPixel;
    copy.bufferImageHeight = readback.height;

    vkCmdCopyImageToBuffer(m_commandBuffer, image.image,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer.buffer, 1, &copy);
}

void VulkanEncoder::CopyVolumeToReadback(const RHIReadback& readback,
    RHITextureHandle source, uint32_t sourceSubresource)
{
    if (VK_NULL_HANDLE == m_commandBuffer || nullptr == m_resources ||
        !readback.IsValid()) return;

    const VulkanImageEntry image = m_resources->Resolve(source);
    const VulkanBufferEntry buffer = m_resources->Resolve(readback.buffer);
    if (!image.IsValid() || !image.is3D || !buffer.IsValid() ||
        sourceSubresource >= image.mipLevels)
        return;

    const uint32_t bytesPerPixel = RHIFormatBytes(readback.format);
    if (0 == bytesPerPixel || 0 != (readback.rowPitch % bytesPerPixel)) return;

    EndRenderTargets();

    VkBufferImageCopy copy{};
    copy.imageSubresource.aspectMask = AspectOf(image.format);
    copy.imageSubresource.mipLevel = sourceSubresource;
    copy.imageSubresource.baseArrayLayer = 0;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent.width = (std::min)(readback.width,
        (std::max)(1u, image.width >> sourceSubresource));
    copy.imageExtent.height = (std::min)(readback.height,
        (std::max)(1u, image.height >> sourceSubresource));
    copy.imageExtent.depth = (std::min)(readback.sliceCount,
        (std::max)(1u, image.depthOrArraySize >> sourceSubresource));
    copy.bufferRowLength = readback.rowPitch / bytesPerPixel;
    copy.bufferImageHeight = readback.height;

    vkCmdCopyImageToBuffer(m_commandBuffer, image.image,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer.buffer, 1, &copy);
}

void VulkanEncoder::CopyPartialToReadback(const RHIReadback&, RHITextureHandle, uint32_t, uint32_t)
{
    NoteUnimplemented("CopyPartialToReadback");  // 〃
}

void VulkanEncoder::CopyBufferToReadback(const RHIReadback& readback,
    RHIBufferHandle source, uint64_t sourceOffset, uint64_t bytes)
{
    if (VK_NULL_HANDLE == m_commandBuffer || nullptr == m_resources ||
        !readback.IsValid()) return;
    const VulkanBufferEntry src = m_resources->Resolve(source);
    const VulkanBufferEntry dst = m_resources->Resolve(readback.buffer);
    if (!src.IsValid() || !dst.IsValid() || sourceOffset >= src.bytes) return;

    EndRenderTargets();
    const uint64_t available = src.bytes - sourceOffset;
    const uint64_t requested = (0 == bytes) ? available : bytes;
    const uint64_t copyBytes = (std::min)(requested,
        (std::min)(available, dst.bytes));
    if (0 == copyBytes) return;

    const VkBufferCopy copy{ sourceOffset, 0, copyBytes };
    vkCmdCopyBuffer(m_commandBuffer, src.buffer, dst.buffer, 1, &copy);
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
    rendering.layerCount = binding.layerCount;
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
    rect.layerCount = binding.layerCount;

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
    rect.layerCount = binding.layerCount;

    vkCmdClearAttachments(m_commandBuffer, 1, &attachment, 1, &rect);
}

void VulkanEncoder::EndRenderTargets()
{
    if (VK_NULL_HANDLE == m_commandBuffer || !m_renderingOpen) return;

    vkCmdEndRendering(m_commandBuffer);
    m_renderingOpen = false;
}

#endif
