// mathematics/easing.hpp -- constexpr easing functions over normalized progress.
//
// The functions do not clamp their input. Their contract is [0, 1], while
// ease_clamped() provides the explicit bounded spelling. Overshooting curves
// (back and elastic) may intentionally return values outside [0, 1].
#ifndef MATHEMATICS_EASING_HPP
#define MATHEMATICS_EASING_HPP

#include <mathematics/scalar.hpp>

#include <concepts>
#include <functional>
#include <type_traits>

namespace math::easing {

struct linear_fn {
    MATHEMATICS_NODISCARD constexpr float operator()(float t) const noexcept {
        return t;
    }
};

struct step_fn {
    MATHEMATICS_NODISCARD constexpr float operator()(float t) const noexcept {
        if (t != t) return t;
        return t < 0.5f ? 0.0f : 1.0f;
    }
};

struct smoothstep_fn {
    MATHEMATICS_NODISCARD constexpr float operator()(float t) const noexcept {
        return t * t * (3.0f - 2.0f * t);
    }
};

struct smootherstep_fn {
    MATHEMATICS_NODISCARD constexpr float operator()(float t) const noexcept {
        return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
    }
};

struct quadratic_in_fn {
    MATHEMATICS_NODISCARD constexpr float operator()(float t) const noexcept {
        return t * t;
    }
};

struct quadratic_out_fn {
    MATHEMATICS_NODISCARD constexpr float operator()(float t) const noexcept {
        const float x = 1.0f - t;
        return 1.0f - x * x;
    }
};

struct quadratic_in_out_fn {
    MATHEMATICS_NODISCARD constexpr float operator()(float t) const noexcept {
        if (t < 0.5f) return 2.0f * t * t;
        const float x = -2.0f * t + 2.0f;
        return 1.0f - x * x * 0.5f;
    }
};

struct cubic_in_fn {
    MATHEMATICS_NODISCARD constexpr float operator()(float t) const noexcept {
        return t * t * t;
    }
};

struct cubic_out_fn {
    MATHEMATICS_NODISCARD constexpr float operator()(float t) const noexcept {
        const float x = 1.0f - t;
        return 1.0f - x * x * x;
    }
};

struct cubic_in_out_fn {
    MATHEMATICS_NODISCARD constexpr float operator()(float t) const noexcept {
        if (t < 0.5f) return 4.0f * t * t * t;
        const float x = -2.0f * t + 2.0f;
        return 1.0f - x * x * x * 0.5f;
    }
};

struct quartic_in_fn {
    MATHEMATICS_NODISCARD constexpr float operator()(float t) const noexcept {
        const float t2 = t * t;
        return t2 * t2;
    }
};

struct quartic_out_fn {
    MATHEMATICS_NODISCARD constexpr float operator()(float t) const noexcept {
        const float x = 1.0f - t;
        const float x2 = x * x;
        return 1.0f - x2 * x2;
    }
};

struct quartic_in_out_fn {
    MATHEMATICS_NODISCARD constexpr float operator()(float t) const noexcept {
        if (t < 0.5f) {
            const float x = 2.0f * t;
            const float x2 = x * x;
            return x2 * x2 * 0.5f;
        }
        const float x = -2.0f * t + 2.0f;
        const float x2 = x * x;
        return 1.0f - x2 * x2 * 0.5f;
    }
};

struct quintic_in_fn {
    MATHEMATICS_NODISCARD constexpr float operator()(float t) const noexcept {
        const float t2 = t * t;
        return t2 * t2 * t;
    }
};

struct quintic_out_fn {
    MATHEMATICS_NODISCARD constexpr float operator()(float t) const noexcept {
        const float x = 1.0f - t;
        const float x2 = x * x;
        return 1.0f - x2 * x2 * x;
    }
};

struct quintic_in_out_fn {
    MATHEMATICS_NODISCARD constexpr float operator()(float t) const noexcept {
        if (t < 0.5f) {
            const float x = 2.0f * t;
            const float x2 = x * x;
            return x2 * x2 * x * 0.5f;
        }
        const float x = -2.0f * t + 2.0f;
        const float x2 = x * x;
        return 1.0f - x2 * x2 * x * 0.5f;
    }
};

struct sine_in_fn {
    MATHEMATICS_NODISCARD constexpr float operator()(float t) const noexcept {
        if (t == 0.0f) return 0.0f;
        if (t == 1.0f) return 1.0f;
        return 1.0f - math::cos(t * half_pi);
    }
};

struct sine_out_fn {
    MATHEMATICS_NODISCARD constexpr float operator()(float t) const noexcept {
        if (t == 0.0f) return 0.0f;
        if (t == 1.0f) return 1.0f;
        return math::sin(t * half_pi);
    }
};

struct sine_in_out_fn {
    MATHEMATICS_NODISCARD constexpr float operator()(float t) const noexcept {
        if (t == 0.0f) return 0.0f;
        if (t == 1.0f) return 1.0f;
        return (1.0f - math::cos(pi * t)) * 0.5f;
    }
};

struct circular_in_fn {
    MATHEMATICS_NODISCARD constexpr float operator()(float t) const noexcept {
        if (t == 0.0f) return 0.0f;
        if (t == 1.0f) return 1.0f;
        return 1.0f - math::sqrt(1.0f - t * t);
    }
};

struct circular_out_fn {
    MATHEMATICS_NODISCARD constexpr float operator()(float t) const noexcept {
        if (t == 0.0f) return 0.0f;
        if (t == 1.0f) return 1.0f;
        const float x = t - 1.0f;
        return math::sqrt(1.0f - x * x);
    }
};

struct circular_in_out_fn {
    MATHEMATICS_NODISCARD constexpr float operator()(float t) const noexcept {
        if (t == 0.0f) return 0.0f;
        if (t == 1.0f) return 1.0f;
        if (t < 0.5f) {
            const float x = 2.0f * t;
            return (1.0f - math::sqrt(1.0f - x * x)) * 0.5f;
        }
        const float x = -2.0f * t + 2.0f;
        return (math::sqrt(1.0f - x * x) + 1.0f) * 0.5f;
    }
};

struct exponential_in_fn {
    MATHEMATICS_NODISCARD constexpr float operator()(float t) const noexcept {
        if (t == 0.0f) return 0.0f;
        if (t == 1.0f) return 1.0f;
        return math::exp2(10.0f * t - 10.0f);
    }
};

struct exponential_out_fn {
    MATHEMATICS_NODISCARD constexpr float operator()(float t) const noexcept {
        if (t == 0.0f) return 0.0f;
        if (t == 1.0f) return 1.0f;
        return 1.0f - math::exp2(-10.0f * t);
    }
};

struct exponential_in_out_fn {
    MATHEMATICS_NODISCARD constexpr float operator()(float t) const noexcept {
        if (t == 0.0f) return 0.0f;
        if (t == 1.0f) return 1.0f;
        if (t < 0.5f) return math::exp2(20.0f * t - 10.0f) * 0.5f;
        return (2.0f - math::exp2(-20.0f * t + 10.0f)) * 0.5f;
    }
};

struct elastic_in_fn {
    MATHEMATICS_NODISCARD constexpr float operator()(float t) const noexcept {
        if (t == 0.0f) return 0.0f;
        if (t == 1.0f) return 1.0f;
        constexpr float c4 = two_pi / 3.0f;
        return -math::exp2(10.0f * t - 10.0f) *
               math::sin((10.0f * t - 10.75f) * c4);
    }
};

struct elastic_out_fn {
    MATHEMATICS_NODISCARD constexpr float operator()(float t) const noexcept {
        if (t == 0.0f) return 0.0f;
        if (t == 1.0f) return 1.0f;
        constexpr float c4 = two_pi / 3.0f;
        return math::exp2(-10.0f * t) *
                   math::sin((10.0f * t - 0.75f) * c4) +
               1.0f;
    }
};

struct elastic_in_out_fn {
    MATHEMATICS_NODISCARD constexpr float operator()(float t) const noexcept {
        if (t == 0.0f) return 0.0f;
        if (t == 1.0f) return 1.0f;
        constexpr float c5 = two_pi / 4.5f;
        if (t < 0.5f) {
            return -(math::exp2(20.0f * t - 10.0f) *
                     math::sin((20.0f * t - 11.125f) * c5)) * 0.5f;
        }
        return math::exp2(-20.0f * t + 10.0f) *
                   math::sin((20.0f * t - 11.125f) * c5) * 0.5f +
               1.0f;
    }
};

struct back_in_fn {
    MATHEMATICS_NODISCARD constexpr float operator()(float t) const noexcept {
        if (t == 0.0f) return 0.0f;
        if (t == 1.0f) return 1.0f;
        constexpr float c1 = 1.70158f;
        constexpr float c3 = c1 + 1.0f;
        return c3 * t * t * t - c1 * t * t;
    }
};

struct back_out_fn {
    MATHEMATICS_NODISCARD constexpr float operator()(float t) const noexcept {
        if (t == 0.0f) return 0.0f;
        if (t == 1.0f) return 1.0f;
        constexpr float c1 = 1.70158f;
        constexpr float c3 = c1 + 1.0f;
        const float x = t - 1.0f;
        return 1.0f + c3 * x * x * x + c1 * x * x;
    }
};

struct back_in_out_fn {
    MATHEMATICS_NODISCARD constexpr float operator()(float t) const noexcept {
        if (t == 0.0f) return 0.0f;
        if (t == 1.0f) return 1.0f;
        constexpr float c2 = 1.70158f * 1.525f;
        if (t < 0.5f) {
            const float x = 2.0f * t;
            return x * x * ((c2 + 1.0f) * x - c2) * 0.5f;
        }
        const float x = 2.0f * t - 2.0f;
        return (x * x * ((c2 + 1.0f) * x + c2) + 2.0f) * 0.5f;
    }
};

struct bounce_out_fn {
    MATHEMATICS_NODISCARD constexpr float operator()(float t) const noexcept {
        if (t == 0.0f) return 0.0f;
        if (t == 1.0f) return 1.0f;
        constexpr float n1 = 7.5625f;
        constexpr float d1 = 2.75f;
        if (t < 1.0f / d1) return n1 * t * t;
        if (t < 2.0f / d1) {
            const float x = t - 1.5f / d1;
            return n1 * x * x + 0.75f;
        }
        if (t < 2.5f / d1) {
            const float x = t - 2.25f / d1;
            return n1 * x * x + 0.9375f;
        }
        const float x = t - 2.625f / d1;
        return n1 * x * x + 0.984375f;
    }
};

struct bounce_in_fn {
    MATHEMATICS_NODISCARD constexpr float operator()(float t) const noexcept;
};

struct bounce_in_out_fn {
    MATHEMATICS_NODISCARD constexpr float operator()(float t) const noexcept;
};

inline constexpr linear_fn linear{};
inline constexpr step_fn step{};
inline constexpr smoothstep_fn smoothstep{};
inline constexpr smootherstep_fn smootherstep{};
inline constexpr quadratic_in_fn quadratic_in{};
inline constexpr quadratic_out_fn quadratic_out{};
inline constexpr quadratic_in_out_fn quadratic_in_out{};
inline constexpr cubic_in_fn cubic_in{};
inline constexpr cubic_out_fn cubic_out{};
inline constexpr cubic_in_out_fn cubic_in_out{};
inline constexpr quartic_in_fn quartic_in{};
inline constexpr quartic_out_fn quartic_out{};
inline constexpr quartic_in_out_fn quartic_in_out{};
inline constexpr quintic_in_fn quintic_in{};
inline constexpr quintic_out_fn quintic_out{};
inline constexpr quintic_in_out_fn quintic_in_out{};
inline constexpr sine_in_fn sine_in{};
inline constexpr sine_out_fn sine_out{};
inline constexpr sine_in_out_fn sine_in_out{};
inline constexpr circular_in_fn circular_in{};
inline constexpr circular_out_fn circular_out{};
inline constexpr circular_in_out_fn circular_in_out{};
inline constexpr exponential_in_fn exponential_in{};
inline constexpr exponential_out_fn exponential_out{};
inline constexpr exponential_in_out_fn exponential_in_out{};
inline constexpr elastic_in_fn elastic_in{};
inline constexpr elastic_out_fn elastic_out{};
inline constexpr elastic_in_out_fn elastic_in_out{};
inline constexpr back_in_fn back_in{};
inline constexpr back_out_fn back_out{};
inline constexpr back_in_out_fn back_in_out{};
inline constexpr bounce_out_fn bounce_out{};
inline constexpr bounce_in_fn bounce_in{};
inline constexpr bounce_in_out_fn bounce_in_out{};

MATHEMATICS_INLINE constexpr float bounce_in_fn::operator()(float t) const noexcept {
    if (t == 0.0f) return 0.0f;
    if (t == 1.0f) return 1.0f;
    return 1.0f - bounce_out(1.0f - t);
}

MATHEMATICS_INLINE constexpr float
bounce_in_out_fn::operator()(float t) const noexcept {
    if (t == 0.0f) return 0.0f;
    if (t == 1.0f) return 1.0f;
    if (t < 0.5f) return (1.0f - bounce_out(1.0f - 2.0f * t)) * 0.5f;
    return (1.0f + bounce_out(2.0f * t - 1.0f)) * 0.5f;
}

} // namespace math::easing

