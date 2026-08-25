// mathematics/views.hpp -- non-owning C++20 range views over packed math types.
//
// These adapters are intended for inspection, serialization and generic range
// algorithms. Core vector and matrix arithmetic keeps using the named members,
// row loads and SIMD paths in the owning types.
#ifndef MATHEMATICS_VIEWS_HPP
#define MATHEMATICS_VIEWS_HPP

#include <mathematics/matrix.hpp>
#include <mathematics/ranges.hpp>
#include <mathematics/vector.hpp>

#include <cstddef>
#include <concepts>
#include <functional>
#include <iterator>
#include <ranges>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>

namespace math {

namespace detail {

template <typename type>
concept component_vector =
    std::same_as<std::remove_cv_t<type>, vector2> ||
    std::same_as<std::remove_cv_t<type>, vector3> ||
    std::same_as<std::remove_cv_t<type>, vector4>;

template <typename type>
concept stored_matrix =
    std::same_as<std::remove_cv_t<type>, matrix3x3> ||
    std::same_as<std::remove_cv_t<type>, matrix4x4>;

// Named members are not an array, so forming &x + index is not a portable way
// to traverse them. At run time, address the actual member through the standard
// layout object's byte representation; constant evaluation selects the named
// member directly because reinterpret_cast is not permitted there.
template <component_vector vector_type>
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr decltype(auto)
component_reference(vector_type& value, std::size_t index) noexcept {
    using unqualified_type = std::remove_cv_t<vector_type>;

    if (std::is_constant_evaluated()) {
        if (index == 0) return (value.x);
        if (index == 1) return (value.y);
        if constexpr (unqualified_type::lane_count >= 3) {
            if (index == 2) return (value.z);
        }
        if constexpr (unqualified_type::lane_count == 4) {
            return (value.w);
        } else {
            // components() only supplies valid indices. This return keeps the
            // invalid-index path well-formed during constant evaluation.
            return (value.x);
        }
    }

    using element_type =
        std::conditional_t<std::is_const_v<vector_type>, const float, float>;
    using byte_type =
        std::conditional_t<std::is_const_v<vector_type>, const std::byte, std::byte>;
    auto* bytes = reinterpret_cast<byte_type*>(&value);
    return *reinterpret_cast<element_type*>(bytes + index * sizeof(float));
}

template <stored_matrix matrix_type>
inline constexpr std::size_t matrix_order_v =
    std::same_as<std::remove_cv_t<matrix_type>, matrix3x3> ? 3u : 4u;

template <component_vector vector_type>
struct component_static_accessor {
    vector_type* pointer;

    template <std::size_t index>
    MATHEMATICS_NODISCARD constexpr decltype(auto)
    operator()(std::integral_constant<std::size_t, index>) const noexcept {
        using unqualified_type = std::remove_cv_t<vector_type>;
        static_assert(index <
                      static_cast<std::size_t>(unqualified_type::lane_count));
        if constexpr (index == 0) {
            return (pointer->x);
        } else if constexpr (index == 1) {
            return (pointer->y);
        } else if constexpr (index == 2) {
            return (pointer->z);
        } else {
            return (pointer->w);
        }
    }
};

template <stored_matrix matrix_type>
struct row_static_accessor {
    matrix_type* pointer;

