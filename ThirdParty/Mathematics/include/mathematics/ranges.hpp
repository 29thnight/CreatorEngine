// mathematics/ranges.hpp -- fixed-extent range algorithms for small math data.
//
// Standard range algorithms intentionally treat range length as a run-time
// property. These terminals use a length encoded in the range type and expand
// every access at compile time, giving optimizers a direct view of the complete
// 2/3/4-element operation.
#ifndef MATHEMATICS_RANGES_HPP
#define MATHEMATICS_RANGES_HPP

#include <mathematics/config.hpp>

#include <array>
#include <cstddef>
#include <concepts>
#include <functional>
#include <iterator>
#include <ranges>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>

namespace math::ranges {

// A transparent wrapper that adds a compile-time extent without replacing the
// wrapped range's iterator. Ordinary range-for and std::ranges algorithms keep
// using the original iterator implementation.
namespace detail {

struct no_fixed_accessor {};

// Whether an accessor can name element `index` at compile time. A view without
// one still answers get<index>() by indexing the wrapped range; both spellings
// have to exist, because structured bindings pick the member get<> as soon as
// the class declares any member named get and never fall back to a free one.
template <typename accessor_type, std::size_t index>
concept static_accessible = requires(accessor_type accessor) {
    accessor(std::integral_constant<std::size_t, index>{});
};

// C++20 equivalent of the C++23 range-adaptor closure protocol. A closure can
// produce another view or consume the range as a terminal operation.
template <typename derived_type>
struct pipe_closure {};

template <std::ranges::range range_type, typename closure_type>
    requires std::derived_from<
                 std::remove_cvref_t<closure_type>,
                 pipe_closure<std::remove_cvref_t<closure_type>>> &&
             requires(range_type&& range, closure_type&& closure) {
                 std::forward<closure_type>(closure)(
                     std::forward<range_type>(range));
             }
MATHEMATICS_INLINE constexpr decltype(auto)
operator|(range_type&& range, closure_type&& closure) {
    return std::forward<closure_type>(closure)(
        std::forward<range_type>(range));
}

template <typename callable_type, typename... bound_types>
class bind_back_closure
    : public pipe_closure<
          bind_back_closure<callable_type, bound_types...>> {
public:
    constexpr bind_back_closure(callable_type callable, bound_types... bound)
        : callable_(std::move(callable)), bound_(std::move(bound)...) {}

    template <std::ranges::range range_type>
    MATHEMATICS_INLINE constexpr decltype(auto)
    operator()(range_type&& range) & {
        return std::apply(
            [&](auto&... bound) -> decltype(auto) {
                return std::invoke(callable_,
                                   std::forward<range_type>(range), bound...);
            },
            bound_);
    }

    template <std::ranges::range range_type>
    MATHEMATICS_INLINE constexpr decltype(auto)
    operator()(range_type&& range) const& {
        return std::apply(
            [&](const auto&... bound) -> decltype(auto) {
                return std::invoke(callable_,
                                   std::forward<range_type>(range), bound...);
            },
            bound_);
    }

    template <std::ranges::range range_type>
    MATHEMATICS_INLINE constexpr decltype(auto)
    operator()(range_type&& range) && {
        return std::apply(
            [&](auto&&... bound) -> decltype(auto) {
                return std::invoke(
                    std::move(callable_), std::forward<range_type>(range),
                    std::forward<decltype(bound)>(bound)...);
            },
            std::move(bound_));
    }

private:
    callable_type callable_;
    std::tuple<bound_types...> bound_;
};

template <typename callable_type, typename... bound_types>
MATHEMATICS_NODISCARD constexpr auto
bind_back(callable_type callable, bound_types&&... bound) {
    return bind_back_closure<callable_type, std::decay_t<bound_types>...>{
        std::move(callable), std::forward<bound_types>(bound)...};
}

} // namespace detail

template <std::ranges::view base_type, std::size_t extent_value,
          typename accessor_type = detail::no_fixed_accessor>
    requires std::ranges::random_access_range<base_type> &&
             std::ranges::sized_range<base_type>
class fixed_extent_view
    : public std::ranges::view_interface<
          fixed_extent_view<base_type, extent_value, accessor_type>> {
public:
    static constexpr std::size_t static_extent = extent_value;

    fixed_extent_view() requires std::default_initializable<base_type> = default;

    constexpr explicit fixed_extent_view(base_type base)
        noexcept(std::is_nothrow_move_constructible_v<base_type>)
        : base_(std::move(base)) {}

    constexpr fixed_extent_view(base_type base, accessor_type accessor)
        noexcept(std::is_nothrow_move_constructible_v<base_type> &&
                 std::is_nothrow_move_constructible_v<accessor_type>)
        : base_(std::move(base)), accessor_(std::move(accessor)) {}

    MATHEMATICS_NODISCARD constexpr auto begin()
        noexcept(noexcept(std::ranges::begin(base_))) {
        return std::ranges::begin(base_);
    }

    MATHEMATICS_NODISCARD constexpr auto begin() const
        noexcept(noexcept(std::ranges::begin(base_)))
        requires std::ranges::range<const base_type> {
        return std::ranges::begin(base_);
    }

    MATHEMATICS_NODISCARD constexpr auto end()
        noexcept(noexcept(std::ranges::end(base_))) {
        return std::ranges::end(base_);
    }

    MATHEMATICS_NODISCARD constexpr auto end() const
        noexcept(noexcept(std::ranges::end(base_)))
        requires std::ranges::range<const base_type> {
        return std::ranges::end(base_);
    }

    MATHEMATICS_NODISCARD static constexpr std::size_t size() noexcept {
        return extent_value;
    }

    // Fixed algorithms and structured bindings prefer this path: an accessor
    // names the element at compile time where one exists, and the wrapped
    // range is indexed where it does not. Regular iteration still delegates to
    // the wrapped range's own iterator.
    template <std::size_t index>
        requires (index < extent_value)
    MATHEMATICS_NODISCARD constexpr decltype(auto) get() {
        if constexpr (detail::static_accessible<accessor_type&, index>) {
            return accessor_(std::integral_constant<std::size_t, index>{});
        } else {
            return std::ranges::begin(base_)[index];
        }
    }

    template <std::size_t index>
        requires (index < extent_value) &&
                 (detail::static_accessible<const accessor_type&, index> ||
                  std::ranges::range<const base_type>)
    MATHEMATICS_NODISCARD constexpr decltype(auto) get() const {
        if constexpr (detail::static_accessible<const accessor_type&, index>) {
            return accessor_(std::integral_constant<std::size_t, index>{});
        } else {
            return std::ranges::begin(base_)[index];
        }
    }

private:
    base_type base_{};
    accessor_type accessor_{};
};

template <std::size_t extent, std::ranges::viewable_range range_type>
MATHEMATICS_NODISCARD constexpr auto with_static_extent(range_type&& range) {
    using view_type = std::views::all_t<range_type>;
    static_assert(std::ranges::random_access_range<view_type>);
    static_assert(std::ranges::sized_range<view_type>);
    return fixed_extent_view<view_type, extent>{
        std::views::all(std::forward<range_type>(range))};
}

template <std::size_t extent, std::ranges::viewable_range range_type,
          typename accessor_type>
MATHEMATICS_NODISCARD constexpr auto
with_static_extent(range_type&& range, accessor_type accessor) {
    using view_type = std::views::all_t<range_type>;
    static_assert(std::ranges::random_access_range<view_type>);
    static_assert(std::ranges::sized_range<view_type>);
    return fixed_extent_view<view_type, extent, accessor_type>{
        std::views::all(std::forward<range_type>(range)), std::move(accessor)};
}

namespace detail {

template <typename type, typename = void>
struct static_extent : std::integral_constant<std::size_t, std::dynamic_extent> {};

template <typename type>
struct static_extent<type, std::void_t<decltype(type::static_extent)>>
    : std::integral_constant<std::size_t, type::static_extent> {};

template <typename type>
struct static_extent<const type> : static_extent<type> {};

template <typename type>
struct static_extent<volatile type> : static_extent<type> {};

template <typename type>
struct static_extent<const volatile type> : static_extent<type> {};

template <typename element_type, std::size_t extent>
struct static_extent<std::span<element_type, extent>>
    : std::integral_constant<std::size_t, extent> {};

template <typename element_type, std::size_t extent>
struct static_extent<std::array<element_type, extent>>
    : std::integral_constant<std::size_t, extent> {};

template <typename range_type>
struct static_extent<std::ranges::ref_view<range_type>>
    : static_extent<range_type> {};

template <typename range_type>
struct static_extent<std::ranges::owning_view<range_type>>
    : static_extent<range_type> {};

template <typename element_type, std::size_t extent>
struct static_extent<element_type[extent]>
    : std::integral_constant<std::size_t, extent> {};

template <typename range_type>
inline constexpr std::size_t static_extent_v =
    static_extent<std::remove_cvref_t<range_type>>::value;

template <typename range_type>
concept fixed_random_access_range =
    std::ranges::random_access_range<range_type> &&
    static_extent_v<range_type> != std::dynamic_extent;

template <std::size_t index, typename range_type>
MATHEMATICS_INLINE constexpr decltype(auto) fixed_element(range_type& range) {
    if constexpr (requires { range.template get<index>(); }) {
        return range.template get<index>();
    } else {
        return std::ranges::begin(range)[index];
    }
}

template <typename range_type, typename function_type, std::size_t... indices>
MATHEMATICS_INLINE constexpr function_type
for_each_fixed_impl(range_type&& range,
                    function_type function,
                    std::index_sequence<indices...>) {
    (static_cast<void>(
         std::invoke(function, fixed_element<indices>(range))), ...);
    return function;
}

template <typename range_type, typename value_type, typename operation_type,
          std::size_t... indices>
MATHEMATICS_INLINE constexpr value_type
fold_fixed_impl(range_type&& range,
                value_type initial,
                operation_type operation,
                std::index_sequence<indices...>) {
    ((initial = std::invoke(operation, std::move(initial),
                            fixed_element<indices>(range))), ...);
    return initial;
}

template <typename range_type, typename output_type, typename function_type,
          std::size_t... indices>
MATHEMATICS_INLINE constexpr output_type
transform_fixed_impl(range_type&& range,
                     output_type output,
                     function_type function,
                     std::index_sequence<indices...>) {
    ((static_cast<void>(*output = std::invoke(
          function, fixed_element<indices>(range))),
      static_cast<void>(++output)), ...);
    return output;
}

} // namespace detail

template <typename range_type>
inline constexpr std::size_t static_extent_v = detail::static_extent_v<range_type>;

template <typename range_type>
concept fixed_random_access_range = detail::fixed_random_access_range<range_type>;

// Like std::ranges::for_each, but the number of invocations is expanded from
// the range type rather than controlled by a run-time loop. The one-argument
// form returns a terminal closure for `range | for_each_fixed(function)`.
struct for_each_fixed_fn {
    template <detail::fixed_random_access_range range_type,
              typename function_type>
        requires std::invocable<
            function_type&, std::ranges::range_reference_t<range_type>>
    MATHEMATICS_INLINE constexpr function_type
    operator()(range_type&& range, function_type function) const {
        return detail::for_each_fixed_impl(
            std::forward<range_type>(range), std::move(function),
            std::make_index_sequence<detail::static_extent_v<range_type>>{});
    }

