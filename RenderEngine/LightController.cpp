#include "LightController.h"
#include "DeviceState.h"
#include "Camera.h"
#include "ShadowMapPass.h"
#include "Texture.h"


LightController::~LightController()
{
}

void LightController::Initialize()
{
    CD3D11_BUFFER_DESC bufferDesc{
        sizeof(LightProperties),
        D3D11_BIND_CONSTANT_BUFFER
    };

	DirectX11::ThrowIfFailed(
        DeviceState::g_pDevice->CreateBuffer(
            &bufferDesc, 
            nullptr, 
            &m_pLightBuffer
        )
    );

    DirectX::SetName(m_pLightBuffer, "Light Buffer");

    CD3D11_BUFFER_DESC counterDesc{
        sizeof(LightCount),
        D3D11_BIND_CONSTANT_BUFFER
    };

    DirectX11::ThrowIfFailed(
        DeviceState::g_pDevice->CreateBuffer(
            &counterDesc, 
            nullptr, 
            &m_pLightCountBuffer
        )
    );

    DirectX::SetName(m_pLightCountBuffer, "Light Count Buffer");


	m_shadowMapPass = std::make_unique<ShadowMapPass>();
}

void LightController::Update()
{
	DeviceState::g_pDeviceContext->UpdateSubresource(
		m_pLightBuffer,
		0,
		nullptr,
		&m_lightProperties,
		0,
		0
	);

    m_lightCountStruct.m_lightCount = m_lightCount;
    DeviceState::g_pDeviceContext->UpdateSubresource(
        m_pLightCountBuffer,
        0,
        nullptr,
        &m_lightCountStruct,
        0,
        0
    );
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
	m_shadowMapBuffer = DirectX11::CreateBuffer(sizeof(ShadowMapConstant), D3D11_BIND_CONSTANT_BUFFER, &m_shadowMapConstant);

	m_shadowMapPass->Initialize(desc.m_textureWidth, desc.m_textureHeight);
	hasLightWithShadows = true;
}

void LightController::RenderAnyShadowMap(RenderScene& scene, Camera& camera)
{
	if (hasLightWithShadows && true == camera.m_applyRenderPipelinePass.m_ShadowPass)
	{
		m_shadowMapPass->Execute(scene, camera);
	}
}

void LightController::CreateShadowCommandList(ID3D11DeviceContext* deferredContext, RenderScene& scene, Camera& camera)
{
	if (hasLightWithShadows && true == camera.m_applyRenderPipelinePass.m_ShadowPass)
	{
		m_shadowMapPass->CreateRenderCommandList(deferredContext, scene, camera);
	}
}

void LightController::UseCloudShadowMap(const std::string_view& filename)
{
	m_shadowMapPass->UseCloudShadowMap(filename);
}

void LightController::UpdateCloudBuffer(ID3D11DeviceContext* deferredContext)
{
	m_shadowMapPass->UpdateCloudBuffer(defferdContext, this);
}

void LightController::PSBindCloudShadowMap(ID3D11DeviceContext* defferdContext, bool isOn) 
{
	m_shadowMapPass->PSBindCloudShadowMap(defferdContext, this, isOn);
}

void LightController::CSBindCloudShadowMap(ID3D11DeviceContext* defferdContext, bool isOn)
{
	m_shadowMapPass->CSBindCloudShadowMap(defferdContext, this, isOn);
}

//Texture* LightController::GetShadowMapTexture()
//{
//	return m_shadowMapPass->m_shadowMapTexture.get();
//}
