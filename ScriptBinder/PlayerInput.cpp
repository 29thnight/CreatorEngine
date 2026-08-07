#include "PlayerInput.h"
#include "ActionMap.h"
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

	// C++ 스크립트 은퇴(9-4): 액션 디스패치 대상이던 ModuleBehavior가 사라졌다.
	// 입력 액션의 C#(ScriptComponent) 전달 경로는 후속 과제 — 액션맵 상태 갱신은
	// InputActionManager의 CheckAction() 경로가 그대로 담당한다.
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
	InputManagement->SetControllerVibration(controllerIndex, leftMotorSpeed,rightMotorSpeed,lowFre,highFre ,tick);
}

void PlayerInputComponent::SetControllerVibration(float tick, float power)
{
	InputManagement->SetControllerVibration(controllerIndex, power, power, power, power,tick);
}
