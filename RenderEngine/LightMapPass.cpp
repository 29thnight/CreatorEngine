#include "LightMapPass.h"
#include "AssetSystem.h"
#include "Material.h"
#include "Skeleton.h"
#include "Scene.h"
#include "Renderer.h"
#include "Mesh.h"
#include "Light.h"
#include "LightProperty.h"

struct alignas(16) CB {
	XMFLOAT2 offset{ 0,0 };
	XMFLOAT2 size{ 0,0 };
	int lightmapIndex = -1;
};

LightMapPass::LightMapPass()
{
	m_pso = std::make_unique<PipelineStateObject>();
	m_pso->m_vertexShader = &AssetsSystems->VertexShaders["VertexShader"];
	m_pso->m_pixelShader = &AssetsSystems->PixelShaders["Lightmap"];

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

	m_pso->m_depthStencilState = DeviceState::g_pDepthStencilState;

	auto linearSampler = std::make_shared<Sampler>(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_WRAP);
	auto pointSampler = std::make_shared<Sampler>(D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP);

	m_pso->m_samplers.push_back(linearSampler);
	m_pso->m_samplers.push_back(pointSampler);
	m_boneBuffer = DirectX11::CreateBuffer(sizeof(Mathf::xMatrix) * Skeleton::MAX_BONES, D3D11_BIND_CONSTANT_BUFFER, nullptr);
	m_cbuffer = DirectX11::CreateBuffer(sizeof(CB), D3D11_BIND_CONSTANT_BUFFER, nullptr);
}

void LightMapPass::Initialize(std::vector<Texture*>& lightmaps)
{
	int size = lightmaps.size();
	D3D11_TEXTURE2D_DESC temp;
	lightmaps[0]->m_pTexture->GetDesc(&temp);

	D3D11_TEXTURE2D_DESC texArrayDesc = {};
	texArrayDesc.Width = temp.Width;
	texArrayDesc.Height = temp.Width;
	texArrayDesc.MipLevels = 1;
	texArrayDesc.ArraySize = size; // N개의 텍스처
	texArrayDesc.Format = temp.Format;
	texArrayDesc.SampleDesc.Count = 1;
	texArrayDesc.Usage = D3D11_USAGE_DEFAULT;
	texArrayDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	texArrayDesc.CPUAccessFlags = 0;

	ID3D11Texture2D* textureArray = nullptr;
	auto hr = DeviceState::g_pDevice->CreateTexture2D(&texArrayDesc, nullptr, &textureArray);

	for (UINT i = 0; i < size; i++) {
		DeviceState::g_pDeviceContext->CopySubresourceRegion(
			textureArray, D3D11CalcSubresource(0, i, 1), 0, 0, 0, // Dest
			lightmaps[i]->m_pTexture, 0, nullptr // Source
		);
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
	srvDesc.Texture2DArray.MostDetailedMip = 0;
	srvDesc.Texture2DArray.MipLevels = 1;
	srvDesc.Texture2DArray.FirstArraySlice = 0;
	srvDesc.Texture2DArray.ArraySize = size;

	ID3D11ShaderResourceView* textureArraySRV = nullptr;
	hr = DeviceState::g_pDevice->CreateShaderResourceView(textureArray, &srvDesc, &textureArraySRV);

	DirectX11::PSSetShaderResources(14, 1, &textureArraySRV); // shadowmap texture array 0
}

void LightMapPass::Execute(RenderScene& scene, Camera& camera)
{
	m_pso->Apply();

	auto& deviceContext = DeviceState::g_pDeviceContext;
	ID3D11RenderTargetView* rtv = camera.m_renderTarget->GetRTV();
	DirectX11::OMSetRenderTargets(1, &rtv, camera.m_depthStencil->m_pDSV);

	camera.UpdateBuffer();
	scene.UseModel();

	Animator* currentAnimator = nullptr;
	for (auto& obj : scene.GetScene()->m_SceneObjects) {
		MeshRenderer* meshRenderer = obj->GetComponent<MeshRenderer>();
		if (nullptr == meshRenderer) continue;
		if (!meshRenderer->IsEnabled()) continue;

		CB buf{};
		buf.offset = meshRenderer->m_LightMapping.lightmapOffset;
		buf.size = meshRenderer->m_LightMapping.lightmapTiling;
		buf.lightmapIndex = meshRenderer->m_LightMapping.lightmapIndex;
		DirectX11::UpdateBuffer(m_cbuffer.Get(), &buf);
		DirectX11::PSSetConstantBuffer(0, 1, m_cbuffer.GetAddressOf());

		scene.UpdateModel(obj->m_transform.GetWorldMatrix());
		Animator* animator = scene.GetScene()->m_SceneObjects[obj->m_parentIndex]->GetComponent<Animator>();
		if (nullptr != animator && animator->IsEnabled())
		{
			if (animator != currentAnimator)
			{
				DirectX11::UpdateBuffer(m_boneBuffer.Get(), animator->m_FinalTransforms);
				currentAnimator = animator;
			}
		}

		meshRenderer->m_Mesh->Draw();
	}
}
