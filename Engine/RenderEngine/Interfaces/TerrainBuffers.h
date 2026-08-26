#pragma once
#include "Core.Minimal.h"
#include <mathematics/vector2.hpp>
#include <mathematics/vector4.hpp>
#include <cstddef>
#include <type_traits>

#define MAX_TERRAIN_LAYERS 16

// 향후 terrain pass가 그대로 constant buffer에 올릴 CPU 정본이다.
// HLSL 배열의 원소 stride가 float4(16바이트)이므로 해당 layout을 고정한다.
cbuffer TerrainLayerBuffer
{
    int useLayer{};
    int numLayers{};
    int padding1{}; // 16바이트 정렬을 위한 패딩
    int padding2{};
    math::vector4 layerTilling[MAX_TERRAIN_LAYERS]{};
};

static_assert(std::is_standard_layout_v<TerrainLayerBuffer>);
static_assert(std::is_trivially_copyable_v<TerrainLayerBuffer>);
static_assert(alignof(TerrainLayerBuffer) == 16u);
static_assert(sizeof(TerrainLayerBuffer) == 272u);
static_assert(offsetof(TerrainLayerBuffer, useLayer) == 0u);
static_assert(offsetof(TerrainLayerBuffer, numLayers) == 4u);
static_assert(offsetof(TerrainLayerBuffer, padding1) == 8u);
static_assert(offsetof(TerrainLayerBuffer, padding2) == 12u);
static_assert(offsetof(TerrainLayerBuffer, layerTilling) == 16u);

//-----------------------------------------------------------------------------
// TerrainBrush / TerrainLayer 정의 (변경 없음)
//-----------------------------------------------------------------------------

class Texture;
struct TerrainBrush
{
    struct BrushMask 
    {
        std::vector<uint8_t> m_mask;
        int m_maskWidth{ 0 };
        int m_maskHeight{ 0 };
    };

    enum class Mode { Raise, Lower, Flatten, PaintLayer, FoliageMode } m_mode;
	enum class FoliageMode { Paint, Erase } m_foliageMode;
    math::vector2 m_center{};
	bool m_isEditMode{ false }; // 편집 모드 여부
    float m_radius{ 1.0f };
    float m_strength{ 1.0f };
    float m_flatTargetHeight{ 0.0f };
    uint32_t m_layerID{ 0 };
	uint32_t m_maskID{ 0xFFFFFFFF }; // 기본은 -1 마스크 ID (추가된 경우에만 사용)
    uint32_t m_foliageTypeID{ 0 };
	int m_foliageDensity{ 10 }; // 식생 밀도

	std::vector<BrushMask> m_masks; // 브러시 마스크들
    std::vector<std::string> m_maskNames{}; // 마스크 이름들

    void SetBrushMode(Mode mode) { m_mode = mode; }

	void SetMaskID(uint32_t maskID) { m_maskID = maskID; }
	std::vector<std::string>& GetMaskNames()
	{
		return m_maskNames;
	}
};

struct TerrainLayer
{
    uint32_t m_layerID{ 0 };
    std::string layerName;
    std::wstring diffuseTexturePath;
    Texture* diffuseTexture{ nullptr };
    float tilling;
};
