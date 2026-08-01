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

    // 셰이더 상수 한도. 광원 64개면 상수 버퍼가 약 4KB로, 루트 CBV로 넘기기에
    // 무난하다. 그 이상은 구조를 바꿔야 한다(타일드/클러스터드) — 광원이 실제로
    // 많은 씬이 나왔을 때 재는 것이 먼저다.
    static constexpr uint32_t kMaxLights = 64;

    bool Initialize(const EnhancedFrameContext& context, std::string& outError) override;
    bool PrepareFrame(const EnhancedFrameContext& context, std::string& outError) override;
    void Declare(EnhancedRenderGraph& graph, const EnhancedFrameContext& context) override;
    void Shutdown() override;

    // 이번 프레임에 실제로 넘긴 광원 수. 한도를 넘으면 잘린다.
    uint32_t GetLastLightCount() const { return static_cast<uint32_t>(m_frameLights.size()); }
    uint32_t GetLastDroppedLights() const { return m_droppedLights; }

    // GBuffer 출력을 입력으로 받는다. Declare 전에 넣어 줘야 한다.
    void SetInputs(const EnhancedGBufferPass::Outputs& inputs) { m_inputs = inputs; }

    // 그림자 맵과 캐스케이드 정보. 그림자 패스가 없으면 넣지 않아도 되고,
    // 그때는 그림자 없이 도는 것이 정상 경로다.
    void SetShadow(RGHandle shadowMap, const EnhancedShadowData& data)
    {
        m_shadowMap = shadowMap;
        m_shadowData = data;
    }

    RGHandle GetOutput() const { return m_output; }

    static constexpr DXGI_FORMAT kOutputFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

private:
    template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    // b0에 올라가는 라이팅 상수. HLSL의 cbuffer와 배치가 같아야 한다.
    struct LightingConstants
    {
        Mathf::Matrix  inverseViewProjection{};
        Mathf::Matrix  lightViewProjection[kShadowCascadeCount]{};
        Mathf::Vector4 eyePosition{};
        Mathf::Vector4 cameraForward{};
        Mathf::Vector4 cascadeSplits{};
        Mathf::Vector4 shadowBias{};
        uint32_t       lightCount{ 0 };
        uint32_t       hasShadow{ 0 };
        uint32_t       padding[2]{};
        EnhancedLight  lights[kMaxLights]{};
    };

    EnhancedGBufferPass::Outputs m_inputs;
    RGHandle m_output;

    std::vector<EnhancedLight> m_frameLights;
    Mathf::Matrix  m_inverseViewProjection{};
    Mathf::Vector4 m_eyePosition{};
    uint32_t       m_droppedLights{ 0 };

    RGHandle           m_shadowMap;
    EnhancedShadowData m_shadowData{};

    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;

    ID3D12PipelineState* m_pso{ nullptr };
    ID3D12RootSignature* m_rootSignature{ nullptr };
    D3D12_GPU_DESCRIPTOR_HANDLE m_sampler{};
};

#endif
