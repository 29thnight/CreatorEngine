#pragma once
// 컴파일타임 리플렉션 코어 (PHASE 18 CT4) — P2996(C++26 표준 리플렉션) 유사 표면.
//
// 매크로 없이, 표준 도입 예정 문법의 라이브러리 근사로 표기한다. 대응표:
//
//   P2996                     여기 (C++20 라이브러리 근사)
//   ─────────────────────     ─────────────────────────────────────
//   ^^T                       meta::reflect<T>()          (타입 서술자)
//   ^^T::m                    &T::m                       (멤버 포인터가 리플렉션 값)
//   obj.[:m:]                 meta::get<&T::m>(obj) 또는 meta::get(m, obj)
//   members_of(^^T)           meta::members_of<T>()
//   identifier_of(m)          meta::identifier_of(m)
//   template for              meta::for_each_member(obj, f)
//
// 타입 선언부(사용자 결정 2026-08-17 — 대소문자 쌍 금지·빌더 표현식). 타입이
// 선언하는 것은 describe() **하나뿐**이다:
//
//   static consteval auto describe()
//   {
//       return meta::describe<Light>(
//           meta::base<Component>(),                    // 부모 (없으면 생략)
//           meta::member<&Light::intensity>(
//               meta::range(0.0f, 100000.0f),           // 멤버 속성 (선택)
//               meta::displayName("Intensity")),
//           meta::member<&Light::castShadow>());
//   }
//
// 런타임 Type 테이블은 외부 창구 Meta::TypeOf<T>()가 공급한다(사용자 지적:
// 전 타입 동일하던 Reflect() 보일러플레이트의 외부화 — CRTP는 상속 목록
// 오염이라 기각). 레거시 타입은 TypeOf가 T::Reflect()로 폴백한다.
//
// 설계 제약 (ReflectionRedesignPlan CT4):
//   - describe()는 static 데이터가 아니라 **함수** — 클래스 상단(멤버 선언 앞)
//     배치를 허용하려면 멤버 포인터 형성이 complete-class 문맥이어야 한다.
//   - 멤버 이름은 __FUNCSIG__의 멤버 포인터 NTTP 표기에서 추출한다. VS 18
//     실측: "void __cdecl f<&Probe::m_value>(const char *)" — 아래 selftest의
//     static_assert가 이 표기의 카나리아다(툴체인 업그레이드 시 최우선 확인).
//   - 속성을 담은 member_info는 std::tuple을 품어 구조적 타입이 아니다 —
//     NTTP 스플라이스는 포인터(get<&T::m>)로, 서술자 값은 get(m, obj)로.
//   - adapt()는 기존 Meta::Type 테이블로의 과도기 다리(소비자 무변경)이며
//     CT7에서 소멸한다. MakeProperty를 재사용하고 속성은 여기서 무시된다
//     (속성 소비는 CT6 — 인스펙터 range/displayName 등) — 골든 diff 0 유지.
//   - P2996이 오면 describe() 선언부가 컴파일러 제공(^^T)으로 접히고 소비
//     표면(get/members_of/for_each_member)은 유지된다.
#include "ReflectionFunction.h"
#include <tuple>
#include <array>
#include <string_view>
#include <type_traits>

namespace meta
{
    namespace detail
    {
        // __FUNCSIG__에서 멤버 포인터 NTTP의 마지막 식별자를 뽑는다.
        // "... member_name_raw<&Owner::m_value>(void)" → "m_value"
        template<auto Ptr>
        consteval std::string_view member_name_raw()
        {
            std::string_view sig = __FUNCSIG__;
            const size_t close = sig.rfind(">(");
            const size_t colons = sig.rfind("::", close);
            return sig.substr(colons + 2, close - (colons + 2));
        }

        // 멤버 **함수** 포인터 NTTP는 표기가 다르다(VS 18 프로브 실측):
        //   "... method_name_raw<void __cdecl P2::Foo(int)>(void)"
        // — &Owner::Name이 아니라 전체 시그니처다. 이름은 인자 여는 괄호
        // 직전의 마지막 "::" 뒤 구간이다. 한계: 반환 타입에 '('가 들어가는
        // 함수 포인터 반환은 오파싱한다 — 현 코퍼스(void/int/bool 반환)엔
        // 없으며 카나리아가 지킨다.
        template<auto Fn>
        consteval std::string_view method_name_raw()
        {
            std::string_view sig = __FUNCSIG__;
            constexpr std::string_view marker = "method_name_raw<";
            const size_t begin = sig.find(marker) + marker.size();
            std::string_view inner = sig.substr(begin, sig.rfind(">(") - begin);
            const size_t argOpen = inner.find('(');
            const size_t colons = inner.rfind("::", argOpen);
            return inner.substr(colons + 2, argOpen - (colons + 2));
        }

