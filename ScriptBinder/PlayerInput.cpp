#include "PlayerInput.h"
#include "ActionMap.h"
#include "ModuleBehavior.h"
#include "InputActionManager.h"
void PlayerInputComponent::Update(float tick)
{
	if (m_actionMap == nullptr) return;

	GameObject* owner = GetOwner();

	std::vector<ModuleBehavior*> scripts{};
	ModuleBehavior* script = nullptr;
	for (auto& component : owner->m_components)
	{
		if (nullptr == component)
			continue;
		script = dynamic_cast<ModuleBehavior*>(component.get());
		if (script != nullptr) break;
	}

	if (script == nullptr) return;

	auto typeName = Meta::Find(script->GetHashedName().ToString());
	void* voidPtr = static_cast<void*>(script);
	m_actionMap->CheckAction(controllerIndex,voidPtr, typeName);
}

void PlayerInputComponent::SetActionMap(std::string mapName)
{
	for (auto& map : InputActionManagers->m_actionMaps)
	{
		if (map->m_name == mapName)
		{
			m_actionMap = map;
		}
	}
}

void PlayerInputComponent::SetActionMap(ActionMap* _actionMap)
{
	m_actionMap = _actionMap;
	m_actionMapName = _actionMap->m_name;
}
