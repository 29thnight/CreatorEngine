#ifndef DYNAMICCPP_EXPORTS
#include "DX12Encoder.h"
#include "DX12DeviceResources.h"

#include <vector>

namespace
{
    // 유니티 빌드에서 익명 네임스페이스가 파일 간 합쳐지므로 이름을 고유하게 둔다.
    D3D_PRIMITIVE_TOPOLOGY EncToD3D12Topology(RHIPrimitiveTopology topology)
    {
        switch (topology)
        {
        case RHIPrimitiveTopology::TriangleStrip: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
        case RHIPrimitiveTopology::LineList:      return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
        case RHIPrimitiveTopology::PointList:     return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
        default:                                  return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        }
    }
}

void DX12Encoder::SetViewportAndScissor(uint32_t width, uint32_t height)
{
    if (nullptr == m_commandList) return;

    const D3D12_VIEWPORT viewport{ 0.f, 0.f,
        static_cast<float>(width), static_cast<float>(height), 0.f, 1.f };
    const D3D12_RECT scissor{ 0, 0,
        static_cast<LONG>(width), static_cast<LONG>(height) };

    m_commandList->RSSetViewports(1, &viewport);
    m_commandList->RSSetScissorRects(1, &scissor);
}

void DX12Encoder::SetPipeline(RHIBindPoint bindPoint, RHIPipelineHandle pipeline)
{
    if (nullptr == m_commandList || nullptr == m_resources) return;

    // ★ 핸들이 짝을 푼다(A-1). 예전에는 호출부가 둘을 따로 넘겼고, 그래서
    //   "파이프라인 P 를 레이아웃 L' 로 걸었다"가 표현 가능했다 — 표가 짝을
    //   들면서 그 조합이 만들어질 자리가 없어졌다.
    const DX12PipelineEntry entry = m_resources->Resolve(pipeline);
    if (!entry.IsValid()) return;   // 이미 놓인 핸들이거나 발급된 적이 없다

    // ★ 루트 시그니처를 먼저 건다. 순서가 뒤집히면 드라이버가 이전 레이아웃으로
    //   PSO를 검증하고, 그 어긋남은 드로우 시점에야 드러난다.
    //
    // ★ 다만 같은 것이면 다시 걸지 않는다.
    //
    //   SetPipeline이 루트 시그니처를 반드시 받는 계약이라, 배치마다 PSO를
    //   가는 패스(Decal)는 루프 안에서 같은 시그니처를 되풀이해 건다. D3D12는
    //   루트 시그니처를 거는 것을 "걸려 있던 루트 인자가 무효가 되는" 사건으로
    //   규정하므로, 되풀이하면 루프 앞에서 건 상수 버퍼와 테이블이 살아 있다는
    //   보장이 없어진다.
    //
    //   ★ 다만 이것은 재현된 결함이 아니다. 필터를 끄고 dx12.decal을 돌려
    //     봤더니 그대로 통과했다 — 적어도 이 드라이버는 같은 객체를 다시 걸
    //     때 인자를 유지한다. 그러니 이 필터가 막는 것은 '지금 나는 버그'가
    //     아니라 '보장되지 않는 동작에 기대는 것'이고, 덤으로 드로우마다
    //     드라이버 호출이 하나 준다.
    const size_t slot = (RHIBindPoint::Compute == bindPoint) ? 1u : 0u;
    if (nullptr != entry.signature && entry.signature != m_boundRootSignature[slot])
    {
        if (RHIBindPoint::Compute == bindPoint)
            m_commandList->SetComputeRootSignature(entry.signature);
        else
            m_commandList->SetGraphicsRootSignature(entry.signature);

        m_boundRootSignature[slot] = entry.signature;
    }

    m_commandList->SetPipelineState(entry.pipeline);
}

void DX12Encoder::SetPrimitiveTopology(RHIPrimitiveTopology topology)
{
    if (nullptr == m_commandList) return;
    m_commandList->IASetPrimitiveTopology(EncToD3D12Topology(topology));
}

