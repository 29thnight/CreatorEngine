#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include <d3d12.h>   // 5a — 인터페이스가 더는 이것을 물지 않는다(비유니티가 잡았다)
#include "../RHIEncoder.h"

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
    void SetPipeline(RHIBindPoint bindPoint, RHIPipelineHandle pipeline) override;
    void SetPrimitiveTopology(RHIPrimitiveTopology topology) override;

    void SetBindings(RHIBindPoint bindPoint, uint32_t slot,
        const RHIBindingTable& table) override;
    void SetSamplers(RHIBindPoint bindPoint, uint32_t slot,
        const RHISamplerTable& table) override;
    void SetConstantBuffer(RHIBindPoint bindPoint, uint32_t slot,
        const RHIBufferSlice& slice) override;
    void SetRootBuffer(RHIBindPoint bindPoint, uint32_t slot,
        const RHIBufferSlice& slice) override;

    void SetVertexBuffer(const RHIBufferSlice& slice, uint32_t stride) override;
    void SetIndexBuffer(const RHIBufferSlice& slice, RHIFormat format) override;

    /// 원시 뷰 경로 (A-4). 인터페이스에서 내려왔다 — 남은 소비자가 벤치
    /// 둘뿐이고, 그들은 인코더와 대조할 기준선으로 원시 경로를 **일부러**
    /// 쓴다(A-1 이 루트 시그니처 17곳을 남긴 것과 같은 기준). 구체 타입을
    /// 드는 자리라 `override` 가 아니다.
    void SetVertexBuffer(const D3D12_VERTEX_BUFFER_VIEW& view);
    void SetIndexBuffer(const D3D12_INDEX_BUFFER_VIEW& view);

private:
    /// 슬라이스를 GPU 주소로 푼다. 링은 리소스가 하나라 표 조회가 배열
    /// 인덱싱 한 번이다 — Allocate 의 175ns 에 비하면 묻힌다(encoderbench 로 잰다).
    D3D12_GPU_VIRTUAL_ADDRESS ResolveSlice(const RHIBufferSlice& slice) const;
public:

    void Draw(uint32_t vertexCount, uint32_t instanceCount,
        uint32_t firstVertex = 0, uint32_t firstInstance = 0) override;
    void DrawIndexed(uint32_t indexCount, uint32_t instanceCount,
        uint32_t firstIndex = 0, int32_t baseVertex = 0, uint32_t firstInstance = 0) override;

    void Dispatch(uint32_t x, uint32_t y, uint32_t z) override;

    void BindRenderTargets(const RHIRenderTargetBinding& binding) override;
    void ClearRenderTargets(const RHIRenderTargetBinding& binding,
        const float rgba[4]) override;
    void ClearDepthTarget(const RHIRenderTargetBinding& binding, float depth) override;

    void UavBarrier(std::span<const RHITextureHandle> textures) override;

    void CopyResource(RHITextureHandle destination, RHITextureHandle source) override;
    void ClearUnorderedAccess(const RHIBindingDesc& view, const float rgba[4]) override;

    void CopyToReadback(const RHIReadback& readback, RHITextureHandle source,
        uint32_t slice = 0, uint32_t sourceSubresource = 0) override;
    void CopyVolumeToReadback(const RHIReadback& readback, RHITextureHandle source,
        uint32_t sourceSubresource = 0) override;
    void CopyPartialToReadback(const RHIReadback& readback, RHITextureHandle source,
        uint32_t slice = 0, uint32_t sourceSubresource = 0) override;
    void CopyBufferToReadback(const RHIReadback& readback, RHIBufferHandle source,
        uint64_t sourceOffset = 0, uint64_t bytes = 0) override;
    void CopyTexture(RHITextureHandle destination, RHITextureHandle source,
        uint32_t destinationSubresource = 0, uint32_t sourceSubresource = 0) override;
    void ClearRenderTargetRect(const RHIRenderTargetBinding& binding,
        const float rgba[4], const RHIRect& rect) override;

    /// 상태 기억을 비우고 리스트를 다시 건다 (G-2a).
    ///
    /// ★ 새로 만드는 대신 제자리에서 비운다 — 그래프가 **패스마다** 부르므로
    ///   (프레임당 17회) 힙 할당이 프레임 경로에 들어가면 안 된다.
    ///   비우는 것이 곧 "이 인코더는 아무것도 안 걸어 두었다"이고, 그것이
    ///   패스 경계를 넘는 기억을 막는 유일한 조건이다.
    void ResetState(ID3D12GraphicsCommandList* commandList)
    {
        m_commandList = commandList;
        m_boundRootSignature[0] = nullptr;
        m_boundRootSignature[1] = nullptr;
        m_heapsBound = false;
    }

    /// 아직 인코더로 못 옮긴 자리가 쓰는 탈출구.
    ///
    /// ★ R3는 패스를 하나씩 옮긴다 — 인코더와 원시 커맨드 리스트를 한
    ///   프레임에 섞어 쓸 수 있으므로(같은 리스트에 기록) 패스 단위로 A/B가
    ///   된다. 그 이행 기간에만 쓰는 문이고, R3가 끝나면 사라진다.
    ID3D12GraphicsCommandList* GetCommandList() const { return m_commandList; }

private:
    /// 테이블을 처음 걸 때 디스크립터 힙을 건다. 한 번만 건다.
    ///
    /// ★ 셰이더 가시 힙과 샘플러 힙을 **늘 함께** 건다. 나눠 걸면 "테이블만
    ///   쓰다가 나중에 샘플러를 쓰는" 패스에서 중간에 다시 걸어야 하는데,
    ///   D3D12에서 힙을 다시 거는 것이 이미 걸어 둔 루트 테이블을 무효로
    ///   만드는지 확실치 않다. 확실치 않은 것에 기대는 대신 그 상황 자체를
    ///   없앤다 — 두 힙 다 프레임 전역이라 함께 거는 값이 사실상 공짜다.
    ///
    ///   (예전에는 패스가 withSamplers로 골라 걸었다. 그 선택이 무엇을
    ///   아꼈는지는 기록이 없고, 재 보면 호출 하나 차이다.)
    void EnsureDescriptorHeaps();

    ID3D12GraphicsCommandList* m_commandList{ nullptr };
    DX12DeviceResources*       m_resources{ nullptr };

    /// [0] = Graphics, [1] = Compute. 둘은 DX12에서 완전히 별개 상태다.
    ID3D12RootSignature*       m_boundRootSignature[2]{ nullptr, nullptr };

    bool                       m_heapsBound{ false };
};

#endif
