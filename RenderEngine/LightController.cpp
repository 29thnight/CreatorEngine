#include "LightController.h"
#include "Camera.h"
#include "Texture.h"


LightController::~LightController() = default;

// ★ DX11 상수 버퍼 셋(광원·광원 수·그림자)을 걷었다 (D4, 2026-08-08).
//
//   Initialize가 셋을 만들고 Update가 매 프레임 UpdateSubresource로 채웠는데,
//   그것을 셰이더에 거는 코드가 0이었다. 광원을 읽는 쪽은 DX12 라이브이고
//   m_lightProperties(CPU)를 프레임마다 밀봉해 간다 - 헤더 주석이 이미
//   "여기 남은 것은 광원 목록과 그 속성뿐"이라고 적어 두었고, 그 문장이
//   버퍼 셋에는 적용되지 않은 채 남아 있었다.

void LightController::Initialize()
{
	hasLightWithShadows = false;
}

void LightController::Update()
{
	// 값은 m_lightProperties에 이미 들어 있다. GPU로 올리는 것은 그리는 쪽의 몫이다.
	m_lightCountStruct.m_lightCount = m_lightCount;
}

Light& LightController::GetLight(uint32 index)
{
	if (index >= MAX_LIGHTS)
	{
		throw std::out_of_range("Light index out of range");
	}
    else
    {
		return m_lightProperties.m_lights[index];
    }
}

Mathf::Vector4 LightController::GetEyePosition()
{
	return m_lightProperties.m_eyePosition;
}

LightController& LightController::AddLight(Light& light)
{
	if (m_lightCount < MAX_LIGHTS)
	{
		light.m_lightStatus = LightStatus::Enabled;
		m_lightProperties.m_lights[m_lightCount] = light;
		m_lightCount++;

		return *this;
	}
	else
	{
		throw std::out_of_range("Light index out of range");
	}
}

LightController& LightController::SetGlobalAmbient(Mathf::Color4 color)
{
	m_lightProperties.m_globalAmbient = color;
	return *this;
}

LightController& LightController::SetEyePosition(Mathf::xVector eyePosition)
{
	m_lightProperties.m_eyePosition = eyePosition;
	return *this;
}

void LightController::SetLightWithShadows(uint32 index, ShadowMapRenderDesc& desc)
{
	Light& light = GetLight(index);
	if (light.m_lightType != LightType::DirectionalLight)
	{
		throw std::invalid_argument("Only directional lights can cast shadows");
	}

	if (hasLightWithShadows)
	{
		throw std::invalid_argument("Only one light can cast shadows");
	}

	light.m_lightStatus = LightStatus::StaticShadows;

	Camera shadowCamera(true);
	shadowCamera.m_eyePosition		= desc.m_eyePosition;
	shadowCamera.m_lookAt			= desc.m_lookAt;
	shadowCamera.m_nearPlane		= desc.m_nearPlane;
	shadowCamera.m_farPlane			= desc.m_farPlane;
	shadowCamera.m_viewWidth		= desc.m_viewWidth;
	shadowCamera.m_viewHeight		= desc.m_viewHeight;
	shadowCamera.m_isOrthographic	= true;
	
	m_shadowMapConstant = {
		desc.m_textureWidth,
		desc.m_textureHeight,
		shadowCamera.CalculateView() * shadowCamera.CalculateProjection()
	};
	m_shadowMapRenderDesc = desc;
	// ★ 여기서 만들던 DX11 상수 버퍼도 걷었다 (D4). 값은 m_shadowMapConstant에
	//   남고, 그것을 쓰는 것은 DX12 EnhancedShadowPass다.

	hasLightWithShadows = true;
}
