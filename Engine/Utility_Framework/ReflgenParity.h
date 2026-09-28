#pragma once
// reflgen 전환 동등성 증명의 도구 (reflgen 도입 P4) — 모듈마다 있는 ReflgenParity.cpp 가 쓴다.
//
// 옛 레시피가 적었던 것(필드 이름·순서·속성, 메서드 이름·파라미터 이름)을 값으로 적어 두고, 다리
// (ReflgenBridge.h)가 만든 엔진 스키마(meta::schema_of<T>)가 그것과 같은지 컴파일 때 견준다.
//
//   static_assert(parity::matches<BoxColliderComponent>(
//       parity::field("m_boxExtent"),
//       parity::field("staticFriction", meta::range(0.0f, 1.0f)),
//       parity::method<&SoundComponent::Pause>("pause")));
//
// 속성은 타입과 값을 모두 본다. 필드는 이름으로 견준다 — 클래스 안에서 멤버 이름은 하나이므로 이름이 같으면
// 멤버 포인터도 같고, 이름으로 적으면 비공개 멤버도 클래스 밖에서 적을 수 있다. 엔진 소비자는 스키마 타입에
// 대한 template 이라 이것이 같으면 동작도 같다.
//
// 메서드는 멤버 포인터로 적고 method_info 타입이 옛 레시피(meta::method<&T::X>)와 같은지 본다 — 같은 함수이고
// 파라미터 이름 수가 같다. 파라미터 이름과 인스펙터 플래그(readOnlyInInspector·hideInInspector)는 값으로 본다.
#include "MetaSchema.h"
#include <array>
#include <cstddef>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace parity
{
    template<class... Attrs>
    struct field_entry
    {
        std::string_view name;
        std::tuple<Attrs...> attributes;
    };

    template<auto Function, std::size_t N>
    struct method_entry
    {
        std::array<std::string_view, N> parameters;
        bool read_only = false;
        bool hidden = false;

        // 옛 레시피의 표기 그대로 적는다(meta::method_info 와 같은 이름).
        consteval method_entry readOnlyInInspector() const
        {
            return { parameters, true, hidden };
        }

        consteval method_entry hideInInspector() const
        {
            return { parameters, read_only, true };
        }
    };

    template<class... Attrs>
    consteval field_entry<Attrs...> field(std::string_view name, Attrs... attributes)
    {
        return { name, { attributes... } };
    }

    template<auto Function, class... Names>
    consteval method_entry<Function, sizeof...(Names)> method(Names... parameters)
    {
        return { { std::string_view(parameters)... } };
    }

    namespace detail
    {
        template<class E> struct is_field_entry : std::false_type {};
        template<class... As> struct is_field_entry<field_entry<As...>> : std::true_type {};

        template<class E> struct is_method_entry : std::false_type {};
        template<auto F, std::size_t N> struct is_method_entry<method_entry<F, N>> : std::true_type {};

        template<template<class> class Pred, class... Es>
        constexpr auto pick(const Es&... entries)
        {
            return std::tuple_cat([&]
            {
                if constexpr (Pred<Es>::value) { return std::tuple{ entries }; }
                else { return std::tuple<>{}; }
            }()...);
        }

        // 엔진 속성에는 비교 연산자가 없다 — 모양으로 가른다(구간·문자열 하나·표지).
        template<class A>
        constexpr bool same_attribute(const A& left, const A& right)
        {
            if constexpr (requires { left.min; left.max; })
            {
                return left.min == right.min && left.max == right.max;
            }
            else if constexpr (requires { left.value; })
            {
                return left.value == right.value;
            }
            else
            {
                static_assert(std::is_empty_v<A>, "값을 가진 속성은 여기서 비교를 더한다");
                return true;
            }
        }

        template<class Tuple, class Same>
        constexpr bool all_pairs(const Tuple& left, const Tuple& right, Same same)
        {
            return [&]<std::size_t... I>(std::index_sequence<I...>)
            {
                return (same(std::get<I>(left), std::get<I>(right)) && ...);
            }(std::make_index_sequence<std::tuple_size_v<Tuple>>{});
        }

        template<class Field, class... Attrs>
        constexpr bool same_field(const Field& actual, const field_entry<Attrs...>& expected)
        {
            if (Field::identifier != expected.name)
            {
                return false;
            }
            if constexpr (!std::is_same_v<std::remove_cvref_t<decltype(actual.attributes)>, std::tuple<Attrs...>>)
            {
                return false; // 속성의 종류·순서가 다르다
            }
            else
            {
                return all_pairs(actual.attributes, expected.attributes,
                    [](const auto& a, const auto& b) { return same_attribute(a, b); });
            }
        }

        template<class Method, auto Function, std::size_t N>
        constexpr bool same_method(const Method& actual, const method_entry<Function, N>& expected)
        {
            if constexpr (!std::is_same_v<Method, meta::method_info<Function, N>>)
            {
                return false; // 다른 함수이거나 파라미터 이름 수가 다르다
            }
            for (std::size_t i = 0; i < N; ++i)
            {
                if (std::string_view(actual.paramNames[i]) != expected.parameters[i])
                {
                    return false;
                }
            }
            return actual.inspectorReadOnly == expected.read_only && actual.inspectorHidden == expected.hidden;
        }

        template<class Actual, class Expected, class Same>
        constexpr bool same_list(const Actual& actual, const Expected& expected, Same same)
        {
            if constexpr (std::tuple_size_v<Actual> != std::tuple_size_v<Expected>)
            {
                return false; // 필드·메서드 수가 다르다
            }
            else
            {
                return [&]<std::size_t... I>(std::index_sequence<I...>)
                {
                    return (same(std::get<I>(actual), std::get<I>(expected)) && ...);
                }(std::make_index_sequence<std::tuple_size_v<Actual>>{});
            }
        }
    }

    // 서술이 다리(meta::of<T>)에서만 오고, 옛 레시피와 같은가.
    template<class T, class... Entries>
    constexpr bool matches(const Entries&... entries)
    {
        if constexpr (meta::detail::has_local_recipe<T> || !meta::detail::has_external_recipe<T>)
        {
            return false; // 옛 레시피가 남았거나 reflgen 서술이 없다
        }
        else
        {
            const auto& schema = meta::schema_of<T>;
            const auto fields = detail::pick<detail::is_field_entry>(entries...);
            const auto methods = detail::pick<detail::is_method_entry>(entries...);
            return detail::same_list(schema.fields, fields,
                       [](const auto& a, const auto& e) { return detail::same_field(a, e); })
                && detail::same_list(schema.methods, methods,
                       [](const auto& a, const auto& e) { return detail::same_method(a, e); });
        }
    }
}
