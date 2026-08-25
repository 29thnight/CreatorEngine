// mathematics/format.hpp — std::format support, and structured bindings.
//
// An OPT-IN header, not part of mathematics.hpp. Including <format> pulls in a large
// chunk of the standard library, and a header-only math library that dragged it
// into every translation unit would be charging every user for a feature most
// of them never call. Include this one where you print.
//
// Every formatter forwards the caller's format spec to the individual floats,
// so a spec that works on a float works here:
//
//   std::format("{}", v)            -> (1, 2, 3)
//   std::format("{:.2f}", v)        -> (1.00, 2.00, 3.00)
//   std::format("{:8.3e}", v)       -> (1.000e+00, 2.000e+00, 3.000e+00)
//
// A matrix prints its rows the way the library stores them -- row-major, one
// row per line -- so what you read matches m[row][col] rather than the
// transpose. Getting that backwards in a debug print is how an afternoon
// disappears.
//
// STRUCTURED BINDINGS need nothing from this header, or from any other. Every
// packed type here has only public data members, which is all the language
// asks, so `auto [x, y, z] = v;` already works out of the box -- for vectors,
// colours, rectangles, quaternions, spheres, boxes, frusta and rays alike. No
// tuple protocol is provided,
// because adding one would buy `get<0>(v)` and `std::apply` and cost a pile of
// specializations nobody has asked for. The tests pin that bindings work, so
// the property cannot be lost by accident.
#ifndef MATHEMATICS_FORMAT_HPP
#define MATHEMATICS_FORMAT_HPP

#include <mathematics/color.hpp>
#include <mathematics/geometry.hpp>
#include <mathematics/matrix.hpp>
#include <mathematics/quaternion.hpp>
#include <mathematics/rect.hpp>
#include <mathematics/vector.hpp>

// GCC only shipped <format> in 13, and libc++ kept it behind a flag for a
// while. Rather than break the build on a toolchain that is otherwise fine,
// this header becomes empty and MATHEMATICS_HAS_FORMAT says so -- callers guard on
// it, and the tests skip themselves.
#if defined(__has_include)
#  if __has_include(<format>)
#    include <format>
#  endif
#endif

#if defined(__cpp_lib_format) && __cpp_lib_format >= 201907L
#  define MATHEMATICS_HAS_FORMAT 1
#else
#  define MATHEMATICS_HAS_FORMAT 0
#endif

#if MATHEMATICS_HAS_FORMAT

#include <string>

namespace math::detail {

// The shared body of every formatter here: remember the caller's spec, then
// replay it around each float. Storing the spec rather than parsing it means a
// new <format> float spec works the day it lands, with nothing to update.
struct float_spec_formatter {
    std::string spec;

    constexpr auto parse(std::format_parse_context& context) {
        auto it = context.begin();
        const auto end = context.end();
        spec.clear();
        while (it != end && *it != '}') {
            spec.push_back(*it);
            ++it;
        }
        return it;
    }

    template <typename output_iterator>
    output_iterator write_float(output_iterator out, float value) const {
        // Rebuilt per call rather than cached: a formatter object is allowed to
        // be const during formatting, and the cost is nothing next to the
        // formatting itself.
        return std::vformat_to(out, "{:" + spec + "}",
                               std::make_format_args(value));
    }
};

} // namespace math::detail

// ------------------------------------------------------------------- vectors
template <>
struct std::formatter<math::vector2> : math::detail::float_spec_formatter {
    template <typename context_type>
    auto format(const math::vector2& v, context_type& context) const {
        auto out = context.out();
        *out++ = '(';
        out = write_float(out, v.x);
        *out++ = ',';  *out++ = ' ';
        out = write_float(out, v.y);
        *out++ = ')';
        return out;
    }
};

template <>
struct std::formatter<math::vector3> : math::detail::float_spec_formatter {
    template <typename context_type>
    auto format(const math::vector3& v, context_type& context) const {
        auto out = context.out();
        *out++ = '(';
        out = write_float(out, v.x);
        *out++ = ',';  *out++ = ' ';
        out = write_float(out, v.y);
        *out++ = ',';  *out++ = ' ';
        out = write_float(out, v.z);
        *out++ = ')';
        return out;
    }
};

