#pragma once
#include "IRenderPass.h"
#include "Texture.h"
#include <unordered_map>

class PositionMapPass final : public IRenderPass
{
public:
	PositionMapPass();

	void Initialize(uint32 width, uint32 height);
	void Execute(RenderScene& scene, Camera& camera) override;
	void ClearTextures();

	void ControlPanel() override;
	void Resize(uint32_t width, uint32_t height) override;

	std::unordered_map<std::string, Texture*> m_positionMapTextures;
	std::unordered_map<std::string, Texture*> m_normalMapTextures;
	ComPtr<ID3D11Buffer> m_Buffer{};
public:
	void CreateTempTexture();

	int posNormMapSize = 400;
	int posNormDilateCount = 2;
	bool isDilateOn{ true };

	ComputeShader* m_edgeComputeShader{};
	ComputeShader* m_edgeCoverComputeShader{};
	Texture* tempTexture{};
};

