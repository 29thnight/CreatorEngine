#pragma once
#include "Core.Minimal.h"
#include "Mesh.h"

class Texture;

struct alignas(16) UiInfo
{
	Mathf::xMatrix world;
	float2 size;
	float2 screenSize;
};
//이미지 여러장 저장용
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

	static constexpr uint32 m_stride = sizeof(Vertex);
	std::vector<Vertex> m_vertices;
	std::vector<uint32> m_indices;

	ComPtr<ID3D11Buffer> m_vertexBuffer{};
	ComPtr<ID3D11Buffer> m_indexBuffer{};

	float2 m_Size{};
	float2 m_Center{};

	UiInfo uiinfo;
};