template <>
struct std::formatter<math::vector4> : math::detail::float_spec_formatter {
    template <typename context_type>
    auto format(const math::vector4& v, context_type& context) const {
        auto out = context.out();
        *out++ = '(';
        out = write_float(out, v.x);
        *out++ = ',';  *out++ = ' ';
        out = write_float(out, v.y);
        *out++ = ',';  *out++ = ' ';
        out = write_float(out, v.z);
        *out++ = ',';  *out++ = ' ';
        out = write_float(out, v.w);
        *out++ = ')';
        return out;
    }
};

// A quaternion prints in storage order (x, y, z, w), scalar last, matching the
// layout -- so what you read lines up with what a debugger shows.
template <>
struct std::formatter<math::quaternion> : math::detail::float_spec_formatter {
    template <typename context_type>
    auto format(const math::quaternion& q, context_type& context) const {
        auto out = context.out();
        *out++ = '(';
        out = write_float(out, q.x);
        *out++ = ',';  *out++ = ' ';
        out = write_float(out, q.y);
        *out++ = ',';  *out++ = ' ';
        out = write_float(out, q.z);
        *out++ = ',';  *out++ = ' ';
        out = write_float(out, q.w);
        *out++ = ')';
        return out;
    }
};

template <>
struct std::formatter<math::color> : math::detail::float_spec_formatter {
    template <typename context_type>
    auto format(const math::color& value, context_type& context) const {
        auto out = context.out();
        out = std::format_to(out, "color(rgba=(");
        out = write_float(out, value.r);
        *out++ = ',';  *out++ = ' ';
        out = write_float(out, value.g);
        *out++ = ',';  *out++ = ' ';
        out = write_float(out, value.b);
        *out++ = ',';  *out++ = ' ';
        out = write_float(out, value.a);
        out = std::format_to(out, "))");
        return out;
    }
};

template <>
struct std::formatter<math::rect> : math::detail::float_spec_formatter {
    template <typename context_type>
    auto format(const math::rect& value, context_type& context) const {
        auto out = context.out();
        out = std::format_to(out, "rect(x=");
        out = write_float(out, value.x);
        out = std::format_to(out, ", y=");
        out = write_float(out, value.y);
        out = std::format_to(out, ", w=");
        out = write_float(out, value.width);
        out = std::format_to(out, ", h=");
        out = write_float(out, value.height);
        *out++ = ')';
        return out;
    }
};

// ------------------------------------------------------------------ matrices
// Row-major, one row per line, in the order the library stores them.
template <>
struct std::formatter<math::matrix4x4> : math::detail::float_spec_formatter {
    template <typename context_type>
    auto format(const math::matrix4x4& m, context_type& context) const {
        auto out = context.out();
        for (int row = 0; row < 4; ++row) {
            *out++ = '[';
            for (int col = 0; col < 4; ++col) {
                if (col != 0) { *out++ = ','; *out++ = ' '; }
                out = write_float(out, m.m[row][col]);
            }
            *out++ = ']';
            if (row != 3) *out++ = '\n';
        }
        return out;
    }
};

template <>
struct std::formatter<math::matrix3x3> : math::detail::float_spec_formatter {
    template <typename context_type>
    auto format(const math::matrix3x3& m, context_type& context) const {
        auto out = context.out();
        for (int row = 0; row < 3; ++row) {
            *out++ = '[';
            for (int col = 0; col < 3; ++col) {
                if (col != 0) { *out++ = ','; *out++ = ' '; }
                out = write_float(out, m.m[row][col]);
            }
            *out++ = ']';
            if (row != 2) *out++ = '\n';
        }
        return out;
    }
};

// ------------------------------------------------------------------ geometry
// These print their MEANING, not their storage: an AABB shows its minimum and
// maximum because that is what a person reads a box for, even though it stores
// a centre and half-widths. The storage is the machine's business.
template <>
struct std::formatter<math::plane> : math::detail::float_spec_formatter {
    template <typename context_type>
    auto format(const math::plane& p, context_type& context) const {
        auto out = context.out();
        out = std::format_to(out, "plane(n=(");
        out = write_float(out, p.a);
        *out++ = ',';  *out++ = ' ';
        out = write_float(out, p.b);
        *out++ = ',';  *out++ = ' ';
        out = write_float(out, p.c);
        out = std::format_to(out, "), d=");
        out = write_float(out, p.d);
        *out++ = ')';
        return out;
    }
};

