// 에디터 창 런타임 표 (PHASE 21 M4 · 계획서 부록 B.3).
//
// 이 TU는 유니티 빌드에서 일부러 뺀다(Editor.vcxproj). `Commands/*.cpp`와
// `EditorMenuRegistry.cpp`가 세운 규약과 같은 이유다 — 유니티에 넣으면 앞 파일이
// 들여온 헤더에 기대도 통과해서, 각 TU가 자기 include를 소유하는지가 평상시
// 빌드에서 검사되지 않는다.

#include "EditorWindowRegistry.h"

#include <algorithm>
#include <cstddef>
#include <string_view>
#include <vector>

namespace editor
{
    namespace
    {
        std::vector<window_entry>& storage()
        {
            static std::vector<window_entry> entries;
            return entries;
        }
    }

    std::vector<window_entry>& window_entries()
    {
        return storage();
    }

    const std::vector<window_entry>& window_entries_of()
    {
        return storage();
    }

    window_entry* find_window(std::string_view stable_id)
    {
        for (window_entry& entry : storage())
        {
            if (entry.stable_id == stable_id)
            {
                return &entry;
            }
        }
        return nullptr;
    }

    const window_entry* find_window_of(std::string_view stable_id)
    {
        for (const window_entry& entry : storage())
        {
            if (entry.stable_id == stable_id)
            {
                return &entry;
            }
        }
        return nullptr;
    }

    void open_window(std::string_view stable_id)
    {
        if (window_entry* entry = find_window(stable_id))
        {
            entry->open = true;
        }
    }

    void close_window(std::string_view stable_id)
    {
        if (window_entry* entry = find_window(stable_id))
        {
            entry->open = false;
        }
    }

    bool is_window_open(std::string_view stable_id)
    {
        const window_entry* entry = find_window_of(stable_id);
        return nullptr != entry && entry->open;
    }

    bool window_declared(std::string_view stable_id)
    {
        return nullptr != find_window_of(stable_id);
    }

    window_registry_stats collect_window_registry_stats()
    {
        window_registry_stats stats{};

        const std::vector<window_entry>& entries = storage();
        stats.total = entries.size();

        for (const window_entry& entry : entries)
        {
            switch (entry.role)
            {
            case window_role::central:   ++stats.central;   break;
            case window_role::panel:     ++stats.panel;     break;
            case window_role::transient: ++stats.transient; break;
            default: break;
            }
        }

        // 안정 식별자 중복. `###` 오른쪽이 겹치면 ImGui가 두 창을 한 창으로 보고
        // 배치가 뒤엉킨다 — 계획서 §1.4가 지목한 파손과 같은 종류라서 센다.
        for (std::size_t left = 0; left < entries.size(); ++left)
        {
            for (std::size_t right = left + 1; right < entries.size(); ++right)
            {
                if (entries[left].stable_id == entries[right].stable_id)
                {
                    ++stats.duplicate_ids;
                }
            }
        }

        // 아무 창도 들어오지 않은 도킹 자리. `floating`은 자리가 아니라 "붙이지
        // 않음"이라 세지 않는다. 이관이 끝나면 이 값이 0이어야 배치가 완성된다.
        for (const dock_slot slot : all_dock_slots)
        {
            if (dock_slot::floating == slot)
            {
                continue;
            }

            const bool occupied = std::any_of(
                entries.begin(), entries.end(),
                [slot](const window_entry& entry) { return entry.dock == slot; });

            if (!occupied)
            {
                ++stats.empty_dock_slots;
            }
        }

        return stats;
    }

    void clear_window_registry()
    {
        storage().clear();
    }
}
