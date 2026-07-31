#ifndef DYNAMICCPP_EXPORTS
#include "DX11RHI.h"
#include "../DeviceState.h"

#include <vector>

namespace
{
    // RHIViewport와 D3D11_VIEWPORT는 필드 순서·의미가 같지만, 배치 가정 대신
    // 명시 변환을 쓴다 — 침묵하는 memcpy는 어긋나는 날 디버깅이 안 된다.
    D3D11_VIEWPORT ToD3D11(const RHIViewport& vp)
    {
        D3D11_VIEWPORT out{};
        out.TopLeftX = vp.x;
        out.TopLeftY = vp.y;
        out.Width    = vp.width;
        out.Height   = vp.height;
        out.MinDepth = vp.minDepth;
        out.MaxDepth = vp.maxDepth;
        return out;
    }
}

void* DX11CommandContext::GetNativeHandle()
{
    return m_native ? m_native : DirectX11::DeviceStates->g_pDeviceContext;
}

void DX11CommandContext::SetViewports(uint32_t count, const RHIViewport* viewports)
{
    // 실사용은 1개다(모든 패스가 단일 뷰포트). 배열 경로도 규약대로 지원한다.
    if (1 == count)
    {
        const D3D11_VIEWPORT vp = ToD3D11(viewports[0]);
        if (m_native) DirectX11::RSSetViewports(m_native, 1, &vp);
        else          DirectX11::RSSetViewports(1, &vp);
        return;
    }

    std::vector<D3D11_VIEWPORT> converted(count);
    for (uint32_t i = 0; i < count; ++i) converted[i] = ToD3D11(viewports[i]);
    if (m_native) DirectX11::RSSetViewports(m_native, count, converted.data());
    else          DirectX11::RSSetViewports(count, converted.data());
}

void DX11CommandContext::SetRenderTargets(uint32_t count, RHINativeRenderTarget const* rtvs,
    RHINativeDepthStencil dsv)
{
    auto* views = reinterpret_cast<ID3D11RenderTargetView* const*>(rtvs);
    auto* depth = static_cast<ID3D11DepthStencilView*>(dsv);
    if (m_native) DirectX11::OMSetRenderTargets(m_native, count, views, depth);
    else          DirectX11::OMSetRenderTargets(count, views, depth);
}

void DX11CommandContext::UnbindRenderTargets()
{
    if (m_native) DirectX11::UnbindRenderTargets(m_native);
    else          DirectX11::UnbindRenderTargets();
}

void DX11CommandContext::SetPixelShaderResources(uint32_t startSlot, uint32_t count,
    RHINativeShaderResource const* srvs)
{
    auto* views = reinterpret_cast<ID3D11ShaderResourceView* const*>(srvs);
    if (m_native) DirectX11::PSSetShaderResources(m_native, startSlot, count, views);
    else          DirectX11::PSSetShaderResources(startSlot, count, views);
}

void DX11CommandContext::SetVertexShaderConstantBuffers(uint32_t startSlot, uint32_t count,
    RHINativeBuffer const* buffers)
{
    auto* raw = reinterpret_cast<ID3D11Buffer* const*>(buffers);
    if (m_native) DirectX11::VSSetConstantBuffer(m_native, startSlot, count, raw);
    else          DirectX11::VSSetConstantBuffer(startSlot, count, raw);
}

void DX11CommandContext::SetPixelShaderConstantBuffers(uint32_t startSlot, uint32_t count,
    RHINativeBuffer const* buffers)
{
    auto* raw = reinterpret_cast<ID3D11Buffer* const*>(buffers);
    if (m_native) DirectX11::PSSetConstantBuffer(m_native, startSlot, count, raw);
    else          DirectX11::PSSetConstantBuffer(startSlot, count, raw);
}

void DX11CommandContext::SetDepthStencilState(RHINativeDepthStencilState state, uint32_t stencilRef)
{
    auto* dss = static_cast<ID3D11DepthStencilState*>(state);
    if (m_native) DirectX11::OMSetDepthStencilState(m_native, dss, stencilRef);
    else          DirectX11::OMSetDepthStencilState(dss, stencilRef);
}

