#include "UIButton.h"
#include "../RenderEngine/DeviceState.h"
#include "../InputManager.h"
#include "ImageComponent.h"
UIButton::UIButton(std::function<void()> func)
{
	m_orderID = Component::Order2Uint(ComponentOrder::BehaviorScript);
	m_typeID = TypeTrait::GUIDCreator::GetTypeID<UIButton>();
	m_clickFunction = func;
}

void UIButton::Update(float deltaSecond)
{
	UpdateCollider();
}

void UIButton::UpdateCollider()
{
	GameObject* owner = GetOwner();
	owner->m_transform.GetWorldPosition();
	auto pos = owner->m_transform.GetWorldPosition();

	ImageComponent* Image = GetOwner()->GetComponent<ImageComponent>();
	obBox.Center = Mathf::Vector3{ pos.m128_f32[0], pos.m128_f32[1],0 };
	obBox.Extents.x = Image->uiinfo.size.x;
	obBox.Extents.y = Image->uiinfo.size.y;
}

bool UIButton::CheckClick(Mathf::Vector2 _mousePos)
{


	Mathf::Vector2 gameViewPos = InputManagement->GameViewpos;
	Mathf::Vector2 gameViewSize = InputManagement->GameViewsize;

	Mathf::Vector2 screenSize = { DirectX11::GetWidth(), DirectX11::GetHeight() };
	float localX = (_mousePos.x - gameViewPos.x) * (screenSize.x / gameViewSize.x);
	float localY = (_mousePos.y - gameViewPos.y) * (screenSize.y / gameViewSize.y);



	float minX = obBox.Center.x - obBox.Extents.x;
	float maxX = obBox.Center.x + obBox.Extents.x;
	float minY = obBox.Center.y - obBox.Extents.y;
	float maxY = obBox.Center.y + obBox.Extents.y;

	if (localX > minX && localX < maxX &&
		localY > minY && localY < maxY)
	{
		isClick = true;
		return true;
	}
	else
	{
		isClick = false;
	}

	return false;
}