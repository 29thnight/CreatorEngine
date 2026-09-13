#pragma once
#include <array>
#include <string_view>

namespace editor
{
    struct EntityIconPreset
    {
        std::string_view id;
        const char* label;
        const wchar_t* filename;
    };

    inline constexpr std::array EntityIconPresets{
        EntityIconPreset{"", "Entity", L"Entity.png"},
        EntityIconPreset{"game-manager", "Game Manager", L"EntityManager.png"},
        EntityIconPreset{"camera", "Camera", L"EntityCamera.png"},
        EntityIconPreset{"light", "Light", L"EntityLight.png"},
        EntityIconPreset{"audio", "Audio", L"EntityAudio.png"},
        EntityIconPreset{"player", "Player", L"EntityPlayer.png"},
        EntityIconPreset{"trigger", "Trigger", L"EntityTrigger.png"},
        EntityIconPreset{"script", "Script", L"EntityScript.png"},
        EntityIconPreset{"prefab", "Prefab", L"EntityPrefab.png"},
    };

    inline size_t EntityIconIndex(std::string_view id) noexcept
    {
        for (size_t i = 0; i < EntityIconPresets.size(); ++i)
            if (EntityIconPresets[i].id == id) return i;
        return 0; // Unknown IDs in newer scenes keep a visible, safe default.
    }
}
