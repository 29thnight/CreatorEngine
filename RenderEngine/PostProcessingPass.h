#pragma once
#include "cbuffers.h"
#include "Texture.h"

struct PostProcessingApply
{
	bool m_Bloom{ true };
};

struct PostProcessingPassSetting;
class PostProcessingPass final : public IRenderPass
{
public:
	PostProcessingPass();
	~PostProcessingPass();

	void Execute(RenderScene& scene, Camera& camera) override;
	void ControlPanel() override;
        void ApplySettings(const PostProcessingPassSetting& setting);
	void Resize(uint32_t width, uint32_t height) override;

private:
	void PrepaerShaderState();
	void TextureInitialization();
	void BloomPass(RenderScene& scene, Camera& camera);
	void GaussianBlurComputeKernel();

private:
	//***** uint32
	uint32 BloomBufferWidth = 240;
	uint32 BloomBufferHeight = 135;

	PostProcessingApply m_PostProcessingApply;
	Texture* m_CopiedTexture;
#pragma region Bloom Pass
	//Bloom Pass Begin --------------------------------
	Texture* m_BloomFilterSRV1;
	Texture* m_BloomFilterSRV2;
	Texture* m_BloomFilterUAV1;
	Texture* m_BloomFilterUAV2;
	Texture* m_BloomResult;

	VertexShader* m_pFullScreenVS;
	PixelShader* m_pBloomCompositePS;
	ComputeShader* m_pBloomDownSampledCS;
	ComputeShader* m_pGaussianBlurCS;

	ThresholdParams m_bloomThreshold;
	BlurParams m_bloomBlur;
	CompositeParams m_bloomComposite;

	ComPtr<ID3D11Buffer> m_bloomThresholdBuffer;
	ComPtr<ID3D11Buffer> m_bloomBlurBuffer;
	ComPtr<ID3D11Buffer> m_bloomCompositeBuffer;
	//Bloom Pass End --------------------------------
#pragma endregion

#pragma region Color Grading Pass
	//Color Grading Pass Begin --------------------------------
	Texture* m_ColorGradingTexture;
	//Color Grading Pass End --------------------------------
#pragma endregion
};