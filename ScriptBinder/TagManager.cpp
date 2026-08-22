#include "TagManager.h"
#include "Core.Minimal.h"
#include "ReflectionYml.h"
#include "EngineMode.h"
#include "Interfaces/AssetAuthoringPort.h"

#include <sstream>

namespace
{
    // 이름→경로 규약은 한 곳에만 둔다. 저작 쓰기와 런타임 읽기가 각자 경로를
    // 조립하면 조용히 갈라진다.
    file::path ResolveTagManagerPath()
    {
        return PathFinder::ProjectSettingPath("TagManager.asset");
    }
}

void TagManager::Initialize()
{
    m_tags.reserve(32);
    m_tagMap.reserve(32);
    m_taggedObjects.reserve(300);
    m_layers.reserve(32);
    m_layerMap.reserve(32);
    m_layeredObjects.reserve(300);

    // 파일이 없으면 기본 태그·레이어를 채우고 저장을 시도한다. 실제 쓰기는
    // Editor Host가 설치한 handler만 수행하므로 모드 분기가 필요 없다 —
    // Player는 handler가 없어 메모리 기본값만 갖고 지나간다(pak에 자산이 빠진
    // 방어 경로이며, 빈 표보다 낫다).
    if (!file::exists(ResolveTagManagerPath()))
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

	Load();
}

void TagManager::Finalize()
{
    // Player에는 handler가 없어 이 호출이 그대로 실패한다 — 모드 분기가 필요 없다.
    // Editor는 authoring handler가 살아 있는 구간에서 Finalize를 부른다
    // (EditorMain이 EditorAssetDatabase::Shutdown 전에 호출한다).
    Save();
    m_tags.clear();
    m_tagMap.clear();
    m_taggedObjects.clear();
    m_layers.clear();
    m_layerMap.clear();
    m_layeredObjects.clear();
}

void TagManager::Load()
{
    file::path path = ResolveTagManagerPath();
    // 예전에는 이 존재 검사가 에디터 전용이었다 — 플레이어에서 파일이 없으면
    // YAML::LoadFile이 예외를 던졌다. 언팩 실패가 크래시로 둔갑하지 않게
    // 양쪽 모두 검사한다.
    if (!file::exists(path))
    {
        return;
    }

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

bool TagManager::Save()
{
    // 빈 시퀀스를 명시한다. 손대지 않은 Node를 흘리면 yaml-cpp가 0바이트를 내고,
    // 그렇게 저장된 자산은 Load가 tags/layers를 하나도 복원하지 못한다.
    YAML::Node tagsNode(YAML::NodeType::Sequence);
    for (const auto& tag : m_tags)
    {
        tagsNode.push_back(tag);
    }
    YAML::Node layersNode(YAML::NodeType::Sequence);
    for (const auto& layer : m_layers)
    {
        layersNode.push_back(layer);
    }

    YAML::Node root;
    root["tags"] = tagsNode;
    root["layers"] = layersNode;

    std::ostringstream payload;
    payload << root;

    // 목적 경로는 Load와 같은 규약으로 만든다. 게시는 Editor Host가 소유하며
    // Player에는 handler가 없어 정상적으로 실패한다.
    ProjectSettingAuthoringRequest request{};
    request.destinationPath = ResolveTagManagerPath();
    request.payload = payload.str();

    if (!AssetAuthoringPort::WriteTagManager(request))
    {
        Debug->LogError(
            "TagManager save requires a complete Editor authoring transaction");
        return false;
    }

    return true;
}

void TagManager::AddTag(std::string_view tag)
{
	if (!EngineMode::IsEditor())
	{
		return;
	}
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

void TagManager::AddLayer(std::string_view layer)
{
    if (!EngineMode::IsEditor())
    {
        return;
    }
    if (layer.empty() || 32 < m_layers.size())
    {
        return;
    }

    if (m_layerMap.find(layer.data()) == m_layerMap.end())
    {
        m_layers.push_back(layer.data());
        m_layerMap[layer.data()] = m_layers.size() - 1;
    }
}

void TagManager::RemoveTag(std::string_view tag)
{
	if (!EngineMode::IsEditor())
	{
		return;
	}
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

void TagManager::RemoveLayer(std::string_view layer)
{
    if (!EngineMode::IsEditor())
    {
        return;
    }
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

void TagManager::AddTagToObject(std::string_view tag, Entity* object)
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
        // If the tag does not exist, you might want to add it or handle the error
		// For now, we'll just add the object to the "Untagged" category
		object->SetTag("Untagged");
		m_taggedObjects["Untagged"].push_back(object);
    }
}

void TagManager::AddObjectToLayer(std::string_view layer, Entity* object)
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
    else
    {
        // If the layer does not exist, you might want to add it or handle the error
        // For now, we'll just add the object to a default layer
		object->SetLayer("Default");
		m_layeredObjects["Default"].push_back(object);
    }
}

void TagManager::RemoveTagFromObject(std::string_view tag, Entity* object)
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

void TagManager::RemoveObjectFromLayer(std::string_view layer, Entity* object)
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
