#include "TerrainMaterial.h"
#include "Texture.h"
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

	DirectX11::DeviceStates->g_pDevice->CreateTexture2D(&desc, nullptr, &m_layerTextureArray);

	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.Format = desc.Format;
	uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
	uavDesc.Texture2DArray.MipSlice = 0;
	uavDesc.Texture2DArray.FirstArraySlice = 0;
	uavDesc.Texture2DArray.ArraySize = desc.ArraySize;

	DirectX11::DeviceStates->g_pDevice->CreateUnorderedAccessView(m_layerTextureArray, &uavDesc, &p_outTextureUAV);


	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = desc.Format;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
	srvDesc.Texture2DArray.MipLevels = 1;
	srvDesc.Texture2DArray.ArraySize = 4; // �ִ� ���̾� ��
	srvDesc.Texture2DArray.FirstArraySlice = 0;

	DirectX11::DeviceStates->g_pDevice->CreateShaderResourceView(m_layerTextureArray, &srvDesc, &m_layerSRV);

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

	//ID3D11Resource* diffuseResource = nullptr;
	//ID3D11Texture2D* diffuseTexture = nullptr;
	//ID3D11ShaderResourceView* diffuseSRV = nullptr;
	if (newLayer.diffuseTexturePath.empty()) {
		Debug->LogError("Diffuse texture path is empty for layer: " + newLayer.layerName);
		return; // diffuseTexturePath�� ��������� ���̾� �߰��� �ߴ�
	}

	file::path path = file::path(newLayer.diffuseTexturePath);
	if (file::exists(path))
	{
		newLayer.diffuseTexture = Texture::LoadFormPath(newLayer.diffuseTexturePath);
	}
	else {
		Debug->LogError("Failed to load diffuse texture: " + newLayer.layerName);
		return; // �ε� ���н� �ش� ���̾�� ����
	}

	//if (CreateTextureFromFile(DirectX11::DeviceStates->g_pDevice, newLayer.diffuseTexturePath, &diffuseResource, &diffuseSRV) == S_OK)
	//{
	//	diffuseResource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&diffuseTexture));
	//	newLayer.diffuseTexture = diffuseTexture;
	//	newLayer.diffuseSRV = diffuseSRV;
	//}
	//else {
	//	Debug->LogError("Failed to load diffuse texture: " + newLayer.layerName);
	//	return; // �ε� ���н� �ش� ���̾�� ����
	//}


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
	DirectX11::CSSetShaderResources(0, 1, &newLayer.diffuseTexture->m_pSRV);
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
	DirectX11::DeviceStates->g_pDeviceContext->ClearUnorderedAccessViewFloat(
		&p_outTextureUAV[layerID],
		clearValue
	);

	D3D11_MAPPED_SUBRESOURCE mapped = {};
	DirectX11::DeviceStates->g_pDeviceContext->Map(m_splatMapTexture.Get(), 0, D3D11_MAP_WRITE_NO_OVERWRITE, 0, &mapped);

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
	
	DirectX11::DeviceStates->g_pDeviceContext->Unmap(m_splatMapTexture.Get(), 0);
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

	HRESULT hr = DirectX11::DeviceStates->g_pDevice->CreateTexture2D(&desc, nullptr, m_splatMapTexture.GetAddressOf());
	if (FAILED(hr)) {
		throw std::runtime_error("Failed to create splat map texture");
	}
	DirectX::SetName(m_splatMapTexture.Get(), "Splat_Map_Texture");

	// SRV ����
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = desc.Format;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = 1;
	hr = DirectX11::DeviceStates->g_pDevice->CreateShaderResourceView(m_splatMapTexture.Get(), &srvDesc, m_splatMapSRV.GetAddressOf());
	if (FAILED(hr)) {
		throw std::runtime_error("Failed to create splat map SRV");
	}
	DirectX::SetName(m_splatMapTexture.Get(), "Splat_Map_SRV");

	// �ʱ� ���÷��� ������ ����
	D3D11_MAPPED_SUBRESOURCE mapped = {};
	hr = DirectX11::DeviceStates->g_pDeviceContext->Map(m_splatMapTexture.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);

	{
		BYTE* pData = static_cast<BYTE*>(mapped.pData);

		for (UINT y = 0; y < height; ++y)
		{
			// ���� ��(row)�� ���� �ּҸ� ����մϴ�.
			BYTE* pRow = pData + y * mapped.RowPitch;

			for (UINT x = 0; x < width; ++x)
			{
				// ���� �ȼ��� �ּҸ� ����մϴ�.
				// DXGI_FORMAT_R8G8B8A8_UNORM�� �ȼ��� 4����Ʈ�Դϴ�.
				BYTE* pPixel = pRow + x * 4;

				// �ȼ� ���� �����մϴ� (R, G, B, A ����)
				pPixel[0] = 255; // R
				pPixel[1] = 0;   // G
				pPixel[2] = 0;   // B
				pPixel[3] = 0;   // A
			}
		}
	}

	DirectX11::DeviceStates->g_pDeviceContext->Unmap(m_splatMapTexture.Get(), 0);
}

