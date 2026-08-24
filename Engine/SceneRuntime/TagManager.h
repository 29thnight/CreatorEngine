#pragma once
#include "Core.Minimal.h"
#include "ClassProperty.h"
#include "Entity.h"

class TagManager : public Singleton<TagManager>
{
private:
	friend Singleton<TagManager>;
	TagManager() = default;
	~TagManager() = default;

public:
	void Initialize();
	void Finalize();
	void Load();
    // 저작 게시는 Editor Host가 소유한다. 여기서는 YAML payload만 만들고 Player에는
    // handler가 없어 정상적으로 실패한다.
    bool Save();
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

    void AddTagToObject(std::string_view tag, Entity* object);
    void RemoveTagFromObject(std::string_view tag, Entity* object);

    void AddObjectToLayer(std::string_view layer, Entity* object);
    void RemoveObjectFromLayer(std::string_view layer, Entity* object);

    std::vector<Entity*> GetObjectsInLayer(std::string_view layer) const
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

    Entity* GetObjectInLayer(std::string_view layer) const
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

	std::vector<Entity*> GetObjectsWithTag(std::string_view tag) const
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

	Entity* GetObjectWithTag(std::string_view tag) const
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
    std::unordered_map<std::string, std::vector<Entity*>> m_taggedObjects{};

    std::unordered_map<std::string, size_t> m_layerMap{};
    std::vector<std::string> m_layers{};
    std::unordered_map<std::string, std::vector<Entity*>> m_layeredObjects{};
	
};

static auto TagManagers = TagManager::GetInstance();
