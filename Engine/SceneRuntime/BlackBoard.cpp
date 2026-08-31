#include "AuthoringParsedDocument.h"
#include "BlackBoard.h"
#include "Entity.h"
#include "Transform.h"
#include "SceneManager.h"
#include "Interfaces/AssetAuthoringPort.h"

#include <sstream>

namespace
{
	// 이름→경로 규약은 한 곳에만 둔다. 저작 쓰기와 런타임 읽기가 각자 경로를
	// 조립하면 조용히 갈라진다.
	file::path ResolveBlackBoardPath(std::string_view name)
	{
		return PathFinder::Relative(
			"BehaviorTree\\" + std::string(name) + ".blackboard");
	}
}

BlackBoardValue& BlackBoard::GetOrCreate(const std::string& key)
{
	return m_values[key]; // default 생성
}

const BlackBoardValue& BlackBoard::GetChecked(const std::string& key, BlackBoardType expected) const
{
	auto it = m_values.find(key);
	if (it == m_values.end())
	{
		// If the key does not exist, throw an error
		Debug->LogError("BlackBoard key not found: " + key);
		throw std::runtime_error("BlackBoard key not found: " + key);
	}

	if (it->second.Type != expected)
	{
		Debug->LogError("BlackBoard type mismatch for key: " + key +
			". Expected: " + BlackBoardTypeToString(expected) +
			", Actual: " + BlackBoardTypeToString(it->second.Type));

		throw std::runtime_error("BlackBoard type mismatch for key: " + key + 
			". Expected: " + BlackBoardTypeToString(expected) + 
			", Actual: " + BlackBoardTypeToString(it->second.Type));
	}

	return it->second;
}

// Setters
void BlackBoard::SetValueAsBool(const std::string& key, bool value)
{
	auto& entry = GetOrCreate(key);
	entry.Type = BlackBoardType::Bool;
	entry.BoolValue = value;
	//m_valueChangedDelegate.Broadcast(key);
}

void BlackBoard::SetValueAsInt(const std::string& key, int value)
{
	auto& entry = GetOrCreate(key);
	entry.Type = BlackBoardType::Int;
	entry.IntValue = value;
	//m_valueChangedDelegate.Broadcast(key);
}

void BlackBoard::SetValueAsFloat(const std::string& key, float value)
{
	auto& entry = GetOrCreate(key);
	entry.Type = BlackBoardType::Float;
	entry.FloatValue = value;
	//m_valueChangedDelegate.Broadcast(key);
}

void BlackBoard::SetValueAsString(const std::string& key, const std::string& value)
{
	auto& entry = GetOrCreate(key);
	entry.Type = BlackBoardType::String;
	entry.StringValue = value;
	//m_valueChangedDelegate.Broadcast(key);
}

void BlackBoard::SetValueAsVector2(const std::string& key, const math::vector2& value)
{
	auto& entry = GetOrCreate(key);
	entry.Type = BlackBoardType::Vector2;
	entry.Vec2Value = value;
	//m_valueChangedDelegate.Broadcast(key);
}

void BlackBoard::SetValueAsVector3(const std::string& key, const math::vector3& value)
{
	auto& entry = GetOrCreate(key);
	entry.Type = BlackBoardType::Vector3;
	entry.Vec3Value = value;
	//m_valueChangedDelegate.Broadcast(key);
}

void BlackBoard::SetValueAsVector4(const std::string& key, const math::vector4& value)
{
	auto& entry = GetOrCreate(key);
	entry.Type = BlackBoardType::Vector4;
	entry.Vec4Value = value;
	//m_valueChangedDelegate.Broadcast(key);
}

void BlackBoard::SetValueAsGameObject(const std::string& key, const std::string& objectName)
{
	auto& entry = GetOrCreate(key);
	entry.Type = BlackBoardType::Entity;
	entry.StringValue = objectName;
	//m_valueChangedDelegate.Broadcast(key);
}

void BlackBoard::SetValueAsTransform(const std::string& key, const std::string& transformPath)
{
	auto& entry = GetOrCreate(key);
	entry.Type = BlackBoardType::Transform;
	entry.StringValue = transformPath;
	//m_valueChangedDelegate.Broadcast(key);
}

// Getters
bool BlackBoard::GetValueAsBool(const std::string& key) const
{
	return GetChecked(key, BlackBoardType::Bool).BoolValue;
}

int BlackBoard::GetValueAsInt(const std::string& key) const
{
	return GetChecked(key, BlackBoardType::Int).IntValue;
}

float BlackBoard::GetValueAsFloat(const std::string& key) const
{
	return GetChecked(key, BlackBoardType::Float).FloatValue;
}

const std::string& BlackBoard::GetValueAsString(const std::string& key) const
{
	return GetChecked(key, BlackBoardType::String).StringValue;
}

const math::vector2& BlackBoard::GetValueAsVector2(const std::string& key) const
{
	return GetChecked(key, BlackBoardType::Vector2).Vec2Value;
}

const math::vector3& BlackBoard::GetValueAsVector3(const std::string& key) const
{
	return GetChecked(key, BlackBoardType::Vector3).Vec3Value;
}

const math::vector4& BlackBoard::GetValueAsVector4(const std::string& key) const
{
	return GetChecked(key, BlackBoardType::Vector4).Vec4Value;
}

