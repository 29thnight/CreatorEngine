#include "PlayerInput.h"
#include "PlayerInputSystem.h"
#include "ActionMap.h"
#include "InputActionManager.h"
#include "SceneManager.h"
#include "InputManager.h"
#include "GameObject.h"
#include "ScriptComponent.h"
#include "ClrHost.h"

void PlayerInputComponent::TickInput(float tick)
{
	if (SceneManagers->m_isGameStart == false) return;
	if (m_actionMap == nullptr)
	{
		SetActionMap(m_actionMapName);
	};

	if (m_actionMap == nullptr) return;
	Entity* owner = GetOwner();
	if (nullptr == owner) return;

	auto& clr = ClrHost::Get();
	if (!clr.IsReady()) return;

	// 액션은 스크립트 타입 이름으로 묶여 있다(InputAction::m_scriptName).
	// 붙어 있는 C# 스크립트마다 자기 몫의 액션만 판정해 큐에 담는다.
	for (auto* script : owner->GetComponents<ScriptComponent>())
	{
		if (nullptr == script || !script->HasInstance()) continue;

		const int instanceId = script->GetInstanceId();
		m_actionMap->CheckAction(controllerIndex, script->m_scriptType,
			[&clr, instanceId](const std::string& functionName)
			{
				// 발생 시점에 부르지 않고 큐에 담는다 — 경계는 틱 경계에서 한 번만 넘는다.
				clr.QueueScriptMessage(instanceId, functionName);
			});
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
	InputManagement->SetControllerVibration(controllerIndex, leftMotorSpeed,rightMotorSpeed,lowFre,highFre ,tick);
}

void PlayerInputComponent::SetControllerVibration(float tick, float power)
{
	InputManagement->SetControllerVibration(controllerIndex, power, power, power, power,tick);
}

void PlayerInputComponent::OnAddedToScene()
{
	PlayerInputSystems->Register(this);
}

void PlayerInputComponent::OnRemovingFromScene()
{
	PlayerInputSystems->Unregister(this);
}
