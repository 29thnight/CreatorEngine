#pragma once
#include <mathematics/transform.hpp>

namespace ImViewGuizmo
{
    using vec3_t = math::vector3;
    using vec4_t = math::vector4;
    using quat_t = math::quaternion;
    using mat4_t = math::matrix4x4;

    // The widget's named column operations map to Mathematics row-vector matrices.
    // Camera space is CreatorEngine's +Z forward, +Y up throughout.
    namespace GizmoMath
    {
        inline float get_vec_comp(const vec3_t& v, int i) { return v[i]; }
        inline float get_vec_comp(const vec4_t& v, int i) { return v[i]; }
        inline vec3_t make_vec3(float x, float y, float z) { return {x, y, z}; }
        inline vec4_t make_vec4(float x, float y, float z, float w) { return {x, y, z, w}; }
        inline quat_t angleAxis(float angle, const vec3_t& axis) { return math::quaternion_from_axis_angle(axis, angle); }
        inline quat_t quatLookAt(const vec3_t& direction, const vec3_t& up)
        {
            return math::normalize(math::quaternion_from_rotation_matrix(
                math::inverse(math::look_at_lh({}, direction, up))));
        }
        inline mat4_t mat4_identity() { return mat4_t::identity(); }
        inline vec3_t cross(const vec3_t& a, const vec3_t& b) { return math::cross(a, b); }
        inline float dot(const vec3_t& a, const vec3_t& b) { return math::dot(a, b); }
        inline float dot(const quat_t& a, const quat_t& b) { return math::dot(a, b); }
        inline float length(const vec3_t& v) { return math::length(v); }
        inline float length2(const vec3_t& v) { return math::dot(v, v); }
        inline vec3_t normalize(const vec3_t& v) { return math::normalize(v); }
        inline vec3_t mix(const vec3_t& a, const vec3_t& b, float t) { return math::lerp(a, b, t); }
        inline vec3_t add_vv(const vec3_t& a, const vec3_t& b) { return a + b; }
        inline vec3_t subtract_vv(const vec3_t& a, const vec3_t& b) { return a - b; }
        inline vec3_t multiply_vf(const vec3_t& v, float f) { return v * f; }
        inline mat4_t mat4_cast(const quat_t& q) { return math::rotation_matrix(q); }
        inline mat4_t transpose(const mat4_t& m) { return math::transpose(m); }
        inline vec3_t get_matrix_col(const mat4_t& m, int i) { return {m.m[i][0], m.m[i][1], m.m[i][2]}; }
        inline void set_matrix_col(mat4_t& m, int i, const vec4_t& v) { for (int j = 0; j < 4; ++j) m.m[i][j] = v[j]; }
        // Mathematics composes in application order; upstream uses Hamilton order.
        inline quat_t multiply_qq(const quat_t& a, const quat_t& b) { return b * a; }
        inline vec3_t multiply_qv(const quat_t& q, const vec3_t& v) { return math::rotate(v, q); }
        inline mat4_t multiply_mm(const mat4_t& a, const mat4_t& b) { return b * a; }
        inline vec4_t multiply_mv4(const mat4_t& m, const vec4_t& v) { return v * m; }
    }
}
