// mathematics/matrix4x4.hpp — 4x4 matrix, row-major with row-vector convention.
//
// The convention is DirectXMath's, decided in docs/PLAN.md §7 and load-bearing
// for everything here:
//
//   * Storage is ROW-MAJOR: m[row][col], and a row is four contiguous floats,
//     so it loads into a register in one go.
//   * Vectors are ROW vectors, transformed as `v * M`, never `M * v`.
//   * Composition therefore reads left to right in application order:
//     `world = scale * rotation * translation` scales first and translates last.
//   * Translation lives in row 3 -- m[3][0..2] -- not in column 3.
//
// Getting any of these backwards produces a matrix that still multiplies without
// complaint and renders everything in the wrong place, so the tests pin all four
// against DirectXMath rather than against themselves.
#ifndef MATHEMATICS_MATRIX4X4_HPP
#define MATHEMATICS_MATRIX4X4_HPP

#include <mathematics/vector.hpp>

#include <optional>

namespace math {

// Sixty-four bytes, standard layout, four-byte aligned -- it uploads to a
// constant buffer as-is.
struct matrix4x4 {
private:
    struct uninitialized_tag {};

    // Runtime-only construction for factories that overwrite all 64 bytes.
// The public default must remain zero-initializing; using it in transpose
    // prevented MSVC from eliminating the returned stack temporary and copy.
    explicit matrix4x4(uninitialized_tag) noexcept {}

    friend constexpr matrix4x4 transpose(const matrix4x4& mat) noexcept;
    friend constexpr matrix4x4 inverse(const matrix4x4& mat) noexcept;

public:
    float m[4][4];

    constexpr matrix4x4() noexcept : m{} {}

    constexpr matrix4x4(float m00, float m01, float m02, float m03,
                        float m10, float m11, float m12, float m13,
                        float m20, float m21, float m22, float m23,
                        float m30, float m31, float m32, float m33) noexcept
        : m{{m00, m01, m02, m03},
            {m10, m11, m12, m13},
            {m20, m21, m22, m23},
            {m30, m31, m32, m33}} {}

    // ------------------------------------------------------------ element access
    MATHEMATICS_NODISCARD constexpr float operator()(int row, int col) const noexcept {
        return m[row][col];
    }
    MATHEMATICS_NODISCARD constexpr float& operator()(int row, int col) noexcept {
        return m[row][col];
    }

#if MATHEMATICS_HAS_MULTIDIM_SUBSCRIPT
    // C++23 spelling: M[row, col]. The call form above stays available so the
    // same source compiles either way.
    MATHEMATICS_NODISCARD constexpr float operator[](int row, int col) const noexcept {
        return m[row][col];
    }
    MATHEMATICS_NODISCARD constexpr float& operator[](int row, int col) noexcept {
        return m[row][col];
    }
#endif

    // ---------------------------------------------------------------- row access
    // A row is four contiguous floats, which is the entire reason for storing
    // row-major: this is one load, and the multiply below is built out of it.
    MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vec_reg row(int i) const noexcept {
        MATHEMATICS_IF_CONSTEVAL { return set(m[i][0], m[i][1], m[i][2], m[i][3]); }
        return load(&m[i][0]);
    }

    MATHEMATICS_INLINE constexpr void set_row(int i, vec_reg r) noexcept {
        MATHEMATICS_IF_CONSTEVAL {
            m[i][0] = lane(r, 0); m[i][1] = lane(r, 1);
            m[i][2] = lane(r, 2); m[i][3] = lane(r, 3);
            return;
        }
        store(&m[i][0], r);
    }

private:
    MATHEMATICS_NODISCARD MATHEMATICS_INLINE static matrix4x4
    from_rows_runtime(vec_reg r0, vec_reg r1, vec_reg r2, vec_reg r3) noexcept {
        matrix4x4 result{uninitialized_tag{}};
        store(&result.m[0][0], r0);
        store(&result.m[1][0], r1);
        store(&result.m[2][0], r2);
        store(&result.m[3][0], r3);
        return result;
    }

public:

    MATHEMATICS_NODISCARD constexpr vector4 get_row(int i) const noexcept {
        return vector4{m[i][0], m[i][1], m[i][2], m[i][3]};
    }

    MATHEMATICS_NODISCARD constexpr vector4 get_column(int j) const noexcept {
        return vector4{m[0][j], m[1][j], m[2][j], m[3][j]};
    }

