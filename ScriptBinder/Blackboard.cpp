#include "Blackboard.h"
#include "GameObject.h"
#include "Transform.h"
#include "SceneManager.h"
#include "MetaYaml.h"

BlackBoardValue& BlackBoard::GetOrCreate(const std::string& key)
{
	return m_values[key]; // default 
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

void BlackBoard::SetValueAsVector2(const std::string& key, const Mathf::Vector2& value)
{
	auto& entry = GetOrCreate(key);
	entry.Type = BlackBoardType::Vector2;
	entry.Vec2Value = value;
	//m_valueChangedDelegate.Broadcast(key);
}

void BlackBoard::SetValueAsVector3(const std::string& key, const Mathf::Vector3& value)
{
	auto& entry = GetOrCreate(key);
	entry.Type = BlackBoardType::Vector3;
	entry.Vec3Value = value;
	//m_valueChangedDelegate.Broadcast(key);
}

void BlackBoard::SetValueAsVector4(const std::string& key, const Mathf::Vector4& value)
{
	auto& entry = GetOrCreate(key);
	entry.Type = BlackBoardType::Vector4;
	entry.Vec4Value = value;
	//m_valueChangedDelegate.Broadcast(key);
}

void BlackBoard::SetValueAsGameObject(const std::string& key, const std::string& objectName)
{
	auto& entry = GetOrCreate(key);
	entry.Type = BlackBoardType::GameObject;
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

const Mathf::Vector2& BlackBoard::GetValueAsVector2(const std::string& key) const
{
	return GetChecked(key, BlackBoardType::Vector2).Vec2Value;
}

const Mathf::Vector3& BlackBoard::GetValueAsVector3(const std::string& key) const
{
	return GetChecked(key, BlackBoardType::Vector3).Vec3Value;
}

const Mathf::Vector4& BlackBoard::GetValueAsVector4(const std::string& key) const
{
	return GetChecked(key, BlackBoardType::Vector4).Vec4Value;
}

GameObject* BlackBoard::GetValueAsGameObject(const std::string& key) const
{
	auto& entry = GetChecked(key, BlackBoardType::GameObject);
	auto gameObject = GameObject::Find(entry.StringValue);
	if (!gameObject)
	{
		Debug->LogError("GameObject not found: " + entry.StringValue);

		throw std::runtime_error("GameObject not found: " + entry.StringValue);
	}

	return gameObject;
}

const Transform& BlackBoard::GetValueAsTransform(const std::string& key) const
{
	auto& entry = GetChecked(key, BlackBoardType::Transform);
	auto gameObject = GameObject::Find(entry.StringValue);
	if (!gameObject)
	{
		Debug->LogError("GameObject not found: " + entry.StringValue);

		throw std::runtime_error("GameObject not found: " + entry.StringValue);
	}

	return gameObject->m_transform;
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

void BlackBoard::Serialize(std::string_view name)
{
	if (m_name != name)
	{
		m_name = name;
	}

	file::path filePath = PathFinder::Relative("BehaviorTree\\" + std::string(name) +".blackboard");
	if (!file::exists(filePath.parent_path()))
	{
		file::create_directories(filePath.parent_path());
	}

	std::ofstream out(filePath.string());
	if (!out.is_open())
	{
		Debug->LogError("Failed to open file for writing: " + filePath.string());

		throw std::runtime_error("Failed to open file for writing: " + filePath.string());
	}

	MetaYml::Node node;
	for(auto& [key, value] : m_values)
	{
		MetaYml::Node entryNode;
		entryNode["key"] = key;
		entryNode["value"] = Meta::Serialize(&value);
		node[m_name].push_back(entryNode);
	}

	out << node;

	out.flush();
}

void BlackBoard::Deserialize(std::string_view name)
{
	if (m_name != name)
	{
		m_name = name;
	}

	file::path filePath = PathFinder::Relative("BehaviorTree\\" + std::string(name) + ".blackboard");
	if (!file::exists(filePath))
	{
		Debug->LogError("Blackboard file not found: " + filePath.string());

		throw std::runtime_error("Blackboard file not found: " + filePath.string());
	}

	MetaYml::Node node = MetaYml::LoadFile(filePath.string());
	for (const auto& entry : node[m_name])
	{
		std::string key = entry["key"].as<std::string>();
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