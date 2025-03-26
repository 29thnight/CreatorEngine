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
	file::path _filepath = PathFinder::Relative("Image\\").string() + filepath.data();
	std::shared_ptr<Texture> newTexture = std::shared_ptr<Texture>(Texture::LoadFormPath(_filepath));
	textures.push_back(newTexture);
}

void UIsprite::Draw()
{
	UINT offset = 0;
	//DirectX11::IASetVertexBuffers(0, 1, m_vertexBuffer.GetAddressOf(), &m_stride, &offset);
	//DirectX11::IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
	//DirectX11::IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	//DirectX11::DrawIndexed(m_indices.size(), 0, 0);
	
	DirectX11::IASetVertexBuffers(0, 1, m_vertexBuffer.GetAddressOf(), &m_stride, &offset);
	DirectX11::IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
	DirectX11::IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	DirectX11::DrawIndexed(m_indices.size(), 0, 0);

}

void UIsprite::SetUI(Mathf::Vector2 position)
{
	Mathf::Vector3 trans = { position.x,position.y, 0 };
	Mathf::Vector3 rotat = { 0, 0, 0 };
	Mathf::Vector3 scale = { 1, 1, 1 };
	Mathf::xMatrix world = Mathf::Matrix(trans, rotat, scale);
	uiinfo.world = world;
	uiinfo.screenSize =
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
