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
	m_pso->Apply();

	ID3D11RenderTargetView* view = camera.m_renderTarget->GetRTV();
	DirectX11::OMSetRenderTargets(1, &view, camera.m_depthStencil->m_pDSV);
	DirectX11::OMSetDepthStencilState(DeviceState::g_pDepthStencilState, 1);
	DirectX11::OMSetBlendState(DeviceState::g_pBlendState, nullptr, 0xFFFFFFFF);

	camera.UpdateBuffer();
	DirectX11::PSSetConstantBuffer(1, 1, &scene.m_LightController->m_pLightBuffer);
	scene.UseModel();
	DirectX11::PSSetConstantBuffer(3, 1, m_boneBuffer.GetAddressOf());
	DirectX11::PSSetConstantBuffer(0, 1, m_materialBuffer.GetAddressOf());

	Animator* currentAnimator = nullptr;

	for (auto& meshRenderer : camera.m_forwardQueue)
	{
		if (nullptr == meshRenderer) continue;
		if (!meshRenderer->IsEnabled()) continue;

		GameObject* sceneObject = meshRenderer->GetOwner();
		if (sceneObject->IsDestroyMark()) continue;

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

		meshRenderer->m_Mesh->Draw();
	}

	ID3D11DepthStencilState* nullDepthStencilState = nullptr;
	ID3D11BlendState* nullBlendState = nullptr;

	DirectX11::OMSetDepthStencilState(nullDepthStencilState, 1);
	DirectX11::OMSetBlendState(nullBlendState, nullptr, 0xFFFFFFFF);

	ID3D11ShaderResourceView* nullSRV = nullptr;
	DirectX11::PSSetShaderResources(0, 1, &nullSRV);
	DirectX11::UnbindRenderTargets();
}

void ForwardPass::ControlPanel()
{
}