void DX12Encoder::SetBindings(RHIBindPoint bindPoint, uint32_t slot,
    const RHIBindingTable& table)
{
    if (nullptr == m_commandList || nullptr == m_resources || !table.IsValid() ||
        0 == table.version || !m_resources->IsDescriptorVersionCurrent(table.version))
    {
        return;
    }

    // 힙이 안 걸려 있으면 이 핸들은 다른 힙을 가리킨다. 부르는 쪽이 기억하는
    // 대신 여기서 보장한다.
    EnsureDescriptorHeaps();

    if (RHIBindPoint::Compute == bindPoint)
        m_commandList->SetComputeRootDescriptorTable(slot, DX12ToGpuHandle(table.backend));
    else
        m_commandList->SetGraphicsRootDescriptorTable(slot, DX12ToGpuHandle(table.backend));
}

void DX12Encoder::SetSamplers(RHIBindPoint bindPoint, uint32_t slot,
    const RHISamplerTable& table)
{
    if (nullptr == m_commandList || !table.IsValid()) return;

    EnsureDescriptorHeaps();

    if (RHIBindPoint::Compute == bindPoint)
        m_commandList->SetComputeRootDescriptorTable(slot, DX12ToGpuHandle(table.backend));
    else
        m_commandList->SetGraphicsRootDescriptorTable(slot, DX12ToGpuHandle(table.backend));
}

D3D12_GPU_VIRTUAL_ADDRESS DX12Encoder::ResolveSlice(const RHIBufferSlice& slice) const
{
    if (nullptr == m_resources || !slice.IsValid()) return 0;

    // ★ 표가 등록 시점에 물어 둔 주소를 쓴다 (A-4). 예전에는 여기서
    //   `Resolve(...)->GetGPUVirtualAddress()` 를 불렀는데, 그것이 D3D12
    //   런타임으로 들어가는 COM 호출이라 드로우당 셋(상수·정점·인덱스)이
    //   되면서 `dx12.encoderbench` 실물 경로에 드로우당 +67~81ns 로 나왔다.
    //   주소는 리소스 수명 동안 안 바뀌므로 물어볼 자리는 등록 시점이다.
    const D3D12_GPU_VIRTUAL_ADDRESS base = m_resources->ResolveGpuAddress(slice.buffer);
    if (0 == base) return 0;
    return base + slice.offset;
}

void DX12Encoder::SetConstantBuffer(RHIBindPoint bindPoint, uint32_t slot,
    const RHIBufferSlice& slice)
{
    const D3D12_GPU_VIRTUAL_ADDRESS address = ResolveSlice(slice);
    if (nullptr == m_commandList || 0 == address) return;

    if (RHIBindPoint::Compute == bindPoint)
        m_commandList->SetComputeRootConstantBufferView(slot, address);
    else
        m_commandList->SetGraphicsRootConstantBufferView(slot, address);
}

void DX12Encoder::SetRootBuffer(RHIBindPoint bindPoint, uint32_t slot,
    const RHIBufferSlice& slice)
{
    const D3D12_GPU_VIRTUAL_ADDRESS address = ResolveSlice(slice);
    if (nullptr == m_commandList || 0 == address) return;

    if (RHIBindPoint::Compute == bindPoint)
        m_commandList->SetComputeRootShaderResourceView(slot, address);
    else
        m_commandList->SetGraphicsRootShaderResourceView(slot, address);
}

void DX12Encoder::SetVertexBuffer(const RHIBufferSlice& slice, uint32_t stride)
{
    const D3D12_GPU_VIRTUAL_ADDRESS address = ResolveSlice(slice);
    if (nullptr == m_commandList || 0 == address) return;

    D3D12_VERTEX_BUFFER_VIEW view{};
    view.BufferLocation = address;
    view.SizeInBytes = static_cast<UINT>(slice.size);
    view.StrideInBytes = stride;
    m_commandList->IASetVertexBuffers(0, 1, &view);
}

void DX12Encoder::SetIndexBuffer(const RHIBufferSlice& slice, RHIFormat format)
{
    const D3D12_GPU_VIRTUAL_ADDRESS address = ResolveSlice(slice);
    if (nullptr == m_commandList || 0 == address) return;

    D3D12_INDEX_BUFFER_VIEW view{};
    view.BufferLocation = address;
    view.SizeInBytes = static_cast<UINT>(slice.size);
    view.Format = ToDXGI(format);
    m_commandList->IASetIndexBuffer(&view);
}

void DX12Encoder::SetVertexBuffer(const D3D12_VERTEX_BUFFER_VIEW& view)
{
    if (nullptr == m_commandList) return;
    m_commandList->IASetVertexBuffers(0, 1, &view);
}

