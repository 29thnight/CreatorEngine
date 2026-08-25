// mathematics/mathematics.hpp — everything, for code that does not care about include cost.
//
// Prefer the individual headers in a translation unit that only needs part of
// the library: each one is self-contained and they are deliberately small.
// This exists so a consumer can write one include and get on with it.
//
// Conventions, all of them DirectXMath's and all of them verified against it
// rather than assumed:
//
//   Storage        row-major matrices, m[row][col], a row contiguous
//   Vectors        row vectors, transformed as `v * M`
//   Composition    left to right in application order, for matrices AND
//                  quaternions -- `a * b` is a followed by b
//   Translation    row 3 of a matrix4x4
//   Quaternions    (x, y, z, w), scalar last; axis-angle is
//                  (axis * sin(t/2), cos(t/2))
//   Handedness     never a default -- look_at_lh/look_at_rh,
//                  perspective_fov_lh/perspective_fov_rh, ...
//   Depth          clip z from 0 at the near plane to 1 at the far plane
//   Angles         radians; degrees() and radians() convert
//
// Everything is constexpr-usable, which is the line DirectXMath cannot cross.
#ifndef MATHEMATICS_MATHEMATICS_HPP
#define MATHEMATICS_MATHEMATICS_HPP

#include <mathematics/color.hpp>
#include <mathematics/config.hpp>
#include <mathematics/geometry.hpp>
#include <mathematics/mdspan.hpp>
#include <mathematics/matrix.hpp>
#include <mathematics/quaternion.hpp>
#include <mathematics/ranges.hpp>
#include <mathematics/rect.hpp>
#include <mathematics/scalar.hpp>
#include <mathematics/transform.hpp>
#include <mathematics/vec_reg.hpp>
#include <mathematics/vector.hpp>
#include <mathematics/views.hpp>

#endif // MATHEMATICS_MATHEMATICS_HPP
