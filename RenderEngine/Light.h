#pragma once
#include "Core.Minimal.h"
#include "Light.h"
#include "ShadowMapPass.h"

constexpr int MAX_LIGHTS = 4;

enum LightType
{
	DirectionalLight,
	PointLight,
	SpotLight,
	LightsTypeMax
};

enum LightStatus
{
	Disabled,
	Enabled,
	StaticShadows,
	LightsStatusMax
};

struct alignas(16) Light
{
	Mathf::Vector4 m_position{};
	Mathf::Vector4 m_direction{};
	Mathf::Color4  m_color{};

	float m_constantAttenuation{ 1.f };
	float m_linearAttenuation{ 0.09f };
	float m_quadraticAttenuation{ 0.032f };
	float m_spotLightAngle{};

	int m_lightType{};
	int m_lightStatus{};
};

struct alignas(16) LightProperties
{
	Mathf::Vector4 m_eyePosition{};
	Mathf::Color4 m_globalAmbient{};
	Light m_lights[MAX_LIGHTS];
};

class Texture;
class Scene;
class ForwardPass;
class GBufferPass;
struct ShadowMapRenderDesc;
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
	void RenderAnyShadowMap(Scene& scene);

	Texture* GetShadowMapTexture();

	uint32 m_lightCount{ 0 };

private:
	friend class ForwardPass;
	friend class DeferredPass;
	friend class GBufferPass;
	friend class ShadowMapPass;

	ID3D11Buffer* m_pLightBuffer{ nullptr };
	ShadowMapRenderDesc m_shadowMapRenderDesc;
	LightProperties m_lightProperties;
	bool hasLightWithShadows{ false };
	ID3D11Buffer* m_shadowMapBuffer{ nullptr };
	std::unique_ptr<Texture> m_shadowMapTexture;
	std::unique_ptr<ShadowMapPass> m_shadowMapPass;
};