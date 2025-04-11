#include "SpriteComponent.h"
#include "../RenderEngine/DeviceState.h"
#include "../RenderEngine/Texture.h"
#include "../RenderEngine/mesh.h"
#include "GameObject.h"
#include "transform.h"
SpriteComponent::SpriteComponent()
{
	
		meta_default(SpriteComponent);
		m_orderID = Component::Order2Uint(ComponentOrder::MeshRenderer);
		m_typeID = TypeTrait::GUIDCreator::GetTypeID<SpriteComponent>();
		m_UIMesh = new UIMesh();
	
}

void SpriteComponent::Load(std::string_view filepath)
{
	file::path _filepath = PathFinder::Relative("UI\\").string() + filepath.data();
	std::shared_ptr<Texture> newTexture = std::shared_ptr<Texture>(Texture::LoadFormPath(_filepath));
	textures.push_back(newTexture);
}

void SpriteComponent::SetTexture(int index)
{
	curindex = index;
	m_curtexture = textures[curindex].get();
	uiinfo.size = textures[curindex]->GetImageSize();
}


void SpriteComponent::Start()
{
	
}

void SpriteComponent::Update(float tick)
{
	pos = m_pOwner->m_transform.position;

	uiinfo.screenSize = { DirectX11::GetWidth(), DirectX11::GetHeight() };
	float aspectRatio = uiinfo.screenSize.x / uiinfo.screenSize.y;

	float ndcX = ((pos.x / uiinfo.screenSize.x) * 2.0f - 1.0f);
	float ndcY = 1.0f - (pos.y / uiinfo.screenSize.y) * 2.0f;
	Mathf::Vector3 ndcpos = { ndcX,ndcY, 0.f };

	float aspect = uiinfo.screenSize.y / uiinfo.screenSize.x;
	float scaleX = (uiinfo.size.x / uiinfo.screenSize.x) * 2.0f;
	float scaleY = (uiinfo.size.y / uiinfo.screenSize.y) * 2.0f;
	scale = { scaleX, scaleY , 1 };
	Mathf::Vector3 parentscale = m_pOwner->m_transform.scale;

	scale *= parentscale;


	auto quat = m_pOwner->m_transform.rotation;
	DirectX::XMMATRIX rotMatrix = DirectX::XMMatrixRotationQuaternion(quat);

	// 2. 오일러 각 추출
	float pitch, yaw, roll;
	pitch = asinf(-rotMatrix.r[2].m128_f32[1]); // -m31

	if (cosf(pitch) > 0.0001f)
	{
		yaw = atan2f(rotMatrix.r[2].m128_f32[0], rotMatrix.r[2].m128_f32[2]); // m13, m33
		roll = atan2f(rotMatrix.r[0].m128_f32[1], rotMatrix.r[1].m128_f32[1]); // m21, m22
	}
	else
	{
		yaw = atan2f(-rotMatrix.r[1].m128_f32[0], rotMatrix.r[0].m128_f32[0]); // -m12, m11
		roll = 0.0f;
	}
	Mathf::Matrix aspectScaleMatrix = Mathf::Matrix::CreateScale({ 1.0f, aspectRatio, 1.0f });
	rotat.z = roll;
	Mathf::Matrix world = Mathf::Matrix::CreateScale(scale)
	*Mathf::Matrix::CreateFromYawPitchRoll(0,0,rotat.z)
	*Mathf::Matrix::CreateTranslation(ndcpos)
		* aspectScaleMatrix;
	uiinfo.world = DirectX::XMMatrixTranspose(world);
	
	
}

void SpriteComponent::FixedUpdate(float fixedTick)
{
}

void SpriteComponent::LateUpdate(float tick)
{
}

void SpriteComponent::UpdateTexture()
{
	if (curindex <= 0)
		curindex = 0;

	if (curindex >= textures.size())
		curindex = textures.size() - 1;

	m_curtexture = textures[curindex].get();
	uiinfo.size = textures[curindex]->GetImageSize();
}
