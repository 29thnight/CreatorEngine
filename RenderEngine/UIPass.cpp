#include "UIPass.h"
#include "AssetSystem.h"
#include "ImGuiRegister.h"
#include "Mesh.h"
#include "Scene.h"

UIPass::UIPass()
{

	m_pso = std::make_unique<PipelineStateObject>();
	m_pso->m_vertexShader = &AssetsSystems->VertexShaders["UI"];
	//m_pso->m_geometryShader = &AssetsSystems->GeometryShaders["BillBoard"];
	m_pso->m_pixelShader = &AssetsSystems->PixelShaders["UI"];
	//m_pso->m_computeShader = &AssetsSystems->ComputeShaders["FireCompute"];
	m_pso->m_primitiveTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	D3D11_INPUT_ELEMENT_DESC vertexLayoutDesc[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		
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

	auto linearSampler = std::make_shared<Sampler>(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_WRAP);
	auto pointSampler = std::make_shared<Sampler>(D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP);

	m_pso->m_samplers.push_back(linearSampler);
	m_pso->m_samplers.push_back(pointSampler);

	CD3D11_DEPTH_STENCIL_DESC depthStencilDesc{ CD3D11_DEFAULT() };
	depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;

	DirectX11::ThrowIfFailed(
		DeviceState::g_pDevice->CreateDepthStencilState(
			&depthStencilDesc,
			&m_NoWriteDepthStencilState
		)
	);

	m_pso->m_depthStencilState = m_NoWriteDepthStencilState.Get();
	m_pso->m_blendState = DeviceState::g_pBlendState;

	m_UIBuffer = DirectX11::CreateBuffer(sizeof(UiInfo), D3D11_BIND_CONSTANT_BUFFER, nullptr);
}

void UIPass::Initialize(Texture* renderTargetView)
{
	m_renderTarget = renderTargetView;
}

void UIPass::pushUI(UIsprite* UI)
{
	_testUI.push_back(UI);
}

void UIPass::Update(float delta)
{
	for (auto& Uiobject : _testUI)
	{
		Uiobject->Update();
	}
}

void UIPass::Execute(RenderScene& scene, Camera& camera)
{
	auto deviceContext = DeviceState::g_pDeviceContext;
	m_pso->Apply();

	ID3D11RenderTargetView* view = camera.m_renderTarget->GetRTV();
	DirectX11::OMSetRenderTargets(1, &view, nullptr);
	
	deviceContext->OMSetDepthStencilState(m_NoWriteDepthStencilState.Get(), 1);
	deviceContext->OMSetBlendState(DeviceState::g_pBlendState, nullptr, 0xFFFFFFFF);
	camera.UpdateBuffer();

	DirectX11::VSSetConstantBuffer(0,1,m_UIBuffer.GetAddressOf());

	
	std::sort(_testUI.begin(), _testUI.end(), compareLayer);
	for (auto& Uiobject : _testUI)
	{
		DirectX11::PSSetShaderResources(0, 1, &Uiobject->m_curtexture->m_pSRV);
		DirectX11::UpdateBuffer(m_UIBuffer.Get(), &Uiobject->uiinfo);
		Uiobject->Draw();
	}

	DirectX11::OMSetDepthStencilState(DeviceState::g_pDepthStencilState, 1);
	DirectX11::OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);

	ID3D11ShaderResourceView* nullSRV = nullptr;
	DirectX11::PSSetShaderResources(0, 1, &nullSRV);
	DirectX11::UnbindRenderTargets();

}

bool UIPass::compareLayer(UIsprite* a, UIsprite* b)
{
	return a->_layerorder < b->_layerorder;
}

void UIPass::DrawCanvas(Mathf::Matrix world, Mathf::Matrix view, Mathf::Matrix projection)
{

}
