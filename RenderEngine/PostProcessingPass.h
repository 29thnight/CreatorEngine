#pragma once
#include "IRenderPass.h"
#include "Texture.h"
#include <array>

struct PostProcessingApply
{
    bool m_Bloom{ true };
};

struct BloomPassSetting;
class PostProcessingPass final : public IRenderPass
{
public:
    PostProcessingPass();
    ~PostProcessingPass();

    void Execute(RenderScene& scene, Camera& camera) override;
    void ControlPanel() override;
    void ApplySettings(const BloomPassSetting& setting);
    void Resize(uint32_t width, uint32_t height) override;

private:
    void PrepaerShaderState();
    void TextureInitialization();
    void BloomPass(RenderScene& scene, Camera& camera);

private:
    PostProcessingApply m_PostProcessingApply{};
    Texture* m_CopiedTexture{};

    // Bloom resources
    static constexpr uint32_t BLOOM_MIP_LEVELS = 4;
    Texture* m_bloomExtractTexture{};
    std::array<Texture*, BLOOM_MIP_LEVELS> m_bloomDownSampledTextures{};
    std::array<Texture*, BLOOM_MIP_LEVELS> m_bloomUpSampledTextures{};
    Texture* m_BloomResult{};

    VertexShader* m_pFullScreenVS{};
    PixelShader* m_pBloomCompositePS{};
    ComputeShader* m_pBloomExtractCS{};
    ComputeShader* m_pBloomDownSampleCS{};
    ComputeShader* m_pBloomUpSampleCS{};

    struct BloomParams
    {
        float threshHold{ 5.f };
        float radius{ 2.f };
        float padding[2]{};
    } m_bloomParams{};

    struct BloomDownSampleParams
    {
        DirectX::XMFLOAT2 texelSize{};
        uint32_t inputTextureMipLevel{};
        uint32_t bloomPassIndex{};
    } m_downSampleParams{};

    struct BloomUpSampleParams
    {
        float radius{ 2.f };
        uint32_t bloomPassIndex{};
        uint32_t inputPreviousUpSampleMipLevel{};
        uint32_t maxBloomPasses{ BLOOM_MIP_LEVELS };
    } m_upSampleParams{};

    struct BloomCompositeParams
    {
        float coefficient{ 0.05f };
    } m_bloomComposite{};

    ComPtr<ID3D11Buffer> m_bloomParamBuffer{};
    ComPtr<ID3D11Buffer> m_downSampleBuffer{};
    ComPtr<ID3D11Buffer> m_upSampleBuffer{};
    ComPtr<ID3D11Buffer> m_bloomCompositeBuffer{};
};