    // Rows 0-2 carry the basis under this convention, row 3 the translation.
    MATHEMATICS_NODISCARD constexpr vector3 right() const noexcept {
        return vector3{m[0][0], m[0][1], m[0][2]};
    }
    MATHEMATICS_NODISCARD constexpr vector3 up() const noexcept {
        return vector3{m[1][0], m[1][1], m[1][2]};
    }
    MATHEMATICS_NODISCARD constexpr vector3 forward() const noexcept {
        return vector3{m[2][0], m[2][1], m[2][2]};
    }
    MATHEMATICS_NODISCARD constexpr vector3 translation() const noexcept {
        return vector3{m[3][0], m[3][1], m[3][2]};
    }

    // --------------------------------------------------------------- constants
    MATHEMATICS_NODISCARD static constexpr matrix4x4 identity() noexcept {
        return matrix4x4{1, 0, 0, 0,
                         0, 1, 0, 0,
                         0, 0, 1, 0,
                         0, 0, 0, 1};
    }
};

static_assert(sizeof(matrix4x4) == 64, "matrix4x4 must stay packed");
static_assert(std::is_standard_layout_v<matrix4x4>);
static_assert(std::is_trivially_copyable_v<matrix4x4>);

// ------------------------------------------------------------------- multiply
// (a * b)[i] = sum over k of a[i][k] * b[k], which with row-major storage is one
// broadcast and one fused multiply-add per element of a's row against a whole
// row of b. Sixteen FMAs and no transpose -- the same shape as
// XMMatrixMultiply, and the reason the row-vector convention is worth keeping.
namespace detail {

// One output row: each of a's four elements scales a whole row of b.
//
// The scalars are broadcast straight from memory rather than loaded as a row and
// shuffled apart. The shuffle form issues sixteen shuffles per multiply and,
// with a single shuffle port, that becomes the bottleneck while the arithmetic
// units idle -- it measured 141 M/s against 251 for this version.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE vec_reg
combine_rows(const float* a_row, vec_reg b0, vec_reg b1, vec_reg b2,
            vec_reg b3) noexcept {
    vec_reg acc = mul(load_splat(a_row + 0), b0);
    acc = mul_add(load_splat(a_row + 1), b1, acc);
    acc = mul_add(load_splat(a_row + 2), b2, acc);
    acc = mul_add(load_splat(a_row + 3), b3, acc);
    return acc;
}

#if MATHEMATICS_SIMD_SSE && MATHEMATICS_HAS_AVX && MATHEMATICS_HAS_FMA
// Two rows at a time in 256-bit registers, halving the operation count: eight
// wide multiply-adds instead of sixteen narrow ones.
//
// This is the only place in the library that reaches past 128 bits, and it is
// here because the 128-bit version could not close the gap to DirectXMath --
// XMMatrixMultiply takes exactly this path, and no amount of tuning within 128
// bits matches an algorithm doing half the work. The narrow version above stays
// as the fallback for targets without AVX.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE matrix4x4
multiply_avx(const matrix4x4& a, const matrix4x4& b) noexcept {
    // Rows 0-1 and 2-3 of each operand, packed into one register per pair.
    __m256 t0 = _mm256_loadu_ps(&a.m[0][0]);
    __m256 t1 = _mm256_loadu_ps(&a.m[2][0]);
    const __m256 u0 = _mm256_loadu_ps(&b.m[0][0]);
    const __m256 u1 = _mm256_loadu_ps(&b.m[2][0]);

    // permute2f128 with 0x00 broadcasts b's row 0 to both halves, 0x11 its row 1
    // -- so one wide operand serves both output rows at once.
    __m256 s0 = _mm256_shuffle_ps(t0, t0, _MM_SHUFFLE(0, 0, 0, 0));
    __m256 s1 = _mm256_shuffle_ps(t1, t1, _MM_SHUFFLE(0, 0, 0, 0));
    __m256 r0 = _mm256_permute2f128_ps(u0, u0, 0x00);
    __m256 c0 = _mm256_mul_ps(s0, r0);
    __m256 c1 = _mm256_mul_ps(s1, r0);

    s0 = _mm256_shuffle_ps(t0, t0, _MM_SHUFFLE(1, 1, 1, 1));
    s1 = _mm256_shuffle_ps(t1, t1, _MM_SHUFFLE(1, 1, 1, 1));
    r0 = _mm256_permute2f128_ps(u0, u0, 0x11);
    const __m256 c2 = _mm256_fmadd_ps(s0, r0, c0);
    const __m256 c3 = _mm256_fmadd_ps(s1, r0, c1);

    s0 = _mm256_shuffle_ps(t0, t0, _MM_SHUFFLE(2, 2, 2, 2));
    s1 = _mm256_shuffle_ps(t1, t1, _MM_SHUFFLE(2, 2, 2, 2));
    __m256 r1 = _mm256_permute2f128_ps(u1, u1, 0x00);
    const __m256 c4 = _mm256_mul_ps(s0, r1);
    const __m256 c5 = _mm256_mul_ps(s1, r1);

    s0 = _mm256_shuffle_ps(t0, t0, _MM_SHUFFLE(3, 3, 3, 3));
    s1 = _mm256_shuffle_ps(t1, t1, _MM_SHUFFLE(3, 3, 3, 3));
    r1 = _mm256_permute2f128_ps(u1, u1, 0x11);
    const __m256 c6 = _mm256_fmadd_ps(s0, r1, c4);
    const __m256 c7 = _mm256_fmadd_ps(s1, r1, c5);

    t0 = _mm256_add_ps(c2, c6);
    t1 = _mm256_add_ps(c3, c7);

    matrix4x4 result;
    _mm256_storeu_ps(&result.m[0][0], t0);
    _mm256_storeu_ps(&result.m[2][0], t1);
    return result;
}
#endif

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr matrix4x4
multiply_scalar(const matrix4x4& a, const matrix4x4& b) noexcept {
    matrix4x4 r;
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j]
                      + a.m[i][2] * b.m[2][j] + a.m[i][3] * b.m[3][j];
        }
    }
    return r;
}

} // namespace detail

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr matrix4x4
operator*(const matrix4x4& a, const matrix4x4& b) noexcept {
    MATHEMATICS_IF_CONSTEVAL { return detail::multiply_scalar(a, b); }

#if MATHEMATICS_SIMD_SSE && MATHEMATICS_HAS_AVX && MATHEMATICS_HAS_FMA
    return detail::multiply_avx(a, b);
#else
    const vec_reg b0 = b.row(0);
    const vec_reg b1 = b.row(1);
    const vec_reg b2 = b.row(2);
    const vec_reg b3 = b.row(3);

    // Unrolled rather than looping over the row index: with a runtime index the
    // compiler cannot keep the result in registers and every set_row becomes a
    // stack round trip.
    matrix4x4 result;
    result.set_row(0, detail::combine_rows(&a.m[0][0], b0, b1, b2, b3));
    result.set_row(1, detail::combine_rows(&a.m[1][0], b0, b1, b2, b3));
    result.set_row(2, detail::combine_rows(&a.m[2][0], b0, b1, b2, b3));
    result.set_row(3, detail::combine_rows(&a.m[3][0], b0, b1, b2, b3));
    return result;
#endif
}

