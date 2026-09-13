#pragma once
#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace editor::components
{
    struct Entry
    {
        std::string type;
        std::string label;
        std::string category;
        bool managed{};
    };

    inline std::string DisplayName(std::string_view type, bool managed = false)
    {
        if (managed)
        {
            const auto last = type.find_last_of(".+");
            if (last != std::string_view::npos) type.remove_prefix(last + 1);
        }
        else if (type.ends_with("Component")) type.remove_suffix(9);
        std::string result;
        for (size_t i = 0; i < type.size(); ++i)
        {
            const auto value = static_cast<unsigned char>(type[i]);
            if (i && std::isupper(value)
                && (std::islower(static_cast<unsigned char>(type[i - 1]))
                    || (i + 1 < type.size() && std::isupper(static_cast<unsigned char>(type[i - 1]))
                        && std::islower(static_cast<unsigned char>(type[i + 1]))))) result += ' ';
            result += static_cast<char>(value);
        }
        return result.empty() ? std::string(type) : result;
    }

    inline std::string Category(std::string_view type)
    {
        struct Group { std::string_view category, types; };
        constexpr std::array groups{
            Group{"Rendering", "|CameraComponent|LightComponent|MeshRenderer|SpriteRenderer|DecalComponent|VolumeComponent|TerrainComponent|FoliageComponent|"},
            Group{"Physics", "|BoxColliderComponent|SphereColliderComponent|CapsuleColliderComponent|RigidBodyComponent|CharacterControllerComponent|RagdollComponent|"},
            Group{"Animation", "|Animator|SpriteSheetComponent|"},
            Group{"Audio", "|SoundComponent|"},
            Group{"AI", "|StateMachineComponent|BehaviorTreeComponent|"},
            Group{"Input", "|PlayerInputComponent|"},
            Group{"UI", "|Canvas|UIComponent|ImageComponent|TextComponent|UIButton|"}
        };
        const std::string key = "|" + std::string(type) + "|";
        for (const auto& group : groups) if (group.types.find(key) != std::string_view::npos)
            return std::string(group.category);
        return "Other";
    }

    inline Entry Native(std::string name) { return {name, DisplayName(name), Category(name), false}; }
    inline Entry Script(std::string name) { return {name, DisplayName(name, true), "Scripts", true}; }

    inline std::string SearchKey(std::string_view text)
    {
        std::string result;
        for (const unsigned char value : text)
            if (!std::isspace(value)) result += static_cast<char>(std::tolower(value));
        return result;
    }

    // Every word must match; spaces in names are optional ("mesh renderer" / "MeshRenderer").
    inline bool Matches(const Entry& entry, std::string_view query)
    {
        const auto key = SearchKey(entry.label + " " + entry.type + " " + entry.category);
        size_t begin = 0;
        while (begin < query.size())
        {
            while (begin < query.size() && std::isspace(static_cast<unsigned char>(query[begin]))) ++begin;
            auto end = begin;
            while (end < query.size() && !std::isspace(static_cast<unsigned char>(query[end]))) ++end;
            if (end > begin && key.find(SearchKey(query.substr(begin, end - begin))) == std::string::npos) return false;
            begin = end;
        }
        return true;
    }

    inline void Sort(std::vector<Entry>& entries)
    {
        std::ranges::sort(entries, [](const Entry& a, const Entry& b) {
            if (a.label != b.label) return a.label < b.label;
            if (a.category != b.category) return a.category < b.category;
            return a.type < b.type;
        });
    }
}
