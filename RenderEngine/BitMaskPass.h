#pragma once
#include "IRenderPass.h"
#include "Texture.h"

class BitMaskPass final : public IRenderPass
{
public:
	BitMaskPass();
	~BitMaskPass();

	void Initialize(Texture* bitmask);
	void Execute(RenderScene& scene, Camera& camera) override;
	void CreateRenderCommandList(ID3D11DeviceContext* deferredContext, RenderScene& scene, Camera& camera) override;
	void ControlPanel() override;
	void Resize(uint32_t width, uint32_t height) override;

private:
	Texture* m_pBitmaskTexture{ nullptr };
	Texture* m_pTempTexture{ nullptr };
	Texture* m_pTempTexture2{ nullptr };

	ComputeShader* m_pEdgefilterShader{};
	ComputeShader* m_pEdgefilterDownSamplingShader{};
	ComputeShader* m_pEdgefilterUpSamplingShader{};
	ComputeShader* m_pAddColorShader{};

	ComPtr<ID3D11Buffer> m_EdgefilterBuffer;
	ComPtr<ID3D11Buffer> m_EdgefilterSamplingBuffer;

	Sampler* sample = nullptr;
	Sampler* pointSample = nullptr;

	//Outline
	bool isOn = true;
	bool blurOutline = true;
	float outlineVelocity = 1.f;
	Mathf::Color4 m_colors[8] = {
		{ 1.f, 0.f, 0.f, 1.f }, // Red
		{ 0.f, 1.f, 0.f, 1.f }, // Green
		{ 0.f, 0.f, 1.f, 1.f }, // Blue
		{ 1.f, 1.f, 0.f, 1.f }, // Yellow
		{ 1.f, 0.f, 1.f, 1.f }, // Magenta
		{ 0.f, 1.f, 1.f, 1.f }, // Cyan
		{ 0.5f, 0.5f, 0.5f, 1.f }, // Gray
		{ 0.2f, 0.2f, 0.2f, 1.f } // Dark Gray}
	};
};