MATHEMATICS_INLINE constexpr matrix4x4& operator*=(matrix4x4& a,
                                             const matrix4x4& b) noexcept {
    return a = a * b;
}

// ------------------------------------------------------------------ transform
// Row vector times matrix. The vector's components broadcast across the matrix's
// rows, which is the same shape as the matrix multiply above.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vector4
operator*(const vector4& v, const matrix4x4& mat) noexcept {
    const vec_reg r = v.reg();
    vec_reg acc = mul(splat_x(r), mat.row(0));
    acc = mul_add(splat_y(r), mat.row(1), acc);
    acc = mul_add(splat_z(r), mat.row(2), acc);
    acc = mul_add(splat_w(r), mat.row(3), acc);
    return vector4::from_reg(acc);
}

// A position: w is taken as 1, so the translation row applies.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vector3
transform_point(const vector3& p, const matrix4x4& mat) noexcept {
    const vec_reg r = p.reg();
    vec_reg acc = mul(splat_x(r), mat.row(0));
    acc = mul_add(splat_y(r), mat.row(1), acc);
    acc = mul_add(splat_z(r), mat.row(2), acc);
    acc = add(acc, mat.row(3));   // w == 1
    return vector3::from_reg(acc);
}

// A direction: w is taken as 0, so translation is ignored. Using transform_point
// for a normal or a velocity is a classic bug; the two names exist to make the
// choice explicit at every call site.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr vector3
transform_direction(const vector3& d, const matrix4x4& mat) noexcept {
    const vec_reg r = d.reg();
    vec_reg acc = mul(splat_x(r), mat.row(0));
    acc = mul_add(splat_y(r), mat.row(1), acc);
    acc = mul_add(splat_z(r), mat.row(2), acc);
    return vector3::from_reg(acc);
}

