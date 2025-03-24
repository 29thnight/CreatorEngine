#include "UIPass.h"
#include "AssetSystem.h"
#include "ImGuiRegister.h"
UIPass::UIPass()
{

	m_pso = std::make_unique<PipelineStateObject>();
	//m_pso->m_vertexShader = &AssetsSystems->VertexShaders["BillBoard"];
	//m_pso->m_geometryShader = &AssetsSystems->GeometryShaders["BillBoard"];
	//m_pso->m_pixelShader = &AssetsSystems->PixelShaders["Fire"];
	//m_pso->m_computeShader = &AssetsSystems->ComputeShaders["FireCompute"];
	m_pso->m_primitiveTopology = D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;
	D3D11_INPUT_ELEMENT_DESC vertexLayoutDesc[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 }
	};
	// 포지션은 위치  TEXCOORD은 x,y사이즈
	
	DirectX11::ThrowIfFailed(
		DeviceState::g_pDevice->CreateInputLayout(
			vertexLayoutDesc,
			_countof(vertexLayoutDesc),
			m_pso->m_vertexShader->GetBufferPointer(),
			m_pso->m_vertexShader->GetBufferSize(),
			&m_pso->m_inputLayout
		)
	);


	//상수버퍼
	/*{
		D3D11_BUFFER_DESC cbDesc = {};
		cbDesc.Usage = D3D11_USAGE_DEFAULT;
		cbDesc.ByteWidth = sizeof(ExplodeParameters);
		cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		DeviceState::g_pDevice->CreateBuffer(&cbDesc, nullptr, m_constantBuffer.GetAddressOf());
	}*/
	//상수버퍼추가할때 참고용
	/*mmParam = new ExplodeParameters;
	mmParam->time = 0.0f;
	mmParam->intensity = 1.0f;
	mmParam->speed = 5.0f;
	mmParam->color = Mathf::Vector4(1.0f, 1.0f, 1.0f, 1.0f);
	mmParam->size = Mathf::Vector2(256.0f, 256.0f);
	mmParam->range = Mathf::Vector2(8.0f, 4.0f);
	SetParameters(mmParam);*/

	//알파블렌딩 추가필요할수도 일단없음
	CD3D11_RASTERIZER_DESC rasterizerDesc{ CD3D11_DEFAULT() };
	DirectX11::ThrowIfFailed(
		DeviceState::g_pDevice->CreateRasterizerState(
			&rasterizerDesc,
			&m_pso->m_rasterizerState
		)
	);

	
	CD3D11_DEPTH_STENCIL_DESC depthDesc{ CD3D11_DEFAULT() };
	depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
	depthDesc.DepthEnable = true;
	depthDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
	DeviceState::g_pDevice->CreateDepthStencilState(&depthDesc, &m_pso->m_depthStencilState);

	//m_baseFireTexture = std::shared_ptr<Texture>(Texture::LoadFormPath("Explosion_01.dds"));
	//m_fireAlphaTexture = std::shared_ptr<Texture>(Texture::LoadFormPath("Explosion_01_a.jpg"));
}

void UIPass::Initialize(Texture* renderTargetView)
{
	m_renderTarget = renderTargetView;
}

void UIPass::Update(float delta)
{

}

void UIPass::DrawCanvas(Mathf::Matrix world, Mathf::Matrix view, Mathf::Matrix projection)
{

}