namespace math {

template <typename easing_type>
    requires std::invocable<const easing_type&, float> &&
             std::convertible_to<
                 std::invoke_result_t<const easing_type&, float>, float>
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr float
ease_clamped(float progress, const easing_type& easing_value) noexcept(
    std::is_nothrow_invocable_v<const easing_type&, float>) {
    if (progress != progress) return progress;
    return static_cast<float>(std::invoke(easing_value, saturate(progress)));
}

// Owning, allocation-free erasure for stateless easing objects. Keeping the
// wrapper to one function pointer makes tween<T> homogeneous even when a pool
// mixes curves. Capturing/stateful functors belong in a static policy layer,
// not in the manager-friendly value type.
class easing_function {
public:
    using function_type = float (*)(float) noexcept;

    constexpr easing_function() noexcept
        : function_(&invoke<easing::linear_fn>) {}

    template <typename easing_type>
        requires std::is_empty_v<std::remove_cvref_t<easing_type>> &&
                 std::default_initializable<std::remove_cvref_t<easing_type>> &&
                 std::is_nothrow_invocable_r_v<
                     float, const std::remove_cvref_t<easing_type>&, float>
    constexpr easing_function(easing_type) noexcept
        : function_(&invoke<std::remove_cvref_t<easing_type>>) {}

    MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr float
    operator()(float progress) const noexcept {
        return function_(progress);
    }

private:
    template <typename easing_type>
    MATHEMATICS_NODISCARD static constexpr float invoke(float progress) noexcept {
        return easing_type{}(progress);
    }

    function_type function_;
};

static_assert(sizeof(easing_function) == sizeof(easing_function::function_type));
static_assert(std::is_trivially_copyable_v<easing_function>);

} // namespace math

#endif // MATHEMATICS_EASING_HPP
