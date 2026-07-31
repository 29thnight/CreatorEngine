#pragma once
#include "RHIDefinitions.h"

// 커맨드 기록 인터페이스(PHASE 3-1).
//
// DX11에서는 immediate/deferred 컨텍스트를, DX12에서는 커맨드 리스트를 감싼다.
// 지금 표면적은 첫 이식 대상(BlitPass)이 요구하는 최소이고, 패스를 옮길 때마다
// 그 패스가 필요로 하는 만큼만 넓힌다 — 인터페이스를 미리 크게 그려 두면
// DX12 쪽에서 한 번도 검증되지 않은 서명이 쌓인다.
class RHICommandContext
{
public:
    virtual ~RHICommandContext() = default;

    // ── 전환기 탈출구 ──
    //
    // 아직 RHI로 넘어오지 않은 경로(PSO Apply·FinishCommandList·프록시 드로우
    // 내부·카메라 버퍼 갱신)가 네이티브 컨텍스트를 요구한다. DX11에서는
    // ID3D11DeviceContext*가 나온다. 이 함수의 호출부 수가 곧 남은 이식량이며,
    // 0이 되는 날 함수도 제거된다. 새 코드가 이것으로 API를 직접 만지는 것은 금지.
    virtual void* GetNativeHandle() = 0;

    virtual void SetViewports(uint32_t count, const RHIViewport* viewports) = 0;

    // dsv는 null이면 깊이 버퍼 없이 바인딩한다.
    virtual void SetRenderTargets(uint32_t count, RHINativeRenderTarget const* rtvs,
        RHINativeDepthStencil dsv) = 0;
    virtual void UnbindRenderTargets() = 0;

    virtual void SetPixelShaderResources(uint32_t startSlot, uint32_t count,
        RHINativeShaderResource const* srvs) = 0;

    virtual void SetVertexShaderConstantBuffers(uint32_t startSlot, uint32_t count,
        RHINativeBuffer const* buffers) = 0;
    virtual void SetPixelShaderConstantBuffers(uint32_t startSlot, uint32_t count,
        RHINativeBuffer const* buffers) = 0;

    // 상태 객체는 전환기에는 네이티브 핸들로 받는다. DX12에서는 이 두 호출이
    // PSO에 흡수되므로(3-4), 이식이 끝나면 인터페이스에서 제거될 후보다 —
    // 그때까지 상태 전환이 어디서 일어나는지를 한 타입으로 추적하는 것이 목적.
    virtual void SetDepthStencilState(RHINativeDepthStencilState state, uint32_t stencilRef) = 0;
    virtual void SetBlendState(RHINativeBlendState state, const float* blendFactor,
        uint32_t sampleMask) = 0;

    virtual void Draw(uint32_t vertexCount, uint32_t startVertex) = 0;
    virtual void DrawIndexed(uint32_t indexCount, uint32_t startIndex, int32_t baseVertex) = 0;

    // ── 3차 슬라이스에서 확장(컴퓨트 계열 + 버퍼 갱신 + 클리어) ──
    //
    // UpdateBuffer는 DX11의 Map/Discard 의미다. DX12에서는 업로드 링버퍼 복사가
    // 되므로 "프레임 안에서 유효한 일회성 갱신"이라는 의미만 계약으로 삼는다 —
    // 갱신 후 이전 내용에 기대는 호출부는 원래부터 잘못이다.
    virtual void UpdateBuffer(RHINativeBuffer buffer, const void* data) = 0;

    virtual void ClearRenderTarget(RHINativeRenderTarget rtv, const float color[4]) = 0;

    virtual void SetComputeShader(RHINativeComputeShader shader) = 0;
    virtual void SetComputeSamplers(uint32_t startSlot, uint32_t count,
        RHINativeSamplerState const* samplers) = 0;
    virtual void SetComputeShaderResources(uint32_t startSlot, uint32_t count,
        RHINativeShaderResource const* srvs) = 0;
    virtual void SetComputeUnorderedAccessViews(uint32_t startSlot, uint32_t count,
        RHINativeUnorderedAccess const* uavs, const uint32_t* initialCounts) = 0;
    virtual void SetComputeConstantBuffers(uint32_t startSlot, uint32_t count,
        RHINativeBuffer const* buffers) = 0;
    virtual void Dispatch(uint32_t x, uint32_t y, uint32_t z) = 0;