// ------------------------------------------------------------------ transpose
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr matrix4x4
transpose(const matrix4x4& mat) noexcept {
    const vec_reg r0 = mat.row(0);
    const vec_reg r1 = mat.row(1);
    const vec_reg r2 = mat.row(2);
    const vec_reg r3 = mat.row(3);

    // Interleave in two passes: pair up the low and high halves of adjacent
    // rows, then pick alternating lanes out of the pairs. Eight shuffles total.
    const vec_reg lo01 = shuffle<0, 1, 0, 1>(r0, r1);   // x0 y0 x1 y1
    const vec_reg hi01 = shuffle<2, 3, 2, 3>(r0, r1);   // z0 w0 z1 w1
    const vec_reg lo23 = shuffle<0, 1, 0, 1>(r2, r3);   // x2 y2 x3 y3
    const vec_reg hi23 = shuffle<2, 3, 2, 3>(r2, r3);   // z2 w2 z3 w3

    const vec_reg c0 = shuffle<0, 2, 0, 2>(lo01, lo23);
    const vec_reg c1 = shuffle<1, 3, 1, 3>(lo01, lo23);
    const vec_reg c2 = shuffle<0, 2, 0, 2>(hi01, hi23);
    const vec_reg c3 = shuffle<1, 3, 1, 3>(hi01, hi23);

    MATHEMATICS_IF_CONSTEVAL {
        return matrix4x4{lane(c0, 0), lane(c0, 1), lane(c0, 2), lane(c0, 3),
                         lane(c1, 0), lane(c1, 1), lane(c1, 2), lane(c1, 3),
                         lane(c2, 0), lane(c2, 1), lane(c2, 2), lane(c2, 3),
                         lane(c3, 0), lane(c3, 1), lane(c3, 2), lane(c3, 3)};
    }
    return matrix4x4::from_rows_runtime(c0, c1, c2, c3);
}

