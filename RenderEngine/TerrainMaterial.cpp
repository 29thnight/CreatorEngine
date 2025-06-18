#include "TerrainMaterial.h"

void TerrainMaterial::Initialize(UINT width, UINT height)
{
	m_width = static_cast<int>(width);
	m_height = static_cast<int>(height);

	m_computeShader = &ShaderSystem->ComputeShaders["TerrainTexture"];
	m_AddLayerBuffer = DirectX11::CreateBuffer(
		sizeof(TerrainAddLayerBuffer),
		D3D11_BIND_CONSTANT_BUFFER,
		nullptr
	);
	m_layerBuffer = DirectX11::CreateBuffer(sizeof(TerrainLayerBuffer), D3D11_BIND_CONSTANT_BUFFER, nullptr);

	//���̾� �ؽ�ó �迭 �ʱ�ȭ
	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = 512;
	desc.Height = 512;
	desc.MipLevels = 1;
	desc.ArraySize = 4; //�ִ� ���̾� ��
	desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;

	DeviceState::g_pDevice->CreateTexture2D(&desc, nullptr, &m_layerTextureArray);

	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.Format = desc.Format;
	uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
	uavDesc.Texture2DArray.MipSlice = 0;
	uavDesc.Texture2DArray.FirstArraySlice = 0;
	uavDesc.Texture2DArray.ArraySize = desc.ArraySize;

	DeviceState::g_pDevice->CreateUnorderedAccessView(m_layerTextureArray, &uavDesc, &p_outTextureUAV);


	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = desc.Format;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
	srvDesc.Texture2DArray.MipLevels = 1;
	srvDesc.Texture2DArray.ArraySize = 4; // �ִ� ���̾� ��
	srvDesc.Texture2DArray.FirstArraySlice = 0;

	DeviceState::g_pDevice->CreateShaderResourceView(m_layerTextureArray, &srvDesc, &m_layerSRV);

	InitSplatMapTexture(width, height);
}


void TerrainMaterial::AddLayer(TerrainLayer& newLayer)
{
	float tillingFactor[4] = { m_layerBufferData.layerTilling0, m_layerBufferData.layerTilling1, m_layerBufferData.layerTilling2, m_layerBufferData.layerTilling3 };

	tillingFactor[newLayer.m_layerID] = newLayer.tilling;

	m_layerBufferData.useLayer = true; // ���̾� ��� ���θ� true�� ����
	m_layerBufferData.layerTilling0 = tillingFactor[0];
	m_layerBufferData.layerTilling1 = tillingFactor[1];
	m_layerBufferData.layerTilling2 = tillingFactor[2];
	m_layerBufferData.layerTilling3 = tillingFactor[3];

	DirectX11::UpdateBuffer(m_layerBuffer.Get(), &m_layerBufferData);

	ID3D11Resource* diffuseResource = nullptr;
	ID3D11Texture2D* diffuseTexture = nullptr;
	ID3D11ShaderResourceView* diffuseSRV = nullptr;
	if (newLayer.diffuseTexturePath.empty()) {
		Debug->LogError("Diffuse texture path is empty for layer: " + newLayer.layerName);
		return; // diffuseTexturePath�� ��������� ���̾� �߰��� �ߴ�
	}

	if (CreateTextureFromFile(DeviceState::g_pDevice, newLayer.diffuseTexturePath, &diffuseResource, &diffuseSRV) == S_OK)
	{
		diffuseResource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&diffuseTexture));
		newLayer.diffuseTexture = diffuseTexture;
		newLayer.diffuseSRV = diffuseSRV;
	}
	else {
		Debug->LogError("Failed to load diffuse texture: " + newLayer.layerName);
		return; // �ε� ���н� �ش� ���̾�� ����
	}


	DirectX11::CSSetShader(m_computeShader->GetShader(), nullptr, 0);
	TerrainAddLayerBuffer addLayerBuffer;
	addLayerBuffer.slice = newLayer.m_layerID;
	DirectX11::UpdateBuffer(m_AddLayerBuffer.Get(), &addLayerBuffer);
	DirectX11::CSSetConstantBuffer(0, 1, m_AddLayerBuffer.GetAddressOf());

	const UINT offsets[]{ 0 };

	ID3D11UnorderedAccessView* uavs[] = { p_outTextureUAV };
	ID3D11UnorderedAccessView* nullUAVs[]{ nullptr };
	DirectX11::CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
	//ID3D11ShaderResourceView* srvs = { m_layers[newLayer.m_layerID].diffuseSRV,m_layers[1].diffuseSRV, m_layers[2].diffuseSRV, m_layers[3].diffuseSRV };
	ID3D11ShaderResourceView* nullSRVs[]{ nullptr };
	DirectX11::CSSetShaderResources(0, 1, &newLayer.diffuseSRV);
	DirectX11::CSSetConstantBuffer(0, 1, m_AddLayerBuffer.GetAddressOf());

	uint32 threadGroupCountX = (uint32)ceilf(512 / 16.0f);
	uint32 threadGroupCountY = (uint32)ceilf(512 / 16.0f);

	//(512 + 15) / 16, (512 + 15) / 16, 4
	DirectX11::Dispatch(threadGroupCountX, threadGroupCountY, 1);

	DirectX11::CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);
	DirectX11::CSSetShaderResources(0, 1, nullSRVs);


}

