#pragma once
// Runtime-only math metadata. The authoring schema and serializers are unchanged.
#include <reflgen/runtime/schema.h>
#include <reflgen/runtime/value_descriptor.h>
#include <mathematics/vector2.hpp>
#include <mathematics/vector3.hpp>
#include <mathematics/vector4.hpp>
#include <mathematics/quaternion.hpp>
#include <mathematics/color.hpp>
#include <mathematics/rect.hpp>
#include <mathematics/matrix4x4.hpp>
#include <mathematics/bounds.hpp>
#include <mathematics/ray.hpp>
#include <mathematics/plane.hpp>
#include <mathematics/frustum.hpp>

namespace reflgen
{
    // Runtime record snapshots use the same named members that can be selected
    // by a path. This keeps whole-value Read/Write round trips coherent; the
    // engine's authoring math serializers retain their existing formats (for
    // example, matrix4x4's flat sixteen-element array).
    template<class T> requires (
        std::is_same_v<T, math::vector2> || std::is_same_v<T, math::vector3>
        || std::is_same_v<T, math::vector4> || std::is_same_v<T, math::quaternion>
        || std::is_same_v<T, math::color> || std::is_same_v<T, math::rect>
        || std::is_same_v<T, math::matrix4x4> || std::is_same_v<T, math::aabb>
        || std::is_same_v<T, math::sphere> || std::is_same_v<T, math::ray>
        || std::is_same_v<T, math::plane> || std::is_same_v<T, math::bounding_frustum>)
    struct runtime_value_traits<T>
    {
        static constexpr bool prefer_record_fields = true;
    };

    // Runtime-only schemas add member navigation without changing the engine's
    // reflected/serialized schema or its existing authoring wire formats.
    template<> struct runtime_schema<math::vector2>
    {
        static constexpr auto value = schema<math::vector2>(
            field<&math::vector2::x>, field<&math::vector2::y>);
    };
    template<> struct runtime_schema<math::vector3>
    {
        static constexpr auto value = schema<math::vector3>(
            field<&math::vector3::x>, field<&math::vector3::y>, field<&math::vector3::z>);
    };
    template<> struct runtime_schema<math::vector4>
    {
        static constexpr auto value = schema<math::vector4>(
            field<&math::vector4::x>, field<&math::vector4::y>, field<&math::vector4::z>, field<&math::vector4::w>);
    };
    template<> struct runtime_schema<math::quaternion>
    {
        static constexpr auto value = schema<math::quaternion>(
            field<&math::quaternion::x>, field<&math::quaternion::y>, field<&math::quaternion::z>, field<&math::quaternion::w>);
    };
    template<> struct runtime_schema<math::color>
    {
        static constexpr auto value = schema<math::color>(
            field<&math::color::r>, field<&math::color::g>, field<&math::color::b>, field<&math::color::a>);
    };
    template<> struct runtime_schema<math::rect>
    {
        static constexpr auto value = schema<math::rect>(
            field<&math::rect::x>, field<&math::rect::y>, field<&math::rect::width>, field<&math::rect::height>);
    };
    template<> struct runtime_schema<math::matrix4x4>
    {
        static constexpr auto value = schema<math::matrix4x4>(field<&math::matrix4x4::m>);
    };
    template<> struct runtime_schema<math::aabb>
    {
        static constexpr auto value = schema<math::aabb>(
            field<&math::aabb::center>, field<&math::aabb::extents>,
            method<&math::aabb::is_empty>, method<&math::aabb::min>, method<&math::aabb::max>,
            method<&math::aabb::corner>.parameters("index"));
    };
    template<> struct runtime_schema<math::sphere>
    {
        static constexpr auto value = schema<math::sphere>(field<&math::sphere::center>, field<&math::sphere::radius>);
    };
    template<> struct runtime_schema<math::ray>
    {
        static constexpr auto value = schema<math::ray>(field<&math::ray::origin>, field<&math::ray::direction>,
            method<&math::ray::point_at>.parameters("distance"));
    };
    template<> struct runtime_schema<math::plane>
    {
        static constexpr auto value = schema<math::plane>(
            field<&math::plane::a>, field<&math::plane::b>, field<&math::plane::c>, field<&math::plane::d>,
            method<&math::plane::normal>);
    };
    template<> struct runtime_schema<math::bounding_frustum>
    {
        static constexpr auto value = schema<math::bounding_frustum>(
            field<&math::bounding_frustum::origin>, field<&math::bounding_frustum::orientation>,
            field<&math::bounding_frustum::right_slope>, field<&math::bounding_frustum::left_slope>,
            field<&math::bounding_frustum::top_slope>, field<&math::bounding_frustum::bottom_slope>,
            field<&math::bounding_frustum::near_plane>, field<&math::bounding_frustum::far_plane>,
            method<&math::bounding_frustum::corner>.parameters("index"), method<&math::bounding_frustum::corners>);
    };
}
