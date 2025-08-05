#pragma once
#include "Core.Minimal.h"

cbuffer TerrainAddLayerBuffer
{
	UINT slice;
};

cbuffer TerrainGizmoBuffer
{
	float2 gBrushPosition;
	float gBrushRadius;
	int maskWidth{ 0 }; // 브러시 마스크의 너비
	int maskHeight{ 0 }; // 브러시 마스크의 높이
    bool32 isEditMode{ false }; // 편집 모드 여부
};

cbuffer TerrainLayerBuffer
{
	bool32 useLayer{ false };
	float layerTilling0;
	float layerTilling1;
	float layerTilling2;
	float layerTilling3;
};

//-----------------------------------------------------------------------------
// TerrainBrush / TerrainLayer 정의 (변경 없음)
//-----------------------------------------------------------------------------

struct TerrainBrush
{
    struct BrushMask 
    {
        std::vector<uint8_t> m_mask;
        int m_maskWidth{ 0 };
        int m_maskHeight{ 0 };

        ID3D11Texture2D* m_maskTexture{ nullptr }; // 브러시 마스크 텍스쳐
        ID3D11ShaderResourceView* m_maskSRV{ nullptr }; // 브러시 마스크 SRV
    };

    enum class Mode { Raise, Lower, Flatten, PaintLayer, FoliageMode } m_mode;
	enum class FoliageMode { Paint, Erase } m_foliageMode;
    DirectX::XMFLOAT2 m_center;
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
    ID3D11Texture2D* diffuseTexture{ nullptr };
    ID3D11ShaderResourceView* diffuseSRV{ nullptr };
    float tilling;
};