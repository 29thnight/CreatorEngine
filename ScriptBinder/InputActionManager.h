#pragma once
#include "Core.Minimal.h"
#include "ActionMap.h"
#include "PlayerInput.h"
#include <nlohmann/json.hpp>
class InputActionManager //: public Singleton<InputActionManager>
{
	//friend class Singleton;
	
public:
	InputActionManager() {};
	~InputActionManager() = default;
	void Update(float tick);
	void AddActionMap();
	ActionMap* AddActionMap(std::string name);
	void DeleteActionMap(std::string name);
	ActionMap* FindActionMap(std::string name);
	
	void SaveManager();
	void LoadManager();

	//맵 하나당 json 한개로저장
	nlohmann::json SerializeMap(ActionMap* _actionMap);
	ActionMap* DeSerializeMap(std::string _filepath);
	void ClearActionMaps() 
	{
		for (auto& actionMap : m_actionMaps) 
		{
			delete actionMap;
		}
		m_actionMaps.clear();
	}
	std::vector<ActionMap*> m_actionMaps;

private:
};
 
extern InputActionManager* InputActionManagers;
