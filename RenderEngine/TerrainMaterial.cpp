#include "TerrainMaterial.h"
#include "Texture.h"
#include <algorithm> // for std::min

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

    m_layerBufferData.useLayer = false;
    m_layerBufferData.numLayers = 0;
    for (int i = 0; i < MAX_TERRAIN_LAYERS; ++i) {
        m_layerBufferData.layerTilling[i] = { 1.0f, 0.f, 0.f, 0.f };
    }

    UpdateBuffer(m_layerBufferData);

	// 초기에는 스플랫맵 1개 레이어용으로 생성
	InitSplatMapTextureArray(width, height, 1);

}

void TerrainMaterial::ClearLayers()
{
    ComPtr<ID3D11Multithread> mt{};
    DirectX11::DeviceStates->g_pDeviceContext->QueryInterface(IID_PPV_ARGS(&mt));
    mt->SetMultithreadProtected(TRUE);
    DirectX::MTGuard lock(mt.Get());

    m_layerBufferData.useLayer = false;
    m_layerBufferData.numLayers = 0;
    for (int i = 0; i < MAX_TERRAIN_LAYERS; ++i) { m_layerBufferData.layerTilling[i] = { 1.0f, 0.0f, 0.0f, 0.0f }; }
    UpdateBuffer(m_layerBufferData);

    // ComPtr은 Reset()으로 안전하게 해제합니다.
    m_splatMapTextureArray.Reset();
    m_splatMapSRV.Reset();

    // Raw 포인터는 수동으로 해제하고 nullptr로 설정합니다.
    if (m_layerTextureArray) m_layerTextureArray->Release();
    if (p_outTextureUAV) p_outTextureUAV->Release();
    if (m_layerSRV) m_layerSRV->Release();
    m_layerTextureArray = nullptr;
    p_outTextureUAV = nullptr;
    m_layerSRV = nullptr;
}


void TerrainMaterial::InitSplatMapTextureArray(UINT width, UINT height, UINT layerCount)
{
	if (layerCount == 0) return;

    ComPtr<ID3D11Multithread> mt{};
    DirectX11::DeviceStates->g_pDeviceContext->QueryInterface(IID_PPV_ARGS(&mt));
    mt->SetMultithreadProtected(TRUE);
    DirectX::MTGuard lock(mt.Get());

	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = width;
	desc.Height = height;
	desc.MipLevels = 1;
	desc.ArraySize = layerCount;
	desc.Format = DXGI_FORMAT_R8_UNORM; // 8-bit 단일 채널 (Grayscale)
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT; // UpdateSubresource를 위해 DEFAULT로 변경
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	desc.CPUAccessFlags = 0;
	desc.MiscFlags = 0;

	DirectX11::DeviceStates->g_pDevice->CreateTexture2D(&desc, nullptr, m_splatMapTextureArray.GetAddressOf());
	DirectX::SetName(m_splatMapTextureArray.Get(), "SplatMapTextureArray");

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = desc.Format;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
	srvDesc.Texture2DArray.MostDetailedMip = 0;
	srvDesc.Texture2DArray.MipLevels = 1;
	srvDesc.Texture2DArray.FirstArraySlice = 0;
	srvDesc.Texture2DArray.ArraySize = layerCount;
	DirectX11::DeviceStates->g_pDevice->CreateShaderResourceView(m_splatMapTextureArray.Get(), &srvDesc, m_splatMapSRV.GetAddressOf());
	DirectX::SetName(m_splatMapSRV.Get(), "SplatMapSRV_Array");
}

void TerrainMaterial::UpdateSplatMapPatch(UINT layerIndex, int offsetX, int offsetY, int patchW, int patchH, std::vector<BYTE>& patchData)
{
	if (!m_splatMapTextureArray) return;

    ComPtr<ID3D11Multithread> mt{};
    DirectX11::DeviceStates->g_pDeviceContext->QueryInterface(IID_PPV_ARGS(&mt));
    mt->SetMultithreadProtected(TRUE);
    DirectX::MTGuard lock(mt.Get());

	D3D11_BOX destBox;
	destBox.left = offsetX;
	destBox.right = offsetX + patchW;
	destBox.top = offsetY;
	destBox.bottom = offsetY + patchH;
	destBox.front = 0;
	destBox.back = 1;

	UINT subresourceIndex = D3D11CalcSubresource(0, layerIndex, 1);
	UINT rowPitch = patchW * 1; // R8_UNORM 이므로 픽셀 당 1바이트
	UINT depthPitch = 0; // 2D 텍스처이므로 0

	DirectX11::DeviceStates->g_pDeviceContext->UpdateSubresource(
		m_splatMapTextureArray.Get(),
		subresourceIndex,
		&destBox,
		patchData.data(),
		rowPitch,
		depthPitch
	);
}

// 상수 버퍼만 업데이트하는 가벼운 함수
void TerrainMaterial::UpdateBuffer(const TerrainLayerBuffer& layers)
{
    ComPtr<ID3D11Multithread> mt{};
    DirectX11::DeviceStates->g_pDeviceContext->QueryInterface(IID_PPV_ARGS(&mt));
    mt->SetMultithreadProtected(TRUE);
    DirectX::MTGuard lock(mt.Get());

	m_layerBufferData = layers;

    DirectX11::UpdateBuffer(m_layerBuffer.Get(), &m_layerBufferData);
}