// -------------------------------------------------------- determinant, inverse
namespace detail {

// The six 2x2 minors of the top two rows and the six of the bottom two. Laplace
// expansion by complementary minors: every cofactor of the 4x4 is a product of
// one from each group, so these twelve values carry the whole computation --
// determinant and all sixteen adjugate entries alike.
struct minors4x4 {
    float s0, s1, s2, s3, s4, s5;   // rows 0,1 over each column pair
    float c0, c1, c2, c3, c4, c5;   // rows 2,3 over each column pair
};

// The same Laplace expansion as the scalar path below, rearranged so each step
// works on four values at once.
//
// Every 2x2 minor is a difference of two products of shuffled rows, and every
// adjugate row turns out to be the same three-term combination of three
// permuted columns against three minor pairs -- so the four rows differ only in
// which inputs they pick and in an alternating sign. Deriving that regularity is
// what makes this expressible with _mm_shuffle_ps alone; DirectXMath's own
// version needs arbitrary two-source permutes, which this backend does not have.
//
//   s_a = (s5,s4,s3,s2)   s_b = (s1,s0,-,-)   minors of rows 0 and 1
//   c_a = (c5,c4,c3,c2)   c_b = (c1,c0,-,-)   minors of rows 2 and 3
//   term_count = (c_n,c_n,s_n,s_n)
//   p_j = column J of the matrix, permuted (1,0,3,2)
//
//   adj0 =  S * (p1*k5 - p2*k4 + p3*k3)     S = (+,-,+,-)
//   adj1 = -S * (p0*k5 - p2*k2 + p3*k1)
//   adj2 =  S * (p0*k4 - p1*k2 + p3*k0)
//   adj3 = -S * (p0*k3 - p1*k1 + p2*k0)
//
// The determinant falls out of the adjugate rather than being computed again:
// (adj * M)[0][0] is the determinant by definition, and column 0 of M is row 0
// of the transpose, which this already has.
#if MATHEMATICS_SIMD_SSE || MATHEMATICS_SIMD_NEON
// The kernel below appears twice -- here feeding inverse() through vec_reg
// references, and in try_inverse_simd fused with its matrix out-parameter.
// The duplication is deliberate; keep the two bodies in sync. Every factoring
// that shared one kernel between the two consumers lost measured throughput:
// an aggregate return of the four rows cost MSVC 42% of try_inverse, vec_reg
// reference out-parameters still cost it 20%, and making inverse() a wrapper
// over try_inverse_simd (the original sharing attempt) cost clang 23% of
// inverse(). Each caller needs the arithmetic fused with its own sink.
//
// The rows are written only on the true return.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE bool
inverse_rows_simd(const matrix4x4& mat, vec_reg& row0, vec_reg& row1,
                  vec_reg& row2, vec_reg& row3) noexcept {
    const vec_reg r0 = mat.row(0);
    const vec_reg r1 = mat.row(1);
    const vec_reg r2 = mat.row(2);
    const vec_reg r3 = mat.row(3);

    const vec_reg s_a = sub(mul(shuffle<2, 1, 1, 0>(r0), shuffle<3, 3, 2, 3>(r1)),
                          mul(shuffle<3, 3, 2, 3>(r0), shuffle<2, 1, 1, 0>(r1)));
    const vec_reg s_b = sub(mul(shuffle<0, 0, 0, 0>(r0), shuffle<2, 1, 2, 1>(r1)),
                          mul(shuffle<2, 1, 2, 1>(r0), shuffle<0, 0, 0, 0>(r1)));
    const vec_reg c_a = sub(mul(shuffle<2, 1, 1, 0>(r2), shuffle<3, 3, 2, 3>(r3)),
                          mul(shuffle<3, 3, 2, 3>(r2), shuffle<2, 1, 1, 0>(r3)));
    const vec_reg c_b = sub(mul(shuffle<0, 0, 0, 0>(r2), shuffle<2, 1, 2, 1>(r3)),
                          mul(shuffle<2, 1, 2, 1>(r2), shuffle<0, 0, 0, 0>(r3)));

    // Two lanes of c and two of s: the two-source shuffle takes its low half
    // from the first operand and its high half from the second, which is exactly
    // the (c,c,s,s) shape these need.
    const vec_reg k5 = shuffle<0, 0, 0, 0>(c_a, s_a);
    const vec_reg k4 = shuffle<1, 1, 1, 1>(c_a, s_a);
    const vec_reg k3 = shuffle<2, 2, 2, 2>(c_a, s_a);
    const vec_reg k2 = shuffle<3, 3, 3, 3>(c_a, s_a);
    const vec_reg k1 = shuffle<0, 0, 0, 0>(c_b, s_b);
    const vec_reg k0 = shuffle<1, 1, 1, 1>(c_b, s_b);

    // Transposed in registers with the row order already permuted to (1,0,3,2),
// so the p_j vectors come out directly. Calling transpose() instead would
    // build a matrix4x4, store all sixteen floats, and load them straight back,
    // and would still need four more shuffles to apply the permute.
    const vec_reg lo01 = shuffle<0, 1, 0, 1>(r1, r0);
    const vec_reg hi01 = shuffle<2, 3, 2, 3>(r1, r0);
    const vec_reg lo23 = shuffle<0, 1, 0, 1>(r3, r2);
    const vec_reg hi23 = shuffle<2, 3, 2, 3>(r3, r2);

    const vec_reg p0 = shuffle<0, 2, 0, 2>(lo01, lo23);
    const vec_reg p1 = shuffle<1, 3, 1, 3>(lo01, lo23);
    const vec_reg p2 = shuffle<0, 2, 0, 2>(hi01, hi23);
    const vec_reg p3 = shuffle<1, 3, 1, 3>(hi01, hi23);
    // Column 0 of the matrix, for the determinant below.
    const vec_reg t0 = shuffle<2, 0, 2, 0>(lo01, lo23);

    // Sign alternation as an XOR of the sign bit rather than a multiply by
    // +/-1: one bitwise op instead of a multiply, and exact for zeros.
    const vec_reg flip_odd = make_mask_reg(0, sign_bit, 0, sign_bit);
    const vec_reg flip_even = make_mask_reg(sign_bit, 0, sign_bit, 0);

    const vec_reg adj0 =
        bit_xor(mul_add(p3, k3, neg_mul_add(p2, k4, mul(p1, k5))), flip_odd);
    const vec_reg adj1 =
        bit_xor(mul_add(p3, k1, neg_mul_add(p2, k2, mul(p0, k5))), flip_even);
    const vec_reg adj2 =
        bit_xor(mul_add(p3, k0, neg_mul_add(p1, k2, mul(p0, k4))), flip_odd);
    const vec_reg adj3 =
        bit_xor(mul_add(p2, k0, neg_mul_add(p1, k1, mul(p0, k3))), flip_even);

// dot4 already broadcasts the determinant across every lane, so the
    // reciprocal stays in vector registers. Extracting it to divide in scalar
    // and broadcasting the result back costs a round trip through the scalar
    // unit for no benefit.
    const vec_reg det = dot4(adj0, t0);
    if (!detail::is_finite_non_zero(get_x(det))) return false;

    const vec_reg inv_det = div(splat(1.0f), det);
    row0 = mul(adj0, inv_det);
    row1 = mul(adj1, inv_det);
    row2 = mul(adj2, inv_det);
    row3 = mul(adj3, inv_det);
    return true;
}

// Same kernel as inverse_rows_simd, fused with the matrix out-parameter; see
// the duplication note above before touching either body.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE bool
try_inverse_simd(const matrix4x4& mat, matrix4x4& result) noexcept {
    const vec_reg r0 = mat.row(0);
    const vec_reg r1 = mat.row(1);
    const vec_reg r2 = mat.row(2);
    const vec_reg r3 = mat.row(3);

    const vec_reg s_a = sub(mul(shuffle<2, 1, 1, 0>(r0), shuffle<3, 3, 2, 3>(r1)),
                          mul(shuffle<3, 3, 2, 3>(r0), shuffle<2, 1, 1, 0>(r1)));
    const vec_reg s_b = sub(mul(shuffle<0, 0, 0, 0>(r0), shuffle<2, 1, 2, 1>(r1)),
                          mul(shuffle<2, 1, 2, 1>(r0), shuffle<0, 0, 0, 0>(r1)));
    const vec_reg c_a = sub(mul(shuffle<2, 1, 1, 0>(r2), shuffle<3, 3, 2, 3>(r3)),
                          mul(shuffle<3, 3, 2, 3>(r2), shuffle<2, 1, 1, 0>(r3)));
    const vec_reg c_b = sub(mul(shuffle<0, 0, 0, 0>(r2), shuffle<2, 1, 2, 1>(r3)),
                          mul(shuffle<2, 1, 2, 1>(r2), shuffle<0, 0, 0, 0>(r3)));

    const vec_reg k5 = shuffle<0, 0, 0, 0>(c_a, s_a);
    const vec_reg k4 = shuffle<1, 1, 1, 1>(c_a, s_a);
    const vec_reg k3 = shuffle<2, 2, 2, 2>(c_a, s_a);
    const vec_reg k2 = shuffle<3, 3, 3, 3>(c_a, s_a);
    const vec_reg k1 = shuffle<0, 0, 0, 0>(c_b, s_b);
    const vec_reg k0 = shuffle<1, 1, 1, 1>(c_b, s_b);

    const vec_reg lo01 = shuffle<0, 1, 0, 1>(r1, r0);
    const vec_reg hi01 = shuffle<2, 3, 2, 3>(r1, r0);
    const vec_reg lo23 = shuffle<0, 1, 0, 1>(r3, r2);
    const vec_reg hi23 = shuffle<2, 3, 2, 3>(r3, r2);

    const vec_reg p0 = shuffle<0, 2, 0, 2>(lo01, lo23);
    const vec_reg p1 = shuffle<1, 3, 1, 3>(lo01, lo23);
    const vec_reg p2 = shuffle<0, 2, 0, 2>(hi01, hi23);
    const vec_reg p3 = shuffle<1, 3, 1, 3>(hi01, hi23);
    const vec_reg t0 = shuffle<2, 0, 2, 0>(lo01, lo23);

    const vec_reg flip_odd = make_mask_reg(0, sign_bit, 0, sign_bit);
    const vec_reg flip_even = make_mask_reg(sign_bit, 0, sign_bit, 0);

    const vec_reg adj0 =
        bit_xor(mul_add(p3, k3, neg_mul_add(p2, k4, mul(p1, k5))), flip_odd);
    const vec_reg adj1 =
        bit_xor(mul_add(p3, k1, neg_mul_add(p2, k2, mul(p0, k5))), flip_even);
    const vec_reg adj2 =
        bit_xor(mul_add(p3, k0, neg_mul_add(p1, k2, mul(p0, k4))), flip_odd);
    const vec_reg adj3 =
        bit_xor(mul_add(p2, k0, neg_mul_add(p1, k1, mul(p0, k3))), flip_even);

    const vec_reg det = dot4(adj0, t0);
    if (!detail::is_finite_non_zero(get_x(det))) return false;

    const vec_reg inv_det = div(splat(1.0f), det);
    result.set_row(0, mul(adj0, inv_det));
    result.set_row(1, mul(adj1, inv_det));
    result.set_row(2, mul(adj2, inv_det));
    result.set_row(3, mul(adj3, inv_det));
    return true;
}
#endif

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr minors4x4
compute_minors(const matrix4x4& x) noexcept {
    return minors4x4{
        x.m[0][0] * x.m[1][1] - x.m[0][1] * x.m[1][0],
        x.m[0][0] * x.m[1][2] - x.m[0][2] * x.m[1][0],
        x.m[0][0] * x.m[1][3] - x.m[0][3] * x.m[1][0],
        x.m[0][1] * x.m[1][2] - x.m[0][2] * x.m[1][1],
        x.m[0][1] * x.m[1][3] - x.m[0][3] * x.m[1][1],
        x.m[0][2] * x.m[1][3] - x.m[0][3] * x.m[1][2],

        x.m[2][0] * x.m[3][1] - x.m[2][1] * x.m[3][0],
        x.m[2][0] * x.m[3][2] - x.m[2][2] * x.m[3][0],
        x.m[2][0] * x.m[3][3] - x.m[2][3] * x.m[3][0],
        x.m[2][1] * x.m[3][2] - x.m[2][2] * x.m[3][1],
        x.m[2][1] * x.m[3][3] - x.m[2][3] * x.m[3][1],
        x.m[2][2] * x.m[3][3] - x.m[2][3] * x.m[3][2]};
}

} // namespace detail

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr float
determinant(const matrix4x4& mat) noexcept {
    const detail::minors4x4 k = detail::compute_minors(mat);
    return k.s0 * k.c5 - k.s1 * k.c4 + k.s2 * k.c3
         + k.s3 * k.c2 - k.s4 * k.c1 + k.s5 * k.c0;
}