        // string_view를 NUL 종단 정적 배열로 물질화한다 — 소비자(yaml-cpp의
        // node[key], Property::name)가 C 문자열을 요구한다.
        template<auto Ptr>
        struct member_name_holder
        {
            static constexpr std::string_view raw = member_name_raw<Ptr>();
            static constexpr auto storage = []
            {
                std::array<char, raw.size() + 1> a{};
                for (size_t i = 0; i < raw.size(); ++i)
                {
                    a[i] = raw[i];
                }
                return a;
            }();
            static constexpr std::string_view view{ storage.data(), raw.size() };
        };

        template<auto Fn>
        struct method_name_holder
        {
            static constexpr std::string_view raw = method_name_raw<Fn>();
            static constexpr auto storage = []
            {
                std::array<char, raw.size() + 1> a{};
                for (size_t i = 0; i < raw.size(); ++i)
                {
                    a[i] = raw[i];
                }
                return a;
            }();
            static constexpr std::string_view view{ storage.data(), raw.size() };
        };

        template<class T>
        struct type_name_holder
        {
            static constexpr std::string_view raw = TypeTrait::type_name<T>();
            static constexpr auto storage = []
            {
                std::array<char, raw.size() + 1> a{};
                for (size_t i = 0; i < raw.size(); ++i)
                {
                    a[i] = raw[i];
                }
                return a;
            }();
            static constexpr std::string_view view{ storage.data(), raw.size() };
        };
    }

    // ── 멤버 속성 (annotations_of 대응 — 소비는 CT6 인스펙터부터) ──────────

    struct display_name_attr
    {
        std::string_view value;
    };

    consteval display_name_attr displayName(std::string_view v)
    {
        return { v };
    }

    template<class V>
    struct range_attr
    {
        V min;
        V max;
    };

    template<class V>
    consteval range_attr<V> range(V minValue, V maxValue)
    {
        return { minValue, maxValue };
    }

    // ── 서술자 ─────────────────────────────────────────────────────────────

    // 멤버 서술자 — 정체성(포인터·이름)은 static, 속성은 값으로 든다.
    template<auto Ptr, class... Attrs>
    struct member_info
    {
        static constexpr auto pointer = Ptr;
        static constexpr std::string_view identifier = detail::member_name_holder<Ptr>::view;

        std::tuple<Attrs...> attributes{};

        template<class A>
        static consteval bool has_attribute()
        {
            return (std::is_same_v<A, Attrs> || ...);
        }

        template<class A>
        consteval A attribute() const
        {
            return std::get<A>(attributes);
        }

        template<class Obj>
        constexpr decltype(auto) get(Obj&& obj) const
        {
            return std::forward<Obj>(obj).*Ptr;
        }
    };

    template<auto Ptr, class... Attrs>
    consteval auto member(Attrs... attrs)
    {
        return member_info<Ptr, Attrs...>{ std::tuple<Attrs...>{ attrs... } };
    }

    // 메서드 서술자 (CT5 — 인스펙터 DrawMethods가 소비하는 Method 체계 공급).
    // 파라미터 이름은 선택 표기: meta::method<&T::Pause>("pause")
    template<auto Fn, size_t NParams>
    struct method_info
    {
        static constexpr auto pointer = Fn;
        static constexpr std::string_view identifier = detail::method_name_holder<Fn>::view;

        std::array<const char*, NParams> paramNames{};
    };

    template<auto Fn, class... Names>
    consteval auto method(Names... names)
    {
        return method_info<Fn, sizeof...(Names)>{ { names... } };
    }

    namespace detail
    {
        template<class E> struct is_member_info : std::false_type {};
        template<auto P, class... As> struct is_member_info<member_info<P, As...>> : std::true_type {};

        template<class E> struct is_method_info : std::false_type {};
        template<auto F, size_t N> struct is_method_info<method_info<F, N>> : std::true_type {};

        // describe 인자에서 종류별 부분 튜플을 뽑는다 (순서 보존).
        template<template<class> class Pred, class... Es>
        consteval auto pick(std::tuple<Es...> t)
        {
            return std::apply([](auto... e)
            {
                return std::tuple_cat([&]
                {
                    if constexpr (Pred<decltype(e)>::value) { return std::tuple{ e }; }
                    else { return std::tuple<>{}; }
                }()...);
            }, t);
        }
    }

