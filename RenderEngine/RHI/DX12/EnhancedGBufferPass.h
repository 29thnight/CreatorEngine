#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include <array>
#include <wrl/client.h>

#include "EnhancedRenderPass.h"

// GBuffer 패스 (PHASE 3-6, 첫 패스).
//
// DX11 GBufferPass를 옮기지 않고 새로 쓴다 — 방향 정정(2026-08-01)대로다.
// 다만 출력 레이아웃은 맞춘다. 정확성 판정이 'DX11과 같은 그림이 나오는가'이므로
// 타깃 구성이 다르면 대조 자체가 성립하지 않는다.
//
// DX11 쪽 구성(SceneRenderer::InitializeTextures):
//   Diffuse      R16G16B16A16_FLOAT
//   MetalRough   R16G16B16A16_FLOAT
//   Normal       R16G16B16A16_FLOAT
//   Emissive     R16G16B16A16_FLOAT
//   Bitmask      R32_UINT
//   + 깊이
//
// 이 슬라이스에서 새로 뚫는 것: 정점/인덱스 버퍼(입력 조립), MRT 5개 동시 출력,
// 깊이 버퍼, 그래프를 통한 transient 타깃 선언과 자동 배리어.
// 재질·텍스처·실제 씬 지오메트리는 씬 연결 슬라이스에서 붙인다.
class EnhancedGBufferPass : public EnhancedRenderPass
{
public:
    static constexpr uint32_t kRenderTargetCount = 5;

    // 다음 패스(Deferred)가 읽을 수 있도록 그래프 핸들을 내놓는다.
    struct Outputs
    {
        RGHandle diffuse;
        RGHandle metalRough;
        RGHandle normal;
        RGHandle emissive;
        RGHandle bitmask;
        RGHandle depth;
    };

    const char* GetName() const override { return "GBuffer"; }

    bool Initialize(const EnhancedFrameContext& context, std::string& outError) override;
    void Declare(EnhancedRenderGraph& graph, const EnhancedFrameContext& context) override;
    void Shutdown() override;

    // Declare 뒤에 유효하다.
    const Outputs& GetOutputs() const { return m_outputs; }

    static DXGI_FORMAT GetRenderTargetFormat(uint32_t index);
    static constexpr DXGI_FORMAT kDepthFormat = DXGI_FORMAT_D32_FLOAT;

private:
    template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    struct Vertex
    {
        float position[3];
        float normal[3];
        float uv[2];
    };

    bool CreateGeometry(const EnhancedFrameContext& context, std::string& outError);
    bool CreatePipeline(const EnhancedFrameContext& context, std::string& outError);

    Outputs m_outputs;

    ComPtr<ID3D12Resource> m_vertexBuffer;
    ComPtr<ID3D12Resource> m_indexBuffer;
    D3D12_VERTEX_BUFFER_VIEW m_vertexView{};
    D3D12_INDEX_BUFFER_VIEW  m_indexView{};
    uint32_t m_indexCount{ 0 };

    // 타깃별 RTV와 깊이 DSV. 그래프가 만든 transient에 매 프레임 뷰를 만든다 —
    // 리소스가 프레임마다 바뀔 수 있으므로 뷰를 캐시하지 않는다.
    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
    uint32_t m_rtvIncrement{ 0 };

    ID3D12PipelineState* m_pso{ nullptr };
    ID3D12RootSignature* m_rootSignature{ nullptr };
};

#endif
