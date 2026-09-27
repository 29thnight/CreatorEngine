#pragma once
// reflgen 다리 (reflgen 도입 파일럿) — reflgen 이 서술한 타입을 엔진 스키마(meta::type_schema)로 옮긴다.
//
// 이 파일은 reflgen 의 주입 header 가 맨 먼저 include 한다(Directory.Build.targets 의 ReflgenAttributeHeaders) —
// 모든 번역 단위의 맨 앞(pch 다음)이다. 두 가지를 둔다:
//
//   ① 엔진 속성 이름공간 creator — 필드에 [[creator::units("m")]] 처럼 단다. 엔진 속성 타입의 별명이라 생성된
//      서술에 그 타입 그대로 실린다.
//   ② meta::of<T> 다리 — reflgen 이 서술한 타입의 로컬 스키마를 엔진 스키마로 옮긴다. 엔진 소비자(직렬화·
//      인스펙터·Meta::Type 어댑터)는 meta::schema_of<T> 만 읽으므로 서술의 출처를 모른다. 특수화가 번역 단위의
//      맨 앞에서 선언되므로 어떤 소비자가 meta::of<T> 를 묻기(meta::reflectable·Meta::HasReflection)보다 먼저 보인다.
//
// 옮기는 규칙 — 결과는 같은 타입을 손으로 쓴 레시피(meta::schema<Self>(meta::field<&Self::x>...))와 **같은 타입,
// 같은 값**이다(ReflgenBridgeSelfTest.cpp 가 증명한다). 엔진 소비자는 스키마 타입에 대한 template 이므로 타입과
// 값이 같으면 동작도 같다.
//   - 필드·메서드: 선언 순서 그대로. 이름은 엔진이 멤버 포인터에서 읽는다(meta::field_info::identifier).
//   - 속성: reflgen::range·display_name·hidden·readonly 는 엔진 속성으로 바꾸고, 엔진 속성(creator::…)은 그대로
//     싣는다. 엔진이 읽지 않는 reflgen 속성(description·serialized_name·category·transient·required)은 컴파일
//     오류다 — 조용히 버리면 붙인 사람이 기대한 일이 일어나지 않는다.
//   - 부모: 엔진 규약대로 클래스 선언(meta::identity<T, Base> 의 meta_identity)에서 온다. reflgen 이 본 반영된
//     부모가 그것과 다르면 컴파일 오류다.
//   - 타입 이름: 엔진 이름(meta::type_name_of<T>)이다 — typeID 와 YAML 헤더가 이것으로 정해진다. 그래서
//     reflgen::reflect("다른 이름") 은 컴파일 오류다.
#include "MetaSchema.h"
#include <reflgen/core/schema.h>
#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

namespace creator
{
    using units = meta::units_attr;
    using debug_only = meta::debug_only_attr;
    using wide = meta::wide_attr;
}

namespace meta
{
    namespace detail::reflgen_bridge
    {
        template<class>
        inline constexpr bool dependent_false = false;

        template<class A>
        struct is_reflgen_range : std::false_type {};

        template<class V>
        struct is_reflgen_range<reflgen::range<V>> : std::true_type {};

        template<class A>
        inline constexpr bool unsupported_reflgen_attribute =
            std::is_same_v<A, reflgen::description> || std::is_same_v<A, reflgen::serialized_name>
            || std::is_same_v<A, reflgen::category> || std::is_same_v<A, reflgen::transient>
            || std::is_same_v<A, reflgen::required>;

        template<class A>
        consteval auto attribute(const A& value)
        {
            if constexpr (is_reflgen_range<A>::value)
            {
                using V = std::remove_cvref_t<decltype(value.min)>;
                return range_attr<V>{ value.min, value.max };
            }
            else if constexpr (std::is_same_v<A, reflgen::display_name>)
            {
                // 문자열 리터럴을 가리킨다 — NUL 종단이다(Property::displayName 이 .data() 를 C 문자열로 쓴다).
                return display_name_attr{ value.value.view() };
            }
            else if constexpr (std::is_same_v<A, reflgen::hidden>)
            {
                return hidden_attr{};
            }
            else if constexpr (std::is_same_v<A, reflgen::readonly>)
            {
                return readonly_attr{};
            }
            else if constexpr (unsupported_reflgen_attribute<A>)
            {
                static_assert(dependent_false<A>,
                    "엔진 소비자가 읽지 않는 reflgen 속성이다(description·serialized_name·category·transient·required) — "
                    "엔진에 대응을 만들기 전에는 쓰지 않는다");
                return value;
            }
            else
            {
                return value; // 엔진 속성(creator::…) — 그대로 싣는다
            }
        }

