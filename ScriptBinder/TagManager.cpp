#include "TagManager.h"
#include "Core.Minimal.h"
#include "ReflectionYml.h"

void TagManager::Initialize()
{
	m_tags.reserve(10);
	m_tagMap.reserve(10);
	m_taggedObjects.reserve(10);

	// Initialize the tag map with some default tags if needed
	m_tagMap["Untagged"] = 0;
	m_tagMap["Respawn"] = 1;
	m_tagMap["Finish"] = 2;
	m_tagMap["MainCamera"] = 3;
	m_tagMap["Player"] = 4;
	m_tagMap["GameController"] = 5;

	m_tags = { "Untagged", "Respawn", "Finish", "MainCamera", "Player", "GameController" };
}

void TagManager::Finalize()
{
	m_tags.clear();
	m_tagMap.clear();
	m_taggedObjects.clear();
}

void TagManager::Load()
{
}

void TagManager::Save()
{

}

void TagManager::AddTag(const std::string_view& tag)
{
	if (tag.empty() || tag == "Untagged")
	{
		return; // Avoid adding empty tags
	}

	if (m_tagMap.find(tag.data()) == m_tagMap.end())
	{
		m_tags.push_back(tag.data());
		m_tagMap[tag.data()] = m_tags.size() - 1;
	}
}

void TagManager::RemoveTag(const std::string_view& tag)
{
	if (tag.empty() || tag == "Untagged")
	{
		return; // Avoid adding empty tags
	}

	auto it = m_tagMap.find(tag.data());
	if (it != m_tagMap.end())
	{
		m_tags.erase(std::remove(m_tags.begin(), m_tags.end(), tag.data()), m_tags.end());
		m_tagMap.erase(it);
	}
}

bool TagManager::HasTag(const std::string_view& tag) const
{
	if (tag.empty() || tag == "Untagged")
	{
		return false;
	}

	auto it = m_tagMap.find(tag.data());
	return it != m_tagMap.end();
}

void TagManager::AddTagToObject(const std::string_view& tag, GameObject* object)
{
	if (tag.empty() || tag == "Untagged")
	{
		return;
	}

	auto it = m_tagMap.find(tag.data());
	if (it != m_tagMap.end())
	{
		m_taggedObjects[tag.data()].push_back(object);
	}
	else
	{
		AddTag(tag);
		m_taggedObjects[tag.data()].push_back(object);
	}
}

void TagManager::RemoveTagFromObject(const std::string_view& tag, GameObject* object)
{
	if (tag.empty() || tag == "Untagged")
	{
		return;
	}

	auto it = m_taggedObjects.find(tag.data());
	if (it != m_taggedObjects.end())
	{
		it->second.erase(std::remove(it->second.begin(), it->second.end(), object), it->second.end());
	}
}
