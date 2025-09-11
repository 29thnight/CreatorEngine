#include "BillboardModuleGPU.h"
#include "ShaderSystem.h"
#include "DataSystem.h"

void BillboardModuleGPU::Initialize()
{
	m_vertices = Quad;
	m_indices = Indices;

	m_pso = std::make_unique<PipelineStateObject>();

	if (m_BillBoardType == BillBoardType::None) {
		m_BillBoardType = BillBoardType::Basic;
	}

	m_instanceCount = 0;

	// ���̴� �̸� ����
	if (m_vertexShaderName == "None") {
		m_vertexShaderName = "BillBoard";
	}

	if (m_pixelShaderName == "None")
	{
		m_pixelShaderName = "BillBoard";
	}

	// ���� ���� ������ ���� (PSO ���� �Ŀ�)
	if (m_blendPreset == BlendPreset::None) {
		m_blendPreset = BlendPreset::Alpha;
	}
	if (m_depthPreset == DepthPreset::None)
	{
		m_depthPreset = DepthPreset::Default;
	}
	if (m_rasterizerPreset == RasterizerPreset::None)
	{
		m_rasterizerPreset = RasterizerPreset::Default;
	}

	// ������Ƽ�� �������� ����
	m_pso->m_primitiveTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	// �θ��� �Լ���� ��� ���� ������Ʈ
	UpdatePSOShaders();
	UpdatePSORenderStates();

	// ���÷� ����
	auto linearSampler = std::make_shared<Sampler>(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_WRAP);
	auto pointSampler = std::make_shared<Sampler>(D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP);
	m_pso->m_samplers.push_back(linearSampler);
	m_pso->m_samplers.push_back(pointSampler);

	CreateBillboard();
}

void BillboardModuleGPU::Release()
{
	if (billboardVertexBuffer) {
		billboardVertexBuffer.Reset();
	}

	if (billboardIndexBuffer) {
		billboardIndexBuffer.Reset();
	}

	if (m_ModelBuffer) {
		m_ModelBuffer.Reset();
	}

	// ��������Ʈ �ִϸ��̼� ���� ���� �߰�
	if (m_SpriteAnimationBuffer) {
		m_SpriteAnimationBuffer.Reset();
	}

	m_particleSRV = nullptr;
	m_assignedTexture = nullptr;

	m_instanceCount = 0;
	m_BillBoardType = BillBoardType::None;
	m_maxCount = 0;

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
		DirectX11::DeviceStates->g_pDevice->CreateBuffer(&vbDesc, &vbData, &billboardVertexBuffer)
	);

	D3D11_BUFFER_DESC ibDesc = {};
	ibDesc.Usage = D3D11_USAGE_DEFAULT;
	ibDesc.ByteWidth = sizeof(uint32) * m_indices.size();
	ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;

	D3D11_SUBRESOURCE_DATA ibData = {};
	ibData.pSysMem = m_indices.data();

	DirectX11::ThrowIfFailed(
		DirectX11::DeviceStates->g_pDevice->CreateBuffer(&ibDesc, &ibData, &billboardIndexBuffer)
	);

	m_ModelBuffer = DirectX11::CreateBuffer(
		sizeof(ModelConstantBuffer),
		D3D11_BIND_CONSTANT_BUFFER,
		&m_ModelConstantBuffer
	);

	if (m_BillBoardType == BillBoardType::SpriteAnimation)
	{
		m_SpriteAnimationBuffer = DirectX11::CreateBuffer(
			sizeof(SpriteAnimationBuffer),
			D3D11_BIND_CONSTANT_BUFFER,
			&m_SpriteAnimationConstantBuffer
		);
	}
}

void BillboardModuleGPU::ResetForReuse()
{
	std::lock_guard<std::mutex> lock(m_resetMutex);

	m_instanceCount = 0;
	m_maxCount = 0;
	m_isRendering = false;
	m_particleSRV = nullptr;

	// ���� �ؽ�ó �ʱ�ȭ
	ClearTextures();

	m_ModelConstantBuffer = {};
	m_ModelConstantBuffer.world = Mathf::Matrix::Identity;
	m_ModelConstantBuffer.view = Mathf::Matrix::Identity;
	m_ModelConstantBuffer.projection = Mathf::Matrix::Identity;

	//if (m_vertexShaderName == "None") {
	//	m_vertexShaderName = "BillBoard";
	//}
	//
	//if (m_pixelShaderName == "None") {
	//	m_pixelShaderName = "BillBoard";
	//}
	//
	//m_blendPreset = BlendPreset::Alpha;
	//m_depthPreset = DepthPreset::Default;
	//m_rasterizerPreset = RasterizerPreset::Default;

	UpdatePSOShaders();
	UpdatePSORenderStates();

	m_gpuWorkPending = false;

}

