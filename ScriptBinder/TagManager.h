#pragma once
#include "Core.Minimal.h"
#include "DLLAcrossSingleton.h"
#include "GameObject.h"

class TagManager : public DLLCore::Singleton<TagManager>
{
private:
	friend DLLCore::Singleton<TagManager>;
	TagManager() = default;
	~TagManager() = default;

public:
	void Initialize();
	void Finalize();
	void Load();
    void Save();
    void AddTag(std::string_view tag);
    void RemoveTag(std::string_view tag);
    bool HasTag(std::string_view tag) const;

    void AddLayer(std::string_view layer);
    void RemoveLayer(std::string_view layer);
    bool HasLayer(std::string_view layer) const;

    std::vector<std::string>& GetTags()
    {
        return m_tags;
    }

    std::vector<std::string>& GetLayers()
    {
        return m_layers;
    }

    size_t GetTagIndex(std::string_view tag) const
    {
        if (tag.empty() || tag == "Untagged")
        {
            return 0;
        }
        auto it = m_tagMap.find(tag.data());
        if (it != m_tagMap.end())
        {
            return it->second;
        }
        return 0; // "Untagged" index
	}

    size_t GetLayerIndex(std::string_view layer) const
    {
        if (layer.empty())
        {
            return 0;
        }
        auto it = m_layerMap.find(layer.data());
        if (it != m_layerMap.end())
        {
            return it->second;
        }
        return 0; // Default layer index
	}

    void AddTagToObject(std::string_view tag, GameObject* object);
    void RemoveTagFromObject(std::string_view tag, GameObject* object);

    void AddObjectToLayer(std::string_view layer, GameObject* object);
    void RemoveObjectFromLayer(std::string_view layer, GameObject* object);

    std::vector<GameObject*> GetObjectsInLayer(std::string_view layer) const
    {
        if (layer.empty())
        {
                return {};
        }

        auto it = m_layeredObjects.find(layer.data());
        if (it != m_layeredObjects.end())
        {
                return it->second;
        }
        return {};
    }

    GameObject* GetObjectInLayer(std::string_view layer) const
    {
        if (layer.empty())
        {
                return nullptr;
        }

        auto it = m_layeredObjects.find(layer.data());
        if (it != m_layeredObjects.end() && !it->second.empty())
        {
                return it->second[0];
        }
        return nullptr;
    }

	std::vector<GameObject*> GetObjectsWithTag(std::string_view tag) const
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

	GameObject* GetObjectWithTag(std::string_view tag) const
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

    void ClearLayers()
    {
       m_layers.clear();
       m_layerMap.clear();
       m_layeredObjects.clear();
    }

private:
    std::unordered_map<std::string, size_t> m_tagMap{};
    std::vector<std::string> m_tags{};
    std::unordered_map<std::string, std::vector<GameObject*>> m_taggedObjects{};

    std::unordered_map<std::string, size_t> m_layerMap{};
    std::vector<std::string> m_layers{};
    std::unordered_map<std::string, std::vector<GameObject*>> m_layeredObjects{};
	
};

static auto TagManagers = TagManager::GetInstance();