#include "UIComponent.h"
#include "../RenderEngine/DeviceState.h"
#include "../RenderEngine/Texture.h"
#include "../RenderEngine/mesh.h"
#include "GameObject.h"
#include "transform.h"

UIComponent::UIComponent()
{
	
		m_orderID = Component::Order2Uint(ComponentOrder::MeshRenderer);
		m_typeID = TypeTrait::GUIDCreator::GetTypeID<UIComponent>();
		m_UIMesh = new UIMesh();
	
}

void UIComponent::Load(Texture* texPtr)
{
	Texture* newTexture = texPtr;
	textures.push_back(newTexture);
	if (textures.size() == 1)
	{
		SetTexture(0);
	}
}

void UIComponent::SetTexture(int index)
{
	curindex = index;
	m_curtexture = textures[curindex];
	uiinfo.size = textures[curindex]->GetImageSize();
}

GameObject* UIComponent::GetNextNavi(Direction dir)
{
	if (navigation[dir] == nullptr)
	{
		return m_pOwner;
	}
	return navigation[dir];
}

void UIComponent::SetNavi(Direction dir, GameObject* other)
{
	navigation[dir] = other;
}

void UIComponent::Update(float tick)
{
	pos = m_pOwner->m_transform.position;

	uiinfo.screenSize = { DirectX11::GetWidth(), DirectX11::GetHeight() };
	float ndcX = ((pos.x / uiinfo.screenSize.x) * 2.0f - 1.0f);
	float ndcY = 1.0f - (pos.y / uiinfo.screenSize.y) * 2.0f;
	Mathf::Vector3 ndcpos = { ndcX,ndcY, 0.f };

	float scaleX = (uiinfo.size.x / uiinfo.screenSize.x) * 2.0f;
	float scaleY = (uiinfo.size.y / uiinfo.screenSize.y) * 2.0f;
	scale = { scaleX, scaleY , 1 };
	Mathf::Vector3 parentscale = m_pOwner->m_transform.scale;

	scale *= parentscale;

	

	auto quat = m_pOwner->m_transform.rotation;
	float pitch, yaw, roll;
	Mathf::QuaternionToEular(quat, pitch,yaw,roll);
	//DirectX::XMMATRIX rotMatrix = DirectX::XMMatrixRotationQuaternion(quat);

	//// 2. 오일러 각 추출
	//
	//pitch = asinf(-rotMatrix.r[2].m128_f32[1]); // -m31

	//if (cosf(pitch) > 0.0001f)
	//{
	//	yaw = atan2f(rotMatrix.r[2].m128_f32[0], rotMatrix.r[2].m128_f32[2]); // m13, m33
	//	roll = atan2f(rotMatrix.r[0].m128_f32[1], rotMatrix.r[1].m128_f32[1]); // m21, m22
	//}
	//else
	//{
	//	yaw = atan2f(-rotMatrix.r[1].m128_f32[0], rotMatrix.r[0].m128_f32[0]); // -m12, m11
	//	roll = 0.0f;
	//}

	rotat.z = roll;
	float aspect = uiinfo.screenSize.y / uiinfo.screenSize.x; // 가로보다 세로가 얼마나 긴지
	Mathf::Matrix toSquare = Mathf::Matrix::CreateScale({ 1.0f, aspect, 1.0f });
	Mathf::Matrix rotMat = Mathf::Matrix::CreateFromYawPitchRoll(0, 0, rotat.z);
	Mathf::Matrix toOriginal = Mathf::Matrix::CreateScale({ 1.0f, 1.0f / aspect, 1.0f });
	Mathf::Matrix world =
		Mathf::Matrix::CreateScale(scale) *
		toSquare *
		rotMat *
		toOriginal *
		Mathf::Matrix::CreateTranslation(ndcpos);

	uiinfo.world = DirectX::XMMatrixTranspose(world);

}

void UIComponent::UpdateTexture()
{
	if (curindex <= 0)
		curindex = 0;

	if (curindex >= textures.size())
		curindex = textures.size() - 1;

	m_curtexture = textures[curindex];
	uiinfo.size = textures[curindex]->GetImageSize();
}
