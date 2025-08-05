#include "TagManager.h"
#include "Core.Minimal.h"
#include "ReflectionYml.h"

void TagManager::Initialize()
{
    m_tags.reserve(32);
    m_tagMap.reserve(32);
    m_taggedObjects.reserve(300);
    m_layers.reserve(32);
    m_layerMap.reserve(32);
    m_layeredObjects.reserve(300);

#ifndef BUILD_FLAG
    file::path path = PathFinder::ProjectSettingPath("TagManager.asset");
    if(!file::exists(path))
    {
        // Initialize the tag map with some default tags if needed
        m_tagMap["Untagged"] = 0;
        m_tagMap["Respawn"] = 1;
        m_tagMap["Finish"] = 2;
        m_tagMap["MainCamera"] = 3;
        m_tagMap["Player"] = 4;
        m_tagMap["GameController"] = 5;

        m_tags = { "Untagged", "Respawn", "Finish", "MainCamera", "Player", "GameController" };

        m_layerMap["Default"] = 0;
        m_layerMap["TransparentFX"] = 1;
        m_layerMap["Ignore RayCast"] = 2;
        m_layerMap["Water"] = 3;
        m_layerMap["UI"] = 4;

        m_layers = { "Default", "TransparentFX", "Ignore RayCast", "Water", "UI" };
	    
        Save();
    }
#endif // !BUILD_FLAG

	Load();
}

void TagManager::Finalize()
{
#ifndef BUILD_FLAG
    Save();
#endif // !BUILD_FLAG
    m_tags.clear();
    m_tagMap.clear();
    m_taggedObjects.clear();
    m_layers.clear();
    m_layerMap.clear();
    m_layeredObjects.clear();
}

void TagManager::Load()
{
    file::path path = PathFinder::ProjectSettingPath("TagManager.asset");
#ifndef BUILD_FLAG
    if (!file::exists(path))
    {
        return;
    }
#endif // !BUILD_FLAG

    YAML::Node root = YAML::LoadFile(path.string());

    if (root["tags"])
    {
        ClearTags();
        size_t index = 0;
        for (const auto& t : root["tags"])
        {
            std::string tag = t.as<std::string>();
            m_tags.push_back(tag);
            m_tagMap[tag] = index++;
        }
    }

    if (root["layers"])
    {
        ClearLayers();
        size_t index = 0;
        for (const auto& l : root["layers"])
        {
            std::string layer = l.as<std::string>();
            m_layers.push_back(layer);
            m_layerMap[layer] = index++;
        }
    }
}

void TagManager::Save()
{
#ifndef BUILD_FLAG
    file::path path = PathFinder::ProjectSettingPath("TagManager.asset");

    YAML::Node root;
    for (const auto& tag : m_tags)
    {
        root["tags"].push_back(tag);
    }
    for (const auto& layer : m_layers)
    {
        root["layers"].push_back(layer);
    }

    std::ofstream fout(path);
    fout << root;
    fout.close();
#endif // !BUILD_FLAG
}

void TagManager::AddTag(std::string_view tag)
{
#ifndef BUILD_FLAG
	if (tag.empty() || tag == "Untagged")
	{
		return; // Avoid adding empty tags
	}

	if (m_tagMap.find(tag.data()) == m_tagMap.end())
	{
		m_tags.push_back(tag.data());
		m_tagMap[tag.data()] = m_tags.size() - 1;
	}
#endif // !BUILD_FLAG
}

void TagManager::AddLayer(std::string_view layer)
{
#ifndef BUILD_FLAG
    if (layer.empty() || 32 < m_layers.size())
    {
        return;
    }

    if (m_layerMap.find(layer.data()) == m_layerMap.end())
    {
        m_layers.push_back(layer.data());
        m_layerMap[layer.data()] = m_layers.size() - 1;
    }
#endif // !BUILD_FLAG
}

void TagManager::RemoveTag(std::string_view tag)
{
#ifndef BUILD_FLAG
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
#endif // !BUILD_FLAG
}

void TagManager::RemoveLayer(std::string_view layer)
{
#ifndef BUILD_FLAG
    if (layer.empty())
    {
        return;
    }

    auto it = m_layerMap.find(layer.data());
    if (it != m_layerMap.end())
    {
        m_layers.erase(std::remove(m_layers.begin(), m_layers.end(), layer.data()), m_layers.end());
        m_layerMap.erase(it);
    }
#endif // !BUILD_FLAG
}

bool TagManager::HasTag(std::string_view tag) const
{
	if (tag.empty() || tag == "Untagged")
	{
		return false;
	}

	auto it = m_tagMap.find(tag.data());
	return it != m_tagMap.end();
}

bool TagManager::HasLayer(std::string_view layer) const
{
    if (layer.empty())
    {
        return false;
    }

    auto it = m_layerMap.find(layer.data());
    return it != m_layerMap.end();
}

void TagManager::AddTagToObject(std::string_view tag, GameObject* object)
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
}

void TagManager::AddObjectToLayer(std::string_view layer, GameObject* object)
{
    if (layer.empty())
    {
        return;
    }

    auto it = m_layerMap.find(layer.data());
    if (it != m_layerMap.end())
    {
        m_layeredObjects[layer.data()].push_back(object);
    }
}

void TagManager::RemoveTagFromObject(std::string_view tag, GameObject* object)
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

void TagManager::RemoveObjectFromLayer(std::string_view layer, GameObject* object)
{
    if (layer.empty())
    {
        return;
    }

    auto it = m_layeredObjects.find(layer.data());
    if (it != m_layeredObjects.end())
    {
        it->second.erase(std::remove(it->second.begin(), it->second.end(), object), it->second.end());
    }
}
