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
// TerrainBrush / TerrainLayer ���� (���� ����)
//-----------------------------------------------------------------------------

struct TerrainBrush
{
    struct BrushMask 
    {
        std::vector<uint8_t> m_mask;
        int m_maskWidth{ 0 };
        int m_maskHeight{ 0 };

        ID3D11Texture2D* m_maskTexture{ nullptr }; // �귯�� ����ũ �ؽ���
        ID3D11ShaderResourceView* m_maskSRV{ nullptr }; // �귯�� ����ũ SRV
    };


    enum class Mode { Raise, Lower, Flatten, PaintLayer } m_mode;
    DirectX::XMFLOAT2 m_center;
    float m_radius{ 1.0f };
    float m_strength{ 1.0f };
    float m_flatTargetHeight{ 0.0f };
    uint32_t m_layerID{ 0 };
	uint32_t m_maskID{ 0xFFFFFFFF }; // �⺻�� -1 ����ũ ID (�߰��� ��쿡�� ���)

	std::vector<BrushMask> m_masks; // �귯�� ����ũ��
    std::vector<std::string> m_maskNames{ "None" }; // ����ũ �̸���

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