// Returns the identity whenever the determinant is not a finite non-zero --
// singular, but also overflowed to infinity or poisoned to NaN by a non-finite
// entry. DirectXMath writes QNaN instead and sets its optional determinant
// output to zero; a caller that ignores the return value gets NaN spreading
// through the scene either way, so this returns something usable and reports
// singularity through the Determinant call the caller should already be making.
//
// The guard covers the non-finite cases and not just `det == 0` for two
// reasons. It is what the paragraph above actually promises: with a plain
// zero test, a matrix holding an infinity reached the division and this
// returned sixteen NaNs, which is the outcome the promise exists to prevent.
// And it is the only way the two implementations can agree everywhere -- the
// scalar and vector determinants place their `0 * inf` products differently, so
// one produced NaN where the other produced infinity, which made inverse
// answer differently at compile time than at run time, and differently on a
// scalar build than on an SSE one.
namespace detail {

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr bool
try_inverse_scalar(const matrix4x4& mat, matrix4x4& r) noexcept {
    const detail::minors4x4 k = detail::compute_minors(mat);

    const float det = k.s0 * k.c5 - k.s1 * k.c4 + k.s2 * k.c3
                    + k.s3 * k.c2 - k.s4 * k.c1 + k.s5 * k.c0;
    if (!detail::is_finite_non_zero(det)) return false;

    const float inv_det = 1.0f / det;
    const auto& x = mat.m;

    r.m[0][0] = ( x[1][1] * k.c5 - x[1][2] * k.c4 + x[1][3] * k.c3) * inv_det;
    r.m[0][1] = (-x[0][1] * k.c5 + x[0][2] * k.c4 - x[0][3] * k.c3) * inv_det;
    r.m[0][2] = ( x[3][1] * k.s5 - x[3][2] * k.s4 + x[3][3] * k.s3) * inv_det;
    r.m[0][3] = (-x[2][1] * k.s5 + x[2][2] * k.s4 - x[2][3] * k.s3) * inv_det;

    r.m[1][0] = (-x[1][0] * k.c5 + x[1][2] * k.c2 - x[1][3] * k.c1) * inv_det;
    r.m[1][1] = ( x[0][0] * k.c5 - x[0][2] * k.c2 + x[0][3] * k.c1) * inv_det;
    r.m[1][2] = (-x[3][0] * k.s5 + x[3][2] * k.s2 - x[3][3] * k.s1) * inv_det;
    r.m[1][3] = ( x[2][0] * k.s5 - x[2][2] * k.s2 + x[2][3] * k.s1) * inv_det;

    r.m[2][0] = ( x[1][0] * k.c4 - x[1][1] * k.c2 + x[1][3] * k.c0) * inv_det;
    r.m[2][1] = (-x[0][0] * k.c4 + x[0][1] * k.c2 - x[0][3] * k.c0) * inv_det;
    r.m[2][2] = ( x[3][0] * k.s4 - x[3][1] * k.s2 + x[3][3] * k.s0) * inv_det;
    r.m[2][3] = (-x[2][0] * k.s4 + x[2][1] * k.s2 - x[2][3] * k.s0) * inv_det;

    r.m[3][0] = (-x[1][0] * k.c3 + x[1][1] * k.c1 - x[1][2] * k.c0) * inv_det;
    r.m[3][1] = ( x[0][0] * k.c3 - x[0][1] * k.c1 + x[0][2] * k.c0) * inv_det;
    r.m[3][2] = (-x[3][0] * k.s3 + x[3][1] * k.s1 - x[3][2] * k.s0) * inv_det;
    r.m[3][3] = ( x[2][0] * k.s3 - x[2][1] * k.s1 + x[2][2] * k.s0) * inv_det;
    return true;
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr matrix4x4
inverse_scalar(const matrix4x4& mat) noexcept {
    matrix4x4 r;
    if (!try_inverse_scalar(mat, r)) return matrix4x4::identity();
    return r;
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr bool
try_inverse_matrix4x4(const matrix4x4& mat, matrix4x4& result) noexcept {
#if MATHEMATICS_SIMD_SSE || MATHEMATICS_SIMD_NEON
    MATHEMATICS_IF_CONSTEVAL { return try_inverse_scalar(mat, result); }
    return try_inverse_simd(mat, result);
#else
    return try_inverse_scalar(mat, result);
#endif
}

} // namespace detail

// The scalar expansion is the definition; the SIMD version was derived from it
// and the tests check the two against each other as well as against
// DirectXMath. Constant evaluation always takes the scalar path.
//
// LIMIT, worth knowing before relying on it. The two paths agree closely for
// matrices comfortably far from singular -- measured over 196k random
// well-conditioned inputs, the worst disagreement was 2.8e-5 relative, and both
// tracked a double-precision Gauss-Jordan reference to within 5e-5. They do NOT
// agree near singularity, and cannot: the vector path derives the determinant
// from its adjugate with fused multiply-adds, the scalar path from a six-term
// minor expansion without them, and no reordering makes two float expansions
// land on the same side of an exact `== 0` test. Over two million matrices built
// to be singular up to rounding, the two disagreed about whether the input was
// singular 23% of the time, and where both called it invertible their answers
// differed by up to 300x -- which is what dividing by a determinant near the
// floor of float precision does to any implementation, ours and DirectXMath's
// alike. Sharing one expansion between the paths was tried and only moved 23%
// to 20%, so the divergence is reported here rather than papered over.
//
// The practical consequences: the singular cases that actually occur -- a zero
// row, a duplicated row, a zero scale -- drive the determinant to exactly zero
// in both paths, and those agree. For anything nearer the edge, treat inverse's
// output as unspecified and ask Determinant, which is one function with one
// answer, rather than inferring singularity from inverse returning the identity.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr std::optional<matrix4x4>
try_inverse(const matrix4x4& mat) noexcept {
    matrix4x4 result;
    if (!detail::try_inverse_matrix4x4(mat, result)) return std::nullopt;
    return result;
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr matrix4x4
inverse(const matrix4x4& mat) noexcept {
#if MATHEMATICS_SIMD_SSE || MATHEMATICS_SIMD_NEON
    MATHEMATICS_IF_CONSTEVAL { return detail::inverse_scalar(mat); }
    // The success path must build the return value directly from registers.
    // Routing it through a zero-initialized local, a matrix out-parameter, and
    // a two-object return cost clang 23% of inverse() throughput while the
    // same arithmetic in try_inverse() kept full speed.
    vec_reg row0, row1, row2, row3;
    if (!detail::inverse_rows_simd(mat, row0, row1, row2, row3)) {
        return matrix4x4::identity();
    }
    return matrix4x4::from_rows_runtime(row0, row1, row2, row3);
#else
    return detail::inverse_scalar(mat);
#endif
}

// ------------------------------------------------------------------ comparison
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr bool
operator==(const matrix4x4& a, const matrix4x4& b) noexcept {
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            if (a.m[i][j] != b.m[i][j]) return false;
        }
    }
    return true;
}

// Written as a positive test rather than a negated one, and not the other way
// round: every comparison against NaN is false, so `diff > eps || diff < -eps`
// never fires on a NaN and reports it as near. A matrix of NaNs then compares
// near-equal to the identity, and any test asserting `m * inverse(m) ~= i`
// passes while holding garbage. The vector near_equal never had this problem --
// it is built on cmp_le, whose ordered comparison already rejects NaN -- so this
// spelling is what keeps the two consistent.
//
// Consistent with that: a NaN is near nothing, not even another NaN, and two
// infinities do not compare near either, since their difference is NaN. Use
// operator== when exact bit-for-bit agreement on non-finite entries is wanted.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr bool
near_equal(const matrix4x4& a, const matrix4x4& b,
          float epsilon = 1e-5f) noexcept {
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            const float diff = a.m[i][j] - b.m[i][j];
            if (!(diff <= epsilon && diff >= -epsilon)) return false;
        }
    }
    return true;
}

} // namespace math

#endif // MATHEMATICS_MATRIX4X4_HPP
