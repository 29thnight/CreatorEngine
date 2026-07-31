#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "RHI.h"

struct ID3D11DeviceContext;

// DX11 백엔드(PHASE 3-1).
//
// 기존 DirectX11:: 자유 함수(DeviceState.h)에 위임한다 — 그 함수들이 드로우콜
// 카운트·어노테이션 같은 부수 동작을 이미 지니고 있어, 여기서 직접
// g_pDeviceContext를 만지면 그 동작이 갈라진다. 이 백엔드가 두꺼워질 이유는
// 없다: DX11은 은퇴 예정이고, 구조가 필요한 쪽은 DX12다.

class DX11CommandContext final : public RHICommandContext
{
public:
    // native가 널이면 immediate 컨텍스트(자유 함수의 기본 경로),
    // 아니면 그 deferred 컨텍스트에 기록한다.
    explicit DX11CommandContext(ID3D11DeviceContext* native = nullptr)
        : m_native(native) {}

    // 전환기 탈출구 — 널(immediate 래퍼)이면 전역 immediate 컨텍스트를 준다.
    void* GetNativeHandle() override;

    void SetViewports(uint32_t count, const RHIViewport* viewports) override;
    void SetRenderTargets(uint32_t count, RHINativeRenderTarget const* rtvs,
        RHINativeDepthStencil dsv) override;
    void UnbindRenderTargets() override;
    void SetPixelShaderResources(uint32_t startSlot, uint32_t count,
        RHINativeShaderResource const* srvs) override;
    void SetVertexShaderConstantBuffers(uint32_t startSlot, uint32_t count,
        RHINativeBuffer const* buffers) override;
    void SetPixelShaderConstantBuffers(uint32_t startSlot, uint32_t count,
        RHINativeBuffer const* buffers) override;
    void SetDepthStencilState(RHINativeDepthStencilState state, uint32_t stencilRef) override;
    void SetBlendState(RHINativeBlendState state, const float* blendFactor,
        uint32_t sampleMask) override;
    void Draw(uint32_t vertexCount, uint32_t startVertex) override;
    void DrawIndexed(uint32_t indexCount, uint32_t startIndex, int32_t baseVertex) override;

    void UpdateBuffer(RHINativeBuffer buffer, const void* data) override;
    void ClearRenderTarget(RHINativeRenderTarget rtv, const float color[4]) override;
    void SetComputeShader(RHINativeComputeShader shader) override;
    void SetComputeSamplers(uint32_t startSlot, uint32_t count,
        RHINativeSamplerState const* samplers) override;
    void SetComputeShaderResources(uint32_t startSlot, uint32_t count,
        RHINativeShaderResource const* srvs) override;
    void SetComputeUnorderedAccessViews(uint32_t startSlot, uint32_t count,
        RHINativeUnorderedAccess const* uavs, const uint32_t* initialCounts) override;
    void SetComputeConstantBuffers(uint32_t startSlot, uint32_t count,
        RHINativeBuffer const* buffers) override;
    void Dispatch(uint32_t x, uint32_t y, uint32_t z) override;
    void CopyResource(RHINativeResource dst, RHINativeResource src) override;
    void SetVertexShaderResources(uint32_t startSlot, uint32_t count,
        RHINativeShaderResource const* srvs) override;
    void ClearDepthStencil(RHINativeDepthStencil dsv, bool clearDepth,
        bool clearStencil, float depth, uint8_t stencil) override;
    void UpdateBufferRange(RHINativeBuffer buffer, uint32_t byteOffset,
        uint32_t byteSize, const void* data) override;
    void SetVertexBuffer(uint32_t slot, RHINativeBuffer buffer,
        uint32_t stride, uint32_t offset) override;
    void SetIndexBuffer(RHINativeBuffer buffer, bool use32BitIndices, uint32_t offset) override;
    void SetPrimitiveTopology(RHIPrimitiveTopology topology) override;

private:
    ID3D11DeviceContext* m_native{ nullptr };
};

class DX11RHIDevice final : public RHIDevice
{
public:
    RHIBackendKind GetBackend() const override { return RHIBackendKind::DX11; }
    const char* GetName() const override { return "Dx11"; }
    RHICommandContext& GetImmediateContext() override { return m_immediate; }

    RHIViewport GetFullViewport() const override;
    RHINativeRenderTarget GetBackBufferRenderTarget() const override;
    RHINativeBlendState GetDefaultBlendState() const override;
    RHINativeDepthStencilState GetDefaultDepthStencilState() const override;

private:
    DX11CommandContext m_immediate;
};

#endif