    template <typename function_type>
    MATHEMATICS_NODISCARD constexpr auto
    operator()(function_type function) const {
        return detail::bind_back(*this, std::move(function));
    }
};

inline constexpr for_each_fixed_fn for_each_fixed{};

// C++20 counterpart to a fixed-size fold-left. Operation is evaluated in range
// order, matching an ordinary accumulator loop. The two-argument form is a
// terminal closure for `range | fold_fixed(initial, operation)`.
struct fold_fixed_fn {
    template <detail::fixed_random_access_range range_type,
              typename value_type,
              typename operation_type>
        requires std::invocable<
                     operation_type&, value_type,
                     std::ranges::range_reference_t<range_type>> &&
                 std::assignable_from<
                     value_type&,
                     std::invoke_result_t<
                         operation_type&, value_type,
                         std::ranges::range_reference_t<range_type>>>
    MATHEMATICS_INLINE constexpr value_type
    operator()(range_type&& range,
               value_type initial,
               operation_type operation) const {
        return detail::fold_fixed_impl(
            std::forward<range_type>(range), std::move(initial),
            std::move(operation),
            std::make_index_sequence<detail::static_extent_v<range_type>>{});
    }

    template <typename value_type, typename operation_type>
    MATHEMATICS_NODISCARD constexpr auto
    operator()(value_type initial, operation_type operation) const {
        return detail::bind_back(
            *this, std::move(initial), std::move(operation));
    }
};

inline constexpr fold_fixed_fn fold_fixed{};

// Eager unary transform with the same output-iterator shape as
// std::ranges::transform. The returned iterator points one past the last result.
struct transform_fixed_fn {
    template <detail::fixed_random_access_range range_type,
              std::weakly_incrementable output_type,
              typename function_type>
        requires std::invocable<
                     function_type&,
                     std::ranges::range_reference_t<range_type>> &&
                 std::indirectly_writable<
                     output_type,
                     std::invoke_result_t<
                         function_type&,
                         std::ranges::range_reference_t<range_type>>>
    MATHEMATICS_INLINE constexpr output_type
    operator()(range_type&& range,
               output_type output,
               function_type function) const {
        return detail::transform_fixed_impl(
            std::forward<range_type>(range), std::move(output),
            std::move(function),
            std::make_index_sequence<detail::static_extent_v<range_type>>{});
    }
};

inline constexpr transform_fixed_fn transform_fixed{};

// Pipeable spelling of the eager output transform:
// `range | transform_fixed_to(output, function)`.
struct transform_fixed_to_fn {
    template <detail::fixed_random_access_range range_type,
              typename output_type,
              typename function_type>
    MATHEMATICS_INLINE constexpr decltype(auto)
    operator()(range_type&& range,
               output_type output,
               function_type function) const {
        return transform_fixed(
            std::forward<range_type>(range), std::move(output),
            std::move(function));
    }

