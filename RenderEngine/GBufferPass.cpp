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
#include "RenderCommand.h"
#include "Terrain.h"

ID3D11ShaderResourceView* nullSRVs[5]{
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr
};

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

	for (uint32 i = 0; i < MAX_BONES; i++)
	{
		InitialMatrix[i] = XMMatrixIdentity();
	}
}

GBufferPass::~GBufferPass()
{
	for (auto& [frame, cmdArr] : m_commandQueueMap)
	{
		for (auto& queue : cmdArr)
		{
			while (!queue.empty())
			{
				ID3D11CommandList* CommandJob;
				if (queue.try_pop(CommandJob))
				{
					Memory::SafeDelete(CommandJob);
				}
			}
		}
	}
}

void GBufferPass::SetRenderTargetViews(ID3D11RenderTargetView** renderTargetViews, uint32 size)
{
	for (uint32 i = 0; i < size; i++)
	{
		m_renderTargetViews[i] = renderTargetViews[i];
	}
}

void GBufferPass::Execute(RenderScene& scene, Camera& camera)
{
	auto cmdQueuePtr = GetCommandQueue(camera.m_cameraIndex);

	if (nullptr != cmdQueuePtr)
	{
		while (!cmdQueuePtr->empty())
		{
			ID3D11CommandList* CommandJob;
			if (cmdQueuePtr->try_pop(CommandJob))
			{
				DirectX11::ExecuteCommandList(CommandJob, true);
				Memory::SafeDelete(CommandJob);
			}
		}
	}
}

void GBufferPass::CreateRenderCommandList(ID3D11DeviceContext* defferdContext, RenderScene& scene, Camera& camera)
{
	if (!RenderPassData::VaildCheck(&camera)) return;
	auto data = RenderPassData::GetData(&camera);

	ID3D11DeviceContext* defferdPtr = defferdContext;

	camera.UpdateBuffer(defferdPtr);
	scene.UseModel(defferdPtr);
	DirectX11::RSSetViewports(defferdPtr, 1, &DeviceState::g_Viewport);
	DirectX11::VSSetConstantBuffer(defferdPtr, 3, 1, m_boneBuffer.GetAddressOf());
	DirectX11::PSSetConstantBuffer(defferdPtr, 1, 1, &scene.m_LightController->m_pLightBuffer);
	DirectX11::PSSetConstantBuffer(defferdPtr, 0, 1, m_materialBuffer.GetAddressOf());

	if (0 == data->m_deferredQueue.size())
	{
		std::cout << camera.m_cameraIndex << " queue Size Zero" << std::endl;
	}
	
	HashedGuid currentAnimatorGuid{};
	HashedGuid currentMaterialGuid{};
	//TODO : Change deferredContext Render
	for (auto& MeshRendererProxy : data->m_deferredQueue)
	{
		scene.UpdateModel(MeshRendererProxy->m_worldMatrix, defferdPtr);

		HashedGuid animatorGuid = MeshRendererProxy->m_animatorGuid;
		if (MeshRendererProxy->m_isAnimationEnabled && HashedGuid::INVAILD_ID != animatorGuid)
		{
			if (animatorGuid != currentAnimatorGuid)
			{
				DirectX11::UpdateBuffer(defferdPtr, m_boneBuffer.Get(), MeshRendererProxy->m_finalTransforms);
				currentAnimatorGuid = MeshRendererProxy->m_animatorGuid;
			}
		}

		HashedGuid materialGuid = MeshRendererProxy->m_materialGuid;
		if (HashedGuid::INVAILD_ID != materialGuid && materialGuid != currentMaterialGuid)
		{
			Material* mat = MeshRendererProxy->m_Material;
			DirectX11::UpdateBuffer(defferdPtr, m_materialBuffer.Get(), &mat->m_materialInfo);

			if (mat->m_pBaseColor)
			{
				DirectX11::PSSetShaderResources(defferdPtr, 0, 1, &mat->m_pBaseColor->m_pSRV);
			}
			if (mat->m_pNormal)
			{
				DirectX11::PSSetShaderResources(defferdPtr, 1, 1, &mat->m_pNormal->m_pSRV);
			}
			if (mat->m_pOccRoughMetal)
			{
				DirectX11::PSSetShaderResources(defferdPtr, 2, 1, &mat->m_pOccRoughMetal->m_pSRV);
			}
			if (mat->m_AOMap)
			{
				DirectX11::PSSetShaderResources(defferdPtr, 3, 1, &mat->m_AOMap->m_pSRV);
			}
			if (mat->m_pEmissive)
			{
				DirectX11::PSSetShaderResources(defferdPtr, 5, 1, &mat->m_pEmissive->m_pSRV);
			}
		}

		MeshRendererProxy->Draw(defferdPtr);
	}

	DirectX11::PSSetShaderResources(defferdPtr, 0, 5, nullSRVs);

	ID3D11RenderTargetView* nullRTV[RTV_TypeMax]{};
	ZeroMemory(nullRTV, sizeof(nullRTV));
	defferdPtr->OMSetRenderTargets(RTV_TypeMax, nullRTV, nullptr);

	ID3D11CommandList* commandList{};
	defferdPtr->FinishCommandList(false, &commandList);
	PushQueue(camera.m_cameraIndex, commandList);
}

void GBufferPass::TerrainRenderCommandList(ID3D11DeviceContext* defferdContext, RenderScene& scene, Camera& camera)
{
	if (!RenderPassData::VaildCheck(&camera)) return;
	auto data = RenderPassData::GetData(&camera);

	ID3D11DeviceContext* defferdPtr = defferdContext;
	m_pso->Apply(defferdPtr);

	for (auto& RTV : m_renderTargetViews)
	{
		defferdPtr->ClearRenderTargetView(RTV, Colors::Transparent);
	}

	DirectX11::OMSetRenderTargets(defferdPtr, RTV_TypeMax, m_renderTargetViews, data->m_depthStencil->m_pDSV);

	camera.UpdateBuffer(defferdPtr);
	scene.UseModel(defferdPtr);
	DirectX11::RSSetViewports(defferdPtr, 1, &DeviceState::g_Viewport);
	DirectX11::VSSetConstantBuffer(defferdPtr, 3, 1, m_boneBuffer.GetAddressOf());
	DirectX11::PSSetConstantBuffer(defferdPtr, 1, 1, &scene.m_LightController->m_pLightBuffer);
	DirectX11::PSSetConstantBuffer(defferdPtr, 0, 1, m_materialBuffer.GetAddressOf());

	for (auto& obj : scene.GetScene()->m_SceneObjects) {
		if (obj->IsDestroyMark()) continue;
		if (obj->HasComponent<TerrainComponent>()) {

			auto terrain = obj->GetComponent<TerrainComponent>();
			auto terrainMesh = terrain->GetMesh();

			if (terrainMesh)
			{
				DirectX11::PSSetConstantBuffer(defferdPtr, 12, 1, terrain->m_layerBuffer.GetAddressOf());
				scene.UpdateModel(obj->m_transform.GetWorldMatrix(), defferdPtr);

				DirectX11::PSSetShaderResources(defferdPtr, 6, 1, terrain->GetLayerSRV());
				DirectX11::PSSetShaderResources(defferdPtr, 7, 1, terrain->GetSplatMapSRV());
				terrainMesh->Draw(defferdPtr);
			}
		}
	}

	DirectX11::PSSetShaderResources(defferdPtr, 0, 5, nullSRVs);
}

void GBufferPass::Resize(uint32_t width, uint32_t height)
{
}
