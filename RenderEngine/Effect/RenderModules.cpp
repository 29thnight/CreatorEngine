#include "RenderModules.h"
#include "ShaderSystem.h"
#include "Renderer.h"

void RenderModules::CleanupRenderState()
{
	ID3D11ShaderResourceView* nullSRV = nullptr;
	DeviceState::g_pDeviceContext->PSSetShaderResources(0, 1, &nullSRV);

	DeviceState::g_pDeviceContext->GSSetShader(nullptr, nullptr, 0);
}

void RenderModules::SaveRenderState()
{
	auto& deviceContext = DeviceState::g_pDeviceContext;

	if (m_prevDepthState) m_prevDepthState->Release();
	if (m_prevBlendState) m_prevBlendState->Release();
	if (m_prevRasterizerState) m_prevRasterizerState->Release();

	deviceContext->OMGetDepthStencilState(&m_prevDepthState, &m_prevStencilRef);
	deviceContext->OMGetBlendState(&m_prevBlendState, m_prevBlendFactor, &m_prevSampleMask);
	deviceContext->RSGetState(&m_prevRasterizerState);
}

void RenderModules::RestoreRenderState()
{
	auto& deviceContext = DeviceState::g_pDeviceContext;

	deviceContext->OMSetDepthStencilState(m_prevDepthState, m_prevStencilRef);
	deviceContext->OMSetBlendState(m_prevBlendState, m_prevBlendFactor, m_prevSampleMask);
	deviceContext->RSSetState(m_prevRasterizerState);

	DirectX11::UnbindRenderTargets();
}


