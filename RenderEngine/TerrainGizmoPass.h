#pragma once
#include "IRenderPass.h"
#include "Texture.h"

cbuffer TerrainGizmoBuffer
{
	float2 gBrushPosition;
	float gBrushRadius;
};

class TerrainGizmoPass : public IRenderPass
{
public:
	TerrainGizmoPass();
	~TerrainGizmoPass() override = default;

	void Execute(RenderScene& scene, Camera& camera) override;
	void ControlPanel() override;
	void Resize(uint32_t width, uint32_t height) override;

private:
	ComPtr<ID3D11Buffer> m_Buffer{};
	Texture* copyTexture{ nullptr };

};