template <>
struct std::formatter<math::sphere> : math::detail::float_spec_formatter {
    template <typename context_type>
    auto format(const math::sphere& s, context_type& context) const {
        auto out = context.out();
        out = std::format_to(out, "sphere(c=(");
        out = write_float(out, s.center.x);
        *out++ = ',';  *out++ = ' ';
        out = write_float(out, s.center.y);
        *out++ = ',';  *out++ = ' ';
        out = write_float(out, s.center.z);
        out = std::format_to(out, "), r=");
        out = write_float(out, s.radius);
        *out++ = ')';
        return out;
    }
};

template <>
struct std::formatter<math::aabb> : math::detail::float_spec_formatter {
    template <typename context_type>
    auto format(const math::aabb& box, context_type& context) const {
        if (box.is_empty()) {
            return std::format_to(context.out(), "aabb(empty)");
        }
        const math::vector3 lo = box.min();
        const math::vector3 hi = box.max();
        auto out = context.out();
        out = std::format_to(out, "aabb(min=(");
        out = write_float(out, lo.x);
        *out++ = ',';  *out++ = ' ';
        out = write_float(out, lo.y);
        *out++ = ',';  *out++ = ' ';
        out = write_float(out, lo.z);
        out = std::format_to(out, "), max=(");
        out = write_float(out, hi.x);
        *out++ = ',';  *out++ = ' ';
        out = write_float(out, hi.y);
        *out++ = ',';  *out++ = ' ';
        out = write_float(out, hi.z);
        out = std::format_to(out, "))");
        return out;
    }
};

template <>
struct std::formatter<math::ray> : math::detail::float_spec_formatter {
    template <typename context_type>
    auto format(const math::ray& input_ray, context_type& context) const {
        auto out = context.out();
        out = std::format_to(out, "ray(o=(");
        out = write_float(out, input_ray.origin.x);
        *out++ = ',';  *out++ = ' ';
        out = write_float(out, input_ray.origin.y);
        *out++ = ',';  *out++ = ' ';
        out = write_float(out, input_ray.origin.z);
        out = std::format_to(out, "), d=(");
        out = write_float(out, input_ray.direction.x);
        *out++ = ',';  *out++ = ' ';
        out = write_float(out, input_ray.direction.y);
        *out++ = ',';  *out++ = ' ';
        out = write_float(out, input_ray.direction.z);
        out = std::format_to(out, "))");
        return out;
    }
};

template <>
struct std::formatter<math::bounding_frustum>
    : math::detail::float_spec_formatter {
    template <typename context_type>
    auto format(const math::bounding_frustum& frustum,
                context_type& context) const {
        auto out = context.out();
        out = std::format_to(out, "frustum(o=(");
        out = write_float(out, frustum.origin.x);
        *out++ = ',';  *out++ = ' ';
        out = write_float(out, frustum.origin.y);
        *out++ = ',';  *out++ = ' ';
        out = write_float(out, frustum.origin.z);
        out = std::format_to(out, "), q=(");
        out = write_float(out, frustum.orientation.x);
        *out++ = ',';  *out++ = ' ';
        out = write_float(out, frustum.orientation.y);
        *out++ = ',';  *out++ = ' ';
        out = write_float(out, frustum.orientation.z);
        *out++ = ',';  *out++ = ' ';
        out = write_float(out, frustum.orientation.w);
        out = std::format_to(out, "), slopes=(");
        out = write_float(out, frustum.right_slope);
        *out++ = ',';  *out++ = ' ';
        out = write_float(out, frustum.left_slope);
        *out++ = ',';  *out++ = ' ';
        out = write_float(out, frustum.top_slope);
        *out++ = ',';  *out++ = ' ';
        out = write_float(out, frustum.bottom_slope);
        out = std::format_to(out, "), depth=(");
        out = write_float(out, frustum.near_plane);
        *out++ = ',';  *out++ = ' ';
        out = write_float(out, frustum.far_plane);
        out = std::format_to(out, "))");
        return out;
    }
};

#endif // MATHEMATICS_HAS_FORMAT

#endif // MATHEMATICS_FORMAT_HPP
