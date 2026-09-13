#pragma once
#include "EntityHandle.h"
#include <vector>
#include <cstddef>

namespace editor
{
    // Session-local history. Scene serial + slot generation prevent pointer/slot reuse.
    class SelectionHistory
    {
    public:
        static constexpr size_t Capacity = 128;

        void Observe(uint32_t scene, EntityHandle selected)
        {
            if (m_scene != scene) { m_entries.clear(); m_cursor = 0; m_scene = scene; }
            if (!selected.IsValid() || selected.sceneId != scene) return;
            if (!m_entries.empty() && m_entries[m_cursor] == selected) return;
            if (!m_entries.empty()) m_entries.resize(m_cursor + 1);
            m_entries.push_back(selected);
            if (m_entries.size() > Capacity) m_entries.erase(m_entries.begin());
            m_cursor = m_entries.size() - 1;
        }

        template<class IsAlive> EntityHandle Peek(int direction, IsAlive alive) const
        {
            const auto i = Find(direction, alive);
            return i >= 0 ? m_entries[static_cast<size_t>(i)] : EntityHandle{};
        }

        template<class IsAlive> EntityHandle Move(int direction, IsAlive alive)
        {
            const auto i = Find(direction, alive);
            if (i < 0) return {};
            m_cursor = static_cast<size_t>(i);
            return m_entries[m_cursor];
        }

        size_t Size() const noexcept { return m_entries.size(); }

    private:
        template<class IsAlive> std::ptrdiff_t Find(int direction, IsAlive alive) const
        {
            if (m_entries.empty() || (direction != -1 && direction != 1)) return -1;
            for (auto i = static_cast<std::ptrdiff_t>(m_cursor) + direction;
                 i >= 0 && i < static_cast<std::ptrdiff_t>(m_entries.size()); i += direction)
                if (alive(m_entries[static_cast<size_t>(i)])) return i;
            return -1;
        }

        uint32_t m_scene{};
        size_t m_cursor{};
        std::vector<EntityHandle> m_entries;
    };
}
