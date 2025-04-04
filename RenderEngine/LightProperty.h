#pragma once
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
	Mathf::Vector4 m_direction{ 0,0,1,0 };
	Mathf::Color4  m_color{};

	float m_constantAttenuation{ 1.f };
	float m_linearAttenuation{ 0.09f };
	float m_quadraticAttenuation{ 0.032f };
	float m_spotLightAngle{60.f};

	int m_lightType{};
	int m_lightStatus{};
	float m_intencity{ 5.f };

	Mathf::Matrix GetViewMatrix() {
		//return XMMatrixLookAtLH(m_position, m_direction, { 0, 1, 0, 0 });
		return XMMatrixLookAtLH(m_direction * -50.f, XMVectorSet(0, 0, 0, 1), { 0, 1, 0, 0 });
	}

	Mathf::Matrix GetProjectionMatrix(float _near, float _far, float width = 32.f, float height = 32.f) {
		if (m_lightType == LightType::DirectionalLight)
			return XMMatrixOrthographicLH(width, height, _near, _far);
		else if (m_lightType == LightType::PointLight)
			return XMMatrixPerspectiveFovLH(m_spotLightAngle, 1.0f, _near, _far);
		else if (m_lightType == LightType::SpotLight)
			return XMMatrixPerspectiveFovLH(m_spotLightAngle, 1.0f, _near, _far);
		else
			return XMMatrixIdentity();
	}
};

struct alignas(16) LightProperties
{
	Mathf::Vector4 m_eyePosition{};
	Mathf::Color4 m_globalAmbient{};
	Light m_lights[MAX_LIGHTS];
};

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