    // 부모 표기 — describe의 첫 인자로만 온다.
    template<class Parent>
    struct base_tag {};

    template<class Parent>
    consteval base_tag<Parent> base()
    {
        return {};
    }

    // 타입 서술자 — meta::reflect<T>()의 산출물.
    template<class T, class Parent, class MembersTuple, class MethodsTuple>
    struct type_desc
    {
        using type = T;
        using parent = Parent;
        static constexpr std::string_view identifier = detail::type_name_holder<T>::view;
        static constexpr size_t member_count = std::tuple_size_v<MembersTuple>;
        static constexpr size_t method_count = std::tuple_size_v<MethodsTuple>;

        MembersTuple members{};
        MethodsTuple methods{};
    };

    template<class T, class Parent, class... Es>
    consteval auto describe(base_tag<Parent>, Es... es)
    {
        auto all = std::tuple<Es...>{ es... };
        auto ms = detail::pick<detail::is_member_info>(all);
        auto fs = detail::pick<detail::is_method_info>(all);
        return type_desc<T, Parent, decltype(ms), decltype(fs)>{ ms, fs };
    }

    template<class T, class... Es>
    consteval auto describe(Es... es)
    {
        auto all = std::tuple<Es...>{ es... };
        auto ms = detail::pick<detail::is_member_info>(all);
        auto fs = detail::pick<detail::is_method_info>(all);
        return type_desc<T, void, decltype(ms), decltype(fs)>{ ms, fs };
    }

    // 정의 단일화 — 컴파일타임 분기용 Meta::HasDescribe(ReflectionType.h)와
    // 같은 판별의 P2996풍 별칭이다.
    template<class T>
    concept reflectable = Meta::HasDescribe<T>;

    // ── 소비 표면 ──────────────────────────────────────────────────────────

    // ^^T 대응
    template<reflectable T>
    consteval auto reflect()
    {
        return T::describe();
    }

    // members_of(^^T) 대응
    template<reflectable T>
    consteval auto members_of()
    {
        return T::describe().members;
    }

    // identifier_of(m) 대응
    template<auto Ptr, class... Attrs>
    consteval std::string_view identifier_of(const member_info<Ptr, Attrs...>&)
    {
        return member_info<Ptr, Attrs...>::identifier;
    }

    // obj.[:m:] 스플라이스 대응 ① — 멤버 포인터 NTTP.
    template<auto Ptr, class Obj>
        requires std::is_member_object_pointer_v<decltype(Ptr)>
    constexpr decltype(auto) get(Obj&& obj)
    {
        return std::forward<Obj>(obj).*Ptr;
    }

    // 스플라이스 대응 ② — 서술자 값. (속성 튜플 탓에 member_info는 구조적
    // 타입이 아니라 NTTP로 못 들어간다 — 값 인자로 받는다.)
    template<auto Ptr, class... Attrs, class Obj>
    constexpr decltype(auto) get(const member_info<Ptr, Attrs...>&, Obj&& obj)
    {
        return std::forward<Obj>(obj).*Ptr;
    }

    // template for 대응 — 부모 서술 보유 시 부모 먼저(직렬화의 부모-우선 순회와
    // 동일 의미론). 레거시 부모(describe 없음)의 몫은 어댑터 Type::parent
    // 체인이 담당하므로 여기서는 건너뛴다.
    template<reflectable T, class F>
    constexpr void for_each_member(T& obj, F&& f)
    {
        static constexpr auto desc = T::describe();
        using Desc = std::remove_cv_t<decltype(desc)>;

        if constexpr (reflectable<typename Desc::parent>)
        {
            for_each_member(static_cast<typename Desc::parent&>(obj), f);
        }

        std::apply([&](const auto&... ms)
        {
            (f(ms.identifier, obj.*(ms.pointer)), ...);
        }, desc.members);
    }

