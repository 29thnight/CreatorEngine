#include "PlayerInput.h"
#include "ActionMap.h"
#include "ModuleBehavior.h"
#include "InputActionManager.h"
#include "SceneManager.h"
#include "InputManager.h"
void PlayerInputComponent::Update(float tick)
{
	if (SceneManagers->m_isGameStart == false) return;
	if (m_actionMap == nullptr)
	{
		SetActionMap(m_actionMapName);
	};

	if (m_actionMap == nullptr) return;
	GameObject* owner = GetOwner();

	std::vector<ModuleBehavior*> scripts{};
	ModuleBehavior* script = nullptr;
	for (auto& component : owner->m_components)
	{
		if (nullptr == component)
			continue;
		script = dynamic_cast<ModuleBehavior*>(component.get());
		if (script != nullptr)
		{
			scripts.push_back(script);
		};
	}

	if (scripts.size() == 0) return;

	for (auto& _script : scripts)
	{
		auto typeName = Meta::Find(_script->GetHashedName().ToString());
		void* voidPtr = static_cast<void*>(_script);
		m_actionMap->CheckAction(controllerIndex, voidPtr, typeName);
	}
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

void PlayerInputComponent::SetControllerVibration(float tick, float leftMotorSpeed, float rightMotorSpeed, float lowFre, float highFre)
{
	InputManagement->SetControllerVibrationTime(controllerIndex,tick);
	InputManagement->SetControllerVibration(controllerIndex, leftMotorSpeed,rightMotorSpeed,lowFre,highFre);
}

void PlayerInputComponent::SetControllerVibration(float tick, float power)
{
	InputManagement->SetControllerVibrationTime(controllerIndex, tick);
	InputManagement->SetControllerVibration(controllerIndex, power, power, power, power);
}
