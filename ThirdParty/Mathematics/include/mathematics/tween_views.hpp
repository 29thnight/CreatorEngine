// mathematics/tween_views.hpp -- lazy easing and interpolation range adaptors.
//
// These views project an existing range of normalized progress values. They do
// not accumulate frame deltas; mutable playback state belongs to tween<T>.
#ifndef MATHEMATICS_TWEEN_VIEWS_HPP
#define MATHEMATICS_TWEEN_VIEWS_HPP

#include <mathematics/tween.hpp>
#include <mathematics/views.hpp>

#include <concepts>
#include <functional>
#include <ranges>
#include <type_traits>
#include <utility>

namespace math::views {

namespace detail {

template <std::ranges::viewable_range range_type, typename function_type>
    requires std::regular_invocable<
        function_type&,
        std::ranges::range_reference_t<std::views::all_t<range_type>>>
MATHEMATICS_NODISCARD constexpr auto
transform_preserving_extent(range_type&& range, function_type function) {
    using base_type = std::views::all_t<range_type>;
    if constexpr (ranges::fixed_random_access_range<base_type>) {
        return transform_fixed(
            std::forward<range_type>(range), std::move(function));
    } else {
        return std::views::transform(
            std::forward<range_type>(range), std::move(function));
    }
}

template <typename range_type>
concept progress_range =
    std::ranges::viewable_range<range_type> &&
    std::convertible_to<
        std::ranges::range_reference_t<std::views::all_t<range_type>>, float>;

// Capturing lambdas are not assignable in C++20, which would make a transform
// result fail the standard `view` concept. Named value projections keep the
// adaptors regular and therefore composable on both C++20 and C++23 builds.
template <typename easing_type, bool clamp_input>
struct easing_projection {
    easing_type easing_value;

    template <typename progress_type>
    MATHEMATICS_NODISCARD constexpr float
    operator()(progress_type&& progress) const noexcept {
        const float value = static_cast<float>(
            std::forward<progress_type>(progress));
        if constexpr (clamp_input)
            return math::ease_clamped(value, easing_value);
        else
            return static_cast<float>(std::invoke(easing_value, value));
    }
};

template <typename value_type, typename interpolator_type>
struct interpolation_projection {
    value_type from;
    value_type to;
    interpolator_type interpolator;

    template <typename progress_type>
    MATHEMATICS_NODISCARD constexpr value_type
    operator()(progress_type&& progress) const noexcept {
        return std::invoke(
            interpolator, from, to,
            static_cast<float>(std::forward<progress_type>(progress)));
    }
};

template <typename value_type, typename easing_type,
          typename interpolator_type>
struct tween_projection {
    value_type from;
    value_type to;
    easing_type easing_value;
    interpolator_type interpolator;

    template <typename progress_type>
    MATHEMATICS_NODISCARD constexpr value_type
    operator()(progress_type&& progress) const noexcept {
        return math::tween_value(
            from, to,
            static_cast<float>(std::forward<progress_type>(progress)),
            easing_value, interpolator);
    }
};

} // namespace detail

struct ease_fn {
    template <detail::progress_range range_type, typename easing_type>
        requires math::detail::easing_object<easing_type>
    MATHEMATICS_NODISCARD constexpr auto
    operator()(range_type&& range, easing_type easing_value) const {
        return detail::transform_preserving_extent(
            std::forward<range_type>(range),
            detail::easing_projection<easing_type, false>{
                std::move(easing_value)});
    }

    template <typename easing_type>
        requires math::detail::easing_object<easing_type>
    MATHEMATICS_NODISCARD constexpr auto
    operator()(easing_type easing_value) const {
        return ranges::detail::bind_back(*this, std::move(easing_value));
    }
};

inline constexpr ease_fn ease{};

struct ease_clamped_fn {
    template <detail::progress_range range_type, typename easing_type>
        requires math::detail::easing_object<easing_type>
    MATHEMATICS_NODISCARD constexpr auto
    operator()(range_type&& range, easing_type easing_value) const {
        return detail::transform_preserving_extent(
            std::forward<range_type>(range),
            detail::easing_projection<easing_type, true>{
                std::move(easing_value)});
    }

