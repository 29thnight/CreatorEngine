#pragma once

#include <cstdint>
#include <cstddef>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace animation
{
    inline constexpr std::uint32_t invalid_task = (std::numeric_limits<std::uint32_t>::max)();
    inline constexpr std::uint32_t worker_current_output = 0;

    // A flat recipe owns no Animator, Controller or asset pointers. Every
    // dependency refers to a task recorded earlier in the same list.
    enum class task_kind : std::uint8_t
    {
        sample_clip,
        blend,
        materialize,
        prepare_composite,
        blend_masked,
        make_additive,
        apply_additive,
        materialize_composite,
        two_bone_ik,
        bone_transform,
        output
    };

    enum class quality_stage : std::uint8_t
    {
        l0, l1, l2, l3, l4, l5, l6, l7
    };

    struct quality_observation final
    {
        float projected_height{};
        bool visible{};
        bool has_camera{};
        bool editor_preview{};
    };

    [[nodiscard]] inline quality_stage choose_quality(
        quality_observation observation) noexcept
    {
        if (observation.editor_preview || !observation.has_camera)
            return quality_stage::l0;
        if (!observation.visible) return quality_stage::l7;
        const float height = std::isfinite(observation.projected_height)
            ? std::clamp(observation.projected_height, 0.f, 1.f) : 0.f;
        if (height >= .12f) return quality_stage::l0;
        if (height >= .08f) return quality_stage::l1;
        if (height >= .05f) return quality_stage::l2;
        if (height >= .035f) return quality_stage::l3;
        if (height >= .02f) return quality_stage::l4;
        if (height >= .01f) return quality_stage::l5;
        return quality_stage::l6;
    }

    // Optional procedural corrections fade before their L1 recipe disappears.
    // Hold the factor while offscreen so the first revisible L0 pose keeps it.
    [[nodiscard]] inline float advance_optional_ik_weight(float current,
        quality_stage stage, float delta_time) noexcept
    {
        constexpr float fade_seconds = .15f;
        const float target = stage == quality_stage::l0 ? 1.f : 0.f;
        if (!std::isfinite(current)) current = target;
        current = std::clamp(current, 0.f, 1.f);
        if (stage == quality_stage::l7 || !std::isfinite(delta_time)) return current;
        const float step = std::clamp(delta_time, 0.f, fade_seconds) / fade_seconds;
        if (std::abs(target - current) <= step + 1.e-6f) return target;
        return current + std::copysign(step, target - current);
    }

    struct task final
    {
        task_kind kind;
        std::uint32_t dependency_a;
        std::uint32_t dependency_b;
        std::uint32_t output_slot;
        int clip_index;
        int next_clip_index;
        float time;
        std::uint8_t sample_slot;
    };
    static_assert(std::is_trivial_v<task> && std::is_standard_layout_v<task>);

    [[nodiscard]] constexpr task make_task(task_kind kind) noexcept
    {
        return { kind, invalid_task, invalid_task, invalid_task, -1, -1, 0.f, 0 };
    }

    class task_list final
    {
    public:
        void clear() noexcept
        {
            m_tasks.clear();
            m_output = invalid_task;
        }

        [[nodiscard]] std::uint32_t append(task item)
        {
            const auto index = m_tasks.size();
            if (index >= invalid_task) throw std::length_error("animation task list overflow");
            if ((item.dependency_a != invalid_task && item.dependency_a >= index)
                || (item.dependency_b != invalid_task && item.dependency_b >= index))
                throw std::logic_error("animation task dependency must precede its consumer");
            m_tasks.push_back(item);
            return static_cast<std::uint32_t>(index);
        }

        void set_output(std::uint32_t index)
        {
            if (index >= m_tasks.size() || m_tasks[index].kind != task_kind::output)
                throw std::logic_error("animation output task is missing");
            m_output = index;
        }

        [[nodiscard]] std::size_t size() const noexcept { return m_tasks.size(); }

        void assign_recipe(const task_list& source)
        {
            m_tasks = source.m_tasks;
            m_output = source.m_output;
        }

        // Once the L1 correction fade completes, make its tasks unreachable
        // without changing the time/event recipe that Update already ran.
        void bypass_corrections() noexcept
        {
            for (std::size_t index = 0; index < m_tasks.size(); ++index)
            {
                const task& candidate = m_tasks[index];
                if (candidate.kind != task_kind::two_bone_ik
                    && candidate.kind != task_kind::bone_transform) continue;
                for (std::size_t consumer = index + 1; consumer < m_tasks.size(); ++consumer)
                {
                    task& item = m_tasks[consumer];
                    if (item.dependency_a == index)
                        item.dependency_a = candidate.dependency_a;
                    if (item.dependency_b == index)
                        item.dependency_b = candidate.dependency_a;
                }
            }
        }

        // Remove optional overlay recipes after state/event advancement has
        // recorded the full task list. The base controller (slot zero) remains
        // an input because this executor has no separate base-pose task.
        // This is a deterministic rewrite of task data; it never calls back
        // into an Animator or an asset.
        void prune_overlay_layers(quality_stage stage)
        {
            if (stage < quality_stage::l2 || m_tasks.empty()) return;
            m_removed.assign(m_tasks.size(), 0);
            m_alias.assign(m_tasks.size(), invalid_task);
            bool changed = false;
            for (std::size_t end = 0; end < m_tasks.size(); ++end)
            {
                const task& consumer = m_tasks[end];
                const bool additive = consumer.kind == task_kind::apply_additive
                    && stage >= quality_stage::l2;
                const bool masked = consumer.kind == task_kind::blend_masked
                    && stage >= quality_stage::l3;
                if ((!additive && !masked) || consumer.output_slot == 0
                    || consumer.output_slot == invalid_task) continue;
                std::size_t first = end;
                while (first > 0)
                {
                    --first;
                    const task& candidate = m_tasks[first];
                    if (candidate.kind == task_kind::sample_clip
                        && candidate.output_slot == consumer.output_slot
                        && candidate.sample_slot == 0) break;
                }
                if (m_tasks[first].kind != task_kind::sample_clip
                    || m_tasks[first].output_slot != consumer.output_slot
                    || m_tasks[first].sample_slot != 0) continue;
                const std::uint32_t predecessor = m_tasks[first].dependency_a;
                if (predecessor != invalid_task
                    && m_tasks[predecessor].kind == task_kind::prepare_composite)
                    continue;
                for (std::size_t index = first; index <= end; ++index)
                {
                    m_removed[index] = 1;
                    m_alias[index] = predecessor;
                }
                changed = true;
            }
            if (!changed) return;
            m_remap.assign(m_tasks.size(), invalid_task);
            m_rewritten.clear();
            m_rewritten.reserve(m_tasks.size());
            for (std::size_t index = 0; index < m_tasks.size(); ++index)
            {
                const auto remap = [&](std::uint32_t old) -> std::uint32_t
                {
                    return old == invalid_task ? invalid_task : m_remap[old];
                };
                if (m_removed[index])
                {
                    m_remap[index] = remap(m_alias[index]);
                    continue;
                }
                task item = m_tasks[index];
                item.dependency_a = remap(item.dependency_a);
                item.dependency_b = remap(item.dependency_b);
                m_remap[index] = static_cast<std::uint32_t>(m_rewritten.size());
                m_rewritten.push_back(item);
            }
            if (m_output != invalid_task) m_output = m_remap[m_output];
            m_tasks.swap(m_rewritten);
        }

        // L4 replaces a single-controller crossfade with its chosen sample.
        // The unused sample and blend become unreachable; playback advancement
        // has already happened before this recipe transformation.
        [[nodiscard]] bool snap_single_blend(bool choose_next)
        {
            for (std::size_t index = 0; index < m_tasks.size(); ++index)
            {
                task& blend = m_tasks[index];
                if (blend.kind != task_kind::blend) continue;
                if (blend.dependency_a == invalid_task || blend.dependency_b == invalid_task)
                    throw std::logic_error("animation blend has missing samples");
                task& current = m_tasks[blend.dependency_a];
                task& next = m_tasks[blend.dependency_b];
                if (current.kind != task_kind::sample_clip
                    || next.kind != task_kind::sample_clip)
                    throw std::logic_error("animation blend has invalid samples");
                const std::uint32_t chosen = choose_next
                    ? blend.dependency_b : blend.dependency_a;
                if (choose_next)
                {
                    // The promoted sample now owns the current worker slot.
                    next.sample_slot = 0;
                    next.dependency_a = current.dependency_a;
                }
                for (std::size_t consumer = index + 1; consumer < m_tasks.size(); ++consumer)
                {
                    task& item = m_tasks[consumer];
                    if (item.dependency_a == index) item.dependency_a = chosen;
                    if (item.dependency_b == index) item.dependency_b = chosen;
                }
                // There is only one contributing controller at this call site.
                return true;
            }
            return false;
        }

        template <class Visit>
        void for_each_reachable_indexed(Visit&& visit)
        {
            if (m_output == invalid_task) return;
            m_reachable.assign(m_tasks.size(), 0);
            m_reachable[m_output] = 1;
            for (std::size_t index = m_tasks.size(); index-- > 0;)
            {
                if (!m_reachable[index]) continue;
                const task& item = m_tasks[index];
                if (item.dependency_a != invalid_task) m_reachable[item.dependency_a] = 1;
                if (item.dependency_b != invalid_task) m_reachable[item.dependency_b] = 1;
            }
            for (std::size_t index = 0; index < m_tasks.size(); ++index)
                if (m_reachable[index])
                    visit(static_cast<std::uint32_t>(index), m_tasks[index]);
        }

        template <class Visit>
        void for_each_reachable(Visit&& visit)
        {
            for_each_reachable_indexed([&](std::uint32_t, const task& item)
            { visit(item); });
        }

        // Snapshot the finalized recipe, including nodes that the executor
        // cannot reach. The caller records this frame's recipe before reuse.
        template <class Visit>
        void for_each_task_snapshot(Visit&& visit) const
        {
            std::vector<std::uint8_t> reachable(m_tasks.size(), 0);
            if (m_output != invalid_task) reachable[m_output] = 1;
            for (std::size_t index = m_tasks.size(); index-- > 0;)
            {
                if (!reachable[index]) continue;
                const task& item = m_tasks[index];
                if (item.dependency_a != invalid_task) reachable[item.dependency_a] = 1;
                if (item.dependency_b != invalid_task) reachable[item.dependency_b] = 1;
            }
            for (std::size_t index = 0; index < m_tasks.size(); ++index)
                visit(static_cast<std::uint32_t>(index), m_tasks[index], reachable[index] != 0);
        }

    private:
        std::vector<task> m_tasks{};
        std::vector<std::uint8_t> m_reachable{};
        std::vector<std::uint8_t> m_removed{};
        std::vector<std::uint32_t> m_alias{};
        std::vector<std::uint32_t> m_remap{};
        std::vector<task> m_rewritten{};
        std::uint32_t m_output{ invalid_task };
    };
}
