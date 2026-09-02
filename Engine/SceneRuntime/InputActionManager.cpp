#include "InputActionManager.h"
#include "AuthoringParsedDocument.h"
#include "AuthoringWriteNode.h"
#include "SceneManager.h"
#include "PathFinder.h"
#include "Interfaces/AssetAuthoringPort.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <unordered_set>
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
		if (!SaveMap(actionMap)) allSaved = false;
	}
	return allSaved;
}

void InputActionManager::LoadManager()
{
	ClearActionMaps();
	namespace fs = std::filesystem;
	const fs::path directory = PathFinder::InputMapPath();
	if (!fs::exists(directory) || !fs::is_directory(directory))
	{
		Debug->LogWarning("Input map directory does not exist: "
			+ directory.string());
		return;
	}

	std::vector<fs::path> files;
	for (const fs::directory_entry& entry : fs::directory_iterator(directory))
	{
		if (entry.is_regular_file() && entry.path().extension() == ".inputmap")
			files.push_back(entry.path());
	}
	std::sort(files.begin(), files.end(), [](const fs::path& lhs, const fs::path& rhs)
	{
		return lhs.generic_string() < rhs.generic_string();
	});

	for (const fs::path& path : files)
	{
		if (ActionMap* map = LoadMap(path.string()))
		{
			m_actionMaps.push_back(map);
		}
		else
		{
			Debug->LogError("Input map rejected: " + path.string());
		}
	}
}

bool InputActionManager::SaveMap(ActionMap* actionMap)
{
	if (nullptr == actionMap || actionMap->m_name.empty()) return false;

	Authoring::WriteDocument document;
	const Authoring::WriteNode root = document.Root();
	root.SetMap();
	root.Child("schemaVersion").SetScalar(1);
	root.Child("mapName").SetScalar(actionMap->m_name);
	const Authoring::WriteNode actions = root.Child("actions");
	actions.SetSequence();

	for (const InputAction* action : actionMap->m_actions)
	{
		if (nullptr == action || action->actionName.empty()
			|| action->key.empty() || action->key.size() > 4)
		{
			Debug->LogError("Input map contains an invalid action: "
				+ actionMap->m_name);
			return false;
		}

		const Authoring::WriteNode output = actions.Append();
		output.SetMap();
		output.Child("actionName").SetScalar(action->actionName);
		output.Child("inputType").SetScalar(InputTypeString(action->inputType));
		output.Child("actionType").SetScalar(ActionTypeString(action->actionType));
		output.Child("keyState").SetScalar(KeyStateString(action->keystate));
		const Authoring::WriteNode keys = output.Child("keys");
		keys.SetSequence(true);
		for (const std::size_t key : action->key)
			keys.Append().SetScalar(static_cast<unsigned long long>(key));
		output.Child("scriptName").SetScalar(action->m_scriptName);
		output.Child("functionName").SetScalar(action->funName);
	}

	UncatalogedAuthoringRequest request{};
	request.destinationPath =
		PathFinder::InputMapPath(actionMap->m_name + ".inputmap");
	request.payload = document.Dump();
	if (request.payload.empty()) return false;
	if (request.payload.back() != '\n') request.payload.push_back('\n');

	if (!AssetAuthoringPort::WriteInputActionMap(request))
	{
		Debug->LogError(
			"Input action map save requires a complete Editor authoring "
			"transaction: " + actionMap->m_name);
		return false;
	}
	return true;
}

ActionMap* InputActionManager::LoadMap(const std::string& filepath)
{
	std::string parseError;
	Authoring::ParsedDocument document =
		Authoring::ParsedDocument::ParseFile(filepath, parseError);
	if (!document)
	{
		Debug->LogError("Input map parse failed: " + filepath + " / " + parseError);
		return nullptr;
	}

	const Authoring::ReadNode root = document.Root();
	const Authoring::ReadNode mapNameNode = root["mapName"];
	const Authoring::ReadNode actionsNode = root["actions"];
	if (!root.IsMap() || root["schemaVersion"].As<int>(0) != 1
		|| !mapNameNode.IsScalar() || !actionsNode.IsSequence())
	{
		Debug->LogError("Input map schema is invalid: " + filepath);
		return nullptr;
	}

	const std::string mapName = mapNameNode.AsString();
	if (mapName.empty()) return nullptr;

	auto map = std::make_unique<ActionMap>();
	map->m_name = mapName;
	std::unordered_set<std::string> actionNames;
	for (const Authoring::ReadNode actionNode : actionsNode)
	{
		const Authoring::ReadNode actionNameNode = actionNode["actionName"];
		const Authoring::ReadNode inputTypeNode = actionNode["inputType"];
		const Authoring::ReadNode actionTypeNode = actionNode["actionType"];
		const Authoring::ReadNode keyStateNode = actionNode["keyState"];
		const Authoring::ReadNode keysNode = actionNode["keys"];
		const Authoring::ReadNode scriptNameNode = actionNode["scriptName"];
		const Authoring::ReadNode functionNameNode = actionNode["functionName"];
		if (!actionNode.IsMap() || !actionNameNode.IsScalar()
			|| !inputTypeNode.IsScalar() || !actionTypeNode.IsScalar()
			|| !keyStateNode.IsScalar() || !keysNode.IsSequence()
			|| !scriptNameNode.IsScalar() || !functionNameNode.IsScalar()
			|| keysNode.Size() == 0 || keysNode.Size() > 4)
		{
			Debug->LogError("Input map action schema is invalid: " + filepath);
			return nullptr;
		}

		const std::string actionName = actionNameNode.AsString();
		const std::string inputTypeName = inputTypeNode.AsString();
		const std::string actionTypeName = actionTypeNode.AsString();
		const std::string keyStateName = keyStateNode.AsString();
		if (actionName.empty() || !actionNames.insert(actionName).second
			|| (inputTypeName != "KeyBoard" && inputTypeName != "GamePad"
				&& inputTypeName != "Mouse")
			|| (actionTypeName != "Value" && actionTypeName != "Button")
			|| (keyStateName != "Down" && keyStateName != "Pressed"
				&& keyStateName != "Released"))
		{
			Debug->LogError("Input map action value is invalid: " + filepath);
			return nullptr;
		}

		auto action = std::make_unique<InputAction>();
		action->actionName = actionName;
		action->inputType = ParseInputType(inputTypeName);
		action->actionType = ParseActionType(actionTypeName);
		action->keystate = ParseKeyState(keyStateName);
		action->m_scriptName = scriptNameNode.AsString();
		action->funName = functionNameNode.AsString();
		action->key.clear();
		for (const Authoring::ReadNode keyNode : keysNode)
		{
			const unsigned long long rawKey =
				keyNode.As<unsigned long long>(
					std::numeric_limits<unsigned long long>::max());
			if (rawKey > static_cast<unsigned long long>(
					std::numeric_limits<std::size_t>::max()))
			{
				Debug->LogError("Input map key is out of range: " + filepath);
				return nullptr;
			}
			action->key.push_back(static_cast<std::size_t>(rawKey));
		}
		if (action->inputType == InputType::GamePad
			&& action->actionType == ActionType::Value)
		{
			action->m_controllerButton =
				static_cast<ControllerButton>(action->key.front());
		}
		map->m_actions.push_back(action.release());
	}
	return map.release();
}
