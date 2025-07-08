#include "BillboardModuleGPU.h"
#include "ShaderSystem.h"

void BillboardModuleGPU::Initialize()
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
	m_pso->m_pixelShader = &ShaderSystem->PixelShaders["BillBoard"];

	D3D11_INPUT_ELEMENT_DESC vertexLayoutDesc[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 }
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


void BillboardModuleGPU::Release()
{
	// ComPtr 리소스들 자동 해제
	if (billboardVertexBuffer) {
		billboardVertexBuffer.Reset();
	}

	if (billboardIndexBuffer) {
		billboardIndexBuffer.Reset();
	}

	if (m_ModelBuffer) {
		m_ModelBuffer.Reset();
	}

	// 일반 포인터들 초기화
	m_particleSRV = nullptr;
	m_assignedTexture = nullptr;

	// 기본값 복원
	m_instanceCount = 0;
	m_BillBoardType = BillBoardType::Basic;
	m_maxCount = 0;

	// 벡터 클리어
	m_vertices.clear();
	m_indices.clear();
}

void BillboardModuleGPU::CreateBillboard()
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

void BillboardModuleGPU::SetTexture(Texture* texture)
{
	m_assignedTexture = texture;
}

void BillboardModuleGPU::SetupRenderTarget(RenderPassData* renderData)
{
	auto& deviceContext = DeviceState::g_pDeviceContext;
	ID3D11RenderTargetView* rtv = renderData->m_renderTarget->GetRTV();
	deviceContext->OMSetRenderTargets(1, &rtv, renderData->m_depthStencil->m_pDSV);
}

void BillboardModuleGPU::BindResource()
{
	auto& deviceContext = DeviceState::g_pDeviceContext;

	// 텍스처 바인딩
	if (m_assignedTexture) {
		ID3D11ShaderResourceView* srv = m_assignedTexture->m_pSRV;
		DirectX11::PSSetShaderResources(0, 1, &srv);
	}

	// 파티클 SRV 바인딩
	deviceContext->VSSetShaderResources(0, 1, &m_particleSRV);
}

nlohmann::json BillboardModuleGPU::SerializeData() const
{
	nlohmann::json json;

	// 빌보드 설정
	json["billboard"] = {
		{"type", static_cast<int>(m_BillBoardType)},
		{"maxCount", m_maxCount}
	};

	// 텍스처 정보 (텍스처 파일 경로나 이름 저장)
	json["texture"] = {
		{"hasTexture", m_assignedTexture != nullptr}
	};

	if (m_assignedTexture)
	{
		// 텍스처 파일 경로나 이름을 저장 (Texture 클래스에 GetPath() 같은 메소드가 있다고 가정)
		// json["texture"]["path"] = m_assignedTexture->GetPath();
		// 또는 텍스처 ID나 이름
		// json["texture"]["name"] = m_assignedTexture->GetName();

		// 현재는 텍스처가 할당되어 있다는 정보만 저장
		json["texture"]["assigned"] = true;
	}

	// 정점 데이터 (커스텀 정점이 있는 경우)
	if (!m_vertices.empty())
	{
		json["vertices"] = nlohmann::json::array();
		for (const auto& vertex : m_vertices)
		{
			json["vertices"].push_back({
				{"position", {
					{"x", vertex.position.x},
					{"y", vertex.position.y},
					{"z", vertex.position.z},
					{"w", vertex.position.w}
				}},
				{"texcoord", {
					{"x", vertex.texcoord.x},
					{"y", vertex.texcoord.y}
				}}
				});
		}
	}

	// 인덱스 데이터 (커스텀 인덱스가 있는 경우)
	if (!m_indices.empty())
	{
		json["indices"] = m_indices;
	}

	return json;
}

void BillboardModuleGPU::DeserializeData(const nlohmann::json& json)
{
	// 빌보드 설정 복원
	if (json.contains("billboard"))
	{
		const auto& billboardJson = json["billboard"];

		if (billboardJson.contains("type"))
		{
			m_BillBoardType = static_cast<BillBoardType>(billboardJson["type"]);
		}

		if (billboardJson.contains("maxCount"))
		{
			m_maxCount = billboardJson["maxCount"];
		}
	}

	// 텍스처 정보 복원
	if (json.contains("texture"))
	{
		const auto& textureJson = json["texture"];

		// 텍스처 로드는 별도로 처리해야 함
		// 텍스처 매니저를 통해 로드하거나, 경로를 저장해두었다가 나중에 로드
		if (textureJson.contains("path"))
		{
			// std::string texturePath = textureJson["path"];
			// m_assignedTexture = TextureManager::LoadTexture(texturePath);
		}
	}

	// 정점 데이터 복원
	if (json.contains("vertices"))
	{
		m_vertices.clear();
		for (const auto& vertexJson : json["vertices"])
		{
			BillboardVertex vertex;

			if (vertexJson.contains("position"))
			{
				const auto& posJson = vertexJson["position"];
				vertex.position.x = posJson.value("x", 0.0f);
				vertex.position.y = posJson.value("y", 0.0f);
				vertex.position.z = posJson.value("z", 0.0f);
				vertex.position.w = posJson.value("w", 1.0f);
			}

			if (vertexJson.contains("texcoord"))
			{
				const auto& texJson = vertexJson["texcoord"];
				vertex.texcoord.x = texJson.value("x", 0.0f);
				vertex.texcoord.y = texJson.value("y", 0.0f);
			}

			m_vertices.push_back(vertex);
		}
	}

	// 인덱스 데이터 복원
	if (json.contains("indices"))
	{
		m_indices = json["indices"].get<std::vector<uint32>>();
	}

	// 복원 후 리소스 재생성 필요
	// Initialize()를 다시 호출하거나 별도 메소드로 GPU 리소스 재생성
}

std::string BillboardModuleGPU::GetModuleType() const
{
	return "BillboardModuleGPU";
}

void BillboardModuleGPU::Render(Mathf::Matrix world, Mathf::Matrix view, Mathf::Matrix projection)
{
	auto& deviceContext = DeviceState::g_pDeviceContext;

	m_ModelConstantBuffer.world = world;
	m_ModelConstantBuffer.view = view;
	m_ModelConstantBuffer.projection = projection;

	deviceContext->VSSetConstantBuffers(0, 1, m_ModelBuffer.GetAddressOf());
	DirectX11::UpdateBuffer(m_ModelBuffer.Get(), &m_ModelConstantBuffer);

	BindResource();

	// 버텍스 및 인덱스 버퍼 설정
	UINT stride = sizeof(BillboardVertex);
	UINT offset = 0;
	deviceContext->IASetVertexBuffers(0, 1, billboardVertexBuffer.GetAddressOf(), &stride, &offset);
	deviceContext->IASetIndexBuffer(billboardIndexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);

	// 인스턴스 렌더링 - 인스턴스 버퍼 없이 인스턴스 수만 사용
	deviceContext->DrawIndexedInstanced(m_indices.size(), m_instanceCount, 0, 0, 0);

	// 리소스 해제
	ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
	deviceContext->VSSetShaderResources(0, 1, nullSRV);

	DirectX11::UnbindRenderTargets();
}

void BillboardModuleGPU::SetParticleData(ID3D11ShaderResourceView* particleSRV, UINT instanceCount)
{
	m_particleSRV = particleSRV;
	m_instanceCount = instanceCount;
}