void TerrainMaterial::RemoveLayer(uint32_t layerID)
{
	//�ش� ���̾��� �ؽ�ó ���� 
	//p_outTextureUAV �� ���� ��ǻƮ ���̴��� ���� ���� �ؽ�ó�� �����ϴ� ������ �ʿ���
	float clearValue[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	DeviceState::g_pDeviceContext->ClearUnorderedAccessViewFloat(
		&p_outTextureUAV[layerID],
		clearValue
	);

	D3D11_MAPPED_SUBRESOURCE mapped = {};
	DeviceState::g_pDeviceContext->Map(m_splatMapTexture.Get(), 0, D3D11_MAP_WRITE_NO_OVERWRITE, 0, &mapped);

	BYTE* rowPtr = reinterpret_cast<BYTE*>(mapped.pData);
	for (UINT y = 0; y < m_height; ++y)
	{
		// �� �ȼ� RGBA ����, 1����Ʈ��
		BYTE* pixel = rowPtr + y * mapped.RowPitch;
		for (UINT x = 0; x < m_width; ++x, pixel += 4)
		{
			pixel[layerID] = 0; // �ش� ä�� 0
		}
	}
	
	DeviceState::g_pDeviceContext->Unmap(m_splatMapTexture.Get(), 0);
}

void TerrainMaterial::ClearLayers()
{
	m_layerBufferData.useLayer = false; // ���̾� ��� ���θ� false�� ����
	m_layerBufferData.layerTilling0 = 1.0f;
	m_layerBufferData.layerTilling1 = 1.0f;
	m_layerBufferData.layerTilling2 = 1.0f;
	m_layerBufferData.layerTilling3 = 1.0f;

	DirectX11::UpdateBuffer(m_layerBuffer.Get(), &m_layerBufferData);

	m_splatMapTexture.Reset();
	m_splatMapSRV.Reset();
	m_layerTextureArray = nullptr; // ���̾� �ؽ�ó �迭 �ʱ�ȭ
	p_outTextureUAV = nullptr; // UAV �ʱ�ȭ
	m_layerSRV = nullptr; // SRV �ʱ�ȭ
}

void TerrainMaterial::InitSplatMapTexture(UINT width, UINT height)
{
	m_splatMapTexture.Reset();
	m_splatMapSRV.Reset();

	// ���÷��� �ؽ�ó �ʱ�ȭ
	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = width;
	desc.Height = height;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // RGBA 8-bit format
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DYNAMIC; // ���� ������Ʈ ����
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE; // CPU���� ���� ����
	desc.MiscFlags = 0;

	HRESULT hr = DeviceState::g_pDevice->CreateTexture2D(&desc, nullptr, m_splatMapTexture.GetAddressOf());
	if (FAILED(hr)) {
		throw std::runtime_error("Failed to create splat map texture");
	}

	// SRV ����
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = desc.Format;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = 1;
	hr = DeviceState::g_pDevice->CreateShaderResourceView(m_splatMapTexture.Get(), &srvDesc, m_splatMapSRV.GetAddressOf());
	if (FAILED(hr)) {
		throw std::runtime_error("Failed to create splat map SRV");
	}

	// �ʱ� ���÷��� ������ ����
	D3D11_MAPPED_SUBRESOURCE mapped = {};
	hr = DeviceState::g_pDeviceContext->Map(m_splatMapTexture.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);

	{
		BYTE* dest = reinterpret_cast<BYTE*>(mapped.pData);

		for (int y = 0; y < width; ++y)
		{
			BYTE* row = dest + y * mapped.RowPitch;
			memset(row, 0, height * 4); // RGBA 4ä���̹Ƿ� 4����Ʈ�� �ʱ�ȭ
		}
	}

	DeviceState::g_pDeviceContext->Unmap(m_splatMapTexture.Get(), 0);
}

void TerrainMaterial::UpdateSplatMapPatch(int offsetX, int offsetY, int patchW, int patchH, std::vector<BYTE>& patchData)
{

	D3D11_MAPPED_SUBRESOURCE mapped = {};
	HRESULT hr = DeviceState::g_pDeviceContext->Map(m_splatMapTexture.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
	if (FAILED(hr)) {
		throw std::runtime_error("Failed to map splat map texture");
		return;
	}

	for (int row = 0; row < patchH; ++row)
	{
		BYTE* destRow = reinterpret_cast<BYTE*>(mapped.pData) + (size_t)(offsetY + row) * mapped.RowPitch + (size_t)(offsetX * 4);
		const BYTE* srcPtr = patchData.data() + (size_t)row * patchW * 4;
		memcpy(destRow, srcPtr, patchW * 4); // RGBA 4ä���̹Ƿ� 4����Ʈ�� ����
	}

	DeviceState::g_pDeviceContext->Unmap(m_splatMapTexture.Get(), 0);
}

void TerrainMaterial::UpdateBuffer(TerrainLayerBuffer layers)
{
	// ���̾� ������ ������� ���� ������Ʈ
	m_layerBufferData = layers;
	DirectX11::UpdateBuffer(m_layerBuffer.Get(), &m_layerBufferData);
}

void TerrainMaterial::MateialDataUpdate(int width, int height, std::vector<TerrainLayer>& layers, std::vector<std::vector<float>>& layerHeightMap)
{
	//size ���� ����
	m_width = static_cast<float>(width);
	m_height = static_cast<float>(height);
	
	//����� �°� ��	�÷��� �ؽ�ó �ʱ�ȭ
	InitSplatMapTexture(width, height);

	//�ҷ����� �� layer data ����
	
	

	float tilefector[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	//���� ���̾� ������ �ؽ�ó�� �ҷ��� m_layerSRV �ݿ�
	for (auto& layer : layers)
	{
		AddLayer(layer);
		if (layer.m_layerID < 4) // �ִ� 4�� ���̾ ���
		{
			tilefector[layer.m_layerID] = layer.tilling;
		}
	}
	m_layerBufferData.useLayer = !layers.empty();
	m_layerBufferData.layerTilling0 = tilefector[0];
	m_layerBufferData.layerTilling1 = tilefector[1];
	m_layerBufferData.layerTilling2 = tilefector[2];
	m_layerBufferData.layerTilling3 = tilefector[3];
	
	DirectX11::UpdateBuffer(m_layerBuffer.Get(), &m_layerBufferData);

	std::vector<BYTE> patchData(m_width * m_height * 4, 0);
	//���̾� ���̸��� ������� splat�� ������Ʈ
	for (int y = 0; y < height; ++y)
	{
		for (int x = 0; x < width; ++x)
		{
			int idx = y * width + x;
			for (int layerIdx = 0; layerIdx < (int)layers.size() && layerIdx < 4; ++layerIdx) // �ִ� 4�� ���̾ ���
			{
				float w = std::clamp(layerHeightMap[layerIdx][idx], 0.0f, 1.0f);
				int dstOffset = (y * width + x) * 4 + layerIdx; // RGBA 4ä��
				patchData[dstOffset] = static_cast<BYTE>(w * 255.0f); // RGBA ä�ο� �� ����
			}
		}
	}

	//layerHeightMap���� splat�� ������Ʈ
	UpdateSplatMapPatch(0, 0, m_width, m_height, patchData);
}
