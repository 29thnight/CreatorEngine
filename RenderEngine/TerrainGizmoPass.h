#pragma once
#include "IRenderPass.h"
#include "Texture.h"
#include "TerrainBuffers.h"

class TerrainGizmoPass : public IRenderPass
{
public:
	TerrainGizmoPass();
	~TerrainGizmoPass() override = default;

	void Execute(RenderScene& scene, Camera& camera) override;
	void CreateRenderCommandList(ID3D11DeviceContext* deferredContext, RenderScene& scene, Camera& camera) override;
	void ControlPanel() override;
	void Resize(uint32_t width, uint32_t height) override;

private:
	ComPtr<ID3D11Buffer> m_Buffer{};
	Texture* m_pTempTexture{ nullptr };
};