    template <typename output_type, typename function_type>
    MATHEMATICS_NODISCARD constexpr auto
    operator()(output_type output, function_type function) const {
        return detail::bind_back(
            *this, std::move(output), std::move(function));
    }
};

inline constexpr transform_fixed_to_fn transform_fixed_to{};

// The tuple protocol, so a fixed-extent range can be consumed without forming
// a loop at all: `auto&& [x, y, z, w] = components(value);`. That matters on
// MSVC, which does not unroll an iterator-driven loop nested inside another
// loop -- any such loop, standard view or raw pointer alike (docs/BASELINE.md
// section 9). Structured bindings prefer the member get<I>() where an accessor
// supplies one; this free get is what std::apply and the fallback path use, and
// fixed_element gives both the same element.
template <std::size_t index, std::ranges::view base_type, std::size_t extent,
          typename accessor_type>
    requires (index < extent)
MATHEMATICS_NODISCARD constexpr decltype(auto)
get(fixed_extent_view<base_type, extent, accessor_type>& view) {
    return detail::fixed_element<index>(view);
}

template <std::size_t index, std::ranges::view base_type, std::size_t extent,
          typename accessor_type>
    requires (index < extent)
MATHEMATICS_NODISCARD constexpr decltype(auto)
get(const fixed_extent_view<base_type, extent, accessor_type>& view) {
    return detail::fixed_element<index>(view);
}

template <std::size_t index, std::ranges::view base_type, std::size_t extent,
          typename accessor_type>
    requires (index < extent)
MATHEMATICS_NODISCARD constexpr decltype(auto)
get(fixed_extent_view<base_type, extent, accessor_type>&& view) {
    return detail::fixed_element<index>(view);
}

} // namespace math::ranges

namespace std {

template <ranges::view base_type, size_t extent, typename accessor_type>
struct tuple_size<
    math::ranges::fixed_extent_view<base_type, extent, accessor_type>>
    : integral_constant<size_t, extent> {};

template <size_t index, ranges::view base_type, size_t extent,
          typename accessor_type>
    requires (index < extent)
struct tuple_element<
    index, math::ranges::fixed_extent_view<base_type, extent, accessor_type>> {
    using type = remove_reference_t<
        decltype(math::ranges::detail::fixed_element<index>(
            declval<math::ranges::fixed_extent_view<
                base_type, extent, accessor_type>&>()))>;
};

} // namespace std

namespace std::ranges {

template <view base_type, std::size_t extent, typename accessor_type>
inline constexpr bool
enable_borrowed_range<
    math::ranges::fixed_extent_view<base_type, extent, accessor_type>> =
    enable_borrowed_range<base_type>;

} // namespace std::ranges

#endif // MATHEMATICS_RANGES_HPP
