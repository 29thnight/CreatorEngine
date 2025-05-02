#include "GBufferPass.h"
#include "ShaderSystem.h"
#include "Material.h"
#include "Skeleton.h"
#include "Scene.h"
#include "RenderableComponents.h"
#include "Mesh.h"
#include "LightController.h"
#include "LightProperty.h"
#include "Benchmark.hpp"

GBufferPass::GBufferPass()
{
	m_pso = std::make_unique<PipelineStateObject>();

	m_pso->m_vertexShader = &ShaderSystem->VertexShaders["VertexShader"];
	m_pso->m_pixelShader = &ShaderSystem->PixelShaders["GBuffer"];
	m_pso->m_primitiveTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	InputLayOutContainer vertexLayoutDesc = {
		{ "POSITION",     0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "NORMAL",       0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD",     0, DXGI_FORMAT_R32G32_FLOAT,       0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD",     1, DXGI_FORMAT_R32G32_FLOAT,       0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TANGENT",      0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "BINORMAL",     0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "BLENDWEIGHT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};
	m_pso->CreateInputLayout(std::move(vertexLayoutDesc));

	CD3D11_RASTERIZER_DESC rasterizerDesc{ CD3D11_DEFAULT() };

	DirectX11::ThrowIfFailed(
		DeviceState::g_pDevice->CreateRasterizerState(
			&rasterizerDesc,
			&m_pso->m_rasterizerState
		)
	);

	m_pso->m_depthStencilState = DeviceState::g_pDepthStencilState;

	auto linearSampler = std::make_shared<Sampler>(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_WRAP);
	auto pointSampler = std::make_shared<Sampler>(D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP);

	m_pso->m_samplers.push_back(linearSampler);
	m_pso->m_samplers.push_back(pointSampler);

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

void GBufferPass::SetEditorRenderTargetViews(ID3D11RenderTargetView** renderTargetViews, uint32 size)
{
	for (uint32 i = 0; i < size; i++)
	{
		m_editorRTV[i] = renderTargetViews[i];
	}
}

void GBufferPass::Execute(RenderScene& scene, Camera& camera)
{
	m_pso->Apply();

	auto& deviceContext = DeviceState::g_pDeviceContext;

	for (auto& RTV : m_renderTargetViews)
	{
		deviceContext->ClearRenderTargetView(RTV, Colors::Transparent);
	}

	deviceContext->OMSetRenderTargets(RTV_TypeMax, m_renderTargetViews, camera.m_depthStencil->m_pDSV);

	camera.UpdateBuffer();
	DirectX11::PSSetConstantBuffer(1, 1, &scene.m_LightController->m_pLightBuffer);
	scene.UseModel();

	DirectX11::VSSetConstantBuffer(3, 1, m_boneBuffer.GetAddressOf());
	DirectX11::PSSetConstantBuffer(0, 1, m_materialBuffer.GetAddressOf());

	Animator* currentAnimator = nullptr;

	for (auto& sceneObject : m_deferredQueue)
	{		
		MeshRenderer* meshRenderer = sceneObject->GetComponent<MeshRenderer>();
		if (nullptr == meshRenderer) continue;
		if (!meshRenderer->IsEnabled()) continue;

		scene.UpdateModel(sceneObject->m_transform.GetWorldMatrix());
		Animator* animator = scene.GetScene()->m_SceneObjects[sceneObject->m_parentIndex]->GetComponent<Animator>();
		if (nullptr != animator && animator->IsEnabled())
		{
			if (animator != currentAnimator)
			{
				DirectX11::UpdateBuffer(m_boneBuffer.Get(), animator->m_FinalTransforms);
				currentAnimator = animator;
			}
		}

		Material* mat = meshRenderer->m_Material;
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
			DirectX11::PSSetShaderResources(5, 1, &mat->m_pEmissive->m_pSRV);
		}
		//36 skinedMesh Draw -> FrameDrop : 형편없구만
		meshRenderer->m_Mesh->Draw();
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

void GBufferPass::PushDeferredQueue(GameObject* sceneObject)
{
	if (nullptr == sceneObject) return;
	m_deferredQueue.push_back(sceneObject);
}

void GBufferPass::ClearDeferredQueue()
{
	m_deferredQueue.clear();
}

void GBufferPass::Resize()
{
}
