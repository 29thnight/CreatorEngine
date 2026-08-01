#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include <wrl/client.h>

#include "EnhancedRenderPass.h"
#include "EnhancedGBufferPass.h"

// Deferred 라이팅 패스 (PHASE 3-6, 두 번째 패스).
//
// GBuffer의 첫 소비자다. 이게 붙으면서 확인되는 것이 둘 있다:
//   ① GBuffer가 hasSideEffect 없이도 살아남는다 — 이 패스가 읽으므로 컬링의
//      역방향 도달이 그것을 살린다. 3-5의 컬링이 실전에서 동작하는지 보는 지점이다.
//   ② GBuffer 타깃의 RENDER_TARGET → PIXEL_SHADER_RESOURCE 전이가 그래프에서
//      자동으로 나온다. 손으로 배리어를 쓰지 않는 것이 3-5의 요점이었다.
//
// 라이팅 계산 자체는 아직 실제 광원을 받지 않는다. 라이트 목록 연결은 다음
// 슬라이스이고, 지금은 GBuffer 값을 조합해 결과를 만든다 — 목적이 '읽기가
// 실제로 되는가'이므로 계산이 복잡할 이유가 없다.
class EnhancedDeferredPass : public EnhancedRenderPass
{
public:
    const char* GetName() const override { return "Deferred"; }

    bool Initialize(const EnhancedFrameContext& context, std::string& outError) override;
    void Declare(EnhancedRenderGraph& graph, const EnhancedFrameContext& context) override;
    void Shutdown() override;

    // GBuffer 출력을 입력으로 받는다. Declare 전에 넣어 줘야 한다.
    void SetInputs(const EnhancedGBufferPass::Outputs& inputs) { m_inputs = inputs; }

    RGHandle GetOutput() const { return m_output; }

    static constexpr DXGI_FORMAT kOutputFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

private:
    template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    EnhancedGBufferPass::Outputs m_inputs;
    RGHandle m_output;

    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;

    ID3D12PipelineState* m_pso{ nullptr };
    ID3D12RootSignature* m_rootSignature{ nullptr };
    D3D12_GPU_DESCRIPTOR_HANDLE m_sampler{};
};

#endif
