// 에디터 창 런타임 표 (PHASE 21 M4 · 계획서 부록 B.3).
//
// 이 TU는 유니티 빌드에서 일부러 뺀다(Editor.vcxproj). `Commands/*.cpp`와
// `EditorMenuRegistry.cpp`가 세운 규약과 같은 이유다 — 유니티에 넣으면 앞 파일이
// 들여온 헤더에 기대도 통과해서, 각 TU가 자기 include를 소유하는지가 평상시
// 빌드에서 검사되지 않는다.

#include "EditorWindowRegistry.h"

#include <algorithm>
#include <cstddef>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace editor
{
    window_table& process_windows()
    {
        static window_table table;
        return table;
    }

    namespace
    {
        std::mutex& request_mutex()
        {
            static std::mutex mutex;
            return mutex;
        }

        std::vector<std::pair<std::string, window_request>>& pending_requests()
        {
            static std::vector<std::pair<std::string, window_request>> requests;
            return requests;
        }
    }

    bool queue_window_request(std::string_view stable_id, window_request request)
    {
        // 선언 여부는 여기서 본다. 없는 이름을 큐에 넣으면 UI 스레드가
        // 조용히 버리고, 오타가 "아무 일도 일어나지 않음" 으로 보인다 —
        // §1.3 의 유령 창과 정확히 반대쪽 실패다.
        if (!window_declared(process_windows(), stable_id)) return false;
        std::lock_guard lock(request_mutex());
        pending_requests().emplace_back(std::string(stable_id), request);
        return true;
    }

    std::string apply_pending_window_requests(window_table& table)
    {
        std::vector<std::pair<std::string, window_request>> requests;
        {
            std::lock_guard lock(request_mutex());
            requests.swap(pending_requests());
        }
        std::string focusId;
        for (const auto& [id, request] : requests)
        {
            switch (request)
            {
            case window_request::open:  open_window(table, id); break;
            case window_request::close: close_window(table, id); break;
            case window_request::focus:
                // 포커스는 열려 있어야 뜻이 있다. 닫힌 창에 포커스를 주면
                // 아무 일도 일어나지 않으므로 함께 연다.
                open_window(table, id);
                focusId = id;
                break;
            }
        }
        return focusId;
    }

    window_entry* find_window(window_table& table, std::string_view stable_id)
    {
        for (window_entry& entry : table.entries)
        {
            if (entry.stable_id == stable_id)
            {
                return &entry;
            }
        }
        return nullptr;
    }

    const window_entry* find_window_of(const window_table& table, std::string_view stable_id)
    {
        for (const window_entry& entry : table.entries)
        {
            if (entry.stable_id == stable_id)
            {
                return &entry;
            }
        }
        return nullptr;
    }

    void open_window(window_table& table, std::string_view stable_id)
    {
        if (window_entry* entry = find_window(table, stable_id))
        {
            entry->open = true;
        }
    }

    void close_window(window_table& table, std::string_view stable_id)
    {
        if (window_entry* entry = find_window(table, stable_id))
        {
            entry->open = false;
        }
    }

    bool is_window_open(const window_table& table, std::string_view stable_id)
    {
        const window_entry* entry = find_window_of(table, stable_id);
        return nullptr != entry && entry->open;
    }

    bool window_declared(const window_table& table, std::string_view stable_id)
    {
        return nullptr != find_window_of(table, stable_id);
    }

    window_registry_stats collect_window_registry_stats(const window_table& table)
    {
        window_registry_stats stats{};

        const std::vector<window_entry>& entries = table.entries;
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

    void clear_window_registry(window_table& table)
    {
        table.entries.clear();
    }
}
