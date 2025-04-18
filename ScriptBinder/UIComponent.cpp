#include "UIComponent.h"
#include "GameObject.h"

float MaxOreder = 100.0f;

void UIComponent::SetNavi(Direction dir, GameObject* otherUI)
{
	navigation[dir] = otherUI;
}

GameObject* UIComponent::GetNextNavi(Direction dir)
{
	if (navigation[dir] == nullptr)
	{
		return m_pOwner;
	}
	return navigation[dir];
}