    // 과도기 어댑터: 서술자 → 기존 Meta::Type 테이블 (CT7에서 소멸).
    template<reflectable T>
    const Meta::Type& adapt()
    {
        static constexpr auto desc = T::describe();
        using Desc = std::remove_cv_t<decltype(desc)>;
        static_assert(Desc::member_count > 0 || Desc::method_count > 0,
            "빈 서술은 지원하지 않는다 — 프로퍼티나 메서드가 하나는 있어야 한다");

        // MethodOnly 타입(UIButton 등)은 멤버 0이므로 빈 뷰를 허용한다.
        const Meta::View<const Meta::Property> propView = []() -> Meta::View<const Meta::Property>
        {
            if constexpr (Desc::member_count > 0)
            {
                static const auto arr = std::apply([](const auto&... ms)
                {
                    return std::to_array({ Meta::MakeProperty(ms.identifier.data(), ms.pointer)... });
                }, desc.members);
                return { arr };
            }
            else
            {
                return {};
            }
        }();

        const Meta::View<const Meta::Method> methodView = []() -> Meta::View<const Meta::Method>
        {
            if constexpr (Desc::method_count > 0)
            {
                static const auto arr = std::apply([](const auto&... fs)
                {
                    return std::to_array({ Meta::MakeMethod(fs.identifier.data(), fs.pointer,
                        std::vector<std::string>(fs.paramNames.begin(), fs.paramNames.end()))... });
                }, desc.methods);
                return { arr };
            }
            else
            {
                return {};
            }
        }();

        const Meta::Type* parent = nullptr;
        if constexpr (!std::is_void_v<typename Desc::parent>)
        {
            // TypeOf 경유 — 부모가 레거시(Reflect)든 신형(describe)이든 무관.
            parent = &Meta::TypeOf<typename Desc::parent>();
        }

        static const Meta::Type type{ Desc::identifier.data(), propView, methodView, parent,
            TypeTrait::GUIDCreator::GetTypeID<T>() };
        return type;
    }

}

namespace Meta
{
    // 런타임 Type 테이블의 단일 창구 (선언은 ReflectionFunction.h — Register가
    // 쓴다). 타입 쪽 보일러플레이트 0: 신형은 describe()만 선언하면 되고,
    // Reflect() 멤버는 아무 타입에도 필요 없다. CT7에서 else 가지(레거시)가
    // 죽으면 이 함수는 meta::adapt의 별명이 된다.
    template<class T>
    const Type& TypeOf()
    {
        if constexpr (::meta::reflectable<T>)
        {
            return ::meta::adapt<T>();
        }
        else
        {
            return T::Reflect();
        }
    }
}

namespace meta
{
    namespace detail::selftest
    {
        struct Canary
        {
            int m_value;
            float m_ranged;

            void Fire(int shots) { m_value = shots; }
            bool IsEmpty() { return m_value == 0; }

            static consteval auto describe()
            {
                return meta::describe<Canary>(
                    meta::member<&Canary::m_value>(),
                    meta::member<&Canary::m_ranged>(
                        meta::range(0.0f, 1.0f),
                        meta::displayName("Ranged")),
                    meta::method<&Canary::Fire>("shots"),
                    meta::method<&Canary::IsEmpty>());
            }
        };

        // __FUNCSIG__ 표기 카나리아 — 여기가 깨지면 member_name_raw의 파서를
        // 새 표기에 맞춰 갱신해야 한다 (계획 CT4 함정 4).
        static_assert(member_name_raw<&Canary::m_value>() == "m_value",
            "MSVC 멤버 포인터 NTTP __FUNCSIG__ 표기가 변했다 — member_name_raw 갱신 필요");
        // 타입 이름은 **한정 이름**이다(네임스페이스 포함) — Canary는 selftest
        // 네임스페이스 안이라 이렇게 나온다. 전역 타입(MeshRenderer 등)은 비한정.
        static_assert(decltype(Canary::describe())::identifier
            == "meta::detail::selftest::Canary");
        static_assert(decltype(Canary::describe())::member_count == 2);
        static_assert(decltype(Canary::describe())::method_count == 2);
        // 멤버 함수 시그니처 표기 카나리아 — 인자 유/무 두 형태를 다 지킨다.
        static_assert(method_name_raw<&Canary::Fire>() == "Fire",
            "MSVC 멤버 함수 NTTP __FUNCSIG__ 표기가 변했다 — method_name_raw 갱신 필요");
        static_assert(method_name_raw<&Canary::IsEmpty>() == "IsEmpty");
        static_assert(std::get<0>(Canary::describe().methods).identifier == "Fire");
        static_assert(std::get<1>(Canary::describe().members)
            .has_attribute<range_attr<float>>());
        static_assert(std::get<1>(Canary::describe().members)
            .attribute<range_attr<float>>().max == 1.0f);
        static_assert(!std::get<0>(Canary::describe().members)
            .has_attribute<display_name_attr>());
    }
}
