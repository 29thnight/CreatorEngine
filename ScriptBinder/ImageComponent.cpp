#include "ImageComponent.h"
#include "../RenderEngine/DeviceState.h"
#include "../RenderEngine/Texture.h"
#include "../RenderEngine/mesh.h"
#include "GameObject.h"
#include "transform.h"

ImageComponent::ImageComponent()
{
	m_name = "ImageComponent";
	m_typeID = TypeTrait::GUIDCreator::GetTypeID<ImageComponent>();
	type = UItype::Image;
}

void ImageComponent::Load(Texture* texPtr)
{
	Texture* newTexture = texPtr;
	textures.push_back(newTexture);
	if (textures.size() == 1)
	{
		SetTexture(0);
	}
}

void ImageComponent::SetTexture(int index)
{
	curindex = index;
	m_curtexture = textures[curindex];
	uiinfo.size = textures[curindex]->GetImageSize();

	origin = { uiinfo.size.x / 2, uiinfo.size.y / 2 };
}


void ImageComponent::Update(float tick)
{
	Transform transform = m_pOwner->m_transform;
	pos = Mathf::Vector3(transform.position);
	scale = { transform.scale};
	auto quat = m_pOwner->m_transform.rotation;
	float pitch, yaw, roll;
	Mathf::QuaternionToEular(quat, pitch, yaw, roll);
	rotate = roll;

}

void ImageComponent::Draw(SpriteBatch* sBatch)
{
	if (_layerorder < 0) _layerorder = 0;
	if(m_curtexture !=nullptr)
	sBatch->Draw(m_curtexture->m_pSRV, { pos.x,pos.y }, nullptr, Colors::White, rotate, origin, scale,
		SpriteEffects_None, _layerorder / MaxOreder);

}


void ImageComponent::UpdateTexture()
{
	if (curindex <= 0)
		curindex = 0;
	if (curindex >= textures.size())
		curindex = textures.size() - 1;

	SetTexture(curindex);
}