    template <typename easing_type>
        requires math::detail::easing_object<easing_type>
    MATHEMATICS_NODISCARD constexpr auto
    operator()(easing_type easing_value) const {
        return ranges::detail::bind_back(*this, std::move(easing_value));
    }
};

inline constexpr ease_clamped_fn ease_clamped{};

struct interpolate_fn {
    template <detail::progress_range range_type, typename value_type,
              typename interpolator_type>
        requires math::detail::interpolator_object<
            interpolator_type, value_type>
    MATHEMATICS_NODISCARD constexpr auto
    operator()(range_type&& range, value_type from, value_type to,
               interpolator_type interpolator) const {
        return detail::transform_preserving_extent(
            std::forward<range_type>(range),
            detail::interpolation_projection<value_type, interpolator_type>{
                std::move(from), std::move(to), std::move(interpolator)});
    }

    template <typename value_type, typename interpolator_type>
        requires math::detail::interpolator_object<
            interpolator_type, value_type>
    MATHEMATICS_NODISCARD constexpr auto
    operator()(value_type from, value_type to,
               interpolator_type interpolator) const {
        return ranges::detail::bind_back(
            *this, std::move(from), std::move(to),
            std::move(interpolator));
    }
};

inline constexpr interpolate_fn interpolate{};

struct lerp_fn {
    template <detail::progress_range range_type, typename value_type>
        requires linearly_interpolable<value_type>
    MATHEMATICS_NODISCARD constexpr auto
    operator()(range_type&& range, value_type from, value_type to) const {
        return interpolate(
            std::forward<range_type>(range), std::move(from), std::move(to),
            interpolation::linear);
    }

    template <typename value_type>
        requires linearly_interpolable<value_type>
    MATHEMATICS_NODISCARD constexpr auto
    operator()(value_type from, value_type to) const {
        return ranges::detail::bind_back(
            *this, std::move(from), std::move(to));
    }
};

inline constexpr lerp_fn lerp{};

struct nlerp_fn {
    template <detail::progress_range range_type>
    MATHEMATICS_NODISCARD constexpr auto
    operator()(range_type&& range, quaternion from, quaternion to) const {
        return interpolate(
            std::forward<range_type>(range), from, to,
            interpolation::normalized_linear);
    }

    MATHEMATICS_NODISCARD constexpr auto
    operator()(quaternion from, quaternion to) const {
        return ranges::detail::bind_back(*this, from, to);
    }
};

inline constexpr nlerp_fn nlerp{};

struct slerp_fn {
    template <detail::progress_range range_type>
    MATHEMATICS_NODISCARD constexpr auto
    operator()(range_type&& range, quaternion from, quaternion to) const {
        return interpolate(
            std::forward<range_type>(range), from, to,
            interpolation::spherical);
    }

    MATHEMATICS_NODISCARD constexpr auto
    operator()(quaternion from, quaternion to) const {
        return ranges::detail::bind_back(*this, from, to);
    }
};

inline constexpr slerp_fn slerp{};

struct tween_fn {
    template <detail::progress_range range_type, typename value_type,
              typename easing_type, typename interpolator_type>
        requires math::detail::easing_object<easing_type> &&
                 math::detail::interpolator_object<
                     interpolator_type, value_type>
    MATHEMATICS_NODISCARD constexpr auto
    operator()(range_type&& range, value_type from, value_type to,
               easing_type easing_value,
               interpolator_type interpolator) const {
        return detail::transform_preserving_extent(
            std::forward<range_type>(range),
            detail::tween_projection<
                value_type, easing_type, interpolator_type>{
                std::move(from), std::move(to), std::move(easing_value),
                std::move(interpolator)});
    }

    template <typename value_type, typename easing_type,
              typename interpolator_type>
        requires math::detail::easing_object<easing_type> &&
                 math::detail::interpolator_object<
                     interpolator_type, value_type>
    MATHEMATICS_NODISCARD constexpr auto
    operator()(value_type from, value_type to, easing_type easing_value,
               interpolator_type interpolator) const {
        return ranges::detail::bind_back(
            *this, std::move(from), std::move(to),
            std::move(easing_value), std::move(interpolator));
    }

    template <typename value_type, typename easing_type = easing::linear_fn>
        requires linearly_interpolable<value_type> &&
                 math::detail::easing_object<easing_type>
    MATHEMATICS_NODISCARD constexpr auto
    operator()(value_type from, value_type to,
               easing_type easing_value = {}) const {
        return (*this)(std::move(from), std::move(to),
                       std::move(easing_value), interpolation::linear);
    }
};

inline constexpr tween_fn tween{};

} // namespace math::views

#endif // MATHEMATICS_TWEEN_VIEWS_HPP
