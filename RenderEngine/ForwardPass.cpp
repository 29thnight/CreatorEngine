#include "ForwardPass.h"
#include "ShaderSystem.h"
#include "Material.h"
#include "Skeleton.h"
#include "Scene.h"
#include "RenderableComponents.h"
#include "Mesh.h"
#include "LightController.h"
#include "LightProperty.h"
#include "Benchmark.hpp"
#include "MeshRendererProxy.h"

ForwardPass::ForwardPass()
{
	m_pso = std::make_unique<PipelineStateObject>();
	m_pso->m_vertexShader = &ShaderSystem->VertexShaders["VertexShader"];
	m_pso->m_pixelShader = &ShaderSystem->PixelShaders["Forward"];
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

	auto linearSampler = std::make_shared<Sampler>(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_WRAP);
	auto pointSampler = std::make_shared<Sampler>(D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP);
	m_pso->m_samplers.push_back(linearSampler);
	m_pso->m_samplers.push_back(pointSampler);

	m_materialBuffer = DirectX11::CreateBuffer(sizeof(MaterialInfomation), D3D11_BIND_CONSTANT_BUFFER, nullptr);
	m_boneBuffer = DirectX11::CreateBuffer(sizeof(Mathf::xMatrix) * Skeleton::MAX_BONES, D3D11_BIND_CONSTANT_BUFFER, nullptr);
}

ForwardPass::~ForwardPass()
{
}

void ForwardPass::Execute(RenderScene& scene, Camera& camera)
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

void ForwardPass::CreateRenderCommandList(ID3D11DeviceContext* defferdContext, RenderScene& scene, Camera& camera)
{
	if (!RenderPassData::VaildCheck(&camera)) return;
	auto renderData = RenderPassData::GetData(&camera);

	ID3D11DeviceContext* defferdPtr = defferdContext;

	m_pso->Apply(defferdPtr);

	ID3D11RenderTargetView* view = renderData->m_renderTarget->GetRTV();
	DirectX11::OMSetRenderTargets(defferdPtr, 1, &view, renderData->m_depthStencil->m_pDSV);
	DirectX11::RSSetViewports(defferdPtr, 1, &DeviceState::g_Viewport);
	scene.UseModel(defferdPtr);
	DirectX11::OMSetDepthStencilState(defferdPtr, DeviceState::g_pDepthStencilState, 1);
	DirectX11::OMSetBlendState(defferdPtr, DeviceState::g_pBlendState, nullptr, 0xFFFFFFFF);
	DirectX11::PSSetConstantBuffer(defferdPtr, 1, 1, &scene.m_LightController->m_pLightBuffer);
	DirectX11::PSSetConstantBuffer(defferdPtr, 3, 1, m_boneBuffer.GetAddressOf());
	DirectX11::PSSetConstantBuffer(defferdPtr, 0, 1, m_materialBuffer.GetAddressOf());

	camera.UpdateBuffer(defferdPtr);
	HashedGuid currentAnimatorGuid{};
	//TODO : Change deferredContext Render
	for (auto& PrimitiveRenderProxy : renderData->m_forwardQueue)
	{
		scene.UpdateModel(PrimitiveRenderProxy->m_worldMatrix, defferdPtr);

		HashedGuid animatorGuid = PrimitiveRenderProxy->m_animatorGuid;
		if (PrimitiveRenderProxy->m_isAnimationEnabled && HashedGuid::INVAILD_ID != animatorGuid)
		{
			if (animatorGuid != currentAnimatorGuid)
			{
				DirectX11::UpdateBuffer(defferdPtr, m_boneBuffer.Get(), PrimitiveRenderProxy->m_finalTransforms);
				currentAnimatorGuid = PrimitiveRenderProxy->m_animatorGuid;
			}
		}

		Material* mat = PrimitiveRenderProxy->m_Material;
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

		PrimitiveRenderProxy->Draw(defferdPtr);
	}

	ID3D11DepthStencilState* nullDepthStencilState = nullptr;
	ID3D11BlendState* nullBlendState = nullptr;

	DirectX11::OMSetDepthStencilState(defferdPtr, nullDepthStencilState, 1);
	DirectX11::OMSetBlendState(defferdPtr, nullBlendState, nullptr, 0xFFFFFFFF);

	ID3D11ShaderResourceView* nullSRV = nullptr;
	DirectX11::PSSetShaderResources(defferdPtr, 0, 1, &nullSRV);
	DirectX11::UnbindRenderTargets(defferdPtr);

	ID3D11CommandList* commandList{};
	defferdPtr->FinishCommandList(false, &commandList);
	PushQueue(camera.m_cameraIndex, commandList);
}

void ForwardPass::ControlPanel()
{
}
