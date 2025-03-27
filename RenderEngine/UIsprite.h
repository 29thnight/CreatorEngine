#pragma once
#include "Core.Minimal.h"
class Texture;

struct UIvertex
{
	DirectX::XMFLOAT3 position; 
	DirectX::XMFLOAT2 texCoord; 
};

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

	void Update();
	void Draw();
	void SetUI(Mathf::Vector2 position,int layerorder);
	void SetTexture(size_t _index = 0);
	float2 GetSize();




	std::vector<std::shared_ptr<Texture>> textures;
	Texture* m_curtexture{};

	
	//숫자가 클수록 젤위에보임
	int _layerorder;
	UiInfo uiinfo;

	Mathf::Vector3 trans{};
	Mathf::Vector4 rotat{ 0,0,0,1 };
	Mathf::Vector3 scale{ 1,1,1 };
private:
	static constexpr uint32 m_stride = sizeof(UIvertex);
	std::vector<UIvertex> m_vertices;
	std::vector<uint32> m_indices;

	ComPtr<ID3D11Buffer> m_vertexBuffer{};
	ComPtr<ID3D11Buffer> m_indexBuffer{};

	

	


	float2 m_Size{};
	float2 m_Center{};

	
	std::vector<UIvertex> UIQuad
	{
		{ { -1.0f, 1.0f, 0.0f}, { 0.0f, 0.0f } }, // 좌상단
		{ { 1.0f,  1.0f, 0.0f}, { 1.0f, 0.0f} }, // 우상단
		{ { 1.0f, -1.0f, 0.0f}, { 1.0f, 1.0f} }, // 우하단
		{ {-1.0f, -1.0f, 0.0f}, {0.0f, 1.0f} }, // 좌하단

	};
	std::vector<uint32> UIIndices = { 0, 1, 2, 0, 2, 3 };
};

