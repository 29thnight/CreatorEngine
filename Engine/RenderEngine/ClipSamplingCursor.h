#pragma once

#include <algorithm>
#include <cstddef>
#include <span>
#include <vector>

namespace Animation
{
    // Same interval as the legacy forward scan: an exact key belongs to the
    // preceding segment, and extrapolation stays in the first/last segment.
    // The index is only a hint. Validate it on every read, including after seeks.
    template <class Key>
    [[nodiscard]] std::size_t FindKeyInterval(std::span<const Key> keys,
        double time, std::size_t& cursor) noexcept
    {
        if (keys.size() <= 2) return cursor = 0;
        const std::size_t last = keys.size() - 2;
        const auto contains = [&](std::size_t index)
        {
            return (index == 0 || keys[index].time < time)
                && (index == last || !(keys[index + 1].time < time));
        };
        const std::size_t index = (std::min)(cursor, last);
        if (contains(index)) return cursor = index;
        if (index < last && contains(index + 1)) return cursor = index + 1;
        if (index > 0 && contains(index - 1)) return cursor = index - 1;

        // Seeks, wraps and long steps cost O(log keys), never a scan from zero.
        const auto upper = std::lower_bound(keys.begin() + 1, keys.end() - 1,
            time, [](const Key& key, double sampleTime) { return key.time < sampleTime; });
        return cursor = static_cast<std::size_t>(upper - keys.begin()) - 1;
    }

    struct TrackKeyCursor final
    {
        std::size_t m_translation{};
        std::size_t m_rotation{};
        std::size_t m_scale{};
    };

    // An evaluation slot owns separate current/next cursors. Animator clears
    // them when rebinding a generation; published animation keys stay immutable.
    class ClipSamplingCursor final
    {
    public:
        [[nodiscard]] std::span<TrackKeyCursor> Prepare(int clipIndex, std::size_t boneCount)
        {
            m_tracks.resize(boneCount);
            if (m_clipIndex != clipIndex)
            {
                std::fill(m_tracks.begin(), m_tracks.end(), TrackKeyCursor{});
                m_clipIndex = clipIndex;
            }
            return m_tracks;
        }

        void Clear() noexcept
        {
            m_clipIndex = -1;
            m_tracks.clear();
        }

        [[nodiscard]] int GetClipIndex() const noexcept { return m_clipIndex; }
        [[nodiscard]] std::span<const TrackKeyCursor> GetTracks() const noexcept { return m_tracks; }

    private:
        int m_clipIndex{ -1 };
        std::vector<TrackKeyCursor> m_tracks{};
    };
}