    template <std::size_t index>
    MATHEMATICS_NODISCARD constexpr auto
    operator()(std::integral_constant<std::size_t, index>) const noexcept {
        constexpr std::size_t order = matrix_order_v<matrix_type>;
        static_assert(index < order);
        using element_type =
            std::conditional_t<std::is_const_v<matrix_type>, const float, float>;
        return std::span<element_type, order>{pointer->m[index]};
    }
};

} // namespace detail

// These assertions are part of the adapter contract. Padding changes fail at
// compile time instead of silently changing component mapping.
static_assert(offsetof(vector2, x) == 0);
static_assert(offsetof(vector2, y) == sizeof(float));
static_assert(offsetof(vector3, x) == 0);
static_assert(offsetof(vector3, y) == sizeof(float));
static_assert(offsetof(vector3, z) == 2 * sizeof(float));
static_assert(offsetof(vector4, x) == 0);
static_assert(offsetof(vector4, y) == sizeof(float));
static_assert(offsetof(vector4, z) == 2 * sizeof(float));
static_assert(offsetof(vector4, w) == 3 * sizeof(float));

// Mutable and const lvalues produce mutable and const references respectively.
// An owning vector is deliberately not itself a range, and temporaries cannot
// bind here, so a view cannot accidentally outlive the object it references.
template <detail::component_vector vector_type>
MATHEMATICS_NODISCARD constexpr auto components(vector_type& value) noexcept {
    constexpr std::size_t count =
        static_cast<std::size_t>(std::remove_cv_t<vector_type>::lane_count);
    auto base = std::views::iota(std::size_t{0}, count) |
                std::views::transform([pointer = &value](std::size_t index)
                                          -> decltype(auto) {
                    return detail::component_reference(*pointer, index);
                });
    return ranges::with_static_extent<count>(
        std::move(base), detail::component_static_accessor<vector_type>{&value});
}

// A row is a fixed-extent span. rows() is convenient for generic algorithms,
// but direct m[row][column] access remains the preferred matrix hot path.
template <detail::stored_matrix matrix_type>
MATHEMATICS_NODISCARD constexpr auto rows(matrix_type& value) noexcept {
    constexpr std::size_t order = detail::matrix_order_v<matrix_type>;
    using element_type =
        std::conditional_t<std::is_const_v<matrix_type>, const float, float>;
    auto base = std::views::iota(std::size_t{0}, order) |
                std::views::transform([pointer = &value](std::size_t row) {
                    return std::span<element_type, order>{pointer->m[row]};
                });
    return ranges::with_static_extent<order>(
        std::move(base), detail::row_static_accessor<matrix_type>{&value});
}

namespace views {

// A lazy transform that preserves a fixed input extent and its compile-time
// get<I>() path. It remains an ordinary random-access view for range-for, while
// fixed terminals can see through the transform without rebuilding a loop.
template <std::ranges::view base_type, typename function_type>
    requires ranges::fixed_random_access_range<base_type> &&
             std::is_object_v<function_type> &&
             std::regular_invocable<
                 function_type&,
                 std::ranges::range_reference_t<base_type>>
class fixed_transform_view
    : public std::ranges::view_interface<
          fixed_transform_view<base_type, function_type>> {
private:
    template <bool constant>
    class iterator {
    private:
        using parent_type = std::conditional_t<
            constant, const fixed_transform_view, fixed_transform_view>;
        using base_range_type =
            std::conditional_t<constant, const base_type, base_type>;
        using base_iterator = std::ranges::iterator_t<base_range_type>;
        using function_reference = std::conditional_t<
            constant, const function_type&, function_type&>;

        friend class fixed_transform_view;

        constexpr iterator(parent_type* parent, base_iterator current)
            : parent_(parent), current_(std::move(current)) {}

    public:
        using iterator_concept = std::random_access_iterator_tag;
        using iterator_category = std::input_iterator_tag;
        using value_type = std::remove_cvref_t<std::invoke_result_t<
            function_reference,
            std::ranges::range_reference_t<base_range_type>>>;
        using difference_type =
            std::ranges::range_difference_t<base_range_type>;

        iterator() = default;

        MATHEMATICS_NODISCARD constexpr decltype(auto) operator*() const {
            return std::invoke(parent_->function_, *current_);
        }

        MATHEMATICS_NODISCARD constexpr decltype(auto)
        operator[](difference_type offset) const {
            return std::invoke(parent_->function_, current_[offset]);
        }

        constexpr iterator& operator++() {
            ++current_;
            return *this;
        }

        constexpr iterator operator++(int) {
            auto previous = *this;
            ++*this;
            return previous;
        }

        constexpr iterator& operator--() {
            --current_;
            return *this;
        }

        constexpr iterator operator--(int) {
            auto previous = *this;
            --*this;
            return previous;
        }

        constexpr iterator& operator+=(difference_type offset) {
            current_ += offset;
            return *this;
        }

        constexpr iterator& operator-=(difference_type offset) {
            current_ -= offset;
            return *this;
        }

        friend constexpr iterator
        operator+(iterator current, difference_type offset) {
            current += offset;
            return current;
        }

        friend constexpr iterator
        operator+(difference_type offset, iterator current) {
            current += offset;
            return current;
        }

        friend constexpr iterator
        operator-(iterator current, difference_type offset) {
            current -= offset;
            return current;
        }

        friend constexpr difference_type
        operator-(const iterator& left, const iterator& right) {
            return left.current_ - right.current_;
        }

        friend constexpr bool
        operator==(const iterator& left, const iterator& right) {
            return left.current_ == right.current_;
        }

        friend constexpr bool
        operator<(const iterator& left, const iterator& right) {
            return left.current_ < right.current_;
        }

        friend constexpr bool
        operator>(const iterator& left, const iterator& right) {
            return right < left;
        }

        friend constexpr bool
        operator<=(const iterator& left, const iterator& right) {
            return !(right < left);
        }

        friend constexpr bool
        operator>=(const iterator& left, const iterator& right) {
            return !(left < right);
        }

    private:
        parent_type* parent_ = nullptr;
        base_iterator current_{};
    };

public:
    static constexpr std::size_t static_extent =
        ranges::static_extent_v<base_type>;

    fixed_transform_view()
        requires std::default_initializable<base_type> &&
                 std::default_initializable<function_type> = default;

    constexpr fixed_transform_view(base_type base, function_type function)
        noexcept(std::is_nothrow_move_constructible_v<base_type> &&
                 std::is_nothrow_move_constructible_v<function_type>)
        : base_(std::move(base)), function_(std::move(function)) {}

    MATHEMATICS_NODISCARD constexpr auto begin() {
        return iterator<false>{this, std::ranges::begin(base_)};
    }

    MATHEMATICS_NODISCARD constexpr auto end() {
        return iterator<false>{
            this, std::ranges::begin(base_) +
                      static_cast<std::ranges::range_difference_t<base_type>>(
                          static_extent)};
    }

    MATHEMATICS_NODISCARD constexpr auto begin() const
        requires std::ranges::range<const base_type> &&
                 std::regular_invocable<
                     const function_type&,
                     std::ranges::range_reference_t<const base_type>> {
        return iterator<true>{this, std::ranges::begin(base_)};
    }

    MATHEMATICS_NODISCARD constexpr auto end() const
        requires std::ranges::range<const base_type> &&
                 std::regular_invocable<
                     const function_type&,
                     std::ranges::range_reference_t<const base_type>> {
        return iterator<true>{
            this, std::ranges::begin(base_) +
                      static_cast<
                          std::ranges::range_difference_t<const base_type>>(
                          static_extent)};
    }

    MATHEMATICS_NODISCARD static constexpr std::size_t size() noexcept {
        return static_extent;
    }

    template <std::size_t index>
        requires (index < static_extent)
    MATHEMATICS_NODISCARD constexpr decltype(auto) get() {
        return std::invoke(
            function_, ranges::detail::fixed_element<index>(base_));
    }

    template <std::size_t index>
        requires (index < static_extent) &&
                 std::regular_invocable<
                     const function_type&,
                     decltype(ranges::detail::fixed_element<index>(
                         std::declval<const base_type&>()))>
    MATHEMATICS_NODISCARD constexpr decltype(auto) get() const {
        return std::invoke(
            function_, ranges::detail::fixed_element<index>(base_));
    }

private:
    base_type base_;
    function_type function_;
};

struct transform_fixed_fn {
    template <std::ranges::viewable_range range_type, typename function_type>
        requires ranges::fixed_random_access_range<
                     std::views::all_t<range_type>> &&
                 std::regular_invocable<
                     function_type&,
                     std::ranges::range_reference_t<
                         std::views::all_t<range_type>>>
    MATHEMATICS_NODISCARD constexpr auto
    operator()(range_type&& range, function_type function) const {
        using base_type = std::views::all_t<range_type>;
        return fixed_transform_view<base_type, function_type>{
            std::views::all(std::forward<range_type>(range)),
            std::move(function)};
    }

