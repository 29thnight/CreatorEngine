#include "ShadowMapPass.h"
#include "AssetSystem.h"
#include "Scene.h"
#include "Mesh.h"
#include "Sampler.h"

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

	m_pso->m_samplers.emplace_back(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_WRAP);
	m_pso->m_samplers.emplace_back(D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP);
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
}

void ShadowMapPass::Execute(Scene& scene)
{
	m_pso->Apply();

	ID3D11RenderTargetView* rtv = m_shadowMapTexture->GetRTV();
	DirectX11::OMSetRenderTargets(1, &rtv, m_shadowMapDSV);

	auto desc = scene.m_LightController.m_shadowMapRenderDesc;

	OrthographicCamera shadowMapCamera;
	shadowMapCamera.m_eyePosition = desc.m_eyePosition;
	shadowMapCamera.m_lookAt = desc.m_lookAt;
	shadowMapCamera.m_nearPlane = desc.m_nearPlane;
	shadowMapCamera.m_farPlane = desc.m_farPlane;
	shadowMapCamera.m_viewHeight = desc.m_viewHeight;
	shadowMapCamera.m_viewWidth = desc.m_viewWidth;

	scene.UseCamera(shadowMapCamera);
	scene.UseModel();

	for (auto& obj : scene.m_SceneObjects)
	{
		if (!obj->m_meshRenderer.m_IsEnabled) continue;

		scene.UpdateModel(obj->m_transform.GetWorldMatrix());
		obj->m_meshRenderer.m_Mesh->Draw();
	}
}
