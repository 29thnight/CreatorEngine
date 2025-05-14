#pragma once
#include "Core.Minimal.h"
#include "GameObject.h"

class TagManager : public Singleton<TagManager>
{
private:
	friend Singleton;
	TagManager() = default;
	~TagManager() = default;

public:
	void Initialize();
	void Finalize();
	void Load();
	void Save();
	void AddTag(const std::string_view& tag);
	void RemoveTag(const std::string_view& tag);
	bool HasTag(const std::string_view& tag) const;

	std::vector<std::string>& GetTags()
	{
		return m_tags;
	}

	void AddTagToObject(const std::string_view& tag, GameObject* object);
	void RemoveTagFromObject(const std::string_view& tag, GameObject* object);

	std::vector<GameObject*> GetObjectsWithTag(const std::string_view& tag) const
	{
		if (tag.empty() || tag == "Untagged")
		{
			return {};
		}

		auto it = m_taggedObjects.find(tag.data());
		if (it != m_taggedObjects.end())
		{
			return it->second;
		}
		return {};
	}

	GameObject* GetObjectWithTag(const std::string_view& tag) const
	{
		if (tag.empty() || tag == "Untagged")
		{
			return nullptr;
		}

		auto it = m_taggedObjects.find(tag.data());
		if (it != m_taggedObjects.end() && !it->second.empty())
		{
			return it->second[0];
		}
		return nullptr;
	}

	void ClearTags()
	{
		m_tags.clear();
		m_tagMap.clear();
		m_taggedObjects.clear();
	}

private:
	std::unordered_map<std::string, size_t> m_tagMap{};
	std::vector<std::string> m_tags{};
	std::unordered_map<std::string, std::vector<GameObject*>> m_taggedObjects{};
	
};