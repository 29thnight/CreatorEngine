// mathematics/matrix.hpp — the matrix types.
//
// matrix4x4 and matrix3x3 follow DirectXMath's convention throughout:
//
//   Storage        row-major, m[row][col]; a row is contiguous
//   Vectors        row vectors, transformed as `v * M`
//   Composition    left to right in application order --
//                  `world = scale * rotation * translation`
//   Translation    row 3 of a matrix4x4, m[3][0..2]
//
// All four are pinned against DirectXMath by the tests rather than against
// Mathematics's own output, because a matrix with any of them backwards still
// multiplies cleanly and simply renders everything in the wrong place.
//
//   Construction   identity, element-wise
//   Access         M(row, col), and M[row, col] on C++23
//                  get_row get_column Right Up Forward Translation
//   Arithmetic     * (matrix, and `v * M`) and *=
//   Operations     transpose determinant inverse
//   Transform      transform_point (w = 1) transform_direction (w = 0)
//   Comparison     == != (exact), near_equal(a, b, epsilon)
//
// A matrix whose determinant is not a finite non-zero -- singular, overflowed,
// or holding a NaN -- inverts to the identity rather than to NaN. Ask
// Determinant first if that distinction matters, and see the note above
// matrix4x4's inverse for why it is the right question to ask: close to
// singular, whether inverse itself reports singularity is not guaranteed to be
// the same at compile time as at run time.
#ifndef MATHEMATICS_MATRIX_HPP
#define MATHEMATICS_MATRIX_HPP

#include <mathematics/matrix3x3.hpp>
#include <mathematics/matrix4x4.hpp>

#endif // MATHEMATICS_MATRIX_HPP
