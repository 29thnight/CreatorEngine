#include "UIComponent.h"
#include "GameObject.h"
UIComponent::UIComponent()
{
}
void UIComponent::SetNavi(Direction dir, GameObject* other)
{
	navigation[dir] = other;
}

GameObject* UIComponent::GetNextNavi(Direction dir)
{
	if (navigation[dir] == nullptr)
	{
		return m_pOwner;
	}
	return navigation[dir];
}
