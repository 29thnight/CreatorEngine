#pragma once

#include <mathematics/matrix4x4.hpp>

#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

namespace ce
{
    // A captured batch owns this arena until every queued command and retained
    // render proxy releases it. Offsets remain valid while the vector grows;
    // pointers are exposed only after the batch seals the arena.
    class animation_palette_arena final
    {
    public:
        struct palette_slice
        {
            std::uint32_t offset{};
            std::uint32_t count{};
            bool copied{};
            bool grew{};
        };

        palette_slice capture(std::uint64_t animator_key,
            std::span<const math::matrix4x4> palette)
        {
            if (m_sealed) throw std::logic_error("animation palette arena is sealed");
            if (palette.empty()) return {};
            if (palette.size() > (std::numeric_limits<std::uint32_t>::max)()
                || m_matrices.size() > (std::numeric_limits<std::uint32_t>::max)() - palette.size())
                throw std::length_error("animation palette arena offset overflow");

            for (const entry& candidate : m_entries)
            {
                if (candidate.animator_key != animator_key
                    || candidate.count != palette.size()) continue;
                const auto* previous = m_matrices.data() + candidate.offset;
                if (0 == std::memcmp(previous, palette.data(),
                    palette.size_bytes()))
                    return { candidate.offset, candidate.count, false, false };
            }

            const auto offset = static_cast<std::uint32_t>(m_matrices.size());
            const auto count = static_cast<std::uint32_t>(palette.size());
            const auto matrixCapacity = m_matrices.capacity();
            const auto entryCapacity = m_entries.capacity();
            m_matrices.insert(m_matrices.end(), palette.begin(), palette.end());
            m_entries.push_back({ animator_key, offset, count });
            return { offset, count, true,
                m_matrices.capacity() != matrixCapacity || m_entries.capacity() != entryCapacity };
        }

        void seal() noexcept { m_sealed = true; }
        [[nodiscard]] std::size_t size() const noexcept { return m_matrices.size(); }

        void reset()
        {
            if (!m_sealed) throw std::logic_error("active animation palette arena cannot reset");
            m_matrices.clear();
            m_entries.clear();
            m_sealed = false;
        }

        const math::matrix4x4* resolve(std::uint32_t offset,
            std::uint32_t count) const noexcept
        {
            if (!m_sealed || count == 0 || offset > m_matrices.size()
                || count > m_matrices.size() - offset) return nullptr;
            return m_matrices.data() + offset;
        }

    private:
        struct entry
        {
            std::uint64_t animator_key{};
            std::uint32_t offset{};
            std::uint32_t count{};
        };
        std::vector<math::matrix4x4> m_matrices;
        std::vector<entry> m_entries;
        bool m_sealed{ false };
    };
}
