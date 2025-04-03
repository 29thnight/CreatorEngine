#include "NormalMapPass.h"
#include "AssetSystem.h"
#include "Scene.h"
#include "Mesh.h"
#include "Sampler.h"
#include "Renderer.h"

NormalMapPass::NormalMapPass()
{
	m_pso = std::make_unique<PipelineStateObject>();

	m_pso->m_vertexShader = &AssetsSystems->VertexShaders["NormalMap"];
	m_pso->m_pixelShader = &AssetsSystems->PixelShaders["PositionMap"];

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

	rasterizerDesc.CullMode = D3D11_CULL_NONE;

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

void NormalMapPass::Initialize(uint32 width, uint32 height)
{
}

void NormalMapPass::Execute(RenderScene& scene, Camera& camera)
{

	ClearTextures();
	m_pso->Apply();

	auto pre = CD3D11_VIEWPORT(
		0.0f,
		0.0f,
		1024,
		1024
	);

	DeviceState::g_pDeviceContext->RSSetViewports(1, &pre);

	int i = 0;
	for (auto& obj : scene.GetScene()->m_SceneObjects)
	{
		auto* renderer = obj->GetComponent<MeshRenderer>();
		if (renderer == nullptr) continue;
		if (!renderer->IsEnabled()) continue;
		auto meshName = renderer->m_Mesh->GetName();
		if (m_normalMapTextures.find(meshName) == m_normalMapTextures.end()) {
			// ¸ðµ¨ÀÇ positionMap »ý¼º
			m_normalMapTextures[meshName] = Texture::Create(1024, 1024, "Normal Map",
				DXGI_FORMAT_R32G32B32A32_FLOAT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
			m_normalMapTextures[meshName]->CreateRTV(DXGI_FORMAT_R32G32B32A32_FLOAT);
			m_normalMapTextures[meshName]->CreateSRV(DXGI_FORMAT_R32G32B32A32_FLOAT);

			auto* rtv = m_normalMapTextures[meshName]->GetRTV();
			DeviceState::g_pDeviceContext->OMSetRenderTargets(1, &rtv, nullptr);

			renderer->m_Mesh->Draw();

			//DirectX::ScratchImage image;
			//HRESULT hr = DirectX::CaptureTexture(DeviceState::g_pDevice, DeviceState::g_pDeviceContext, m_positionMapTextures[meshName]->m_pTexture, image);
			//
			//std::wstring a = std::to_wstring(i++);
			//a += L"Lightmap.png";
			//DirectX::SaveToWICFile(*image.GetImage(0, 0, 0), DirectX::WIC_FLAGS_NONE,
			//	GUID_ContainerFormatPng, a.c_str());

			ID3D11RenderTargetView* nullRTV = nullptr;
			DeviceState::g_pDeviceContext->OMSetRenderTargets(1, &nullRTV, nullptr);
		}
	}


	DeviceState::g_pDeviceContext->RSSetViewports(1, &DeviceState::g_Viewport);
}

void NormalMapPass::ClearTextures()
{
	for (auto& texture : m_normalMapTextures)
	{
		delete texture.second;
	}
	m_normalMapTextures.clear();
}