void BillboardModule::Initialize()
{
	m_vertices = Quad;
	m_indices = Indices;

	m_pso = std::make_unique<PipelineStateObject>();
	m_BillBoardType = BillBoardType::Basic;
	m_instanceCount = 0;

	D3D11_BLEND_DESC blendDesc = {};
	blendDesc.AlphaToCoverageEnable = FALSE;
	blendDesc.IndependentBlendEnable = FALSE;
	blendDesc.RenderTarget[0].BlendEnable = TRUE;
	blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
	blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
	blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

	DirectX11::ThrowIfFailed(
		DeviceState::g_pDevice->CreateBlendState(&blendDesc, &m_pso->m_blendState)
	);

	CD3D11_RASTERIZER_DESC rasterizerDesc{ CD3D11_DEFAULT() };
	DirectX11::ThrowIfFailed(
		DeviceState::g_pDevice->CreateRasterizerState(
			&rasterizerDesc,
			&m_pso->m_rasterizerState
		)
	);

	CD3D11_DEPTH_STENCIL_DESC depthDesc{ CD3D11_DEFAULT() };
	depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	depthDesc.DepthEnable = true;
	depthDesc.DepthFunc = D3D11_COMPARISON_LESS;
	DeviceState::g_pDevice->CreateDepthStencilState(&depthDesc, &m_pso->m_depthStencilState);

	m_pso->m_primitiveTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	m_pso->m_vertexShader = &ShaderSystem->VertexShaders["BillBoard"];

	D3D11_INPUT_ELEMENT_DESC vertexLayoutDesc[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "POSITION", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
		{ "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 1, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
		{ "TEXCOORD", 2, DXGI_FORMAT_R32_UINT, 1, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
		{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
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

	auto linearSampler = std::make_shared<Sampler>(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_WRAP);
	auto pointSampler = std::make_shared<Sampler>(D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP);

	m_pso->m_samplers.push_back(linearSampler);
	m_pso->m_samplers.push_back(pointSampler);

	CreateBillboard();
}

void BillboardModule::CreateBillboard()
{

	D3D11_BUFFER_DESC vbDesc = {};
	vbDesc.Usage = D3D11_USAGE_DEFAULT;
	vbDesc.ByteWidth = sizeof(BillboardVertex) * 4;
	vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

	D3D11_SUBRESOURCE_DATA vbData = {};
	vbData.pSysMem = m_vertices.data();

	DirectX11::ThrowIfFailed(
		DeviceState::g_pDevice->CreateBuffer(&vbDesc, &vbData, &billboardVertexBuffer)
	);

	D3D11_BUFFER_DESC ibDesc = {};
	ibDesc.Usage = D3D11_USAGE_DEFAULT;
	ibDesc.ByteWidth = sizeof(uint32) * m_indices.size();
	ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;

	D3D11_SUBRESOURCE_DATA ibData = {};
	ibData.pSysMem = m_indices.data();

	DirectX11::ThrowIfFailed(
		DeviceState::g_pDevice->CreateBuffer(&ibDesc, &ibData, &billboardIndexBuffer)
	);

	m_ModelBuffer = DirectX11::CreateBuffer(
		sizeof(ModelConstantBuffer),
		D3D11_BIND_CONSTANT_BUFFER,
		&m_ModelConstantBuffer
	);
}

void BillboardModule::InitializeInstance(UINT count)
{
	D3D11_BUFFER_DESC ibDesc = {};
	ibDesc.Usage = D3D11_USAGE_DYNAMIC;
	ibDesc.ByteWidth = sizeof(BillBoardInstanceData) * count;
	ibDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	ibDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;  // CPU에서 쓰기 가능하도록
	ibDesc.MiscFlags = 0;

	ID3D11Buffer* instanceBuffer;
	DirectX11::ThrowIfFailed(
		DeviceState::g_pDevice->CreateBuffer(&ibDesc, nullptr, &instanceBuffer)
	);

	m_InstanceBuffer.Attach(instanceBuffer);
	m_maxCount = count;
}

void BillboardModule::SetupInstancing(BillBoardInstanceData* instance, UINT count)
{
	// 버퍼가 없거나 크기가 부족하면 재생성
	if (m_InstanceBuffer == nullptr || count > m_maxCount)
	{
		UINT newMaxCount = static_cast<UINT>(count * 1.5f);
		InitializeInstance(newMaxCount);
	}

	// 버퍼 데이터 업데이트
	D3D11_MAPPED_SUBRESOURCE mappedResource;
	auto& deviceContext = DeviceState::g_pDeviceContext;

	HRESULT hr = deviceContext->Map(
		m_InstanceBuffer.Get(),
		0,
		D3D11_MAP_WRITE_DISCARD,
		0,
		&mappedResource
	);

	if (SUCCEEDED(hr))
	{
		// 메모리 복사
		memcpy(mappedResource.pData, instance, sizeof(BillBoardInstanceData) * count);

		// 언맵
		deviceContext->Unmap(m_InstanceBuffer.Get(), 0);
	}

	m_instanceCount = count;
}

void BillboardModule::Render(Mathf::Matrix world, Mathf::Matrix view, Mathf::Matrix projection)
{
	auto& deviceContext = DeviceState::g_pDeviceContext;

	m_ModelConstantBuffer.world = world;
	m_ModelConstantBuffer.view = view;
	m_ModelConstantBuffer.projection = projection;

	deviceContext->VSSetConstantBuffers(0, 1, m_ModelBuffer.GetAddressOf());
	DirectX11::UpdateBuffer(m_ModelBuffer.Get(), &m_ModelConstantBuffer);

	// 버텍스 및 인스턴스 버퍼 설정
	ID3D11Buffer* buffers[2] = { billboardVertexBuffer.Get(), m_InstanceBuffer.Get() };
	UINT strides[2] = { sizeof(BillboardVertex), sizeof(BillBoardInstanceData) };
	UINT offsets[2] = { 0, 0 };

	deviceContext->IASetVertexBuffers(0, 2, buffers, strides, offsets);
	deviceContext->IASetIndexBuffer(billboardIndexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);

	// 인덱스 기반 인스턴스 렌더링
	deviceContext->DrawIndexedInstanced(m_indices.size(), m_instanceCount, 0, 0, 0);

	// 정리
	DirectX11::UnbindRenderTargets();
}

void MeshModule::Initialize()
{
	//m_gameObject->GetComponent<meshrenderer
}

void MeshModule::Render(Mathf::Matrix world, Mathf::Matrix view, Mathf::Matrix projection)
{
}

void MeshModule::SetupInstancing(void* instanceData, UINT count)
{
}

