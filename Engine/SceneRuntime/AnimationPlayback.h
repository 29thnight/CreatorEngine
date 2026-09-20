#pragma once

#include <mathematics/matrix4x4.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>
#include <vector>

namespace animation
{
    // Gameplay reads wrapped progress; event traversal must retain the full step.
    struct ClipStep final
    {
        float time{};
        float progress{};
        double eventBegin{};
        double eventEnd{};
    };

    [[nodiscard]] inline ClipStep AdvanceClip(double time, double deltaTicks,
        double duration, bool looping) noexcept
    {
        if (!std::isfinite(time) || !std::isfinite(deltaTicks)
            || !std::isfinite(duration) || duration <= 0.0)
            return {};

        const double begin = looping ? time : std::clamp(time, 0.0, duration);
        const double end = begin + deltaTicks;
        if (!std::isfinite(end)) return {};
        const double eventEnd = looping ? end : std::clamp(end, 0.0, duration);
        double sampleTime = eventEnd;
        if (looping)
        {
            sampleTime = std::fmod(end, duration);
            if (sampleTime < 0.0) sampleTime += duration;
        }
        return { static_cast<float>(sampleTime),
            static_cast<float>(sampleTime / duration), begin / duration,
            eventEnd / duration };
    }

    // Forward: (begin,end], reverse: [end,begin). Equal endpoints mean no motion.
    // A key at 1 belongs to the ending cycle; a key at 0 to the next cycle.
    // Authored order breaks ties, including during reverse playback.
    template <typename Event, typename KeyOf, typename Emit>
    std::size_t ForEachCrossedEvent(std::span<const Event> events,
        double begin, double end, bool looping, KeyOf keyOf, Emit emit)
    {
        if (events.empty() || !std::isfinite(begin) || !std::isfinite(end)
            || begin == end)
            return 0;
        if (!looping)
        {
            begin = std::clamp(begin, 0.0, 1.0);
            end = std::clamp(end, 0.0, 1.0);
            if (begin == end) return 0;
        }
        const bool forward = end > begin;
        std::vector<std::size_t> order;
        order.reserve(events.size());
        for (std::size_t i = 0; i < events.size(); ++i)
        {
            const double key = keyOf(events[i]);
            if (std::isfinite(key) && key >= 0.0 && key <= 1.0)
                order.push_back(i);
        }
        if (order.empty()) return 0;
        std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b)
        {
            return forward ? keyOf(events[a]) < keyOf(events[b])
                : keyOf(events[a]) > keyOf(events[b]);
        });

        const double firstCycle = looping ? std::floor(begin) : 0.0;
        const double lastCycle = looping
            ? (forward ? std::floor(end) : std::floor(end) - 1.0) : 0.0;
        std::size_t count = 0;
        for (double cycle = firstCycle;;)
        {
            for (const std::size_t index : order)
            {
                const double position = cycle + keyOf(events[index]);
                const bool crossed = forward ? begin < position && position <= end
                    : end <= position && position < begin;
                if (crossed)
                {
                    emit(events[index]);
                    ++count;
                }
            }
            if (cycle == lastCycle) break;
            const double next = cycle + (forward ? 1.0 : -1.0);
            if (next == cycle) break; // Beyond representable cycle precision.
            cycle = next;
        }
        return count;
    }

    // Only evaluated, enabled channels may overwrite a bone. If every layer
    // excludes it, retain its previous local pose and still follow its parent.
    class LayerLocalPose final
    {
    public:
        explicit LayerLocalPose(const math::matrix4x4& previous) : m_local(previous) {}

        void Apply(const math::matrix4x4& local, bool enabled,
            bool hasChannel, bool maskAllows) noexcept
        {
            if (enabled && hasChannel && maskAllows) m_local = local;
        }

        [[nodiscard]] const math::matrix4x4& Local() const noexcept { return m_local; }

    private:
        math::matrix4x4 m_local;
    };
}
