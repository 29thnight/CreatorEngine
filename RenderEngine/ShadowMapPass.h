#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include "IRenderPass.h"
#include "Camera.h"

class Texture;
class Scene;
class RenderScene;

class ShadowMapPass final : public IRenderPass
{
public:
	ShadowMapPass();
	~ShadowMapPass() {};

	void Initialize(uint32 width, uint32 height);
	void Execute(RenderScene& scene, Camera& camera) override;

	Camera m_shadowCamera{};
	std::unique_ptr<Texture> m_shadowMapTexture{};
	ID3D11DepthStencilView* m_shadowMapDSV{ nullptr };
};