bool BillboardModuleGPU::IsReadyForReuse() const
{
	// GPU �۾��� �Ϸ�Ǿ���, ������ ���� �ƴ� ���� ���� ����
	bool ready = !m_isRendering &&
		!m_gpuWorkPending.load() &&
		m_instanceCount == 0;

	// �ʼ� ���ҽ����� ��ȿ���� Ȯ��
	bool resourcesValid = billboardVertexBuffer != nullptr &&
		billboardIndexBuffer != nullptr &&
		m_ModelBuffer != nullptr &&
		m_pso != nullptr;

	return ready && resourcesValid;
}

void BillboardModuleGPU::WaitForGPUCompletion()
{
	m_gpuWorkPending = false;
}

void BillboardModuleGPU::UpdatePSOShaders()
{
	RenderModules::UpdatePSOShaders();

	if (m_pso && m_pso->m_vertexShader) {
		if (m_pso->m_inputLayout) {
			m_pso->m_inputLayout = nullptr;
		}

		D3D11_INPUT_ELEMENT_DESC vertexLayoutDesc[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 }
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
	}
}

void BillboardModuleGPU::SetupRenderTarget(RenderPassData* renderData)
{
	auto& deviceContext = DirectX11::DeviceStates->g_pDeviceContext;
	ID3D11RenderTargetView* rtv = renderData->m_renderTarget->GetRTV();
	deviceContext->OMSetRenderTargets(1, &rtv, renderData->m_depthStencil->m_pDSV);
}

void BillboardModuleGPU::BindResource()
{
	auto& deviceContext = DirectX11::DeviceStates->g_pDeviceContext;

	// �θ��� ���� �ؽ�ó ���ε� ���
	BindTextures();

	// ��ƼŬ SRV ���ε�
	deviceContext->VSSetShaderResources(0, 1, &m_particleSRV);
}

void BillboardModuleGPU::SetBillboardType(BillBoardType type)
{
	m_BillBoardType = type;

	// SpriteAnimation Ÿ������ ����� �� ���� ����
	if (type == BillBoardType::SpriteAnimation && !m_SpriteAnimationBuffer)
	{
		m_SpriteAnimationBuffer = DirectX11::CreateBuffer(
			sizeof(SpriteAnimationBuffer),
			D3D11_BIND_CONSTANT_BUFFER,
			&m_SpriteAnimationConstantBuffer
		);
	}
}

void BillboardModuleGPU::Render(Mathf::Matrix world, Mathf::Matrix view, Mathf::Matrix projection)
{
	if (!m_enabled) return;

	m_isRendering = true;
	m_gpuWorkPending = true;

	auto& deviceContext = DirectX11::DeviceStates->g_pDeviceContext;

	m_ModelConstantBuffer.world = world;
	m_ModelConstantBuffer.view = view;
	m_ModelConstantBuffer.projection = projection;

	deviceContext->VSSetConstantBuffers(0, 1, m_ModelBuffer.GetAddressOf());
	DirectX11::UpdateBuffer(m_ModelBuffer.Get(), &m_ModelConstantBuffer);

	// ��������Ʈ �ִϸ��̼� Ÿ���� �� �߰� ���� ���ε�
	if (m_BillBoardType == BillBoardType::SpriteAnimation && m_SpriteAnimationBuffer)
	{
		deviceContext->PSSetConstantBuffers(0, 1, m_SpriteAnimationBuffer.GetAddressOf());
		DirectX11::UpdateBuffer(m_SpriteAnimationBuffer.Get(), &m_SpriteAnimationConstantBuffer);
	}

	BindResource();

	UINT stride = sizeof(BillboardVertex);
	UINT offset = 0;
	deviceContext->IASetVertexBuffers(0, 1, billboardVertexBuffer.GetAddressOf(), &stride, &offset);
	deviceContext->IASetIndexBuffer(billboardIndexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);

	deviceContext->DrawIndexedInstanced(m_indices.size(), m_instanceCount, 0, 0, 0);

	ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
	deviceContext->VSSetShaderResources(0, 1, nullSRV);

	DirectX11::UnbindRenderTargets();

	m_isRendering = false;

	if (GetTextureCount() > 0) {
		std::vector<ID3D11ShaderResourceView*> nullSRVs(GetTextureCount(), nullptr);
		deviceContext->PSSetShaderResources(0, nullSRVs.size(), nullSRVs.data());
	}
}

