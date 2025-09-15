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

	//레이어 텍스처 배열 초기화
	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = 512;
	desc.Height = 512;
	desc.MipLevels = 1;
	desc.ArraySize = 4; //최대 레이어 수
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
	srvDesc.Texture2DArray.ArraySize = 4; // 최대 레이어 수
	srvDesc.Texture2DArray.FirstArraySlice = 0;

	DirectX11::DeviceStates->g_pDevice->CreateShaderResourceView(m_layerTextureArray, &srvDesc, &m_layerSRV);

	InitSplatMapTexture(width, height);
}


void TerrainMaterial::AddLayer(TerrainLayer& newLayer)
{
	float tillingFactor[4] = { m_layerBufferData.layerTilling0, m_layerBufferData.layerTilling1, m_layerBufferData.layerTilling2, m_layerBufferData.layerTilling3 };

	tillingFactor[newLayer.m_layerID] = newLayer.tilling;

	m_layerBufferData.useLayer = true; // 레이어 사용 여부를 true로 설정
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
		return; // diffuseTexturePath가 비어있으면 레이어 추가를 중단
	}

	file::path path = file::path(newLayer.diffuseTexturePath);
	if (file::exists(path))
	{
		newLayer.diffuseTexture = Texture::LoadFormPath(newLayer.diffuseTexturePath);
	}
	else {
		Debug->LogError("Failed to load diffuse texture: " + newLayer.layerName);
		return; // 로드 실패시 해당 레이어는 무시
	}

	//if (CreateTextureFromFile(DirectX11::DeviceStates->g_pDevice, newLayer.diffuseTexturePath, &diffuseResource, &diffuseSRV) == S_OK)
	//{
	//	diffuseResource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&diffuseTexture));
	//	newLayer.diffuseTexture = diffuseTexture;
	//	newLayer.diffuseSRV = diffuseSRV;
	//}
	//else {
	//	Debug->LogError("Failed to load diffuse texture: " + newLayer.layerName);
	//	return; // 로드 실패시 해당 레이어는 무시
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
	//해당 레이어의 텍스처 제거 
	//p_outTextureUAV 를 통해 컴퓨트 셰이더로 집어 넣은 텍스처를 제거하는 로직이 필요함
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
		// 각 픽셀 RGBA 순서, 1바이트씩
		BYTE* pixel = rowPtr + y * mapped.RowPitch;
		for (UINT x = 0; x < m_width; ++x, pixel += 4)
		{
			pixel[layerID] = 0; // 해당 채널 0
		}
	}
	
	DirectX11::DeviceStates->g_pDeviceContext->Unmap(m_splatMapTexture.Get(), 0);
}

void TerrainMaterial::ClearLayers()
{
	m_layerBufferData.useLayer = false; // 레이어 사용 여부를 false로 설정
	m_layerBufferData.layerTilling0 = 1.0f;
	m_layerBufferData.layerTilling1 = 1.0f;
	m_layerBufferData.layerTilling2 = 1.0f;
	m_layerBufferData.layerTilling3 = 1.0f;

	DirectX11::UpdateBuffer(m_layerBuffer.Get(), &m_layerBufferData);

	m_splatMapTexture.Reset();
	m_splatMapSRV.Reset();
	m_layerTextureArray = nullptr; // 레이어 텍스처 배열 초기화
	p_outTextureUAV = nullptr; // UAV 초기화
	m_layerSRV = nullptr; // SRV 초기화
}

