#pragma once
#include "TerrainBuffers.h"
#include <vector>

//-----------------------------------------------------------------------------
// TerrainMaterial: 지형 레이어의 CPU 자료
//
// ★ DX11 GPU 자원을 걷어낸 자리다(PHASE 11 착수, 2026-08-08).
//   여기 있던 것: 스플랫맵 텍스처 배열 + SRV, 레이어 Albedo 텍스처 배열 +
//   UAV + SRV, 레이어 상수 버퍼 둘, 그리고 레이어 배열을 굽던 DX11 컴퓨트
//   셰이더(TerrainTexture.cs).
//
//   ★ 그 전부의 소비자가 0이었다. DX12에 지형 렌더 경로가 없어서 만들기만
//   하고 아무도 읽지 않았다(GetSplatMapSRV·GetLayerSRV·GetLayerBuffer의
//   호출자 0곳). 즉 걷어내면서 잃은 기능이 없다.
//
//   이제 CPU 자료가 진실이다 — 편집(조각·페인팅)이 여기에 쓰고, 새로 쓸
//   지형 패스가 여기서 읽어 올린다. 그 순서가 뒤집혀 있던 것이 T5에서
//   발견한 결함이었다(GPU 버퍼만 갱신하고 CPU 배열은 그대로 두던 것).
//-----------------------------------------------------------------------------

class Texture;
class TerrainMaterial
{
public:
	TerrainMaterial() = default;
	~TerrainMaterial() = default;

	void Initialize(uint32_t width, uint32_t height);
	void ClearLayers();

	/// 레이어 상수(타일링·개수)를 갱신한다.
	void UpdateBuffer(const TerrainLayerBuffer& layers);

	/// 스플랫 마스크의 한 조각을 갱신한다 — 페인팅이 부르는 자리.
	/// 조각 경계는 호출부가 이미 영역 단위로 들고 있어 그대로 쓴다.
	void UpdateSplatMapPatch(uint32_t layerIndex, int offsetX, int offsetY,
	                         int patchW, int patchH, const std::vector<uint8_t>& patchData);

	/// CPU 자료 전체 재구성. 크기 변경·레이어 추가·삭제가 부른다.
	void MateialDataUpdate(
		int width, int height,
		std::vector<TerrainLayer>& layers,
		std::vector<std::vector<float>>& layerHeightMap
	);

	/// 레이어별 스플랫 마스크(width × height, 8비트 가중치).
	const std::vector<std::vector<uint8_t>>& GetSplatMasks() const { return m_splatMasks; }

	/// 자료가 바뀔 때마다 오른다. 지형 패스가 "올린 것이 최신인가"를 이 값으로
	/// 판정한다 — 마스크 전체를 매 프레임 비교할 이유가 없다.
	uint32_t GetRevision() const { return m_revision; }

	// 레이어 상수. Terrain 쪽이 직접 읽고 쓴다.
	TerrainLayerBuffer m_layerBufferData;

	int m_width{ 0 };
	int m_height{ 0 };

private:
	void ResetLayerConstants();

	std::vector<std::vector<uint8_t>> m_splatMasks;
	uint32_t m_revision{ 0 };
};