    // 전체 서브리소스 복사(DX11 CopyResource 의미). DX12에서는 양쪽 리소스의
    // 상태 전이가 앞뒤로 붙는다 — 렌더그래프(3-5)가 이 호출을 복사 노드로 본다.
    virtual void CopyResource(RHINativeResource dst, RHINativeResource src) = 0;

    // ── 5차 슬라이스에서 확장(본체 패스: GBuffer·Deferred·Shadow·Forward) ──

    virtual void SetVertexShaderResources(uint32_t startSlot, uint32_t count,
        RHINativeShaderResource const* srvs) = 0;

    // 플래그 대신 bool 두 개 — D3D11_CLEAR_*와 D3D12_CLEAR_FLAG_*를 양쪽에서
    // 각자 조립하게 하고, 인터페이스에는 API 상수를 노출하지 않는다.
    virtual void ClearDepthStencil(RHINativeDepthStencil dsv, bool clearDepth,
        bool clearStencil, float depth, uint8_t stencil) = 0;

    // 버퍼의 바이트 구간 갱신(DX11 UpdateSubresource+box 의미). 인스턴스 행렬처럼
    // 프레임마다 앞부분 N바이트만 채우는 용도다. UpdateBuffer와 같은 일회성 계약.
    virtual void UpdateBufferRange(RHINativeBuffer buffer, uint32_t byteOffset,
        uint32_t byteSize, const void* data) = 0;

    // ── 6차 슬라이스에서 확장(입력 조립 — DecalPass의 데칼 박스 메시) ──
    //
    // 인덱스 포맷은 DXGI 상수 대신 use32BitIndices 하나로 받는다(실사용이 16/32 둘뿐).
    virtual void SetVertexBuffer(uint32_t slot, RHINativeBuffer buffer,
        uint32_t stride, uint32_t offset) = 0;
    virtual void SetIndexBuffer(RHINativeBuffer buffer, bool use32BitIndices, uint32_t offset) = 0;
    virtual void SetPrimitiveTopology(RHIPrimitiveTopology topology) = 0;

    // ── 단일 슬롯 편의 오버로드(비가상) ──
    //
    // 배열 인자 판은 T* const* → void* const* 변환이 안 돼 호출부마다 임시 핸들
    // 변수가 필요하다. 단일 슬롯은 값 전달이라 어떤 네이티브 포인터든 그대로
    // 들어간다 — 머티리얼 텍스처 바인딩처럼 한 슬롯씩 거는 코드(GBuffer·Forward에
    // 수십 곳)의 소음을 없애는 것이 목적이다.
    void SetPixelShaderResource(uint32_t slot, RHINativeShaderResource srv)
    { SetPixelShaderResources(slot, 1, &srv); }
    void SetVertexShaderResource(uint32_t slot, RHINativeShaderResource srv)
    { SetVertexShaderResources(slot, 1, &srv); }
    void SetVertexShaderConstantBuffer(uint32_t slot, RHINativeBuffer buffer)
    { SetVertexShaderConstantBuffers(slot, 1, &buffer); }
    void SetPixelShaderConstantBuffer(uint32_t slot, RHINativeBuffer buffer)
    { SetPixelShaderConstantBuffers(slot, 1, &buffer); }
    void SetComputeShaderResource(uint32_t slot, RHINativeShaderResource srv)
    { SetComputeShaderResources(slot, 1, &srv); }
    void SetComputeConstantBuffer(uint32_t slot, RHINativeBuffer buffer)
    { SetComputeConstantBuffers(slot, 1, &buffer); }
    void SetComputeUnorderedAccessView(uint32_t slot, RHINativeUnorderedAccess uav)
    { SetComputeUnorderedAccessViews(slot, 1, &uav, nullptr); }
    void SetComputeSampler(uint32_t slot, RHINativeSamplerState sampler)
    { SetComputeSamplers(slot, 1, &sampler); }
    void SetRenderTarget(RHINativeRenderTarget rtv, RHINativeDepthStencil dsv)
    { SetRenderTargets(1, &rtv, dsv); }
};
