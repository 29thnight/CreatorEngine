#include "PositionMapPass.h"
#include "AssetSystem.h"
#include "Scene.h"
#include "Mesh.h"
#include "Sampler.h"
#include "Renderer.h"

PositionMapPass::PositionMapPass()
{
	m_pso = std::make_unique<PipelineStateObject>();

	m_pso->m_vertexShader = &AssetsSystems->VertexShaders["PositionMap"];
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

void PositionMapPass::Initialize(uint32 width, uint32 height)
{
}

void PositionMapPass::Execute(RenderScene& scene, Camera& camera)
{

	ClearTextures();
	m_pso->Apply();
	int size = 2048;

	auto pre = CD3D11_VIEWPORT(
		0.0f,
		0.0f,
		size,
		size
	);

	DeviceState::g_pDeviceContext->RSSetViewports(1, &pre);

	int i = 0;
	for (auto& obj : scene.GetScene()->m_SceneObjects)
	{
		auto* renderer = obj->GetComponent<MeshRenderer>();
		if (renderer == nullptr) continue;
		if (!renderer->IsEnabled()) continue;
		auto meshName = renderer->m_Mesh->GetName();
		if (m_positionMapTextures.find(meshName) == m_positionMapTextures.end()) {
			// 모델의 positionMap 생성
			m_positionMapTextures[meshName] = Texture::Create(size, size, "Position Map",
				DXGI_FORMAT_R32G32B32A32_FLOAT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
			m_positionMapTextures[meshName]->CreateRTV(DXGI_FORMAT_R32G32B32A32_FLOAT);
			m_positionMapTextures[meshName]->CreateSRV(DXGI_FORMAT_R32G32B32A32_FLOAT);

			auto* rtv = m_positionMapTextures[meshName]->GetRTV();
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

void PositionMapPass::ClearTextures()
{
	for (auto& texture : m_positionMapTextures)
	{
		delete texture.second;
	}
	m_positionMapTextures.clear();
}

/*
 - 권용우
메쉬별로 positionMap 생성.
픽셀셰이더를 사용하므로 viewport와 positionMap의 width, height가 일치하는지 확인하기.
(이거 때문에 사이즈 달라져서 개고생함.)

추가 개선 가능 여부
1. 같은 UV를 사용하는 메쉬의 경우 해당 텍스쳐에서 작업할 수 있도록 수정 할 수 있을거같음.
*/