        // 람다(std::apply)로 펴지 않는다 — 람다 안에서는 consteval 함수(with·params·schema)를 그 매개변수로 부를 수
        // 없다(MSVC 는 consteval 을 둘러싼 람다로 전파하지 않는다). index_sequence 로 이 함수 안에서 바로 편다.
        template<auto Member, class... Attrs, std::size_t... I>
        consteval auto field(const reflgen::field_descriptor<Member, Attrs...>& source, std::index_sequence<I...>)
        {
            return meta::field<Member>.with(reflgen_bridge::attribute(std::get<I>(source.attributes))...);
        }

        template<auto Member, class... Attrs>
        consteval auto field(const reflgen::field_descriptor<Member, Attrs...>& source)
        {
            return reflgen_bridge::field(source, std::index_sequence_for<Attrs...>{});
        }

        // 파라미터 이름은 생성된 서술의 문자열 리터럴이다 — NUL 종단이다.
        template<auto Function, std::size_t N, std::size_t... I>
        consteval auto parameters(const reflgen::method_descriptor<Function, N>& source, std::index_sequence<I...>)
        {
            return meta::method<Function>.params(source.parameter_names[I].data()...);
        }

        template<auto Function, std::size_t N, class... Attrs>
        consteval auto method(const reflgen::method_descriptor<Function, N, Attrs...>& source)
        {
            static_assert(sizeof...(Attrs) == 0, "엔진 메서드 서술에는 속성이 없다 — 메서드에 속성을 달지 않는다");
            if constexpr (N == 0)
            {
                return meta::method<Function>;
            }
            else
            {
                return reflgen_bridge::parameters(source, std::make_index_sequence<N>{});
            }
        }

        // reflgen 이 본 반영된 부모가 엔진의 부모(meta_identity)와 같은가. reflgen 은 반영된 부모만 적으므로 부모가
        // 아직 reflgen 으로 옮기지 않은 타입이면 비어 있다 — 그것은 맞다.
        template<class T>
        consteval bool bases_agree()
        {
            using reflgen_bases = typename std::remove_cvref_t<decltype(reflgen::schema_of<T>)>::base_types;
            if constexpr (std::is_same_v<reflgen_bases, reflgen::type_list<>>)
            {
                return true;
            }
            else if constexpr (declares_identity<T>)
            {
                return std::is_same_v<reflgen_bases, reflgen::type_list<typename T::meta_identity::base_type>>;
            }
            else
            {
                return false;
            }
        }

        template<class T, std::size_t... F, std::size_t... M>
        consteval auto schema(std::index_sequence<F...>, std::index_sequence<M...>)
        {
            constexpr const auto& source = reflgen::schema_of<T>;
            return meta::schema<T>(reflgen_bridge::field(std::get<F>(source.fields))...,
                reflgen_bridge::method(std::get<M>(source.methods))...);
        }

        template<class T>
        consteval auto schema()
        {
            static_assert(std::tuple_size_v<std::remove_cvref_t<decltype(reflgen::schema_of<T>.attributes)>> == 0,
                "엔진 타입 서술에는 속성이 없다 — 타입에 속성을 달지 않는다");
            static_assert(reflgen::schema_of<T>.name == type_name_of<T>(),
                "엔진 타입 이름은 선언에서 온다(typeID·YAML 헤더) — reflgen::reflect 에 이름을 주지 않는다");
            static_assert(bases_agree<T>(),
                "reflgen 이 본 반영된 부모가 엔진의 부모(meta::identity<T, Base>)와 다르다");

            using source_type = std::remove_cvref_t<decltype(reflgen::schema_of<T>)>;
            return reflgen_bridge::schema<T>(std::make_index_sequence<source_type::field_count>{},
                std::make_index_sequence<source_type::method_count>{});
        }
    }

    // reflgen 이 서술한 타입의 외부 서술 — meta::reflectable<T> 가 이것으로 참이 된다. 손으로 쓴 레시피
    // (static reflect())도 가진 타입은 meta::reflect<T>() 가 이중 서술로 막는다.
    template<class T>
        requires reflgen::reflectable<T>
    struct of<T>
    {
        static constexpr auto value = detail::reflgen_bridge::schema<T>();
    };
}
