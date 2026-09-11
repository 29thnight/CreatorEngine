// 창 본문 보관소 (PHASE 21 M4 2·3단계 → W3).
//
// 이 TU도 유니티에서 뺀다 — EditorWindowRegistry.cpp와 같은 이유.
// 여기에는 본문이 없고 ImGui도 부르지 않는다.

#include "EditorWindowBody.h"

#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

namespace editor::windows
{
    namespace
    {
        // 스물다섯 개짜리 선형 탐색이다. 해시 테이블을 쓰지 않는 이유가 있다 —
        // 옛 `ImGuiRegister`의 `unordered_map`은 순회가 비결정적이라 골든을
        // 흔들었고(§1.3-4), 여기서 되풀이할 이유가 없다. 프레임당 스물다섯 번의
        // 문자열 비교는 이 창들이 그리는 일에 비하면 없는 값이다.
        body_binding_record* find_record(body_table& table, std::string_view stable_id)
        {
            for (body_binding_record& one : table.entries)
            {
                if (one.stable_id == stable_id)
                {
                    return &one;
                }
            }
            return nullptr;
        }

        const body_binding_record* find_record_of(const body_table& table,
                                                  std::string_view stable_id)
        {
            for (const body_binding_record& one : table.entries)
            {
                if (one.stable_id == stable_id)
                {
                    return &one;
                }
            }
            return nullptr;
        }

        // 토큰은 저장소를 가리지 않고 한 줄로 올라간다. 표마다 세면 지역 표의
        // 토큰이 제품 표의 것과 겹칠 수 있고, 핸들이 표 포인터까지 보므로
        // 당장은 무해하지만 겹치지 않는 쪽이 추적하기 쉽다.
        std::uint64_t next_token()
        {
            static std::uint64_t counter = 0;
            return ++counter;
        }

        // 이름과 토큰이 **둘 다** 맞을 때만 지운다. 같은 이름을 다시 건 뒤
        // 옛 핸들이 죽어도 새 바인딩은 살아남는다.
        void unbind_record(body_table& table, std::string_view stable_id,
                           std::uint64_t token)
        {
            for (auto it = table.entries.begin(); it != table.entries.end(); ++it)
            {
                if (it->stable_id == stable_id && it->token == token)
                {
                    table.entries.erase(it);
                    return;
                }
            }
        }
    }

    body_table& process_window_bodies()
    {
        static body_table store;
        return store;
    }

    window_body_binding bind_window_body(body_table& table, std::string_view stable_id,
                                         std::function<void()> body)
    {
        const std::uint64_t token = next_token();
        if (body_binding_record* existing = find_record(table, stable_id))
        {
            existing->body = std::move(body);
            existing->token = token;
            return window_body_binding(table, stable_id, token);
        }
        table.entries.push_back(body_binding_record{ stable_id, std::move(body), token });
        return window_body_binding(table, stable_id, token);
    }

    window_body_binding bind_window_body(std::string_view stable_id,
                                         std::function<void()> body)
    {
        return bind_window_body(process_window_bodies(), stable_id, std::move(body));
    }

    window_body_binding::~window_body_binding()
    {
        reset();
    }

    window_body_binding::window_body_binding(window_body_binding&& other) noexcept
        : m_table(other.m_table), m_stableId(other.m_stableId), m_token(other.m_token)
    {
        other.m_table = nullptr;
        other.m_stableId = std::string_view{};
        other.m_token = 0;
    }

    window_body_binding& window_body_binding::operator=(window_body_binding&& other) noexcept
    {
        if (this == &other)
        {
            return *this;
        }
        reset();
        m_table = other.m_table;
        m_stableId = other.m_stableId;
        m_token = other.m_token;
        other.m_table = nullptr;
        other.m_stableId = std::string_view{};
        other.m_token = 0;
        return *this;
    }

    void window_body_binding::reset() noexcept
    {
        if (nullptr == m_table)
        {
            return;
        }
        unbind_record(*m_table, m_stableId, m_token);
        m_table = nullptr;
        m_stableId = std::string_view{};
        m_token = 0;
    }

    std::vector<std::string_view> bound_window_bodies(const body_table& table)
    {
        std::vector<std::string_view> names;
        names.reserve(table.entries.size());
        for (const body_binding_record& one : table.entries)
        {
            names.push_back(one.stable_id);
        }
        return names;
    }

    std::vector<std::string_view> bound_window_bodies()
    {
        return bound_window_bodies(process_window_bodies());
    }

    namespace detail
    {
        void run_window_body(body_table& table, std::string_view stable_id)
        {
            if (body_binding_record* record = find_record(table, stable_id))
            {
                if (record->body)
                {
                    record->body();
                }
            }
        }

        void run_window_body(std::string_view stable_id)
        {
            run_window_body(process_window_bodies(), stable_id);
        }

        bool window_body_bound(const body_table& table, std::string_view stable_id)
        {
            const body_binding_record* record = find_record_of(table, stable_id);
            return nullptr != record && static_cast<bool>(record->body);
        }

        bool window_body_bound(std::string_view stable_id)
        {
            return window_body_bound(process_window_bodies(), stable_id);
        }
    }
}
