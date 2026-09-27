#pragma once

#include <mathematics/transform.hpp>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Animation
{
    inline thread_local std::uint64_t pose_storage_growths = 0;
    inline void ResetPoseStorageGrowths() noexcept { pose_storage_growths = 0; }
    [[nodiscard]] inline std::uint64_t TakePoseStorageGrowths() noexcept
    {
        const auto count = pose_storage_growths;
        pose_storage_growths = 0;
        return count;
    }
    // Local-space animation value. Scale keeps all three authored axes.
    struct LocalTransform final
    {
        math::vector3 m_translation{};
        math::quaternion m_rotation{ 0.f, 0.f, 0.f, 1.f };
        math::vector3 m_scale{ 1.f, 1.f, 1.f };

        [[nodiscard]] math::matrix4x4 ToMatrix() const noexcept
        { return math::compose(m_scale, m_rotation, m_translation); }
    };

    [[nodiscard]] inline LocalTransform Blend(const LocalTransform& current,
        const LocalTransform& next, float alpha) noexcept
    {
        // Exact endpoints retain the sampled value. Interior rotations use the
        // shortest arc; scale can cross zero without losing its authored sign.
        if (alpha == 0.f) return current;
        if (alpha == 1.f) return next;
        return {
            math::lerp(current.m_translation, next.m_translation, alpha),
            math::slerp(math::normalize(current.m_rotation),
                math::normalize(next.m_rotation), alpha),
            math::lerp(current.m_scale, next.m_scale, alpha)
        };
    }

    [[nodiscard]] inline LocalTransform BlendMasked(const LocalTransform& base,
        const LocalTransform& layer, float weight) noexcept
    {
        if (!std::isfinite(weight) || weight <= 0.f) return base;
        if (weight >= 1.f) return layer;
        return Blend(base, layer, weight);
    }

    // Scale deltas are differences so zero and signed reference scales remain
    // representable. Rotation composition follows math::quaternion's row-vector
    // convention: reference * delta == source.
    struct AdditiveDelta final
    {
        math::vector3 m_translation{};
        math::quaternion m_rotation{ 0.f, 0.f, 0.f, 1.f };
        math::vector3 m_scale{};
    };

    [[nodiscard]] inline AdditiveDelta MakeAdditive(const LocalTransform& source,
        const LocalTransform& reference) noexcept
    {
        return { source.m_translation - reference.m_translation,
            math::normalize(math::inverse(reference.m_rotation) * source.m_rotation),
            source.m_scale - reference.m_scale };
    }

    [[nodiscard]] inline LocalTransform ApplyAdditive(const LocalTransform& base,
        const AdditiveDelta& delta, float weight) noexcept
    {
        if (!std::isfinite(weight) || weight <= 0.f) return base;
        const float alpha = (std::min)(weight, 1.f);
        return { base.m_translation + delta.m_translation * alpha,
            math::normalize(base.m_rotation * math::slerp(
                math::quaternion::identity(), delta.m_rotation, alpha)),
            base.m_scale + delta.m_scale * alpha };
    }

    // Bone-indexed SoA pose. Final output belongs to an instance; evaluation
    // scratch belongs to the executing worker and reuses capacity across jobs.
    class LocalPose final
    {
    public:
        void Resize(std::size_t count)
        {
            const auto translationsCapacity = m_translations.capacity();
            const auto rotationsCapacity = m_rotations.capacity();
            const auto scalesCapacity = m_scales.capacity();
            m_translations.resize(count);
            m_rotations.resize(count, math::quaternion{ 0.f, 0.f, 0.f, 1.f });
            m_scales.resize(count, math::vector3{ 1.f, 1.f, 1.f });
            pose_storage_growths += std::uint64_t(m_translations.capacity() != translationsCapacity)
                + std::uint64_t(m_rotations.capacity() != rotationsCapacity)
                + std::uint64_t(m_scales.capacity() != scalesCapacity);
        }

        void Clear() noexcept
        {
            m_translations.clear();
            m_rotations.clear();
            m_scales.clear();
        }

        [[nodiscard]] std::size_t GetCount() const noexcept { return m_translations.size(); }
        [[nodiscard]] bool has_storage() const noexcept
        {
            return m_translations.capacity() != 0 || m_rotations.capacity() != 0
                || m_scales.capacity() != 0;
        }
        [[nodiscard]] std::span<const math::vector3> GetTranslations() const noexcept { return m_translations; }
        [[nodiscard]] std::span<const math::quaternion> GetRotations() const noexcept { return m_rotations; }
        [[nodiscard]] std::span<const math::vector3> GetScales() const noexcept { return m_scales; }

        [[nodiscard]] LocalTransform GetTransform(std::size_t bone) const noexcept
        {
            assert(bone < GetCount());
            return { m_translations[bone], m_rotations[bone], m_scales[bone] };
        }

        void SetTransform(std::size_t bone, const LocalTransform& local) noexcept
        {
            assert(bone < GetCount());
            m_translations[bone] = local.m_translation;
            m_rotations[bone] = local.m_rotation;
            m_scales[bone] = local.m_scale;
        }

    private:
        std::vector<math::vector3> m_translations{};
        std::vector<math::quaternion> m_rotations{};
        std::vector<math::vector3> m_scales{};
    };
}
