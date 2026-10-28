#include "../Utility_Framework/Foundation/EngineNodeMap.h"
#include "../Containers/ContainerAPI.h"
#pragma once
#include "../Utility_Framework/Foundation/Core.Minimal.h"
#include "../Utility_Framework/Reflection/ClassProperty.h"
#include "Entity.h"

class TagManager : public Singleton<TagManager>
{
  private:
    // Keys remain owning strings. Heterogeneous lookup respects view bounds
    // without allocating a temporary string, including for embedded nulls.
    static ce::utf8_string_view TagView(std::string_view tag) noexcept
    {
        return {tag.data(), tag.size()};
    }
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
    // Restore ordered definitions without disturbing the object membership index.
    void SetTagDefinitions(const ce::dynamic_array<ce::utf8_string>& tags);

    ce::dynamic_array<ce::utf8_string>& GetTags() { return m_tags; }

    size_t GetTagIndex(std::string_view tag) const
    {
        if (tag.empty() || tag == "Untagged")
        {
            return 0;
        }
        auto it = m_tagMap.find(TagView(tag));
        if (it != m_tagMap.end())
        {
            return it->second;
        }
        return 0; // "Untagged" index
    }

    void AddTagToObject(std::string_view tag, Entity* object);
    void RemoveTagFromObject(std::string_view tag, Entity* object);

    ce::dynamic_array<Entity*> GetObjectsWithTag(std::string_view tag) const
    {
        if (tag.empty() || tag == "Untagged")
        {
            return {};
        }

        auto it = m_taggedObjects.find(TagView(tag));
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

        auto it = m_taggedObjects.find(TagView(tag));
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
    // ★ 로드가 실패한 상태를 저장하면 **자산이 지워진다.**
    //
    //   `Finalize()`가 종료 시 `Save()`를 무조건 부른다. 파싱이 실패하면
    //   메모리 상태가 비어 있고, 그 빈 상태가 디스크를 덮어 **태그가
    //   통째로 사라진다.** 실제로 이 경로로 자산을 한 번 잃었다(D3-b-L 실측).
    //
    //   백엔드를 ryml로 옮기면서 이 위험이 커졌다 — 두 파서의 수용 범위가
    //   다르므로(D3-b-2b-0: 21건) yaml-cpp가 읽던 파일을 ryml이 거부할 수 있고,
    //   그 순간 저장이 손실을 확정한다. 그래서 로드 성공 여부를 기억한다.
    bool m_loadSucceeded{false};
    ce::engine_node_map<ce::utf8_string, size_t> m_tagMap{};
    ce::dynamic_array<ce::utf8_string> m_tags{};
    ce::engine_node_map<ce::utf8_string, ce::dynamic_array<Entity*>> m_taggedObjects{};
};

static auto TagManagers = TagManager::GetInstance();
