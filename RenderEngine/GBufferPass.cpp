#include "GBufferPass.h"
#include "AssetSystem.h"
#include "Material.h"
#include "Skeleton.h"
#include "Scene.h"
#include "Mesh.h"

GBufferPass::GBufferPass()
{
	m_pso = std::make_unique<PipelineStateObject>();

	m_pso->m_vertexShader = &AssetsSystems->VertexShaders["VertexShader"];
	m_pso->m_pixelShader = &AssetsSystems->PixelShaders["GBuffer"];

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
			ARRAYSIZE(vertexLayoutDesc),
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

	m_materialBuffer = DirectX11::CreateBuffer(sizeof(MaterialInfomation), D3D11_BIND_CONSTANT_BUFFER, nullptr);
	m_boneBuffer = DirectX11::CreateBuffer(sizeof(Mathf::xMatrix) * Skeleton::MAX_BONES, D3D11_BIND_CONSTANT_BUFFER, nullptr);
}

GBufferPass::~GBufferPass()
{
	//TODO: default 로 변경할 것
}

void GBufferPass::SetRenderTargetViews(ID3D11RenderTargetView** renderTargetViews, uint32 size)
{
	for (uint32 i = 0; i < size; i++)
	{
		m_renderTargetViews[i] = renderTargetViews[i];
	}
}

void GBufferPass::Execute(Scene& scene)
{
	m_pso->Apply();

	auto& deviceContext = DeviceState::g_pDeviceContext;
	for (auto& RTV : m_renderTargetViews)
	{
		deviceContext->ClearRenderTargetView(RTV, Colors::Transparent);
	}

	deviceContext->OMSetRenderTargets(RTV_TypeMax, m_renderTargetViews, DeviceState::g_pDepthStencilView);

	scene.UseCamera(scene.m_MainCamera);
	DirectX11::PSSetConstantBuffer(1, 1, &scene.m_LightController.m_pLightBuffer);
	scene.UseModel();

	DirectX11::VSSetConstantBuffer(3, 1, m_materialBuffer.GetAddressOf());
	DirectX11::VSSetConstantBuffer(0, 1, m_boneBuffer.GetAddressOf());

	Animator* currentAnimator = nullptr;

	for (auto& sceneObject : scene.m_SceneObjects)
	{
		if (!sceneObject->m_meshRenderer.m_IsEnabled) continue;

		MeshRenderer& meshRenderer = sceneObject->m_meshRenderer;
		scene.UpdateModel(sceneObject->m_transform.GetWorldMatrix());
		Animator* animator = meshRenderer.m_Animator;
		if (nullptr != animator && animator->m_IsEnabled)
		{
			if (animator != currentAnimator)
			{
				DirectX11::UpdateBuffer(m_boneBuffer.Get(), animator->m_FinalTransforms);
				currentAnimator = animator;
			}
		}

		Material* mat = meshRenderer.m_Material;
		DirectX11::UpdateBuffer(m_materialBuffer.Get(), &mat->m_materialInfo);

		if (mat->m_pBaseColor)
		{
			DirectX11::PSSetShaderResources(0, 1, &mat->m_pBaseColor->m_pSRV);
		}
		if (mat->m_pNormal)
		{
			DirectX11::PSSetShaderResources(1, 1, &mat->m_pNormal->m_pSRV);
		}
		if (mat->m_pOccRoughMetal)
		{
			DirectX11::PSSetShaderResources(2, 1, &mat->m_pOccRoughMetal->m_pSRV);
		}
		if (mat->m_AOMap)
		{
			DirectX11::PSSetShaderResources(3, 1, &mat->m_AOMap->m_pSRV);
		}
		if (mat->m_pEmissive)
		{
			DirectX11::PSSetShaderResources(4, 1, &mat->m_pEmissive->m_pSRV);
		}

		meshRenderer.m_Mesh->Draw();
	}

	ID3D11ShaderResourceView* nullSRV = nullptr;
	for (uint32 i = 0; i < 5; i++)
	{
		DirectX11::PSSetShaderResources(i, 1, &nullSRV);
	}

	ID3D11RenderTargetView* nullRTV[RTV_TypeMax]{};
	ZeroMemory(nullRTV, sizeof(nullRTV));
	deviceContext->OMSetRenderTargets(RTV_TypeMax, nullRTV, nullptr);
}
