#include "LightmapShadowPass.h"
#include "AssetSystem.h"
#include "Scene.h"
#include "Mesh.h"
#include "Light.h"
#include "Sampler.h"
#include "Renderer.h"

LightmapShadowPass::LightmapShadowPass() {
	m_pso = std::make_unique<PipelineStateObject>();

	m_pso->m_vertexShader = &AssetsSystems->VertexShaders["VertexShader"];
	m_pso->m_pixelShader = &AssetsSystems->PixelShaders["ShadowMap"];

	D3D11_INPUT_ELEMENT_DESC vertexLayoutDesc[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "BINORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "BLENDWEIGHT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};

	DirectX11::ThrowIfFailed(
		DeviceState::g_pDevice->CreateInputLayout(
			vertexLayoutDesc,
			_countof(vertexLayoutDesc),
			m_pso->m_vertexShader->GetBufferPointer(),
			m_pso->m_vertexShader->GetBufferSize(),
			&m_pso->m_inputLayout
		)
	);

	CD3D11_RASTERIZER_DESC rasterizerDesc{ CD3D11_DEFAULT() };

	DirectX11::ThrowIfFailed(
		DeviceState::g_pDevice->CreateRasterizerState(
			&rasterizerDesc,
			&m_pso->m_rasterizerState
		)
	);

	auto linearSampler = std::make_shared<Sampler>(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_WRAP);
	auto pointSampler = std::make_shared<Sampler>(D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP);

	m_pso->m_samplers.push_back(linearSampler);
	m_pso->m_samplers.push_back(pointSampler);

	m_shadowmapTextures.reserve(MAX_LIGHTS);

	for (int i = 0; i < MAX_LIGHTS; i++) {
		CreateShadowMap(shadowmapSize, shadowmapSize);
	}
}

LightmapShadowPass::~LightmapShadowPass()
{
	ClearShadowMap();
}

void LightmapShadowPass::Initialize(uint32 width, uint32 height)
{
}

void LightmapShadowPass::Execute(RenderScene& scene, Camera& camera)
{
	m_pso->Apply();

	auto buffer = DirectX11::CreateBuffer(sizeof(ShadowMapConstant), D3D11_BIND_CONSTANT_BUFFER, nullptr);
	scene.UseModel();
	for (int i = 0; i < MAX_LIGHTS; i++) {

		auto& light = scene.m_LightController->GetLight(i);


		// 비활성화된 라이트는 굽지 않음.
		if (light.m_lightStatus == LightStatus::Disabled || light.m_lightType != LightType::DirectionalLight)
			continue;

		ID3D11RenderTargetView* rtv = m_shadowmapTextures[i]->GetRTV();

		DirectX11::OMSetRenderTargets(1, &rtv, m_shadowMapDSV);

		// shadowMap 정의
		ShadowMapRenderDesc desc;
		desc.m_eyePosition = XMLoadFloat4(&(light.m_direction)) * -10.f;
		desc.m_lookAt = XMVectorSet(0, 0, 0, 1);
		desc.m_viewWidth = 32;
		desc.m_viewHeight = 32;
		desc.m_nearPlane = 0.1f;
		desc.m_farPlane = 200.f;
		desc.m_textureWidth = shadowmapSize;
		desc.m_textureHeight = shadowmapSize;

		m_shadowCamera.m_eyePosition = desc.m_eyePosition;
		m_shadowCamera.m_lookAt = desc.m_lookAt;
		m_shadowCamera.m_nearPlane = desc.m_nearPlane;
		m_shadowCamera.m_farPlane = desc.m_farPlane;
		m_shadowCamera.m_viewHeight = desc.m_viewHeight;
		m_shadowCamera.m_viewWidth = desc.m_viewWidth;

		m_shadowCamera.m_isOrthographic = true;

		ShadowMapConstant m_shadowMapConstant = {
			desc.m_textureWidth,
			desc.m_textureHeight,
			m_shadowCamera.CalculateView() * m_shadowCamera.CalculateProjection()
		};
		DirectX11::UpdateBuffer(buffer, &m_shadowMapConstant);

		m_shadowCamera.UpdateBuffer();

		// shadowMap에 Draw
		for (auto& obj : scene.GetScene()->m_SceneObjects)
		{
			auto* renderer = obj->GetComponent<MeshRenderer>();
			if (renderer == nullptr) continue;
			if (!renderer->IsEnabled()) continue;

			scene.UpdateModel(obj->m_transform.GetWorldMatrix());
			renderer->m_Mesh->Draw();
		}

		DirectX11::ClearDepthStencilView(m_shadowMapDSV, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
	}
	DirectX11::UnbindRenderTargets();
	//ClearShadowMap();
}

void LightmapShadowPass::CreateShadowMap(uint32 width, uint32 height)
{
	Texture* m_shadowMapTexture = Texture::Create(width, height, "Lightmap Shadow Map",
		DXGI_FORMAT_R32_TYPELESS, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
	m_shadowMapTexture->CreateRTV(DXGI_FORMAT_R32_FLOAT);
	m_shadowMapTexture->CreateSRV(DXGI_FORMAT_R32_FLOAT);

	CD3D11_TEXTURE2D_DESC1 depthStencilDesc(
		DXGI_FORMAT_R24G8_TYPELESS,
		width,
		height,
		1,
		1,
		D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE
	);

	ComPtr<ID3D11Texture2D1> depthStencil;
	DirectX11::ThrowIfFailed(
		DeviceState::g_pDevice->CreateTexture2D1(
			&depthStencilDesc,
			nullptr,
			&depthStencil
		)
	);

	CD3D11_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc(D3D11_DSV_DIMENSION_TEXTURE2D);
	depthStencilViewDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;

	DirectX11::ThrowIfFailed(
		DeviceState::g_pDevice->CreateDepthStencilView(
			depthStencil.Get(),
			&depthStencilViewDesc,
			&m_shadowMapDSV
		)
	);



	m_shadowmapTextures.push_back(m_shadowMapTexture);
}

void LightmapShadowPass::ClearShadowMap()
{
	//std::cout << "Lightmap Shadow Clear" << std::endl;
	for (auto& tex : m_shadowmapTextures) {
		DirectX11::ClearRenderTargetView(tex->GetRTV(), Colors::Transparent);
	}
}

/*
 - 권용우
라이트맵에 사용할 shadowMap.

enable된 directional light만 베이킹.

추가 개선 가능 여부

*/