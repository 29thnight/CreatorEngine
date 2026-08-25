// mathematics/vector.hpp — the vector types.
//
// vector2, vector3 and vector4 are packed, standard-layout types with the full
// operator set. They are what most code should use: `v.x` reaches a real member,
// a vector3 array is a twelve-bytes-per-element position stream, and they sit in
// structs with no alignment demands.
//
// Operations promote to vec_reg, compute there, and store back, all force-inlined
// so a chain of them stays in registers. Code that crosses non-inlined
// boundaries in a hot loop should hold vec_reg directly instead
// (mathematics/vec_reg.hpp).
//
//   Arithmetic   + - * / unary- and the compound forms; * and / also by scalar
//   Comparison   == != (exact), near_equal(a, b, epsilon)
//   Lane-wise    Abs Min Max Clamp Saturate Lerp
//   Geometry     dot length length_sq distance distance_sq
//                normalize normalize_unchecked normalize_est reflect refract
//   vector3      cross
//   vector2      cross (scalar) perpendicular
//
// Component-wise multiply is `*`, following HLSL and GLM; the dot product is
// always Dot, never an operator (docs/PLAN.md §2.7).
#ifndef MATHEMATICS_VECTOR_HPP
#define MATHEMATICS_VECTOR_HPP

#include <mathematics/vector2.hpp>
#include <mathematics/vector3.hpp>
#include <mathematics/vector4.hpp>

#endif // MATHEMATICS_VECTOR_HPP
