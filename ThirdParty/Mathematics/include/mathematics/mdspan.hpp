// mathematics/mdspan.hpp -- optional C++23 mdspan views over matrix storage.
//
// The header remains includable in C++20 mode, where MATHEMATICS_HAS_MDSPAN is
// zero and no mdspan-dependent declarations are exposed.
#ifndef MATHEMATICS_MDSPAN_HPP
#define MATHEMATICS_MDSPAN_HPP

#include <mathematics/config.hpp>

#if MATHEMATICS_HAS_MDSPAN

#include <mathematics/matrix.hpp>

#include <cstddef>
#include <mdspan>
#include <type_traits>

namespace math {

namespace detail {

template <typename Element, std::size_t order>
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr Element&
matrix_element_at(Element (*storage)[order][order], std::size_t offset) noexcept {
    if (std::is_constant_evaluated()) {
        return (*storage)[offset / order][offset % order];
    }

    // float[N][N] is an array of rows, not one float[N*N]. Address the actual
    // float subobject through bytes instead of flattening it with invalid float
    // pointer arithmetic. This also avoids quotient/remainder instructions in
    // dynamic mdspan access after inlining.
    using byte_type =
        std::conditional_t<std::is_const_v<Element>, const std::byte, std::byte>;
    auto* bytes = reinterpret_cast<byte_type*>(storage);
    return *reinterpret_cast<Element*>(
        bytes + offset * sizeof(std::remove_const_t<Element>));
}

template <typename Element, std::size_t order>
struct matrix_offset_handle {
    Element (*storage)[order][order]{};
    std::size_t base_offset{};

    constexpr matrix_offset_handle() noexcept = default;
    constexpr matrix_offset_handle(Element (*storage_in)[order][order],
                                   std::size_t offset_in) noexcept
        : storage(storage_in), base_offset(offset_in) {}

    template <typename other_element_type>
        requires std::is_convertible_v<other_element_type (*)[order][order],
                                       Element (*)[order][order]>
    constexpr matrix_offset_handle(
        matrix_offset_handle<other_element_type, order> other) noexcept
        : storage(other.storage), base_offset(other.base_offset) {}
};

template <typename Element, std::size_t order>
struct matrix_offset_accessor {
    using offset_policy = matrix_offset_accessor;
    using element_type = Element;
    using reference = element_type&;
    using data_handle_type = matrix_offset_handle<element_type, order>;

    constexpr matrix_offset_accessor() noexcept = default;

    template <typename other_element_type>
        requires std::is_convertible_v<other_element_type (*)[order][order],
                                       Element (*)[order][order]>
    constexpr matrix_offset_accessor(
        const matrix_offset_accessor<other_element_type, order>&) noexcept {}

    MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr reference
    access(data_handle_type handle, std::size_t offset) const noexcept {
        return matrix_element_at(handle.storage, handle.base_offset + offset);
    }

    MATHEMATICS_NODISCARD constexpr data_handle_type
    offset(data_handle_type handle, std::size_t offset_value) const noexcept {
        handle.base_offset += offset_value;
        return handle;
    }
};

template <typename Element, std::size_t order>
struct matrix_accessor {
    using offset_policy = matrix_offset_accessor<Element, order>;
    using element_type = Element;
    using reference = element_type&;
    using data_handle_type = element_type (*)[order][order];

    constexpr matrix_accessor() noexcept = default;

    template <typename other_element_type>
        requires std::is_convertible_v<other_element_type (*)[order][order],
                                       Element (*)[order][order]>
    constexpr matrix_accessor(
        const matrix_accessor<other_element_type, order>&) noexcept {}

    MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr reference
    access(data_handle_type handle, std::size_t offset) const noexcept {
        return matrix_element_at(handle, offset);
    }

    MATHEMATICS_NODISCARD constexpr typename offset_policy::data_handle_type
    offset(data_handle_type handle, std::size_t offset_value) const noexcept {
        return {handle, offset_value};
    }
};

} // namespace detail

template <typename element_type, std::size_t order,
          typename layout_policy = std::layout_right>
using basic_matrix_mdspan =
    std::mdspan<element_type, std::extents<std::size_t, order, order>,
                layout_policy, detail::matrix_accessor<element_type, order>>;

using matrix3x3_mdspan = basic_matrix_mdspan<float, 3>;
using const_matrix3x3_mdspan = basic_matrix_mdspan<const float, 3>;
using matrix4x4_mdspan = basic_matrix_mdspan<float, 4>;
using const_matrix4x4_mdspan = basic_matrix_mdspan<const float, 4>;

using matrix3x3_transpose_mdspan =
    basic_matrix_mdspan<float, 3, std::layout_left>;
using const_matrix3x3_transpose_mdspan =
    basic_matrix_mdspan<const float, 3, std::layout_left>;
using matrix4x4_transpose_mdspan =
    basic_matrix_mdspan<float, 4, std::layout_left>;
using const_matrix4x4_transpose_mdspan =
    basic_matrix_mdspan<const float, 4, std::layout_left>;

// Every returned view aliases the input matrix; the matrix must outlive it.
MATHEMATICS_NODISCARD constexpr matrix3x3_mdspan
as_mdspan(matrix3x3& matrix) noexcept {
    return matrix3x3_mdspan{&matrix.m, matrix3x3_mdspan::extents_type{}};
}

MATHEMATICS_NODISCARD constexpr const_matrix3x3_mdspan
as_mdspan(const matrix3x3& matrix) noexcept {
    return const_matrix3x3_mdspan{&matrix.m,
                                 const_matrix3x3_mdspan::extents_type{}};
}

MATHEMATICS_NODISCARD constexpr matrix4x4_mdspan
as_mdspan(matrix4x4& matrix) noexcept {
    return matrix4x4_mdspan{&matrix.m, matrix4x4_mdspan::extents_type{}};
}

MATHEMATICS_NODISCARD constexpr const_matrix4x4_mdspan
as_mdspan(const matrix4x4& matrix) noexcept {
    return const_matrix4x4_mdspan{&matrix.m,
                                 const_matrix4x4_mdspan::extents_type{}};
}

// layout_left changes only the index mapping. For these square row-major
// matrices, view(row, column) therefore aliases matrix.m[column][row].
MATHEMATICS_NODISCARD constexpr matrix3x3_transpose_mdspan
transpose_view(matrix3x3& matrix) noexcept {
    return matrix3x3_transpose_mdspan{
        &matrix.m, matrix3x3_transpose_mdspan::extents_type{}};
}

MATHEMATICS_NODISCARD constexpr const_matrix3x3_transpose_mdspan
transpose_view(const matrix3x3& matrix) noexcept {
    return const_matrix3x3_transpose_mdspan{
        &matrix.m, const_matrix3x3_transpose_mdspan::extents_type{}};
}

MATHEMATICS_NODISCARD constexpr matrix4x4_transpose_mdspan
transpose_view(matrix4x4& matrix) noexcept {
    return matrix4x4_transpose_mdspan{
        &matrix.m, matrix4x4_transpose_mdspan::extents_type{}};
}

MATHEMATICS_NODISCARD constexpr const_matrix4x4_transpose_mdspan
transpose_view(const matrix4x4& matrix) noexcept {
    return const_matrix4x4_transpose_mdspan{
        &matrix.m, const_matrix4x4_transpose_mdspan::extents_type{}};
}

} // namespace math

#endif // MATHEMATICS_HAS_MDSPAN

#endif // MATHEMATICS_MDSPAN_HPP