void DX11CommandContext::SetBlendState(RHINativeBlendState state, const float* blendFactor,
    uint32_t sampleMask)
{
    auto* blend = static_cast<ID3D11BlendState*>(state);
    if (m_native) DirectX11::OMSetBlendState(m_native, blend, blendFactor, sampleMask);
    else          DirectX11::OMSetBlendState(blend, blendFactor, sampleMask);
}

void DX11CommandContext::Draw(uint32_t vertexCount, uint32_t startVertex)
{
    if (m_native) DirectX11::Draw(m_native, vertexCount, startVertex);
    else          DirectX11::Draw(vertexCount, startVertex);
}

void DX11CommandContext::DrawIndexed(uint32_t indexCount, uint32_t startIndex, int32_t baseVertex)
{
    if (m_native) DirectX11::DrawIndexed(m_native, indexCount, startIndex, baseVertex);
    else          DirectX11::DrawIndexed(indexCount, startIndex, baseVertex);
}

void DX11CommandContext::UpdateBuffer(RHINativeBuffer buffer, const void* data)
{
    auto* raw = static_cast<ID3D11Buffer*>(buffer);
    if (m_native) DirectX11::UpdateBuffer(m_native, raw, data);
    else          DirectX11::UpdateBuffer(raw, data);
}

void DX11CommandContext::ClearRenderTarget(RHINativeRenderTarget rtv, const float color[4])
{
    auto* view = static_cast<ID3D11RenderTargetView*>(rtv);
    if (m_native) DirectX11::ClearRenderTargetView(m_native, view, color);
    else          DirectX11::ClearRenderTargetView(view, color);
}

void DX11CommandContext::SetComputeShader(RHINativeComputeShader shader)
{
    auto* cs = static_cast<ID3D11ComputeShader*>(shader);
    if (m_native) DirectX11::CSSetShader(m_native, cs, nullptr, 0);
    else          DirectX11::CSSetShader(cs, nullptr, 0);
}

void DX11CommandContext::SetComputeSamplers(uint32_t startSlot, uint32_t count,
    RHINativeSamplerState const* samplers)
{
    auto* raw = reinterpret_cast<ID3D11SamplerState* const*>(samplers);
    if (m_native) DirectX11::CSSetSamplers(m_native, startSlot, count, raw);
    else          DirectX11::CSSetSamplers(startSlot, count, raw);
}

void DX11CommandContext::SetComputeShaderResources(uint32_t startSlot, uint32_t count,
    RHINativeShaderResource const* srvs)
{
    auto* views = reinterpret_cast<ID3D11ShaderResourceView* const*>(srvs);
    if (m_native) DirectX11::CSSetShaderResources(m_native, startSlot, count, views);
    else          DirectX11::CSSetShaderResources(startSlot, count, views);
}

void DX11CommandContext::SetComputeUnorderedAccessViews(uint32_t startSlot, uint32_t count,
    RHINativeUnorderedAccess const* uavs, const uint32_t* initialCounts)
{
    auto* views = reinterpret_cast<ID3D11UnorderedAccessView* const*>(uavs);
    if (m_native) DirectX11::CSSetUnorderedAccessViews(m_native, startSlot, count, views, initialCounts);
    else          DirectX11::CSSetUnorderedAccessViews(startSlot, count, views, initialCounts);
}

void DX11CommandContext::SetComputeConstantBuffers(uint32_t startSlot, uint32_t count,
    RHINativeBuffer const* buffers)
{
    auto* raw = reinterpret_cast<ID3D11Buffer* const*>(buffers);
    if (m_native) DirectX11::CSSetConstantBuffer(m_native, startSlot, count, raw);
    else          DirectX11::CSSetConstantBuffer(startSlot, count, raw);
}

void DX11CommandContext::Dispatch(uint32_t x, uint32_t y, uint32_t z)
{
    if (m_native) DirectX11::Dispatch(m_native, x, y, z);
    else          DirectX11::Dispatch(x, y, z);
}