void DX12Encoder::SetIndexBuffer(const D3D12_INDEX_BUFFER_VIEW& view)
{
    if (nullptr == m_commandList) return;
    m_commandList->IASetIndexBuffer(&view);
}

void DX12Encoder::Draw(uint32_t vertexCount, uint32_t instanceCount,
    uint32_t firstVertex, uint32_t firstInstance)
{
    if (nullptr == m_commandList || 0 == vertexCount || 0 == instanceCount) return;
    m_commandList->DrawInstanced(vertexCount, instanceCount, firstVertex, firstInstance);
}

void DX12Encoder::DrawIndexed(uint32_t indexCount, uint32_t instanceCount,
    uint32_t firstIndex, int32_t baseVertex, uint32_t firstInstance)
{
    if (nullptr == m_commandList || 0 == indexCount || 0 == instanceCount) return;
    m_commandList->DrawIndexedInstanced(indexCount, instanceCount, firstIndex,
        baseVertex, firstInstance);
}

void DX12Encoder::Dispatch(uint32_t x, uint32_t y, uint32_t z)
{
    if (nullptr == m_commandList) return;
    if (0 == x || 0 == y || 0 == z) return;
    m_commandList->Dispatch(x, y, z);
}

// 뷰는 디바이스 서비스의 힙에 있다. 인코더는 그 힙을 뒤지지 않고 서비스에
// 맡긴다 — 인덱스에서 핸들을 얻는 산술이 두 곳에 생기면 어긋난다(R2b가
// RHIRenderTargetBinding에 핸들 대신 인덱스를 담은 이유가 그것이다).
void DX12Encoder::EnsureDescriptorHeaps()
{
    if (m_heapsBound) return;
    if (nullptr == m_commandList || nullptr == m_resources) return;

    m_resources->BindDescriptorHeaps(m_commandList, true);
    m_heapsBound = true;
}

void DX12Encoder::BindRenderTargets(const RHIRenderTargetBinding& binding)
{
    if (nullptr == m_commandList || nullptr == m_resources) return;
    m_resources->BindRenderTargets(m_commandList, binding);
}

void DX12Encoder::ClearRenderTargets(const RHIRenderTargetBinding& binding,
    const float rgba[4])
{
    if (nullptr == m_commandList || nullptr == m_resources) return;
    m_resources->ClearRenderTargets(m_commandList, binding, rgba);
}

void DX12Encoder::ClearDepthTarget(const RHIRenderTargetBinding& binding, float depth)
{
    if (nullptr == m_commandList || nullptr == m_resources) return;
    m_resources->ClearDepthTarget(m_commandList, binding, depth);
}

void DX12Encoder::ResourceBarriers(const RHIBarrierBatch& batch)
{
    if (nullptr == m_commandList || nullptr == m_resources || batch.IsEmpty()) return;

    std::vector<D3D12_RESOURCE_BARRIER> barriers;
    barriers.reserve(batch.GetBarrierCount());

    for (const RHITransition& transition : batch.textureTransitions)
    {
        ID3D12Resource* const resource = m_resources->Resolve(transition.texture);
        if (nullptr == resource) continue;

        const D3D12_RESOURCE_STATES before =
            DX12DeviceResources::ToD3D12(transition.before);
        const D3D12_RESOURCE_STATES after =
            DX12DeviceResources::ToD3D12(transition.after);
        if (before == after) continue;

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = resource;
        barrier.Transition.StateBefore = before;
        barrier.Transition.StateAfter = after;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barriers.push_back(barrier);
    }

    for (const RHIBufferTransition& transition : batch.bufferTransitions)
    {
        ID3D12Resource* const resource = m_resources->Resolve(transition.buffer);
        if (nullptr == resource) continue;

        const D3D12_RESOURCE_STATES before =
            DX12DeviceResources::ToD3D12(transition.before);
        const D3D12_RESOURCE_STATES after =
            DX12DeviceResources::ToD3D12(transition.after);
        if (before == after) continue;

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = resource;
        barrier.Transition.StateBefore = before;
        barrier.Transition.StateAfter = after;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barriers.push_back(barrier);
    }

    for (RHITextureHandle handle : batch.uavTextures)
    {
        ID3D12Resource* resource = m_resources->Resolve(handle);
        if (nullptr == resource) continue;
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        barrier.UAV.pResource = resource;
        barriers.push_back(barrier);
    }

    for (RHIBufferHandle handle : batch.uavBuffers)
    {
        ID3D12Resource* resource = m_resources->Resolve(handle);
        if (nullptr == resource) continue;
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        barrier.UAV.pResource = resource;
        barriers.push_back(barrier);
    }

    if (!barriers.empty())
        m_commandList->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
}

