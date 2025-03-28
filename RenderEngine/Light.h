#pragma once
#include "ShadowMapPass.h"
#include "LightProperty.h"

class Texture;
class Scene;
class Camera;
class ForwardPass;
class GBufferPass;
class SceneRenderer;
class RenderScene;
class LightController
{
public:
	LightController() = default;
	~LightController();

public:
	void Initialize();
	void Update();
	Light& GetLight(uint32 index);
	Mathf::Vector4 GetEyePosition();
	LightController& AddLight(Light& light);
	LightController& SetGlobalAmbient(Mathf::Color4 color);
	LightController& SetEyePosition(Mathf::xVector eyePosition);
	void SetLightWithShadows(uint32 index, ShadowMapRenderDesc& desc);
	void RenderAnyShadowMap(RenderScene& scene, Camera& camera);

	Texture* GetShadowMapTexture();

	uint32 m_lightCount{ 0 };

private:
	friend class ForwardPass;
	friend class DeferredPass;
	friend class GBufferPass;
	friend class ShadowMapPass;
	friend class SceneRenderer;

	ID3D11Buffer* m_pLightBuffer{ nullptr };
	ShadowMapRenderDesc m_shadowMapRenderDesc;
	ShadowMapConstant m_shadowMapConstant;
	LightProperties m_lightProperties;
	bool hasLightWithShadows{ false };
	ID3D11Buffer* m_shadowMapBuffer{ nullptr };
	std::unique_ptr<ShadowMapPass> m_shadowMapPass;
};