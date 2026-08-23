#include "TerrainMaterial.h"
#include <algorithm>

void TerrainMaterial::Initialize(uint32_t width, uint32_t height)
{
	m_width = static_cast<int>(width);
	m_height = static_cast<int>(height);

	ResetLayerConstants();

	// 초기에는 레이어 하나짜리 마스크로 시작한다(예전 InitSplatMapTextureArray가
	// layerCount = 1로 텍스처 배열을 만들던 것과 같은 자리다).
	m_splatMasks.assign(1, std::vector<uint8_t>(static_cast<size_t>(m_width) * m_height, 0));
	++m_revision;
}

void TerrainMaterial::ResetLayerConstants()
{
	m_layerBufferData.useLayer = false;
	m_layerBufferData.numLayers = 0;
	for (int i = 0; i < MAX_TERRAIN_LAYERS; ++i)
	{
		m_layerBufferData.layerTilling[i] = { 1.0f, 0.f, 0.f, 0.f };
	}
}

void TerrainMaterial::ClearLayers()
{
	ResetLayerConstants();
	m_splatMasks.clear();
	++m_revision;
}

void TerrainMaterial::UpdateBuffer(const TerrainLayerBuffer& layers)
{
	m_layerBufferData = layers;
	++m_revision;
}

void TerrainMaterial::UpdateSplatMapPatch(uint32_t layerIndex, int offsetX, int offsetY,
                                          int patchW, int patchH, const std::vector<uint8_t>& patchData)
{
	if (layerIndex >= m_splatMasks.size()) return;
	if (patchW <= 0 || patchH <= 0) return;

	// 조각이 지형 밖으로 나가면 걸친 부분만 쓴다. 예전에는 D3D11_BOX가
	// 범위를 벗어나면 UpdateSubresource가 조용히 아무 일도 하지 않았다 —
	// 같은 자리에서 이제는 클램프한다.
	auto& mask = m_splatMasks[layerIndex];
	if (mask.size() != static_cast<size_t>(m_width) * m_height) return;

	const int x0 = std::max(0, offsetX);
	const int y0 = std::max(0, offsetY);
	const int x1 = std::min(m_width, offsetX + patchW);
	const int y1 = std::min(m_height, offsetY + patchH);

	for (int y = y0; y < y1; ++y)
	{
		const int srcRow = (y - offsetY) * patchW;
		const int dstRow = y * m_width;
		for (int x = x0; x < x1; ++x)
		{
			const size_t src = static_cast<size_t>(srcRow) + (x - offsetX);
			if (src >= patchData.size()) continue;
			mask[static_cast<size_t>(dstRow) + x] = patchData[src];
		}
	}

	++m_revision;
}

void TerrainMaterial::MateialDataUpdate(int width, int height,
                                        std::vector<TerrainLayer>& layers,
                                        std::vector<std::vector<float>>& layerHeightMap)
{
	m_width = width;
	m_height = height;

	const size_t layerCount = std::min<size_t>(layers.size(), MAX_TERRAIN_LAYERS);
	const size_t pixels = static_cast<size_t>(width) * height;

	// 레이어 가중치(0..1 float)를 8비트로 굳혀 둔다. 예전에는 이 값을 바로
	// 스플랫맵 텍스처 배열에 올리고 CPU 쪽은 남기지 않았다.
	m_splatMasks.assign(layerCount, std::vector<uint8_t>(pixels, 0));
	for (size_t i = 0; i < layerCount; ++i)
	{
		if (i >= layerHeightMap.size()) continue;
		const auto& weights = layerHeightMap[i];

		auto& mask = m_splatMasks[i];
		const size_t count = std::min(pixels, weights.size());
		for (size_t p = 0; p < count; ++p)
		{
			const float weight = std::clamp(weights[p], 0.0f, 1.0f);
			mask[p] = static_cast<uint8_t>(weight * 255.0f);
		}
	}

	m_layerBufferData.useLayer = !layers.empty();
	m_layerBufferData.numLayers = static_cast<int>(layers.size());
	for (int i = 0; i < MAX_TERRAIN_LAYERS; ++i)
	{
		m_layerBufferData.layerTilling[i] = (static_cast<size_t>(i) < layers.size())
			? DirectX::XMFLOAT4{ layers[i].tilling, 0.f, 0.f, 0.f }
			: DirectX::XMFLOAT4{ 1.0f, 0.f, 0.f, 0.f };
	}

	++m_revision;
}
