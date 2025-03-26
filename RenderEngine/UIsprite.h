#pragma once
#include "Core.Minimal.h"
class Texture;

struct UIvertex
{
	DirectX::XMFLOAT3 position; // 3D ��ġ
	DirectX::XMFLOAT2 texCoord; // �ؽ�ó ��ǥ
};

struct alignas(16) UiInfo
{
	Mathf::xMatrix world;
	float2 size;
	float2 screenSize;
};
//�̹��� ������ �����
class UIsprite
{

public:
	UIsprite();
	~UIsprite();
	void Loadsprite(std::string_view filepath);

	void Draw();
	void SetUI(Mathf::Vector2 position);
	void SetTexture(size_t _index = 0);
	float2 GetSize();
	std::vector<std::shared_ptr<Texture>> textures;
	Texture* m_curtexture{};

	static constexpr uint32 m_stride = sizeof(UIvertex);
	std::vector<UIvertex> m_vertices;
	std::vector<uint32> m_indices;

	ComPtr<ID3D11Buffer> m_vertexBuffer{};
	ComPtr<ID3D11Buffer> m_indexBuffer{};


	float2 m_Size{};
	float2 m_Center{};

	UiInfo uiinfo;
	std::vector<UIvertex> UIQuad
	{
		{ { -0.5f, 0.5f, 0.0f }, { 0.0f, 0.0f } }, // �»��
		{ { 0.5f,  0.5f, 0.0f}, {1.0f, 0.0f} }, // ����
		{ { 0.5f, -0.5f, 0.0f}, {1.0f, 1.0f} }, // ���ϴ�
		{ {-0.5f, -0.5f, 0.0f}, {0.0f, 1.0f} }, // ���ϴ�

	};
	std::vector<uint32> UIIndices = { 0, 1, 2,  2, 3, 0 };
};

