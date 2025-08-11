#include "UIComponent.h"
#include "GameObject.h"

float MaxOreder = 100.0f;

void UIComponent::SetNavi(Direction dir, const std::shared_ptr<GameObject>& otherUI)
{
	navigation[dir] = otherUI;
}

GameObject* UIComponent::GetNextNavi(Direction dir)
{
	if (navigation.find(dir) != navigation.end() && !navigation[dir].expired())
	{
		std::shared_ptr<GameObject> sharedPtr = navigation[dir].lock();
		return sharedPtr.get();
	}
	return nullptr;
}
