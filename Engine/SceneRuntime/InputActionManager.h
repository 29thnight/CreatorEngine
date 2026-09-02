#pragma once
#include "Core.Minimal.h"
#include "ActionMap.h"
#include "PlayerInput.h"
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
	
	// 저작 게시는 Editor Host가 소유한다. 맵마다 payload만 만들고 Player에는
	// handler가 없어 정상적으로 실패한다. 한 맵이 실패해도 나머지는 계속 쓴다.
	bool SaveManager();
	void LoadManager();

	// 맵 하나당 YAML 1.2 `.inputmap` 하나로 저장한다.
	bool SaveMap(ActionMap* actionMap);
	ActionMap* LoadMap(const std::string& filepath);
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
