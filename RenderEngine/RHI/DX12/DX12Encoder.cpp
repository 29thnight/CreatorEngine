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

void DX12Encoder::SetPipeline(RHIBindPoint bindPoint, ID3D12PipelineState* pipeline,
    ID3D12RootSignature* rootSignature)
{
    if (nullptr == m_commandList) return;

    // ★ 루트 시그니처를 먼저 건다. 순서가 뒤집히면 드라이버가 이전 레이아웃으로
    //   PSO를 검증하고, 그 어긋남은 드로우 시점에야 드러난다.
    if (nullptr != rootSignature)
    {
        if (RHIBindPoint::Compute == bindPoint)
            m_commandList->SetComputeRootSignature(rootSignature);
        else
            m_commandList->SetGraphicsRootSignature(rootSignature);
    }

    if (nullptr != pipeline) m_commandList->SetPipelineState(pipeline);
}

void DX12Encoder::SetPrimitiveTopology(RHIPrimitiveTopology topology)
{
    if (nullptr == m_commandList) return;
    m_commandList->IASetPrimitiveTopology(EncToD3D12Topology(topology));
}

void DX12Encoder::SetBindings(RHIBindPoint bindPoint, uint32_t slot,
    const RHIBindingTable& table)
{
    if (nullptr == m_commandList || !table.IsValid()) return;

    if (RHIBindPoint::Compute == bindPoint)
        m_commandList->SetComputeRootDescriptorTable(slot, table.gpu);
    else
        m_commandList->SetGraphicsRootDescriptorTable(slot, table.gpu);
}

void DX12Encoder::SetConstantBuffer(RHIBindPoint bindPoint, uint32_t slot,
    D3D12_GPU_VIRTUAL_ADDRESS address)
{
    if (nullptr == m_commandList || 0 == address) return;

    if (RHIBindPoint::Compute == bindPoint)
        m_commandList->SetComputeRootConstantBufferView(slot, address);
    else
        m_commandList->SetGraphicsRootConstantBufferView(slot, address);
}

void DX12Encoder::SetRootBuffer(RHIBindPoint bindPoint, uint32_t slot,
    D3D12_GPU_VIRTUAL_ADDRESS address)
{
    if (nullptr == m_commandList || 0 == address) return;

    if (RHIBindPoint::Compute == bindPoint)
        m_commandList->SetComputeRootShaderResourceView(slot, address);
    else
        m_commandList->SetGraphicsRootShaderResourceView(slot, address);
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

void DX12Encoder::UavBarrier(std::span<ID3D12Resource* const> resources)
{
    if (nullptr == m_commandList || resources.empty()) return;

    std::vector<D3D12_RESOURCE_BARRIER> barriers;
    barriers.reserve(resources.size());
    for (ID3D12Resource* resource : resources)
    {
        if (nullptr == resource) continue;
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        barrier.UAV.pResource = resource;
        barriers.push_back(barrier);
    }
    if (barriers.empty()) return;

    m_commandList->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
}

void DX12Encoder::CopyResource(ID3D12Resource* destination, ID3D12Resource* source)
{
    if (nullptr == m_commandList || nullptr == destination || nullptr == source) return;
    m_commandList->CopyResource(destination, source);
}

void DX12Encoder::ClearUnorderedAccess(ID3D12Resource* resource,
    const RHIBindingDesc& view, const float rgba[4])
{
    if (nullptr == m_commandList || nullptr == resource || nullptr == rgba) return;
    if (nullptr == m_resources) return;

    // ★ DX12는 같은 UAV의 셰이더 가시 GPU 핸들과 비가시 CPU 핸들을 짝으로
    //   요구한다. 호출부가 그것을 알 이유가 없어서 여기서 맞춘다 —
    //   VolumetricFog가 이것 하나 때문에 비가시 힙을 따로 들고 같은 뷰를
    //   두 번 만들고 있었다.
    RHIBindingDesc descriptor = view;
    descriptor.resource = resource;

    const RHIBindingTable shaderVisible = m_resources->CreateBindings({ &descriptor, 1 });
    if (!shaderVisible.IsValid()) return;

    const D3D12_CPU_DESCRIPTOR_HANDLE cpuOnly =
        m_resources->CreateClearDescriptor(descriptor);
    if (0 == cpuOnly.ptr) return;

    m_commandList->ClearUnorderedAccessViewFloat(shaderVisible.gpu, cpuOnly,
        resource, rgba, 0, nullptr);
}

#endif