void TerrainMaterial::UpdateSplatMapPatch(int offsetX, int offsetY, int patchW, int patchH, std::vector<BYTE>& patchData)
{

	D3D11_MAPPED_SUBRESOURCE mapped = {};
	HRESULT hr = DirectX11::DeviceStates->g_pDeviceContext->Map(m_splatMapTexture.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
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

	DirectX11::DeviceStates->g_pDeviceContext->Unmap(m_splatMapTexture.Get(), 0);
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
	
	//���̾� ������
	// ���� ���ҽ��� ��� �����մϴ�.
	if (m_layerTextureArray) { m_layerTextureArray->Release(); m_layerTextureArray = nullptr; }
	if (m_layerSRV) { m_layerSRV->Release(); m_layerSRV = nullptr; }
	if (p_outTextureUAV) { p_outTextureUAV->Release(); p_outTextureUAV = nullptr; }

	if (!layers.empty())
	{
		// --- ������ �ؽ�ó �迭�� UAV(Unordered Access View)�� �����մϴ�. ---
		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = 512;
		desc.Height = 512;
		desc.MipLevels = 1;
		desc.ArraySize = std::min((UINT)layers.size(), 4U); // ���� ���̾� ��, �ִ� 4��
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // ��ǻƮ ���̴��� ���������� �� ����
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;

		DirectX11::DeviceStates->g_pDevice->CreateTexture2D(&desc, nullptr, &m_layerTextureArray);

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.Format = desc.Format;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
		uavDesc.Texture2DArray.MipSlice = 0;
		uavDesc.Texture2DArray.FirstArraySlice = 0;
		uavDesc.Texture2DArray.ArraySize = desc.ArraySize;
		DirectX11::DeviceStates->g_pDevice->CreateUnorderedAccessView(m_layerTextureArray, &uavDesc, &p_outTextureUAV);

		// --- �� ���̾ ��ȸ�ϸ�, ��ǻƮ ���̴��� ������¡ �� ���縦 �����մϴ�. ---
		DirectX11::CSSetShader(m_computeShader->GetShader(), nullptr, 0);
		ID3D11UnorderedAccessView* uavs[] = { p_outTextureUAV };
		DirectX11::CSSetUnorderedAccessViews(0, 1, uavs, nullptr);

		for (size_t i = 0; i < layers.size() && i < 4; ++i)
		{
			if (layers[i].diffuseTexture && layers[i].diffuseTexture->m_pSRV)
			{
				// ��ǻƮ ���̴��� ��� �����̽��� ���� �ε��� ������ �����մϴ�.
				TerrainAddLayerBuffer addLayerBuffer;
				addLayerBuffer.slice = i;
				DirectX11::UpdateBuffer(m_AddLayerBuffer.Get(), &addLayerBuffer);
				DirectX11::CSSetConstantBuffer(0, 1, m_AddLayerBuffer.GetAddressOf());

				// ��ǻƮ ���̴��� ���� ���� �ؽ�ó�� �����մϴ�.
				ID3D11ShaderResourceView* srvs[] = { layers[i].diffuseTexture->m_pSRV };
				DirectX11::CSSetShaderResources(0, 1, srvs);

				// ��ǻƮ ���̴��� �����Ͽ� ������¡ �� ���縦 �����մϴ�.
				uint32 threadGroupCountX = (uint32)ceilf(512 / 16.0f);
				uint32 threadGroupCountY = (uint32)ceilf(512 / 16.0f);
				DirectX11::Dispatch(threadGroupCountX, threadGroupCountY, 1);
			}
		}

		// --- �۾� �Ϸ� �� ���ҽ��� �����ϰ�, �������� ���� ���� SRV�� �����մϴ�. ---
		ID3D11UnorderedAccessView* nullUAVs[]{ nullptr };
		DirectX11::CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);
		ID3D11ShaderResourceView* nullSRVs[]{ nullptr };
		DirectX11::CSSetShaderResources(0, 1, nullSRVs);

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = desc.Format;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
		srvDesc.Texture2DArray.MipLevels = 1;
		srvDesc.Texture2DArray.ArraySize = desc.ArraySize;
		srvDesc.Texture2DArray.FirstArraySlice = 0;
		DirectX11::DeviceStates->g_pDevice->CreateShaderResourceView(m_layerTextureArray, &srvDesc, &m_layerSRV);
	}

	//���÷���(Splat Map) �ؽ�ó �籸��

	//����� �°� ��	�÷��� �ؽ�ó �ʱ�ȭ
	InitSplatMapTexture(width, height);

	//�ҷ����� �� layer data ����
	std::vector<BYTE> splatData(width * height * 4, 0);
	if (!layers.empty())
	{
		for (int y = 0; y < height; ++y)
		{
			for (int x = 0; x < width; ++x)
			{
				int pixelIndex = (y * width + x);
				int byteOffset = pixelIndex * 4;
				for (int layerIdx = 0; layerIdx < layers.size() && layerIdx < 4; ++layerIdx)
				{
					float weight = std::clamp(layerHeightMap[layerIdx][pixelIndex], 0.0f, 1.0f);
					splatData[byteOffset + layerIdx] = static_cast<BYTE>(weight * 255.0f);
				}
			}
		}
	}
	UpdateSplatMapPatch(0, 0, width, height, splatData);

	// 3. ���̾� Ÿ�ϸ�(Tiling) ��� ���� ������Ʈ (���� ������ ����)
	float tilefactor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	for (size_t i = 0; i < layers.size() && i < 4; ++i)
	{
		tilefactor[i] = layers[i].tilling;
	}
	m_layerBufferData.useLayer = !layers.empty();
	m_layerBufferData.layerTilling0 = tilefactor[0];
	m_layerBufferData.layerTilling1 = tilefactor[1];
	m_layerBufferData.layerTilling2 = tilefactor[2];
	m_layerBufferData.layerTilling3 = tilefactor[3];

	DirectX11::UpdateBuffer(m_layerBuffer.Get(), &m_layerBufferData);
}
