#include "InputActionManager.h"

void InputActionManager::Update(float tick)
{
	if (!m_actionMaps.empty())
	{
		for (auto& actionMap : m_actionMaps)
		{
			actionMap->CheckAction();
		}
	}
}

ActionMap* InputActionManager::AddActionMap(std::string name)
{
	for (auto& actionMap : m_actionMaps)
	{
		if (actionMap->m_name == name)
		{
			return actionMap;
		}
		
	}
	
	ActionMap* newActionMap = new ActionMap();
	newActionMap->m_name = name;
	m_actionMaps.push_back(newActionMap);
	return newActionMap;
}

ActionMap* InputActionManager::FindActionMap(std::string name)
{
	for (auto& actionMap : m_actionMaps)
	{
		if (actionMap->m_name == name)
		{
			return actionMap;
		}
	}
	std::cout << "ActionMap not found: " << name << std::endl;
	return nullptr;
}
