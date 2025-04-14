#include "UICollider.h"
#include "../RenderEngine/DeviceState.h"
#include "../InputManager.h"
#include "UIComponent.h"
UICollider::UICollider()
{
	m_orderID = Component::Order2Uint(ComponentOrder::Physics);
	m_typeID = TypeTrait::GUIDCreator::GetTypeID<UICollider>();

}

void UICollider::SetCollider()
{
	UIComponent* uiComponent = GetOwner()->GetComponent<UIComponent>();
	obBox.Center = uiComponent->pos;
	obBox.Extents.x = uiComponent->uiinfo.size.x;
	obBox.Extents.y = uiComponent->uiinfo.size.y;
}

//checkcollison
void UICollider::CheckClick(Mathf::Vector2 _mousePos)
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
	}
	else
	{
		isClick = false;
	}
}



void UICollider::Update(float deltaSecond)
{
	UIComponent* uiComponent = GetOwner()->GetComponent<UIComponent>();
	obBox.Center = uiComponent->pos;
	obBox.Extents.x = uiComponent->uiinfo.size.x;
	obBox.Extents.y = uiComponent->uiinfo.size.y;



	if (isClick)
	{
		std::cout<< ("UICollider Clicked") << std::endl;
	}
	else
	{
		std::cout << ("UICollider Not Clicked") << std::endl;
	}
}


