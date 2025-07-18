#include "InputActionManager.h"
#include "SceneManager.h"
InputActionManager* InputActionManagers = nullptr;
void InputActionManager::Update(float tick)
{
	if (SceneManagers->m_isGameStart == false) return;
	if (!m_actionMaps.empty())
	{
		for (auto& actionMap : m_actionMaps)
		{
			actionMap->CheckAction();
		}
	}
}

void InputActionManager::AddActionMap()
{
	std::string baseName = "NewActionMap";
	std::string finalName = baseName;

	int uniqueIndex = 0;

	// 이미 존재하는 이름이 있는 동안 반복
	while (FindActionMap(finalName) != nullptr)
	{
		finalName = baseName + std::to_string(uniqueIndex);
		uniqueIndex++;
	}

	ActionMap* newActionMap = new ActionMap();
	newActionMap->m_name = finalName;
	m_actionMaps.push_back(newActionMap);


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
