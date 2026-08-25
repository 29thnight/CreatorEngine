// mathematics/matrix3x3.hpp — 3x3 matrix, row-major with row-vector convention.
//
// Same conventions as matrix4x4: row-major storage, `v * M`, composition reading
// left to right. A 3x3 carries rotation and scale but no translation, which is
// what makes it the right type for transforming normals and for the linear part
// of a transform.
#ifndef MATHEMATICS_MATRIX3X3_HPP
#define MATHEMATICS_MATRIX3X3_HPP

#include <mathematics/vector.hpp>

#include <optional>

namespace math {

// Thirty-six bytes, packed. A row is three contiguous floats, so unlike
// matrix4x4 it does not fill a register -- the operations here stay scalar for
// the same measured reason vector3's do (docs/PLAN.md Phase 2): at three
// components the register round trip costs more than it saves.
struct matrix3x3 {
    float m[3][3];

    constexpr matrix3x3() noexcept : m{} {}

    constexpr matrix3x3(float m00, float m01, float m02,
                        float m10, float m11, float m12,
                        float m20, float m21, float m22) noexcept
        : m{{m00, m01, m02}, {m10, m11, m12}, {m20, m21, m22}} {}

    // ------------------------------------------------------------ element access
    MATHEMATICS_NODISCARD constexpr float operator()(int row, int col) const noexcept {
        return m[row][col];
    }
    MATHEMATICS_NODISCARD constexpr float& operator()(int row, int col) noexcept {
        return m[row][col];
    }

#if MATHEMATICS_HAS_MULTIDIM_SUBSCRIPT
    MATHEMATICS_NODISCARD constexpr float operator[](int row, int col) const noexcept {
        return m[row][col];
    }
    MATHEMATICS_NODISCARD constexpr float& operator[](int row, int col) noexcept {
        return m[row][col];
    }
#endif

    MATHEMATICS_NODISCARD constexpr vector3 get_row(int i) const noexcept {
        return vector3{m[i][0], m[i][1], m[i][2]};
    }

    MATHEMATICS_NODISCARD constexpr vector3 get_column(int j) const noexcept {
        return vector3{m[0][j], m[1][j], m[2][j]};
    }

    MATHEMATICS_NODISCARD static constexpr matrix3x3 identity() noexcept {
        return matrix3x3{1, 0, 0,
                         0, 1, 0,
                         0, 0, 1};
    }
};

static_assert(sizeof(matrix3x3) == 36, "matrix3x3 must stay packed");
static_assert(std::is_standard_layout_v<matrix3x3>);
static_assert(std::is_trivially_copyable_v<matrix3x3>);

// ------------------------------------------------------------------- multiply
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr matrix3x3
operator*(const matrix3x3& a, const matrix3x3& b) noexcept {
    matrix3x3 r;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j]
                      + a.m[i][2] * b.m[2][j];
        }
    }
    return r;
}

MATHEMATICS_INLINE constexpr matrix3x3& operator*=(matrix3x3& a,
                                             const matrix3x3& b) noexcept {
    return a = a * b;
}

// Row vector times matrix, matching the 4x4 convention.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vector3
operator*(const vector3& v, const matrix3x3& mat) noexcept {
    return vector3{
        v.x * mat.m[0][0] + v.y * mat.m[1][0] + v.z * mat.m[2][0],
        v.x * mat.m[0][1] + v.y * mat.m[1][1] + v.z * mat.m[2][1],
        v.x * mat.m[0][2] + v.y * mat.m[1][2] + v.z * mat.m[2][2]};
}

// ------------------------------------------------------------------ transpose
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr matrix3x3
transpose(const matrix3x3& mat) noexcept {
    return matrix3x3{mat.m[0][0], mat.m[1][0], mat.m[2][0],
                     mat.m[0][1], mat.m[1][1], mat.m[2][1],
                     mat.m[0][2], mat.m[1][2], mat.m[2][2]};
}

// -------------------------------------------------------- determinant, inverse
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr float
determinant(const matrix3x3& mat) noexcept {
    const auto& x = mat.m;
    return x[0][0] * (x[1][1] * x[2][2] - x[1][2] * x[2][1])
         - x[0][1] * (x[1][0] * x[2][2] - x[1][2] * x[2][0])
         + x[0][2] * (x[1][0] * x[2][1] - x[1][1] * x[2][0]);
}

namespace detail {

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr bool
try_inverse_matrix3x3(const matrix3x3& mat, matrix3x3& result) noexcept {
    const auto& x = mat.m;

    // The adjugate is the transpose of the cofactor matrix, so the row and
    // column indices swap on the way out.
    const float c00 = x[1][1] * x[2][2] - x[1][2] * x[2][1];
    const float c01 = x[1][2] * x[2][0] - x[1][0] * x[2][2];
    const float c02 = x[1][0] * x[2][1] - x[1][1] * x[2][0];

    const float det = x[0][0] * c00 + x[0][1] * c01 + x[0][2] * c02;
    if (!detail::is_finite_non_zero(det)) return false;
    const float inv_det = 1.0f / det;

    result = matrix3x3{
        c00 * inv_det,
        (x[0][2] * x[2][1] - x[0][1] * x[2][2]) * inv_det,
        (x[0][1] * x[1][2] - x[0][2] * x[1][1]) * inv_det,

        c01 * inv_det,
        (x[0][0] * x[2][2] - x[0][2] * x[2][0]) * inv_det,
        (x[0][2] * x[1][0] - x[0][0] * x[1][2]) * inv_det,

        c02 * inv_det,
        (x[0][1] * x[2][0] - x[0][0] * x[2][1]) * inv_det,
        (x[0][0] * x[1][1] - x[0][1] * x[1][0]) * inv_det};
    return true;
}

} // namespace detail

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr std::optional<matrix3x3>
try_inverse(const matrix3x3& mat) noexcept {
    matrix3x3 result;
    if (!detail::try_inverse_matrix3x3(mat, result)) return std::nullopt;
    return result;
}

// Returns the identity unless the determinant is a finite non-zero, matching
// matrix4x4's choice -- see the longer note there for why the guard rejects
// infinities and NaN rather than only exact zero.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr matrix3x3
inverse(const matrix3x3& mat) noexcept {
    matrix3x3 result;
    if (!detail::try_inverse_matrix3x3(mat, result)) return matrix3x3::identity();
    return result;
}

// ------------------------------------------------------------------ comparison
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr bool
operator==(const matrix3x3& a, const matrix3x3& b) noexcept {
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            if (a.m[i][j] != b.m[i][j]) return false;
        }
    }
    return true;
}

// Positive test, not a negated one -- see the note on matrix4x4's near_equal for
// why the negated spelling silently accepts NaN.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr bool
near_equal(const matrix3x3& a, const matrix3x3& b,
          float epsilon = 1e-5f) noexcept {
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            const float diff = a.m[i][j] - b.m[i][j];
            if (!(diff <= epsilon && diff >= -epsilon)) return false;
        }
    }
    return true;
}

} // namespace math

#endif // MATHEMATICS_MATRIX3X3_HPP
