#include "InputActionManager.h"
#include "SceneManager.h"
#include "PathFinder.h"
#include "Interfaces/AssetAuthoringPort.h"
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

bool InputActionManager::SaveManager()
{
	if (m_actionMaps.empty()) return true;

	// 한 맵이 실패해도 나머지는 계속 쓴다 — 기존 동작(전부 시도)을 유지하되 실패를
	// 삼키지 않고 호출자에게 돌려준다.
	bool allSaved = true;
	for (auto& actionMap : m_actionMaps)
	{
		if (!SerializeMap(actionMap)) allSaved = false;
	}
	return allSaved;
}

void InputActionManager::LoadManager()
{
	ClearActionMaps();
	namespace fs = std::filesystem;
	fs::path dirPath = PathFinder::InputMapPath();
	if (!fs::exists(dirPath) || !fs::is_directory(dirPath))
	{
		std::cerr << "Directory does not exist: " << dirPath << std::endl;
		return;
	}

	for (const auto& entry : fs::directory_iterator(dirPath))
	{
		if (entry.is_regular_file() && entry.path().extension() == ".json")
		{
			std::string filePath = entry.path().string();
			m_actionMaps.push_back(DeSerializeMap(filePath));
		}
	}
}

bool InputActionManager::SerializeMap(ActionMap* _actionMap)
{
	if (nullptr == _actionMap) return false;

	nlohmann::json json;
	nlohmann::json actionArray = nlohmann::json::array();
	json["mapName"] = _actionMap->m_name;
	for (auto& action : _actionMap->m_actions)
	{
		nlohmann::json actionJson;
		actionJson["actionName"] = action->actionName;
		actionJson["inputType"] = InputTypeString(action->inputType);
		actionJson["actionType"] = ActionTypeString(action->actionType);
		actionJson["keystate"] = KeyStateString(action->keystate);
		int i = 0;
		for (auto& key : action->key)
		{
			if (action->inputType == InputType::KeyBoard)
			{
				actionJson["key" + std::to_string(i)] = KeyBoardString(static_cast<KeyBoard>(key));
			}
			else if (action->inputType == InputType::GamePad)
			{
				actionJson["key" + std::to_string(i)] = ControllerButtonString(static_cast<ControllerButton>(key));
			}
			else
			{

			}

			i++;
		}
		actionJson["scpritName"] = action->m_scriptName;
		actionJson["funName"] = action->funName;
		actionArray.push_back(actionJson);
	}
	json["actions"] = actionArray;

	// replace_extension은 이름에 '.'이 있으면 그 뒤를 통째로 잘라낸다("Player.v2" →
	// "Player.json"). 문자열로 붙여 이름을 보존한다.
	UncatalogedAuthoringRequest request{};
	request.destinationPath = PathFinder::InputMapPath(_actionMap->m_name + ".json");
	request.payload = json.dump(4);

	if (!AssetAuthoringPort::WriteInputActionMap(request))
	{
		Debug->LogError(
			"Input action map save requires a complete Editor authoring "
			"transaction: " + _actionMap->m_name);
		return false;
	}

	return true;
}

ActionMap* InputActionManager::DeSerializeMap(std::string _filepath)
{
	std::ifstream file(_filepath);
	if (!file.is_open())
	{
		std::cerr << "Failed to open: " << _filepath << std::endl;
		return nullptr;
	}

	nlohmann::json json;
	try {
		file >> json;
	}
	catch (std::exception& e) {
		std::cerr << "JSON parsing error: " << e.what() << std::endl;
		return nullptr;
	}
	ActionMap* newMap = new ActionMap();
	newMap->m_name = json["mapName"];

	for (auto& actionJson : json["actions"])
	{
		auto action = newMap->AddAction();

		action->actionName = actionJson["actionName"];
		action->inputType = ParseInputType(actionJson["inputType"]);
		action->actionType = ParseActionType(actionJson["actionType"]);
		action->keystate = ParseKeyState(actionJson["keystate"]);
		action->funName = actionJson["funName"];
		action->m_scriptName = actionJson["scpritName"];
		action->key.resize(4);
		// key0, key1, key2... 식으로 키값들 파싱
		for (int i = 0; ; ++i)
		{
			std::string keyName = "key" + std::to_string(i);
			if (actionJson.contains(keyName))
			{
				std::string keyname = actionJson[keyName];
				if (action->inputType == InputType::KeyBoard)
				{
					action->key[i] = static_cast<size_t>(ParseKeyBoard(keyname));
				}
				else if (action->inputType == InputType::GamePad)
				{
					if (ParseControllerButton(keyname) == ControllerButton::LEFT_THUMB ||
						ParseControllerButton(keyname) == ControllerButton::RIGHT_THUMB
						)
					{
						action->SetControllerButton(ParseControllerButton(keyname));
					}
					else
					{
						action->key[i] = static_cast<size_t>(ParseControllerButton(keyname));
					}
					
				}
				else
				{

				}


			}
			else
				break;
		}


	}

	return newMap;

}