Entity* BlackBoard::GetValueAsGameObject(const std::string& key) const
{
	auto& entry = GetChecked(key, BlackBoardType::Entity);
	auto gameObject = Entity::Find(entry.StringValue);
	if (!gameObject)
	{
		Debug->LogError("Entity not found: " + entry.StringValue);

		throw std::runtime_error("Entity not found: " + entry.StringValue);
	}

	return gameObject;
}

const Transform& BlackBoard::GetValueAsTransform(const std::string& key) const
{
	auto& entry = GetChecked(key, BlackBoardType::Transform);
	auto gameObject = Entity::Find(entry.StringValue);
	if (!gameObject)
	{
		Debug->LogError("Entity not found: " + entry.StringValue);

		throw std::runtime_error("Entity not found: " + entry.StringValue);
	}

	return gameObject->Transform_();
}

void BlackBoard::AddKey(const std::string& key, const BlackBoardType& type)
{
	if (HasKey(key)) return;

	m_values[key].Type = type;
}

// Other
bool BlackBoard::HasKey(const std::string& key) const
{
	return m_values.find(key) != m_values.end();
}

BlackBoardType BlackBoard::GetType(const std::string& key) const
{
	auto it = m_values.find(key);
	if (it != m_values.end())
		return it->second.Type;
	return BlackBoardType::None;
}

void BlackBoard::RemoveKey(const std::string& key)
{
	m_values.erase(key);
}

void BlackBoard::RenameKey(const std::string& curKey, const std::string& newKey)
{
	BlackBoardValue curValue;
	if(m_values.find(curKey) != m_values.end())
	{
		curValue = m_values[curKey];
		m_values.erase(curKey);
		m_values[newKey] = curValue;
	}
}

bool BlackBoard::Serialize(std::string_view name)
{
	// 빈 이름은 파일명이 ".blackboard"가 되는데, 선행 점만 있는 이름은
	// stem()이 이름 전체를 돌려주므로 Editor가 확장자를 한 번 더 붙인다.
	// 그러면 저장 경로와 Deserialize의 읽기 경로가 조용히 갈라진다.
	if (name.empty())
	{
		Debug->LogError("BlackBoard save requires a non-empty name");
		return false;
	}

	if (m_name != name)
	{
		m_name = name;
	}

	// 빈 시퀀스를 명시한다. 손대지 않은 Node를 그대로 흘리면 yaml-cpp가 0바이트를
	// 내보내고, 그렇게 저장된 자산은 Deserialize가 값 하나도 복원하지 못한다.
	MetaYml::Node entriesNode(MetaYml::NodeType::Sequence);
	for (auto& [key, value] : m_values)
	{
		MetaYml::Node entryNode;
		entryNode["key"] = key;
		entryNode["value"] = Meta::Serialize(&value);
		entriesNode.push_back(entryNode);
	}

	MetaYml::Node node;
	node[m_name] = entriesNode;

	std::ostringstream payload;
	payload << node;

	// 목적 경로는 런타임 읽기와 같은 규약에서 만든다. 확장자를 붙이고 파일을
	// 게시하는 일은 Editor Host가 소유한다.
	const file::path assetPath = ResolveBlackBoardPath(name);

	TextAssetAuthoringRequest request{};
	request.destinationDirectory = assetPath.parent_path();
	request.name = assetPath.stem().wstring();
	request.payload = payload.str();

	TextAssetAuthoringResult result{};
	if (!AssetAuthoringPort::WriteBlackBoard(request, result))
	{
		Debug->LogError(
			"BlackBoard save requires a complete Editor authoring transaction: " +
			std::string(name));
		return false;
	}

	return true;
}

void BlackBoard::Deserialize(std::string_view name)
{
	if (m_name != name)
	{
		m_name = name;
	}

	file::path filePath = ResolveBlackBoardPath(name);
	if (!file::exists(filePath))
	{
		Debug->LogError("Blackboard file not found: " + filePath.string());

		throw std::runtime_error("Blackboard file not found: " + filePath.string());
	}

	// D3-b-L: ryml로 읽는다. 이 파서는 자기 파일만 읽고 평범한 데이터를
	// 내놓으므로 소비자가 backend에 묶여 있지 않다 — 씬 경로보다 먼저 옮길 수 있다.
	//
	// ★ 문서가 트리를 소유한다. 아래 노드들은 이 스코프를 벗어나면 안 된다.
	std::string parseError;
	const Authoring::ParsedDocument document =
		Authoring::ParsedDocument::ParseFile(filePath.string(), parseError);
	if (!document)
	{
		Debug->LogError("Blackboard parse failed: " + filePath.string() + " (" + parseError + ")");
		throw std::runtime_error("Blackboard parse failed: " + filePath.string());
	}
	const Authoring::ReadNode entries = document.Root()[m_name.c_str()];
	for (const auto entry : entries)
	{
		std::string key = entry["key"].AsString();
		if (key.empty() || m_values.find(key) != m_values.end())
			continue; // Skip empty keys

		BlackBoardValue& bbValue = m_values[key]; // Get or create the entry
		Meta::Deserialize(&bbValue, entry["value"]);
	}
}

void BlackBoard::Clear()
{
	m_name.clear();
	m_values.clear();
}