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
    enum class Mode { Raise, Lower, Flatten, PaintLayer } m_mode;
    DirectX::XMFLOAT2 m_center;
    float m_radius{ 1.0f };
    float m_strength{ 1.0f };
    float m_flatTargetHeight{ 0.0f };
    uint32_t m_layerID{ 0 };

    void SetBrushMode(Mode mode) { m_mode = mode; }
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