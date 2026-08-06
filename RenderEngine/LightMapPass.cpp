#include "LightMapPass.h"
#include "Animator.h"
#include "RHI/RHI.h"
#include "ShaderSystem.h"
#include "Material.h"
#include "Skeleton.h"
#include "GameObject.h"
#include "Scene.h"
#include "Mesh.h"
#include "LightController.h"

struct alignas(16) CB {
	XMFLOAT2 offset{ 0,0 };
	XMFLOAT2 size{ 0,0 };
	XMFLOAT3 cameraPos{ 0,0,0 };
	int lightmapIndex = -1;
};

LightMapPass::LightMapPass()
{
	m_pso = std::make_unique<PipelineStateObject>();
	m_pso->m_vertexShader = &ShaderSystem->VertexShaders["VertexShader"];
	m_pso->m_pixelShader = &ShaderSystem->PixelShaders["Lightmap"];

	D3D11_INPUT_ELEMENT_DESC vertexLayoutDesc[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "BINORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "BLENDWEIGHT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};

	DirectX11::ThrowIfFailed(
		DirectX11::DeviceStates->g_pDevice->CreateInputLayout(
			vertexLayoutDesc,
			_countof(vertexLayoutDesc),
			m_pso->m_vertexShader->GetBufferPointer(),
			m_pso->m_vertexShader->GetBufferSize(),
			&m_pso->m_inputLayout
		)
	);

	CD3D11_RASTERIZER_DESC rasterizerDesc{ CD3D11_DEFAULT() };

	DirectX11::ThrowIfFailed(
		DirectX11::DeviceStates->g_pDevice->CreateRasterizerState(
			&rasterizerDesc,
			&m_pso->m_rasterizerState
		)
	);

	m_pso->m_depthStencilState = DirectX11::DeviceStates->g_pDepthStencilState;

	auto linearSampler = std::make_shared<Sampler>(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_WRAP);
	auto pointSampler = std::make_shared<Sampler>(D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP);

	m_pso->m_samplers.push_back(linearSampler);
	m_pso->m_samplers.push_back(pointSampler);
	m_materialBuffer = DirectX11::CreateBuffer(sizeof(MaterialInfomation), D3D11_BIND_CONSTANT_BUFFER, nullptr);
	m_boneBuffer = DirectX11::CreateBuffer(sizeof(Mathf::xMatrix) * Skeleton::MAX_BONES, D3D11_BIND_CONSTANT_BUFFER, nullptr);
	m_cbuffer = DirectX11::CreateBuffer(sizeof(CB), D3D11_BIND_CONSTANT_BUFFER, nullptr);
	DirectX::SetName(m_cbuffer.Get(), "bind lightmapping data");
	DirectX::SetName(m_materialBuffer.Get(), "materialData");
}

void LightMapPass::Initialize(std::vector<Texture*>& lightmaps, std::vector<Texture*>& directionalmaps)
{
	m_plightmaps = &lightmaps;
	m_pDirectionalMaps = &directionalmaps;
}

void LightMapPass::Execute(RenderScene& scene, Camera& camera)
{
	ExecuteCommandList(scene, camera);
}

void LightMapPass::CreateRenderCommandList(RHICommandContext& context, RenderScene& scene, Camera& camera)
{
	if (!RenderPassData::VaildCheck(&camera)) return;
	auto renderData = RenderPassData::GetData(&camera);

	ID3D11DeviceContext* deferredPtr = static_cast<ID3D11DeviceContext*>(context.GetNativeHandle()); // 전환기 탈출구(잔존 네이티브 경로용)

	m_pso->Apply(deferredPtr);

	//DirectX11::ClearRenderTargetView(camera.m_renderTarget->GetRTV(), Colors::White);
	context.ClearDepthStencil(renderData->m_depthStencil->m_pDSV, true, false, 1.0f, 0);
	ID3D11RenderTargetView* rtv = renderData->m_renderTarget->GetRTV();
	context.SetRenderTarget(rtv, renderData->m_depthStencil->m_pDSV); //������ ��� ���ϸ� ����Ʈ���� �������� ��� �� �ڿ� ��ü�� ���̰�, ����ϸ� ����Ʈ���� �ȳ�����

	renderData->BindFrameCameraBuffers(context);
	scene.UseModel(deferredPtr);

	Animator* currentAnimator = nullptr;
	Scene* activeScene = scene.GetScene();
	for (auto& renderer : activeScene->GetMeshRenderers())
	{
		if (!renderer || !renderer->IsEnabled()) continue;

		CB buf{};
		buf.offset = renderer->m_LightMapping.lightmapOffset;
		buf.size = renderer->m_LightMapping.lightmapTiling;
		buf.cameraPos = XMFLOAT3(renderData->GetFrameSnapshot().eyePosition.m128_f32[0], renderData->GetFrameSnapshot().eyePosition.m128_f32[1], renderData->GetFrameSnapshot().eyePosition.m128_f32[2]);
		buf.lightmapIndex = renderer->m_LightMapping.lightmapIndex;
		context.UpdateBuffer(m_cbuffer.Get(), &buf);
		context.SetPixelShaderConstantBuffer(1, m_cbuffer.Get());

		auto obj = renderer->GetOwner();
		scene.UpdateModel(obj->m_transform.GetWorldMatrix(), deferredPtr);
		Animator* animator = scene.GetScene()->m_SceneObjects[obj->m_parentIndex]->GetComponent<Animator>();
		if (nullptr != animator && animator->IsEnabled())
		{
			if (animator != currentAnimator)
			{
				context.UpdateBuffer(m_boneBuffer.Get(), animator->m_FinalTransforms);
				currentAnimator = animator;
			}
		}

		Material* mat = renderer->m_Material.get();
		context.UpdateBuffer(m_materialBuffer.Get(), &mat->m_materialInfo);
		context.SetPixelShaderConstantBuffer(0, m_materialBuffer.Get());

		if (renderer->m_LightMapping.lightmapIndex >= 0) // �Ǵ� ����Ʈ���� �����ǰ� �ִٸ� ����ϴ� �������
		{
			if (m_plightmaps != nullptr && (*m_plightmaps).size() > renderer->m_LightMapping.lightmapIndex)
				context.SetPixelShaderResource(14, (*m_plightmaps)[renderer->m_LightMapping.lightmapIndex]->m_pSRV);
			if (m_pDirectionalMaps != nullptr && (*m_pDirectionalMaps).size() > renderer->m_LightMapping.lightmapIndex)
				context.SetPixelShaderResource(15, (*m_pDirectionalMaps)[renderer->m_LightMapping.lightmapIndex]->m_pSRV);
		}

		if (mat->m_pBaseColor)
		{
			context.SetPixelShaderResource(0, mat->m_pBaseColor->m_pSRV);
		}
		if (mat->m_pNormal)
		{
			context.SetPixelShaderResource(1, mat->m_pNormal->m_pSRV);
		}
		if (mat->m_pOccRoughMetal)
		{
			context.SetPixelShaderResource(2, mat->m_pOccRoughMetal->m_pSRV);
		}
		if (mat->m_AOMap)
		{
			context.SetPixelShaderResource(3, mat->m_AOMap->m_pSRV);
		}
		if (mat->m_pEmissive)
		{
			context.SetPixelShaderResource(5, mat->m_pEmissive->m_pSRV);
		}

		renderer->m_Mesh->Draw(deferredPtr);
	}

	ID3D11CommandList* commandList{};
	deferredPtr->FinishCommandList(false, &commandList);
	PushQueue(camera.m_cameraIndex, commandList);
}


void LightMapPass::Resize(uint32_t width, uint32_t height)
{
}