void BillboardModuleGPU::SetParticleData(ID3D11ShaderResourceView* particleSRV, UINT instanceCount)
{
	m_particleSRV = particleSRV;
	m_instanceCount = instanceCount;
}

void BillboardModuleGPU::SetSpriteAnimation(uint32 frameCount, float duration, uint32 gridColumns, uint32 gridRows)
{
	m_SpriteAnimationConstantBuffer.frameCount = frameCount;
	m_SpriteAnimationConstantBuffer.animationDuration = duration;
	m_SpriteAnimationConstantBuffer.gridColumns = gridColumns;
	m_SpriteAnimationConstantBuffer.gridRows = gridRows;
}


// ����ȭ �Լ�


nlohmann::json BillboardModuleGPU::SerializeData() const
{
	nlohmann::json json;

	// ������ ����
	json["billboard"] = {
		{"type", static_cast<int>(m_BillBoardType)},
		{"maxCount", m_maxCount}
	};

	// ��������Ʈ �ִϸ��̼� ���� (SpriteAnimation Ÿ���� ���� ����)
	if (m_BillBoardType == BillBoardType::SpriteAnimation)
	{
		json["spriteAnimation"] = {
			{"frameCount", m_SpriteAnimationConstantBuffer.frameCount},
			{"animationDuration", m_SpriteAnimationConstantBuffer.animationDuration},
			{"gridColumns", m_SpriteAnimationConstantBuffer.gridColumns},
			{"gridRows", m_SpriteAnimationConstantBuffer.gridRows}
		};
	}

	json.merge_patch(SerializeRenderStates());

	// �ؽ�ó ����
	json["textures"] = SerializeTextures();

	if (m_assignedTexture)
	{
		json["texture"]["name"] = m_assignedTexture->m_name;
		json["texture"]["assigned"] = true;
	}

	// ���� ������ (Ŀ���� ������ �ִ� ���)
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

	// �ε��� ������ (Ŀ���� �ε����� �ִ� ���)
	if (!m_indices.empty())
	{
		json["indices"] = m_indices;
	}

	return json;
}

void BillboardModuleGPU::DeserializeData(const nlohmann::json& json)
{
	if (!m_pso) {
		Initialize();
	}

	// ������ ���� ����
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

	// ��������Ʈ �ִϸ��̼� ���� ����
	if (json.contains("spriteAnimation") && m_BillBoardType == BillBoardType::SpriteAnimation)
	{
		const auto& spriteAnimJson = json["spriteAnimation"];

		if (spriteAnimJson.contains("frameCount"))
		{
			m_SpriteAnimationConstantBuffer.frameCount = spriteAnimJson["frameCount"];
		}

		if (spriteAnimJson.contains("animationDuration"))
		{
			m_SpriteAnimationConstantBuffer.animationDuration = spriteAnimJson["animationDuration"];
		}

		if (spriteAnimJson.contains("gridColumns"))
		{
			m_SpriteAnimationConstantBuffer.gridColumns = spriteAnimJson["gridColumns"];
		}

		if (spriteAnimJson.contains("gridRows"))
		{
			m_SpriteAnimationConstantBuffer.gridRows = spriteAnimJson["gridRows"];
		}

		if (!m_SpriteAnimationBuffer)
		{
			m_SpriteAnimationBuffer = DirectX11::CreateBuffer(
				sizeof(SpriteAnimationBuffer),
				D3D11_BIND_CONSTANT_BUFFER,
				&m_SpriteAnimationConstantBuffer
			);
		}
	}

	DeserializeRenderStates(json);

	// �ؽ�ó ���� ����
	DeserializeTextures(json);

	// ���� ������ ����
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

	// �ε��� ������ ����
	if (json.contains("indices"))
	{
		m_indices = json["indices"].get<std::vector<uint32>>();
	}
}

std::string BillboardModuleGPU::GetModuleType() const
{
	return "BillboardModuleGPU";
}
