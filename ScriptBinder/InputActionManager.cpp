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

void InputActionManager::DeleteActionMap(std::string name)
{
	auto deleteMap = FindActionMap(name);
	if (deleteMap != nullptr)
	{
		auto it = std::find(m_actionMaps.begin(), m_actionMaps.end(), deleteMap);
		if (it != m_actionMaps.end())
		{
			delete* it;                      
			m_actionMaps.erase(it);          
		}
	}
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
