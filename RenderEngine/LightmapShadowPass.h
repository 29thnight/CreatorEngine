#pragma once
#include "IRenderPass.h"
#include "Camera.h"
#include "Texture.h"

class Texture;
class Scene;
class LightController;

class LightmapShadowPass final : public IRenderPass
{
public:
	LightmapShadowPass();
	virtual ~LightmapShadowPass();

	void Initialize(uint32 width, uint32 height);
	void Execute(RenderScene& scene, Camera& camera) override;
	void CreateShadowMap(uint32 width, uint32 height);
	void ClearShadowMap();
	void ControlPanel() override;
	void Resize() override;

	int shadowmapSize = 4096;

	Camera m_shadowCamera{};
	//Texture* m_shadowMapTexture{};
	ID3D11DepthStencilView* m_shadowMapDSV{ nullptr };
	std::vector<Texture*> m_shadowmapTextures;
};

