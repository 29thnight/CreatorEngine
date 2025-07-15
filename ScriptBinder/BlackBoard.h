#pragma once
#include "BlackBoardValue.h"

class GameObject;
class Transform;
class MenuBarWindow;
class BlackBoard
{
public:
	// Setters
	void SetValueAsBool(const std::string& key, bool value);
	void SetValueAsInt(const std::string& key, int value);
	void SetValueAsFloat(const std::string& key, float value);
	void SetValueAsString(const std::string& key, const std::string& value);
	void SetValueAsVector2(const std::string& key, const Mathf::Vector2& value);
	void SetValueAsVector3(const std::string& key, const Mathf::Vector3& value);
	void SetValueAsVector4(const std::string& key, const Mathf::Vector4& value);
	void SetValueAsGameObject(const std::string& key, const std::string& objectName);
	void SetValueAsTransform(const std::string& key, const std::string& transformPath);

	// Getters
	bool GetValueAsBool(const std::string& key) const;
	int GetValueAsInt(const std::string& key) const;
	float GetValueAsFloat(const std::string& key) const;
	const std::string& GetValueAsString(const std::string& key) const;
	const Mathf::Vector2& GetValueAsVector2(const std::string& key) const;
	const Mathf::Vector3& GetValueAsVector3(const std::string& key) const;
	const Mathf::Vector4& GetValueAsVector4(const std::string& key) const;
	const GameObject& GetValueAsGameObject(const std::string& key) const;
	const Transform& GetValueAsTransform(const std::string& key) const;

	// Management
	void AddKey(const std::string& key, const BlackBoardType& type);
	bool HasKey(const std::string& key) const;
	BlackBoardType GetType(const std::string& key) const;
	void RemoveKey(const std::string& key);
	void RenameKey(const std::string& curKey, const std::string& newKey);
	void Clear();

	// Serialization
	void Serialize(const std::string_view& name);
	void Deserialize(const std::string_view& name);

private:
	friend class MenuBarWindow; // Allow MenuBarWindow to access private members

	std::string m_name; // Name of the blackboard
	std::unordered_map<std::string, BlackBoardValue> m_values;

	BlackBoardValue& GetOrCreate(const std::string& key);
	const BlackBoardValue& GetChecked(const std::string& key, BlackBoardType expected) const;
};

using ConditionFunc = std::function<bool(const BlackBoard&)>;