#pragma once
#include "Core.Minimal.h"
#include "ActionMap.h"
class InputActionManager : public Singleton<InputActionManager>
{
	friend class Singleton;
public:
	InputActionManager() = default;
	~InputActionManager() = default;
	void Update(float tick);
	ActionMap* AddActionMap(std::string name);
	void DeleteActionMap(std::string name);
	ActionMap* FindActionMap(std::string name);
	
private:
	std::vector<ActionMap*> m_actionMaps;
};

static auto& InputActionManagers = InputActionManager::GetInstance();