void TerrainMaterial::MateialDataUpdate(int width, int height, std::vector<TerrainLayer>& layers, std::vector<std::vector<float>>& layerHeightMap)
{
    ComPtr<ID3D11Multithread> mt{};
    DirectX11::DeviceStates->g_pDeviceContext->QueryInterface(IID_PPV_ARGS(&mt));
    mt->SetMultithreadProtected(TRUE);
    DirectX::MTGuard lock(mt.Get());

    m_width = width;
    m_height = height;

    // 1. 레이어 Albedo 텍스처 배열 재구성
    if (m_layerTextureArray) { m_layerTextureArray->Release(); m_layerTextureArray = nullptr; }
    if (m_layerSRV) { m_layerSRV->Release(); m_layerSRV = nullptr; }
    if (p_outTextureUAV) { p_outTextureUAV->Release(); p_outTextureUAV = nullptr; }

    if (!layers.empty())
    {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = 512; desc.Height = 512; desc.MipLevels = 1;
        desc.ArraySize = std::min((UINT)layers.size(), (UINT)MAX_TERRAIN_LAYERS);
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1; desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
        DirectX11::DeviceStates->g_pDevice->CreateTexture2D(&desc, nullptr, &m_layerTextureArray);

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = desc.Format; uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
        uavDesc.Texture2DArray.MipSlice = 0; uavDesc.Texture2DArray.FirstArraySlice = 0;
        uavDesc.Texture2DArray.ArraySize = desc.ArraySize;
        DirectX11::DeviceStates->g_pDevice->CreateUnorderedAccessView(m_layerTextureArray, &uavDesc, &p_outTextureUAV);
    
        DirectX11::CSSetShader(m_computeShader->GetShader(), nullptr, 0);
        ID3D11UnorderedAccessView* uavs[] = { p_outTextureUAV };
        DirectX11::CSSetUnorderedAccessViews(0, 1, uavs, nullptr);

        for (size_t i = 0; i < layers.size() && i < MAX_TERRAIN_LAYERS; ++i)
        {
            if (layers[i].diffuseTexture && layers[i].diffuseTexture->m_pSRV)
            {
                TerrainAddLayerBuffer addLayerBuffer; addLayerBuffer.slice = i;
                DirectX11::UpdateBuffer(m_AddLayerBuffer.Get(), &addLayerBuffer);
                DirectX11::CSSetConstantBuffer(0, 1, m_AddLayerBuffer.GetAddressOf());
                ID3D11ShaderResourceView* srvs[] = { layers[i].diffuseTexture->m_pSRV };
                DirectX11::CSSetShaderResources(0, 1, srvs);
                uint32 tgX = (uint32)ceilf(512 / 16.0f), tgY = (uint32)ceilf(512 / 16.0f);
                DirectX11::Dispatch(tgX, tgY, 1);
            }
        }
        ID3D11UnorderedAccessView* nullUAVs[]{ nullptr }; DirectX11::CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);
        ID3D11ShaderResourceView* nullSRVs[]{ nullptr }; DirectX11::CSSetShaderResources(0, 1, nullSRVs);

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = desc.Format; srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
        srvDesc.Texture2DArray.MipLevels = 1; srvDesc.Texture2DArray.ArraySize = desc.ArraySize;
        srvDesc.Texture2DArray.FirstArraySlice = 0;
        DirectX11::DeviceStates->g_pDevice->CreateShaderResourceView(m_layerTextureArray, &srvDesc, &m_layerSRV);
    }

    // 2. 스플랫맵 텍스처 배열 재구성
    UINT numLayers = static_cast<UINT>(layers.size());
    InitSplatMapTextureArray(width, height, std::min(numLayers, (UINT)MAX_TERRAIN_LAYERS));

    for (UINT i = 0; i < numLayers && i < MAX_TERRAIN_LAYERS; ++i)
    {
        std::vector<BYTE> splatData(width * height, 0);
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                int pixelIndex = y * width + x;
                float weight = std::clamp(layerHeightMap[i][pixelIndex], 0.0f, 1.0f);
                splatData[pixelIndex] = static_cast<BYTE>(weight * 255.0f);
            }
        }
        UpdateSplatMapPatch(i, 0, 0, width, height, splatData);
    }

    // 3. 상수 버퍼 업데이트
    m_layerBufferData.useLayer = !layers.empty();
    m_layerBufferData.numLayers = static_cast<int>(layers.size());
    for (int i = 0; i < MAX_TERRAIN_LAYERS; ++i)
    {
        if (i < layers.size())
            m_layerBufferData.layerTilling[i] = { layers[i].tilling, 0.f, 0.f, 0.f };
        else
            m_layerBufferData.layerTilling[i] = { 1.0f, 0.f, 0.f, 0.f };
    }
    UpdateBuffer(m_layerBufferData);
}