    template <typename function_type>
    MATHEMATICS_NODISCARD constexpr auto
    operator()(function_type function) const {
        return ranges::detail::bind_back(*this, std::move(function));
    }
};

inline constexpr transform_fixed_fn transform_fixed{};

// Same tuple protocol as fixed_extent_view, so a transformed fixed range can
// also be bound instead of iterated: `auto [a, b] = view | transform_fixed(f);`
template <std::size_t index, std::ranges::view base_type,
          typename function_type>
    requires (index < fixed_transform_view<base_type,
                                           function_type>::static_extent)
MATHEMATICS_NODISCARD constexpr decltype(auto)
get(fixed_transform_view<base_type, function_type>& view) {
    return ranges::detail::fixed_element<index>(view);
}

template <std::size_t index, std::ranges::view base_type,
          typename function_type>
    requires (index < fixed_transform_view<base_type,
                                           function_type>::static_extent)
MATHEMATICS_NODISCARD constexpr decltype(auto)
get(const fixed_transform_view<base_type, function_type>& view) {
    return ranges::detail::fixed_element<index>(view);
}

template <std::size_t index, std::ranges::view base_type,
          typename function_type>
    requires (index < fixed_transform_view<base_type,
                                           function_type>::static_extent)
MATHEMATICS_NODISCARD constexpr decltype(auto)
get(fixed_transform_view<base_type, function_type>&& view) {
    return ranges::detail::fixed_element<index>(view);
}

} // namespace views

} // namespace math

namespace std {

template <ranges::view base_type, typename function_type>
struct tuple_size<math::views::fixed_transform_view<base_type, function_type>>
    : integral_constant<
          size_t,
          math::views::fixed_transform_view<base_type,
                                            function_type>::static_extent> {};

template <size_t index, ranges::view base_type, typename function_type>
    requires (index < math::views::fixed_transform_view<
                          base_type, function_type>::static_extent)
struct tuple_element<
    index, math::views::fixed_transform_view<base_type, function_type>> {
    using type = remove_reference_t<
        decltype(math::ranges::detail::fixed_element<index>(
            declval<math::views::fixed_transform_view<base_type,
                                                      function_type>&>()))>;
};

} // namespace std

#endif // MATHEMATICS_VIEWS_HPP
