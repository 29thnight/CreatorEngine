#pragma once
#include "PhysicsStep.h"
#include <bit>

namespace ce::physics
{
// Fixed-capacity open-address index plus dense active slots. No callback allocations
// and no scan of inactive buckets when producing the required Persist output.
class sensor_pairs final
{
    struct slot
    {
        event_endpoint first{}, second{};
        std::size_t dense = 0;
        std::uint8_t state = 0; // Empty / occupied.
        bool entered = false;
    };

  public:
    explicit sensor_pairs(std::size_t capacity)
        : m_slots(std::bit_ceil((std::max)(std::size_t{2}, capacity * 2))), m_capacity(capacity)
    {
        m_active.reserve(capacity);
    }

    bool Enter(event_endpoint first, event_endpoint second) noexcept
    {
        Canonicalize(first, second);
        const auto [found, index] = Find(first, second);
        if (found)
            return true;
        if (m_active.size() == m_capacity || index == m_slots.size())
            return false;

        m_slots[index] = {first, second, m_active.size(), 1, true};
        m_active.push_back(index);
        return true;
    }

    void Exit(event_endpoint first, event_endpoint second) noexcept
    {
        Canonicalize(first, second);
        const auto [found, index] = Find(first, second);
        if (!found)
            return;

        auto& value = m_slots[index];
        const auto moved = m_active.back();
        m_active[value.dense] = moved;
        m_slots[moved].dense = value.dense;
        m_active.pop_back();
        auto hole = index;
        auto next = (hole + 1) & (m_slots.size() - 1);
        while (m_slots[next].state)
        {
            const auto home = Hash(m_slots[next].first, m_slots[next].second) & (m_slots.size() - 1);
            const auto distance = [&](std::size_t to) { return (to - home) & (m_slots.size() - 1); };
            if (distance(hole) < distance(next))
            {
                m_slots[hole] = m_slots[next];
                m_active[m_slots[hole].dense] = hole;
                hole = next;
            }
            next = (next + 1) & (m_slots.size() - 1);
        }
        m_slots[hole].state = 0;
    }

    template<class Emit>
    void FinishTick(Emit&& emit)
    {
        for (const auto index : m_active)
        {
            auto& value = m_slots[index];
            if (!value.entered)
                emit(value.first, value.second);
            value.entered = false;
        }
    }

  private:
    static void Canonicalize(event_endpoint& first, event_endpoint& second)
    {
        if (std::tuple{second.body, second.shape} < std::tuple{first.body, first.shape})
            std::swap(first, second);
    }

    static std::uint64_t Hash(const event_endpoint& first, const event_endpoint& second) noexcept
    {
        std::uint64_t hash = 14695981039346656037ull;
        for (const auto value : {first.body.scene.value, std::uint64_t{first.body.slot},
                                std::uint64_t{first.body.generation}, std::uint64_t{first.shape.value},
                                second.body.scene.value, std::uint64_t{second.body.slot},
                                std::uint64_t{second.body.generation}, std::uint64_t{second.shape.value}})
            hash = (hash ^ value) * 1099511628211ull;

        return hash;
    }

    std::pair<bool, std::size_t> Find(const event_endpoint& first, const event_endpoint& second) const noexcept
    {
        const auto hash = Hash(first, second);
        for (std::size_t probe = 0; probe < m_slots.size(); ++probe)
        {
            const auto index = (hash + probe) & (m_slots.size() - 1);
            const auto& value = m_slots[index];
            if (value.state == 0)
                return {false, index};
            if (value.first.body == first.body && value.first.shape == first.shape &&
                value.second.body == second.body && value.second.shape == second.shape)
                return {true, index};
        }
        return {false, m_slots.size()};
    }

    std::vector<slot> m_slots;
    std::vector<std::size_t> m_active;
    std::size_t m_capacity;
};
} // namespace ce::physics
