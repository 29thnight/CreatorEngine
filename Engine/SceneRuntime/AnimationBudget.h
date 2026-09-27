#pragma once

#include "AnimTaskList.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace animation
{
    inline constexpr std::size_t task_kind_count =
        static_cast<std::size_t>(task_kind::output) + 1;

    [[nodiscard]] inline quality_stage choose_quality_hysteretic(
        quality_observation observation, quality_stage previous,
        double hysteresis) noexcept
    {
        const quality_stage raw = choose_quality(observation);
        if (raw == quality_stage::l7 || previous == quality_stage::l7
            || !observation.visible || observation.editor_preview
            || !observation.has_camera || hysteresis <= 0.) return raw;
        constexpr std::array<double, 7> boundaries{
            .12, .08, .05, .035, .02, .01, .005 };
        const double height = observation.projected_height;
        if (!std::isfinite(height)) return raw;
        const int next = static_cast<int>(raw);
        const int prior = static_cast<int>(previous);
        if (next == prior + 1 && prior < 7
            && height >= boundaries[prior] * (1. - hysteresis))
            return previous;
        if (next + 1 == prior && next < 7
            && height < boundaries[next] * (1. + hysteresis))
            return previous;
        return raw;
    }

    // Costs are measured per task kind and per evaluated bone. A recipe is
    // copied only on the owner thread, after all Update jobs have joined.
    class task_cost_model final
    {
    public:
        void observe(task_kind kind, double microseconds, std::uint32_t activeBones,
            std::uint32_t fullBones = 0) noexcept
        {
            if (!std::isfinite(microseconds) || microseconds < 0.) return;
            const std::size_t index = static_cast<std::size_t>(kind);
            double unit = microseconds / scale(kind, activeBones,
                fullBones ? fullBones : activeBones);
            auto& estimate = m_unitMicroseconds[index];
            if (m_observations[index] >= 8)
            {
                // A worker can be descheduled between the two QPC reads.
                // That is a frame spike, not a change in the recipe's cost.
                const double anchor = (std::max)(estimate, .001);
                unit = std::clamp(unit, anchor * .5, anchor * 1.5);
            }
            estimate = m_observations[index] ? estimate * .85 + unit * .15 : unit;
            ++m_observations[index];
        }

        [[nodiscard]] double estimate(task_kind kind, std::uint32_t activeBones,
            std::uint32_t fullBones) const noexcept
        {
            const std::size_t index = static_cast<std::size_t>(kind);
            // A conservative seed applies only until the first real sample.
            const double unit = m_observations[index] ? m_unitMicroseconds[index]
                : (kind == task_kind::sample_clip ? .10 : .012);
            return unit * scale(kind, activeBones, fullBones);
        }

        [[nodiscard]] bool calibrated() const noexcept
        {
            return m_observations[static_cast<std::size_t>(task_kind::sample_clip)] >= 8;
        }

        void observe_interpolation(double microseconds, std::uint32_t bones) noexcept
        {
            if (!std::isfinite(microseconds) || microseconds < 0.) return;
            double unit = microseconds / (std::max)(bones, std::uint32_t{ 1 });
            if (m_interpolationObservations >= 8)
            {
                const double anchor = (std::max)(m_interpolationUnitUs, .001);
                unit = std::clamp(unit, anchor * .5, anchor * 1.5);
            }
            m_interpolationUnitUs = m_interpolationObservations
                ? m_interpolationUnitUs * .85 + unit * .15 : unit;
            ++m_interpolationObservations;
        }

        [[nodiscard]] double recipe_cost(const task_list& recipe, quality_stage stage,
            std::uint32_t fullBones, std::uint32_t lowBones,
            bool transitioning, bool chooseNext, std::uint8_t l6Interval) const
        {
            if (stage == quality_stage::l7) return 0.;
            static thread_local task_list degraded;
            degraded.assign_recipe(recipe);
            degraded.prune_overlay_layers(stage);
            if (stage >= quality_stage::l4 && transitioning)
                (void)degraded.snap_single_blend(chooseNext);
            const std::uint32_t bones = stage >= quality_stage::l5
                ? lowBones : fullBones;
            double cost = 0.;
            degraded.for_each_reachable([&](const task& item)
            {
                if (stage >= quality_stage::l1
                    && (item.kind == task_kind::two_bone_ik
                        || item.kind == task_kind::bone_transform)) return;
                cost += estimate(item.kind, bones, fullBones);
            });
            if (stage == quality_stage::l6)
            {
                const double interval = static_cast<double>((std::max)(
                    l6Interval, std::uint8_t{ 1 }));
                const double interpolation = (m_interpolationObservations
                    ? m_interpolationUnitUs : .08) * fullBones;
                cost = cost / interval + interpolation * (1. - 1. / interval);
            }
            return cost;
        }

    private:
        [[nodiscard]] static double scale(task_kind kind,
            std::uint32_t activeBones, std::uint32_t fullBones) noexcept
        {
            if (kind == task_kind::output) return 1.;
            // L5 samples a prefix, while FK and publication still traverse
            // the complete skeleton so socket and skinning results stay valid.
            const auto bones = kind == task_kind::sample_clip
                ? activeBones : fullBones;
            return static_cast<double>((std::max)(bones, std::uint32_t{ 1 }));
        }

        std::array<double, task_kind_count> m_unitMicroseconds{};
        std::array<std::uint32_t, task_kind_count> m_observations{};
        double m_interpolationUnitUs{};
        std::uint32_t m_interpolationObservations{};
    };

    struct budget_choice final
    {
        quality_stage stage{ quality_stage::l0 };
        double predictedMicroseconds{};
    };

    template <class Estimate>
    [[nodiscard]] budget_choice choose_budget_stage(quality_stage maximum,
        quality_stage previous, double remainingMicroseconds,
        std::uint16_t& promotionFrames, std::uint16_t promotionGraceFrames,
        double hysteresis, Estimate&& estimate)
    {
        const auto cap = static_cast<int>(maximum);
        if (cap == static_cast<int>(quality_stage::l7))
            return { quality_stage::l7, 0. };
        int desired = 0;
        for (; desired < cap; ++desired)
            if (estimate(static_cast<quality_stage>(desired)) <= remainingMicroseconds)
                break;
        const int prior = static_cast<int>(previous);
        if (desired < prior && prior <= cap)
        {
            const double promotionCost = estimate(static_cast<quality_stage>(desired));
            if (promotionCost <= remainingMicroseconds * (1. - hysteresis))
                promotionFrames = static_cast<std::uint16_t>((std::min)(
                    static_cast<unsigned>(promotionFrames) + 1u, 65535u));
            else promotionFrames = 0;
            if (promotionFrames < promotionGraceFrames) desired = prior;
        }
        else promotionFrames = 0;
        const auto selected = static_cast<quality_stage>(desired);
        return { selected, estimate(selected) };
    }
}
