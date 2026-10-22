#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace material_graph
{
    inline bool ScenePacketReuseEnabled()
    {
        static const bool enabled = []
        {
            char* value{};
            size_t length{};
            const bool disabled =
                _dupenv_s(&value, &length, "CREATOR_SCENE_PACKET_REUSE") == 0 && value && std::strcmp(value, "0") == 0;
            std::free(value);
            return !disabled;
        }();
        return enabled;
    }

    struct ScenePacketRecord
    {
        uint32_t record{}, visibleOffset{}, bucket{}, capacity{};
        bool operator==(const ScenePacketRecord&) const = default;
    };

    // CPU-only plan. Owns all identity strings; never retains a Draw, generation,
    // GPU resource, upload slice or borrowed frame pointer.
    class ScenePacketGroups
    {
      public:
        struct Input
        {
            std::string_view semantic;
            bool doubleSided{};
            uint32_t page{};
            size_t index{};
            uint32_t record{}, capacity{};
        };
        struct Group
        {
            std::tuple<std::string, bool, uint32_t> first;
            std::vector<size_t> second;
        };

        bool Update(std::span<const Input> inputs, bool reuse)
        {
            bool matches = reuse && m_initialized && inputs.size() == m_inputs.size();
            for (size_t i = 0; matches && i < inputs.size(); ++i)
            {
                const auto& a = inputs[i];
                const auto& b = m_inputs[i];
                matches = a.semantic == b.semantic && a.doubleSided == b.doubleSided && a.page == b.page &&
                          a.index == b.index && a.record == b.record && a.capacity == b.capacity;
            }
            if (matches)
            {
                return true;
            }
            // A failed allocation may leave staging vectors partially updated.
            // Never accept those keys as a completed plan on the next frame.
            m_initialized = false;
            m_inputs.resize(inputs.size());
            m_order.resize(inputs.size());
            for (size_t i = 0; i < inputs.size(); ++i)
            {
                const auto& a = inputs[i];
                m_inputs[i] = {std::string(a.semantic), a.doubleSided, a.page, a.index, a.record, a.capacity};
                m_order[i] = i;
            }
#if defined(CREATOR_PACKET_GROUP_REBUILD_TEST_HOOK)
            CREATOR_PACKET_GROUP_REBUILD_TEST_HOOK;
#endif
            std::sort(m_order.begin(), m_order.end(),
                      [&](size_t a, size_t b)
                      {
                          const auto& x = m_inputs[a];
                          const auto& y = m_inputs[b];
                          return std::tie(x.semantic, x.doubleSided, x.page, x.index) <
                                 std::tie(y.semantic, y.doubleSided, y.page, y.index);
                      });
            for (auto& group : m_groups)
            {
                group.second.clear();
            }
            size_t active{};
            m_templates.resize(inputs.size());
            size_t output{};
            for (const auto index : m_order)
            {
                const auto& input = m_inputs[index];
                const auto key = std::tie(input.semantic, input.doubleSided, input.page);
                if (!active || m_groups[active - 1].first != key)
                {
                    if (active == m_groups.size())
                    {
                        m_groups.emplace_back();
                    }
                    m_groups[active++].first = key;
                }
                m_groups[active - 1].second.push_back(input.index);
                m_templates[output++] = {input.record, 0, static_cast<uint32_t>(active - 1), input.capacity};
            }
            m_capacities.assign(active, 0);
            m_capacity = 0;
            m_capacityValid = true;
            for (const auto& record : m_templates)
            {
                auto& bucket = m_capacities[record.bucket];
                m_capacityValid = m_capacityValid && record.record != 0 &&
                                  uint64_t(bucket) + record.capacity <= UINT32_MAX &&
                                  m_capacity + record.capacity <= UINT32_MAX;
                bucket += record.capacity;
                m_capacity += record.capacity;
            }
            m_active = active;
            m_spans.resize(active);
            for (size_t i = 0; i < active; ++i)
            {
                m_spans[i] = m_groups[i].second;
            }
            ++m_revision;
            m_initialized = true;
            return false;
        }
        auto Groups() const { return std::span(m_groups).first(m_active); }
        auto Spans() const { return std::span<const std::span<const size_t>>(m_spans); }
        auto Templates() const { return std::span<const ScenePacketRecord>(m_templates); }
        auto Capacities() const { return std::span<const uint32_t>(m_capacities); }
        uint64_t Capacity() const { return m_capacity; }
        bool CapacityValid() const { return m_capacityValid; }
        uint64_t Revision() const { return m_revision; }

      private:
        struct StoredInput
        {
            std::string semantic;
            bool doubleSided{};
            uint32_t page{};
            size_t index{};
            uint32_t record{}, capacity{};
        };
        std::vector<StoredInput> m_inputs;
        std::vector<size_t> m_order;
        std::vector<Group> m_groups;
        std::vector<std::span<const size_t>> m_spans;
        std::vector<ScenePacketRecord> m_templates;
        std::vector<uint32_t> m_capacities;
        uint64_t m_capacity{};
        bool m_capacityValid{};
        size_t m_active{};
        uint64_t m_revision{};
        bool m_initialized{};
    };

    struct ScenePacketRecords
    {
        std::vector<ScenePacketRecord> m_records;
        uint64_t m_revision{};
        const ScenePacketGroups* m_owner{};
        bool Prepare(const ScenePacketGroups& groups)
        {
            if (m_owner == &groups && m_revision == groups.Revision())
            {
                return true;
            }
            const auto source = groups.Templates();
            m_records.assign(source.begin(), source.end());
            m_revision = groups.Revision();
            m_owner = &groups;
            return false;
        }
    };
} // namespace material_graph
