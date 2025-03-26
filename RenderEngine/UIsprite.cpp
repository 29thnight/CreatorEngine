#include "UIsprite.h"
#include "Texture.h"
#include "PathFinder.h"
#include "DeviceState.h"


UIsprite::UIsprite()
{
	
}

UIsprite::~UIsprite()
{
	
}

void UIsprite::Loadsprite(std::string_view filepath)
{
	m_vertices = UIQuad;
	m_indices = UIIndices;
	m_vertexBuffer = DirectX11::CreateBuffer(sizeof(UIvertex) * m_vertices.size(), D3D11_BIND_VERTEX_BUFFER, m_vertices.data());
	m_indexBuffer = DirectX11::CreateBuffer(sizeof(uint32) * m_indices.size(), D3D11_BIND_INDEX_BUFFER, m_indices.data());
	file::path _filepath = PathFinder::Relative("Image\\").string() + filepath.data();
	std::shared_ptr<Texture> newTexture = std::shared_ptr<Texture>(Texture::LoadFormPath(_filepath));
	textures.push_back(newTexture);

}

void UIsprite::Draw()
{
	UINT offset = 0;
	DirectX11::IASetVertexBuffers(0, 1, m_vertexBuffer.GetAddressOf(), &m_stride, &offset);
	DirectX11::IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
	DirectX11::IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	DirectX11::DrawIndexed(m_indices.size(), 0, 0);
	

}

void UIsprite::SetUI(Mathf::Vector2 position)
{
	uiinfo.screenSize = { DirectX11::GetWidth(), DirectX11::GetHeight()};

	float ndcX = (position.x / uiinfo.screenSize.x) * 2.0f - 1.0f;
	float ndcY = 1.0f - (position.y / uiinfo.screenSize.y) * 2.0f;
	Mathf::Vector3 trans = { ndcX,ndcY, 0 };
	Mathf::Vector3 rotat = { 0, 0, 0 };
	float scaleX = (uiinfo.size.x / uiinfo.screenSize.x) * 2.0f;
	float scaleY = (uiinfo.size.y / uiinfo.screenSize.y) * 2.0f;
	Mathf::Vector3 scale = { scaleX, scaleY, 1 };
	Mathf::xMatrix world = Mathf::Matrix(trans, rotat, scale);
	uiinfo.world = world;
	
}



void UIsprite::SetTexture(size_t _index)
{
	m_curtexture = textures[_index].get();
	uiinfo.size = textures[_index].get()->GetImageSize();
}

float2 UIsprite::GetSize()
{
	return m_curtexture->GetImageSize();
}
