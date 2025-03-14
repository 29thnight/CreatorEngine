#pragma once
#include "IRenderPass.h"
#include "Camera.h"
#include "Texture.h"

class Texture;
class Scene;
class LightController;

struct alignas(16) ShadowMapConstant
{
	float m_shadowMapWidth{};
	float m_shadowMapHeight{};
	Mathf::xMatrix m_lightViewProjection{};
};

struct ShadowMapRenderDesc
{
	Mathf::xVector m_eyePosition{};
	Mathf::xVector m_lookAt{};
	float m_nearPlane{ 0.1f };
	float m_farPlane{ 200.f };
	float m_viewWidth{ 1.f };
	float m_viewHeight{ 1.f };
	float m_textureWidth{ 8192.f };
	float m_textureHeight{ 8192.f };
};

class ShadowMapPass final : public IRenderPass
{
public:
	ShadowMapPass();

	void Initialize(uint32 width, uint32 height);
	void Execute(Scene& scene) override;

	std::unique_ptr<Texture> m_shadowMapTexture{};
	ID3D11DepthStencilView* m_shadowMapDSV{ nullptr };
};