void TerrainMaterial::InitSplatMapTexture(UINT width, UINT height)
{
	m_splatMapTexture.Reset();
	m_splatMapSRV.Reset();

	// 스플랫맵 텍스처 초기화
	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = width;
	desc.Height = height;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // RGBA 8-bit format
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DYNAMIC; // 동적 업데이트 가능
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE; // CPU에서 쓰기 가능
	desc.MiscFlags = 0;

	HRESULT hr = DirectX11::DeviceStates->g_pDevice->CreateTexture2D(&desc, nullptr, m_splatMapTexture.GetAddressOf());
	if (FAILED(hr)) {
		throw std::runtime_error("Failed to create splat map texture");
	}
	DirectX::SetName(m_splatMapTexture.Get(), "Splat_Map_Texture");

	// SRV 생성
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

	// 초기 스플랫맵 데이터 설정
	D3D11_MAPPED_SUBRESOURCE mapped = {};
	hr = DirectX11::DeviceStates->g_pDeviceContext->Map(m_splatMapTexture.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);

	{
		BYTE* pData = static_cast<BYTE*>(mapped.pData);

		for (UINT y = 0; y < height; ++y)
		{
			// 현재 행(row)의 시작 주소를 계산합니다.
			BYTE* pRow = pData + y * mapped.RowPitch;

			for (UINT x = 0; x < width; ++x)
			{
				// 현재 픽셀의 주소를 계산합니다.
				// DXGI_FORMAT_R8G8B8A8_UNORM은 픽셀당 4바이트입니다.
				BYTE* pPixel = pRow + x * 4;

				// 픽셀 값을 설정합니다 (R, G, B, A 순서)
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
		memcpy(destRow, srcPtr, patchW * 4); // RGBA 4채널이므로 4바이트씩 복사
	}

	DirectX11::DeviceStates->g_pDeviceContext->Unmap(m_splatMapTexture.Get(), 0);
}

void TerrainMaterial::UpdateBuffer(TerrainLayerBuffer layers)
{
	// 레이어 정보를 기반으로 버퍼 업데이트
	m_layerBufferData = layers;
	DirectX11::UpdateBuffer(m_layerBuffer.Get(), &m_layerBufferData);
}

void TerrainMaterial::MateialDataUpdate(int width, int height, std::vector<TerrainLayer>& layers, std::vector<std::vector<float>>& layerHeightMap)
{
	//size 변경 부터
	m_width = static_cast<float>(width);
	m_height = static_cast<float>(height);
	
	//레이어 제구성
	// 기존 리소스를 모두 해제합니다.
	if (m_layerTextureArray) { m_layerTextureArray->Release(); m_layerTextureArray = nullptr; }
	if (m_layerSRV) { m_layerSRV->Release(); m_layerSRV = nullptr; }
	if (p_outTextureUAV) { p_outTextureUAV->Release(); p_outTextureUAV = nullptr; }

	if (!layers.empty())
	{
		// --- 목적지 텍스처 배열과 UAV(Unordered Access View)를 생성합니다. ---
		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = 512;
		desc.Height = 512;
		desc.MipLevels = 1;
		desc.ArraySize = std::min((UINT)layers.size(), 4U); // 실제 레이어 수, 최대 4개
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // 컴퓨트 셰이더가 최종적으로 쓸 형식
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

		// --- 각 레이어를 순회하며, 컴퓨트 셰이더로 리사이징 및 복사를 수행합니다. ---
		DirectX11::CSSetShader(m_computeShader->GetShader(), nullptr, 0);
		ID3D11UnorderedAccessView* uavs[] = { p_outTextureUAV };
		DirectX11::CSSetUnorderedAccessViews(0, 1, uavs, nullptr);

		for (size_t i = 0; i < layers.size() && i < 4; ++i)
		{
			if (layers[i].diffuseTexture && layers[i].diffuseTexture->m_pSRV)
			{
				// 컴퓨트 셰이더에 어느 슬라이스에 쓸지 인덱스 정보를 전달합니다.
				TerrainAddLayerBuffer addLayerBuffer;
				addLayerBuffer.slice = i;
				DirectX11::UpdateBuffer(m_AddLayerBuffer.Get(), &addLayerBuffer);
				DirectX11::CSSetConstantBuffer(0, 1, m_AddLayerBuffer.GetAddressOf());

				// 컴퓨트 셰이더가 읽을 원본 텍스처를 설정합니다.
				ID3D11ShaderResourceView* srvs[] = { layers[i].diffuseTexture->m_pSRV };
				DirectX11::CSSetShaderResources(0, 1, srvs);

				// 컴퓨트 셰이더를 실행하여 리사이징 및 복사를 수행합니다.
				uint32 threadGroupCountX = (uint32)ceilf(512 / 16.0f);
				uint32 threadGroupCountY = (uint32)ceilf(512 / 16.0f);
				DirectX11::Dispatch(threadGroupCountX, threadGroupCountY, 1);
			}
		}

		// --- 작업 완료 후 리소스를 정리하고, 렌더링을 위한 최종 SRV를 생성합니다. ---
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

	//스플랫맵(Splat Map) 텍스처 재구성

	//사이즈에 맞게 스	플렛맵 텍스처 초기화
	InitSplatMapTexture(width, height);

	//불러오기 전 layer data 정리
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

	// 3. 레이어 타일링(Tiling) 상수 버퍼 업데이트 (이하 로직은 동일)
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
