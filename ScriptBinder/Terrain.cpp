#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "Terrain.h"


void TerrainComponent::loatFromPng(const std::string& filepath, std::vector<std::vector<float>>& outLayerWeights, int& outWidth, int& outHeights) {
	int width, height, channels;
	unsigned char* data = stbi_load(filepath.c_str(), &width, &height, &channels, 0);
	if (!data) {
		throw std::runtime_error("Failed to load image: " + filepath);
	}
	outWidth = width;
	outHeights = height;
	// 높이 맵 초기화
	m_heightMap.resize(width * height);
	m_vNormalMap.resize(width * height, DirectX::XMFLOAT3{ 0.0f, 1.0f, 0.0f });
	// 레이어 가중치 초기화
	outLayerWeights.resize(m_layers.size());
	for (auto& layer : outLayerWeights) {
		layer.resize(width * height, 0.0f);
	}
	// 픽셀 데이터 처리
	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			int idx = y * width + x;
			unsigned char* pixel = &data[(y * width + x) * channels];
			// 높이 맵은 R 채널로 설정
			m_heightMap[idx] = pixel[0] / 255.0f;
			// 레이어 가중치 설정 (예시: R=레이어1, G=레이어2 등)
			for (size_t i = 0; i < m_layers.size(); ++i) {
				if (i < channels) {
					outLayerWeights[i][idx] = pixel[i] / 255.0f;
				}
			}
		}
	}
	stbi_image_free(data);
}