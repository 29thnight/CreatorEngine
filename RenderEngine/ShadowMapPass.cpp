#include "ShadowMapPass.h"
#include "AssetSystem.h"
#include "Scene.h"
#include "Mesh.h"
#include "Sampler.h"
#include "Renderer.h"
#include "Light.h"

ShadowMapPass::ShadowMapPass()
{
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
}

void ShadowMapPass::Initialize(uint32 width, uint32 height)
{
	Texture* shadowMapTexture = Texture::Create(width, height, "Shadow Map",
		DXGI_FORMAT_R32_TYPELESS, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
	shadowMapTexture->CreateRTV(DXGI_FORMAT_R32_FLOAT);
	shadowMapTexture->CreateSRV(DXGI_FORMAT_R32_FLOAT);

	m_shadowMapTexture = std::unique_ptr<Texture>(shadowMapTexture);

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
	m_shadowCamera.m_isOrthographic = true;
}

void ShadowMapPass::Execute(RenderScene& scene, Camera& camera)
{
	m_pso->Apply();

	ID3D11RenderTargetView* rtv = m_shadowMapTexture->GetRTV();
	DirectX11::ClearRenderTargetView(rtv, Colors::Transparent);

	DirectX11::OMSetRenderTargets(1, &rtv, m_shadowMapDSV);
	
	auto desc = scene.m_LightController->m_shadowMapRenderDesc;

	m_shadowCamera.m_eyePosition = XMLoadFloat4(&(scene.m_LightController->GetLight(0).m_direction)) * -5.f;
	m_shadowCamera.m_lookAt = desc.m_lookAt;
	m_shadowCamera.m_nearPlane = desc.m_nearPlane;
	m_shadowCamera.m_farPlane = desc.m_farPlane;
	m_shadowCamera.m_viewHeight = desc.m_viewHeight;
	m_shadowCamera.m_viewWidth = desc.m_viewWidth;

	auto& shadowMapConstant = scene.m_LightController->m_shadowMapConstant;

	shadowMapConstant.m_shadowMapWidth = desc.m_textureWidth;
	shadowMapConstant.m_shadowMapHeight = desc.m_textureHeight;
	shadowMapConstant.m_lightViewProjection = m_shadowCamera.CalculateView() * m_shadowCamera.CalculateProjection();

	DirectX11::UpdateBuffer(scene.m_LightController->m_shadowMapBuffer, &shadowMapConstant);

	m_shadowCamera.UpdateBuffer();
	scene.UseModel();

	for (auto& obj : scene.GetScene()->m_SceneObjects)
	{
		MeshRenderer* meshRenderer = obj->GetComponent<MeshRenderer>();
		if (nullptr == meshRenderer) continue;
		if (!meshRenderer->IsEnabled()) continue;

		scene.UpdateModel(obj->m_transform.GetWorldMatrix());
		meshRenderer->m_Mesh->Draw();
	}

	//DirectX11::ClearRenderTargetView(rtv, Colors::Transparent);
	DirectX11::ClearDepthStencilView(m_shadowMapDSV, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
	DirectX11::UnbindRenderTargets();
}

void ShadowMapPass::ControlPanel()
{
    ImGui::Image((ImTextureID)m_shadowMapTexture->m_pSRV, ImVec2(512, 512));
}