void DX11CommandContext::SetVertexBuffer(uint32_t slot, RHINativeBuffer buffer,
    uint32_t stride, uint32_t offset)
{
    auto* raw = static_cast<ID3D11Buffer*>(buffer);
    ID3D11DeviceContext* target = m_native ? m_native : DirectX11::DeviceStates->g_pDeviceContext;
    target->IASetVertexBuffers(slot, 1, &raw, &stride, &offset);
}

void DX11CommandContext::SetIndexBuffer(RHINativeBuffer buffer, bool use32BitIndices, uint32_t offset)
{
    auto* raw = static_cast<ID3D11Buffer*>(buffer);
    ID3D11DeviceContext* target = m_native ? m_native : DirectX11::DeviceStates->g_pDeviceContext;
    target->IASetIndexBuffer(raw, use32BitIndices ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT, offset);
}

void DX11CommandContext::SetPrimitiveTopology(RHIPrimitiveTopology topology)
{
    D3D11_PRIMITIVE_TOPOLOGY d3dTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    switch (topology)
    {
    case RHIPrimitiveTopology::PointList:     d3dTopology = D3D11_PRIMITIVE_TOPOLOGY_POINTLIST; break;
    case RHIPrimitiveTopology::LineList:      d3dTopology = D3D11_PRIMITIVE_TOPOLOGY_LINELIST; break;
    case RHIPrimitiveTopology::LineStrip:     d3dTopology = D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP; break;
    case RHIPrimitiveTopology::TriangleList:  d3dTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST; break;
    case RHIPrimitiveTopology::TriangleStrip: d3dTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP; break;
    }

    ID3D11DeviceContext* target = m_native ? m_native : DirectX11::DeviceStates->g_pDeviceContext;
    target->IASetPrimitiveTopology(d3dTopology);
}

void DX11CommandContext::CopyResource(RHINativeResource dst, RHINativeResource src)
{
    auto* rawDst = static_cast<ID3D11Resource*>(dst);
    auto* rawSrc = static_cast<ID3D11Resource*>(src);
    if (m_native) DirectX11::CopyResource(m_native, rawDst, rawSrc);
    else          DirectX11::CopyResource(rawDst, rawSrc);
}

void DX11CommandContext::SetVertexShaderResources(uint32_t startSlot, uint32_t count,
    RHINativeShaderResource const* srvs)
{
    auto* views = reinterpret_cast<ID3D11ShaderResourceView* const*>(srvs);
    // immediate 자유 함수가 없는 호출이라 이쪽 분기만 직접 컨텍스트를 쓴다
    // (Set 계열은 자유 함수에 부수 동작이 없어 의미가 갈라지지 않는다).
    if (m_native) DirectX11::VSSetShaderResources(m_native, startSlot, count, views);
    else          DirectX11::DeviceStates->g_pDeviceContext->VSSetShaderResources(startSlot, count, views);
}

void DX11CommandContext::ClearDepthStencil(RHINativeDepthStencil dsv, bool clearDepth,
    bool clearStencil, float depth, uint8_t stencil)
{
    uint32_t flags = 0;
    if (clearDepth)   flags |= D3D11_CLEAR_DEPTH;
    if (clearStencil) flags |= D3D11_CLEAR_STENCIL;

    auto* view = static_cast<ID3D11DepthStencilView*>(dsv);
    if (m_native) DirectX11::ClearDepthStencilView(m_native, view, flags, depth, stencil);
    else          DirectX11::ClearDepthStencilView(view, flags, depth, stencil);
}

void DX11CommandContext::UpdateBufferRange(RHINativeBuffer buffer, uint32_t byteOffset,
    uint32_t byteSize, const void* data)
{
    D3D11_BOX destBox{};
    destBox.left = byteOffset;
    destBox.right = byteOffset + byteSize;
    destBox.top = 0;
    destBox.bottom = 1;
    destBox.front = 0;
    destBox.back = 1;

    auto* raw = static_cast<ID3D11Buffer*>(buffer);
    ID3D11DeviceContext* target = m_native ? m_native : DirectX11::DeviceStates->g_pDeviceContext;
    target->UpdateSubresource(raw, 0, &destBox, data, 0, 0);
}

#endif
