#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "RHIEncoder.h"

class DX12DeviceResources;

// RHIEncoder의 DX12 구현 (PHASE 3-1 재정의, R3).
//
// 커맨드 리스트 하나를 감싼다. 만드는 것은 그래프이고, 조각마다 하나다 —
// AddSplitPass의 워커들이 각자 다른 커맨드 리스트에 적으므로 프레임 전역으로
// 두면 조각이 서로의 기록을 덮는다.
//
// ★ 값 하나를 기억한다: 지금 걸려 있는 루트 시그니처(바인드 포인트마다).
//
//   SetPipeline이 루트 시그니처를 반드시 받는 계약이라(RHIEncoder ③ 참고)
//   드로우를 여러 번 하는 패스는 배치마다 SetPipeline을 부른다. D3D12는
//   루트 시그니처를 거는 것을 "루트 인자가 무효가 되는" 사건으로 규정하므로
//   그것을 되풀이하지 않는다. 근거와 한계는 SetPipeline 구현부에 적었다 —
//   재현된 결함을 고친 것이 아니라 보장 없는 동작을 피하는 쪽이다.
//
//   기억이 낡지 않는 것은 수명이 보장한다: 그래프가 패스마다 인코더를 새로
//   만든다. 아직 안 옮긴 패스가 같은 커맨드 리스트에 원시로 루트를 걸어도
//   그 기억은 이미 사라진 뒤다.
//
//   그 밖의 상태는 들지 않는다. ClearUnorderedAccess가 디스크립터를 두
//   벌(셰이더 가시 · 비가시) 만들어야 해서 디바이스 서비스를 알아야 하고,
//   그래서 resources를 든다. 나머지는 전부 커맨드 리스트로 그대로 흘린다 —
//   상태를 들수록 '인코더가 기억하는 것'과 '커맨드 리스트가 기억하는 것'이
//   갈려 어긋난다.
class DX12Encoder final : public RHIEncoder
{
public:
    DX12Encoder(ID3D12GraphicsCommandList* commandList, DX12DeviceResources* resources)
        : m_commandList(commandList), m_resources(resources) {}

    void SetViewportAndScissor(uint32_t width, uint32_t height) override;
    void SetPipeline(RHIBindPoint bindPoint, ID3D12PipelineState* pipeline,
        ID3D12RootSignature* rootSignature) override;
    void SetPrimitiveTopology(RHIPrimitiveTopology topology) override;

    void SetBindings(RHIBindPoint bindPoint, uint32_t slot,
        const RHIBindingTable& table) override;
    void SetSamplers(RHIBindPoint bindPoint, uint32_t slot,
        const RHISamplerTable& table) override;
    void SetConstantBuffer(RHIBindPoint bindPoint, uint32_t slot,
        D3D12_GPU_VIRTUAL_ADDRESS address) override;
    void SetRootBuffer(RHIBindPoint bindPoint, uint32_t slot,
        D3D12_GPU_VIRTUAL_ADDRESS address) override;

    void SetVertexBuffer(const D3D12_VERTEX_BUFFER_VIEW& view) override;
    void SetIndexBuffer(const D3D12_INDEX_BUFFER_VIEW& view) override;

    void Draw(uint32_t vertexCount, uint32_t instanceCount,
        uint32_t firstVertex = 0, uint32_t firstInstance = 0) override;
    void DrawIndexed(uint32_t indexCount, uint32_t instanceCount,
        uint32_t firstIndex = 0, int32_t baseVertex = 0, uint32_t firstInstance = 0) override;

    void Dispatch(uint32_t x, uint32_t y, uint32_t z) override;

    void BindRenderTargets(const RHIRenderTargetBinding& binding) override;
    void ClearRenderTargets(const RHIRenderTargetBinding& binding,
        const float rgba[4]) override;
    void ClearDepthTarget(const RHIRenderTargetBinding& binding, float depth) override;

    void UavBarrier(std::span<ID3D12Resource* const> resources) override;

    void CopyResource(ID3D12Resource* destination, ID3D12Resource* source) override;
    void ClearUnorderedAccess(ID3D12Resource* resource,
        const RHIBindingDesc& view, const float rgba[4]) override;

    /// 아직 인코더로 못 옮긴 자리가 쓰는 탈출구.
    ///
    /// ★ R3는 패스를 하나씩 옮긴다 — 인코더와 원시 커맨드 리스트를 한
    ///   프레임에 섞어 쓸 수 있으므로(같은 리스트에 기록) 패스 단위로 A/B가
    ///   된다. 그 이행 기간에만 쓰는 문이고, R3가 끝나면 사라진다.
    ID3D12GraphicsCommandList* GetCommandList() const { return m_commandList; }

private:
    ID3D12GraphicsCommandList* m_commandList{ nullptr };
    DX12DeviceResources*       m_resources{ nullptr };

    /// [0] = Graphics, [1] = Compute. 둘은 DX12에서 완전히 별개 상태다.
    ID3D12RootSignature*       m_boundRootSignature[2]{ nullptr, nullptr };
};

#endif