void DX12Encoder::UavBarrier(std::span<const RHITextureHandle> textures)
{
    RHIBarrierBatch batch{};
    batch.uavTextures = textures;
    ResourceBarriers(batch);
}

void DX12Encoder::UavBarrierBuffers(std::span<const RHIBufferHandle> buffers)
{
    RHIBarrierBatch batch{};
    batch.uavBuffers = buffers;
    ResourceBarriers(batch);
}

void DX12Encoder::CopyResource(RHITextureHandle destination, RHITextureHandle source)
{
    if (nullptr == m_commandList || nullptr == m_resources) return;

    ID3D12Resource* dst = m_resources->Resolve(destination);
    ID3D12Resource* src = m_resources->Resolve(source);
    if (nullptr == dst || nullptr == src) return;
    m_commandList->CopyResource(dst, src);
}

void DX12Encoder::ClearUnorderedAccess(const RHIBindingDesc& view, const float rgba[4])
{
    if (nullptr == m_commandList || nullptr == rgba || nullptr == m_resources) return;

    // 짝 맞추기는 디바이스 서비스에 한 벌만 둔다 — 그래프 밖(PrepareFrame)에도
    // 호출부가 있어서 그쪽이 본체다. 여기는 통로다.
    m_resources->ClearUnorderedAccess(m_commandList, view, rgba);
}

// ── 복사 (V2-d) ──
//
// 전부 통로다 — 짝 맞추기(배치 풋프린트·행 간격)는 디바이스 서비스에 한 벌만
// 있고, 그래프 밖(PrepareFrame)에도 호출부가 있어 그쪽이 본체다.

void DX12Encoder::CopyToReadback(const RHIReadback& readback, RHITextureHandle source,
    uint32_t slice, uint32_t sourceSubresource)
{
    if (nullptr == m_commandList || nullptr == m_resources) return;
    m_resources->CopyToReadback(m_commandList, readback,
        m_resources->Resolve(source), slice, sourceSubresource);
}

void DX12Encoder::CopyVolumeToReadback(const RHIReadback& readback, RHITextureHandle source,
    uint32_t sourceSubresource)
{
    if (nullptr == m_commandList || nullptr == m_resources) return;
    m_resources->CopyVolumeToReadback(m_commandList, readback,
        m_resources->Resolve(source), sourceSubresource);
}

void DX12Encoder::CopyPartialToReadback(const RHIReadback& readback, RHITextureHandle source,
    uint32_t slice, uint32_t sourceSubresource)
{
    if (nullptr == m_commandList || nullptr == m_resources) return;
    m_resources->CopyPartialToReadback(m_commandList, readback,
        m_resources->Resolve(source), slice, sourceSubresource);
}

void DX12Encoder::CopyBufferToReadback(const RHIReadback& readback, RHIBufferHandle source,
    uint64_t sourceOffset, uint64_t bytes)
{
    if (nullptr == m_commandList || nullptr == m_resources) return;
    m_resources->CopyBufferToReadback(m_commandList, readback,
        m_resources->Resolve(source), sourceOffset, bytes);
}

void DX12Encoder::CopyTexture(RHITextureHandle destination, RHITextureHandle source,
    uint32_t destinationSubresource, uint32_t sourceSubresource)
{
    if (nullptr == m_commandList || nullptr == m_resources) return;

    ID3D12Resource* const dstResource = m_resources->Resolve(destination);
    ID3D12Resource* const srcResource = m_resources->Resolve(source);
    if (nullptr == dstResource || nullptr == srcResource) return;

    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource = dstResource;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = destinationSubresource;

    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource = srcResource;
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = sourceSubresource;

    m_commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
}

void DX12Encoder::ClearRenderTargetRect(const RHIRenderTargetBinding& binding,
    const float rgba[4], const RHIRect& rect)
{
    if (nullptr == m_commandList || nullptr == m_resources || nullptr == rgba) return;

    const D3D12_RECT native{ rect.left, rect.top, rect.right, rect.bottom };
    m_resources->ClearRenderTargetsRect(m_commandList, binding, rgba, &native);
}

#endif
