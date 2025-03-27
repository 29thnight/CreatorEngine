#pragma once
#include "IRenderPass.h"
#include "Texture.h"

struct PostProcessingApply
{
	bool m_Bloom{ true };
};

cbuffer BloomBuffer
{
	float threshold{ 0.8f };
	float knee{ 1.0f };
};

cbuffer DownSampledBuffer
{
	//***** float2-> int2
	float2 inputSize;
	int2 outputSize;
};

cbuffer BlurBuffer
{
	int2 texSize;
};

cbuffer UpsampleBuffer
{
	int2 inputSize;
	int2 outputSize;
};

class Camera;
class PostProcessingPass final : public IRenderPass
{
public:
	PostProcessingPass();
	~PostProcessingPass();

	void Execute(Scene& scene, Camera& camera) override;
	void ControlPanel() override;

private:
	void TextureInitialization();
	void BloomPass(Scene& scene, Camera& camera);

private:
	//***** uint32
	const int32 BloomBufferWidth = 240;
	const int32 BloomBufferHeight = 135;

	PostProcessingApply m_PostProcessingApply;
	Texture* m_CopiedTexture;
	//Bloom Pass
	Texture* m_ApplyBloomCurveTexture;
	Texture* m_BloomDownSampledTexture1_StepX;
	Texture* m_BloomDownSampledTexture1_StepY;
	Texture* m_BloomDownSampledTexture2_StepX;
	Texture* m_BloomDownSampledTexture2_StepY;
	Texture* m_BloomDownSampledTextureFinal_StepX;
	Texture* m_BloomDownSampledTextureFinal_StepY;

	ComputeShader* m_pBloomDownSampledCS;
	ComputeShader* m_pGaussianBlur6x6_AxisX_CS;
	ComputeShader* m_pGaussianBlur6x6_AxisY_CS;
	ComputeShader* m_pGaussianBlur11x11_AxisX_CS;
	ComputeShader* m_pGaussianBlur11x11_AxisY_CS;

	BloomBuffer m_BloomConstant;
	DownSampledBuffer m_DownSampledConstant;
	BlurBuffer m_BlurConstant;
	UpsampleBuffer m_UpsampleConstant;

	ComPtr<ID3D11Buffer> m_BloomBuffer;
	ComPtr<ID3D11Buffer> m_DownSampledBuffer;
	ComPtr<ID3D11Buffer> m_BlurBuffer;
	ComPtr<ID3D11Buffer> m_UpsampleBuffer;

};