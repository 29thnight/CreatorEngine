#pragma once
// meta 스키마 코어 (PHASE 18 CT9) — 엔진 비의존 순수 계층.
//
// 이 헤더는 std만 의존한다(엔진 include 0) — 별도 라이브러리로 떼어낼 수 있는
// 경계가 이 파일이다. 엔진 결합(정체성 스탬핑 identity·Meta::Type 어댑터·
// selftest 교차 검증)은 ReflectionMeta.h가 이 위에 얹는다.
//
// 모델 (사용자 확정 2026-08-17):
//   - 클래스 선언이 상속 관계의 **유일한 원본**이다: meta::identity<T, Base>가
//     identity_descriptor를 공개하고, schema<T>는 그것으로 부모를 자동 추론한다.
//     서술식에 base<> 표기는 존재하지 않는다.
//   - reflect()는 **로컬 스키마**만 적는다: 이 타입이 직접 선언한 필드·메서드.
//     상속 합성은 질의(fields<T>)가 계산한다 — 데이터에는 direct base만 있다.
//   - 소비는 consteval 질의(meta::reflect<T>() 등)로 한다. 런타임 소비자
//     (직렬화·인스펙터·어댑터)를 위한 물질화는 schema_of<T> 변수 템플릿
//     **한 곳**이다(타입당 canonical 단일 주소 — CT8 계약 승계).
//
//   타입 선언부:
//
//     class BoxColliderComponent
//         : public meta::identity<BoxColliderComponent, Component>
//     {
//     public:
//         static consteval auto reflect()
//         {
//             return meta::schema<Self>(
//                 meta::field<&Self::m_boxExtent>,
//                 meta::field<&Self::staticFriction>
//                     .with(meta::range(0.0f, 1.0f)),
//                 meta::field<&Self::density>);
//         }
//     };
//
//   침투가 곤란한 타입은 외부 서술로:
//
//     template<> struct meta::of<ThirdPartyThing> { static constexpr auto value = ...; };
//
// 설계 제약:
//   - 이름은 __FUNCSIG__의 NTTP 표기에서 추출한다(VS 18 실측 표기 — selftest의
//     카나리아 static_assert가 표기 변화를 감지한다. ReflectionMeta.h 참조).
//   - 속성 튜플 탓에 field_info는 구조적 타입이 아니다 — NTTP 스플라이스는
//     포인터(get<&T::m>)로, 서술자 값은 get(f, obj)로.
//   - P2996이 오면 reflect() 선언부가 컴파일러 제공(^^T)으로 접히고 질의
//     표면(fields/localFields/for_each_field)은 유지된다.
#include <tuple>
#include <array>
#include <string_view>
#include <type_traits>
#include <cstddef>
#include <limits>

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
        // — 이름은 인자 여는 괄호 직전의 마지막 "::" 뒤 구간이다.
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

        // 타입 이름 — TypeTrait::type_name과 **동일 알고리즘**(class/struct/enum
        // 접두 1회 제거, 한정 이름 유지). 라이브러리 독립성 때문에 복제하며,
        // 두 구현의 동일 출력은 ReflectionMeta.h selftest가 교차 검증한다.
        template<class T>
        consteval std::string_view type_name()
        {
            std::string_view sig = __FUNCSIG__;
            constexpr std::string_view marker = "type_name<";
            const size_t start = sig.find(marker) + marker.size();
            const size_t end = sig.rfind(">(");
            std::string_view name = sig.substr(start, end - start);

            for (std::string_view prefix : { std::string_view("class "),
                std::string_view("struct "), std::string_view("enum ") })
            {
                if (name.starts_with(prefix))
                {
                    name.remove_prefix(prefix.size());
                    break;
                }
            }
            return name;
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
                for (size_t i = 0; i < raw.size(); ++i) { a[i] = raw[i]; }
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
                for (size_t i = 0; i < raw.size(); ++i) { a[i] = raw[i]; }
                return a;
            }();
            static constexpr std::string_view view{ storage.data(), raw.size() };
        };

        template<class T>
        struct type_name_holder
        {
            static constexpr std::string_view raw = type_name<T>();
            static constexpr auto storage = []
            {
                std::array<char, raw.size() + 1> a{};
                for (size_t i = 0; i < raw.size(); ++i) { a[i] = raw[i]; }
                return a;
            }();
            static constexpr std::string_view view{ storage.data(), raw.size() };
        };

        // PTM에서 선언 주체를 뽑는다 — 로컬 스키마 규약(자기 멤버만 적는다)상
        // owner = 선언 클래스다. 인스펙터 그루핑(declaringType)이 소비한다.
        template<class P> struct member_pointer_traits;
        template<class V, class C> struct member_pointer_traits<V C::*>
        {
            using owner = C;
            using value = V;
        };
    }

    // ── 멤버 속성 (annotations_of 대응) ────────────────────────────────────

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

    struct units_attr
    {
        std::string_view value;
    };

    consteval units_attr units(std::string_view v)
    {
        return { v };
    }

    // ── 서술자 ─────────────────────────────────────────────────────────────

    // 필드 서술자 — 정체성(포인터·이름·선언 주체)은 static, 속성은 값으로 든다.
    template<auto Ptr, class... Attrs>
    struct field_info
    {
        static constexpr auto pointer = Ptr;
        static constexpr std::string_view identifier = detail::member_name_holder<Ptr>::view;
        using owner_type = typename detail::member_pointer_traits<decltype(Ptr)>::owner;

        std::tuple<Attrs...> attributes{};

        template<class A>
        static consteval bool has_attribute()
        {
            return (std::is_same_v<A, Attrs> || ...);
        }

        // constexpr(비-consteval) — 어댑터가 런타임 참조로도 읽는다(CT6-b).
        template<class A>
        constexpr A attribute() const
        {
            return std::get<A>(attributes);
        }

        template<class Obj>
        constexpr decltype(auto) get(Obj&& obj) const
        {
            return std::forward<Obj>(obj).*Ptr;
        }

        // 속성 부착 — 가변이라 한 번의 with로 전부 단다:
        //   meta::field<&Self::hp>.with(meta::range(0, 100), meta::units("HP"))
        template<class... More>
        consteval auto with(More... more) const
        {
            return field_info<Ptr, Attrs..., More...>{
                std::tuple_cat(attributes, std::tuple<More...>{ more... }) };
        }
    };

    // 서술식의 필드 항 — 값이라 괄호가 없다: meta::field<&Self::x>
    template<auto Ptr>
    inline constexpr field_info<Ptr> field{};

    // 메서드 서술자 — 파라미터 이름은 선택 표기: meta::method<&T::Fire>.params("shots")
    template<auto Fn, size_t NParams>
    struct method_info
    {
        static constexpr auto pointer = Fn;
        static constexpr std::string_view identifier = detail::method_name_holder<Fn>::view;

        std::array<const char*, NParams> paramNames{};

        template<class... Names>
        consteval auto params(Names... names) const
        {
            static_assert(NParams == 0, "params()는 한 번만 부른다");
            return method_info<Fn, sizeof...(Names)>{ { names... } };
        }
    };

    template<auto Fn>
    inline constexpr method_info<Fn, 0> method{};

    namespace detail
    {
        template<class E> struct is_field_info : std::false_type {};
        template<auto P, class... As> struct is_field_info<field_info<P, As...>> : std::true_type {};

        template<class E> struct is_method_info : std::false_type {};
        template<auto F, size_t N> struct is_method_info<method_info<F, N>> : std::true_type {};

        // schema 인자에서 종류별 부분 튜플을 뽑는다 (순서 보존).
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

    // ── 상속 선언 원본 ─────────────────────────────────────────────────────

    struct no_base {};

    // 클래스 선언(meta::identity<D, B> 상속)이 공개하는 상속 서술자 —
    // schema<T>의 부모 자동 추론이 읽는 유일한 원본이다. identity를 못 쓰는
    // 타입(다중 상속 루트 Entity 등)은 이 별칭을 직접 선언한다:
    //   public: using meta_identity = meta::identity_descriptor<Entity, Object>;
    template<class Derived, class Base>
    struct identity_descriptor
    {
        using type = Derived;
        using base_type = Base;
        static constexpr bool has_base = !std::is_same_v<Base, no_base>;
    };

    // ── 타입 스키마 ────────────────────────────────────────────────────────

    // 로컬 스키마 — 이 타입이 **직접 선언한** 필드·메서드와 direct base만 담는다.
    // 전체 체인은 질의(fields<T>/bases<T>)가 계산한다.
    template<class T, class Base, class FieldsTuple, class MethodsTuple>
    struct type_schema
    {
        using type = T;
        using base_type = Base;
        static constexpr bool has_base = !std::is_same_v<Base, no_base>;
        static constexpr std::string_view identifier = detail::type_name_holder<T>::view;
        static constexpr size_t field_count = std::tuple_size_v<FieldsTuple>;
        static constexpr size_t method_count = std::tuple_size_v<MethodsTuple>;

        FieldsTuple fields{};
        MethodsTuple methods{};
    };

    namespace detail
    {
        template<class T>
        concept declares_identity = requires { typename T::meta_identity; };
    }

    template<class T, class... Entries>
    consteval auto schema(Entries... entries)
    {
        auto all = std::tuple<Entries...>{ entries... };
        auto fs = detail::pick<detail::is_field_info>(all);
        auto ms = detail::pick<detail::is_method_info>(all);

        if constexpr (detail::declares_identity<T>)
        {
            using Id = typename T::meta_identity;
            // 상속받은 meta_identity(자기 identity 선언 누락)는 조용한 부모
            // 소실이 아니라 즉사여야 한다 — 엔진 리플렉션은 단일 상속 계약이다.
            static_assert(std::is_same_v<typename Id::type, T>,
                "meta_identity 불일치 — 이 타입은 자기 identity 선언 없이 조상 것을 상속받고 있다. "
                "meta::identity<T, Base> 상속 또는 meta_identity 별칭을 직접 선언하라.");
            return type_schema<T, typename Id::base_type, decltype(fs), decltype(ms)>{ fs, ms };
        }
        else
        {
            return type_schema<T, no_base, decltype(fs), decltype(ms)>{ fs, ms };
        }
    }

    // ── 서술 공급원 이중화: in-class 레시피 / 외부 특수화 ──────────────────

    // 침투 불가 타입의 외부 서술 훅 — 특수화가 static constexpr value를 든다.
    template<class T>
    struct of;

    namespace detail
    {
        template<class T> concept has_local_recipe = requires { T::reflect(); };
        template<class T> concept has_external_recipe = requires { of<T>::value; };
    }

    template<class T>
    concept reflectable = detail::has_local_recipe<T> || detail::has_external_recipe<T>;

    // ^^T 대응 — 공급원 무관 단일 창구. 소비자는 T::reflect()를 직접 읽지 않는다.
    template<reflectable T>
    consteval auto reflect()
    {
        static_assert(!(detail::has_local_recipe<T> && detail::has_external_recipe<T>),
            "이중 서술 금지 — in-class reflect()와 meta::of<T>가 동시에 존재한다");
        if constexpr (detail::has_local_recipe<T>) { return T::reflect(); }
        else { return of<T>::value; }
    }

    // 런타임 소비자용 canonical 물질화 — 타입당 프로그램 전역 단 하나
    // (inline 변수 템플릿·상수 초기화). consteval 질의는 값 모델이지만,
    // 직렬화·인스펙터처럼 런타임에 튜플을 도는 소비자는 전부 여기를 참조로
    // 읽는다(CT8의 canonical 주소 계약 승계 — 소비처별 정적 사본 금지).
    template<reflectable T>
    inline constexpr auto schema_of = reflect<T>();

    // ── 질의 표면 (상속 합성은 데이터가 아니라 여기서 계산한다) ────────────

    template<reflectable T>
    consteval auto localFields()
    {
        return reflect<T>().fields;
    }

    template<reflectable T>
    consteval auto fields()
    {
        using S = std::remove_cvref_t<decltype(reflect<T>())>;
        if constexpr (S::has_base && reflectable<typename S::base_type>)
        {
            return std::tuple_cat(fields<typename S::base_type>(), localFields<T>());
        }
        else
        {
            return localFields<T>();
        }
    }

    template<reflectable T>
    consteval auto localMethods()
    {
        return reflect<T>().methods;
    }

    template<reflectable T>
    consteval auto methods()
    {
        using S = std::remove_cvref_t<decltype(reflect<T>())>;
        if constexpr (S::has_base && reflectable<typename S::base_type>)
        {
            return std::tuple_cat(methods<typename S::base_type>(), localMethods<T>());
        }
        else
        {
            return localMethods<T>();
        }
    }

    template<class... Ts>
    struct type_list {};

    namespace detail
    {
        template<class... As, class... Bs>
        consteval type_list<As..., Bs...> concat(type_list<As...>, type_list<Bs...>)
        {
            return {};
        }
    }

    template<reflectable T>
    using direct_base_t = typename std::remove_cvref_t<decltype(reflect<T>())>::base_type;

    // base chain — 데이터에는 direct base만 있고 전체 사슬은 여기서 계산한다.
    template<reflectable T>
    consteval auto bases()
    {
        using S = std::remove_cvref_t<decltype(reflect<T>())>;
        if constexpr (S::has_base && reflectable<typename S::base_type>)
        {
            return detail::concat(type_list<typename S::base_type>{},
                bases<typename S::base_type>());
        }
        else
        {
            return type_list<>{};
        }
    }

    // identifier_of(m) 대응
    template<auto Ptr, class... Attrs>
    consteval std::string_view identifier_of(const field_info<Ptr, Attrs...>&)
    {
        return field_info<Ptr, Attrs...>::identifier;
    }

    template<class T>
    consteval std::string_view type_name_of()
    {
        return detail::type_name_holder<T>::view;
    }

    // 인스펙터 그루핑용 — 합성(fields<T>) 뒤에도 출신 클래스를 안다.
    template<auto Ptr, class... Attrs>
    consteval std::string_view declaringType(const field_info<Ptr, Attrs...>&)
    {
        return type_name_of<typename field_info<Ptr, Attrs...>::owner_type>();
    }

    // obj.[:m:] 스플라이스 대응 ① — 멤버 포인터 NTTP.
    template<auto Ptr, class Obj>
        requires std::is_member_object_pointer_v<decltype(Ptr)>
    constexpr decltype(auto) get(Obj&& obj)
    {
        return std::forward<Obj>(obj).*Ptr;
    }

    // 스플라이스 대응 ② — 서술자 값. (속성 튜플 탓에 field_info는 구조적
    // 타입이 아니라 NTTP로 못 들어간다 — 값 인자로 받는다.)
    template<auto Ptr, class... Attrs, class Obj>
    constexpr decltype(auto) get(const field_info<Ptr, Attrs...>&, Obj&& obj)
    {
        return std::forward<Obj>(obj).*Ptr;
    }

    // ── 열거형 (외부 라이브러리 비의존 — magic_enum 대체) ──────────────────
    // 같은 원리의 자급 구현: 후보 값 범위를 __FUNCSIG__ NTTP 표기로 스캔한다.
    // 유효 열거자는 "LightType::Directional"(비스코프드는 무자격일 수 있음),
    // 무효 값은 "(enum LightType)0x5" 캐스트 표기 — '(' 존재가 판별자다.
    // 범위는 magic_enum 기본과 동일([-128, 128], 부호 없는 기저는 [0, 128],
    // 기저 타입 한계로 클램프)이라 산출 집합·순서(값 오름차순)가 동일하다 —
    // 콘솔 enum 설정·인스펙터 콤보의 파리티 전제.

    namespace detail
    {
        template<auto V>
        consteval std::string_view enum_entry_raw()
        {
            std::string_view sig = __FUNCSIG__;
            constexpr std::string_view marker = "enum_entry_raw<";
            const size_t begin = sig.find(marker) + marker.size();
            std::string_view inner = sig.substr(begin, sig.rfind(">(") - begin);
            if (inner.find('(') != std::string_view::npos)
            {
                return {};
            }
            const size_t colons = inner.rfind("::");
            return colons == std::string_view::npos ? inner : inner.substr(colons + 2);
        }

        template<auto V>
        struct enum_name_holder
        {
            static constexpr std::string_view raw = enum_entry_raw<V>();
            static constexpr auto storage = []
            {
                std::array<char, raw.size() + 1> a{};
                for (size_t i = 0; i < raw.size(); ++i) { a[i] = raw[i]; }
                return a;
            }();
            static constexpr std::string_view view{ storage.data(), raw.size() };
        };

        template<class E>
        consteval long long enum_scan_lo()
        {
            using U = std::underlying_type_t<E>;
            if constexpr (std::is_signed_v<U>)
            {
                constexpr long long umin = (long long)(std::numeric_limits<U>::min)();
                return umin < -128 ? -128 : umin;
            }
            else
            {
                return 0;
            }
        }

        template<class E>
        consteval long long enum_scan_hi()
        {
            using U = std::underlying_type_t<E>;
            constexpr unsigned long long umax = (unsigned long long)(std::numeric_limits<U>::max)();
            return umax > 128ull ? 128ll : (long long)umax;
        }

        template<class E, long long I>
        consteval bool enum_valid()
        {
            return !enum_entry_raw<static_cast<E>(I)>().empty();
        }

        template<class E, long long Lo, size_t... Is>
        consteval size_t enum_count_impl(std::index_sequence<Is...>)
        {
            return (size_t(enum_valid<E, Lo + (long long)Is>()) + ... + size_t(0));
        }
    }

    template<class E>
    struct enum_entry
    {
        std::string_view name;   // NUL 종단 보장(정적 물질화) — .data() 소비 가능
        E value;
    };

    template<class E>
    consteval size_t enum_count()
    {
        constexpr long long lo = detail::enum_scan_lo<E>();
        constexpr long long hi = detail::enum_scan_hi<E>();
        return detail::enum_count_impl<E, lo>(
            std::make_index_sequence<size_t(hi - lo + 1)>{});
    }

    namespace detail
    {
        template<class E, long long Lo, size_t N, size_t... Is>
        consteval auto enum_entries_impl(std::index_sequence<Is...>)
        {
            std::array<enum_entry<E>, N> out{};
            size_t k = 0;
            ([&]
            {
                constexpr long long I = Lo + (long long)Is;
                if constexpr (enum_valid<E, I>())
                {
                    out[k] = enum_entry<E>{
                        enum_name_holder<static_cast<E>(I)>::view,
                        static_cast<E>(I) };
                    ++k;
                }
            }(), ...);
            return out;
        }
    }

    // 열거자 표 — 값 오름차순, 타입당 canonical 하나(inline 변수 템플릿).
    template<class E>
        requires std::is_enum_v<E>
    inline constexpr auto enum_entries = detail::enum_entries_impl<
        E, detail::enum_scan_lo<E>(), enum_count<E>()>(
        std::make_index_sequence<size_t(
            detail::enum_scan_hi<E>() - detail::enum_scan_lo<E>() + 1)>{});

    template<class E>
        requires std::is_enum_v<E>
    constexpr std::string_view enum_name(E v)
    {
        for (const auto& e : enum_entries<E>)
        {
            if (e.value == v) { return e.name; }
        }
        return {};
    }

    // template for 대응 — 부모 우선(직렬화의 부모-우선 순회와 동일 의미론).
    // 서술 없는 부모의 몫은 소비자 측(어댑터 Type::parent 체인)이 담당한다.
    // 런타임 순회이므로 canonical 물질화(schema_of)를 참조로 읽는다.
    template<reflectable T, class F>
    constexpr void for_each_field(T& obj, F&& f)
    {
        using S = std::remove_cvref_t<decltype(schema_of<T>)>;

        if constexpr (S::has_base && reflectable<typename S::base_type>)
        {
            for_each_field(static_cast<typename S::base_type&>(obj), f);
        }

        std::apply([&](const auto&... fs)
        {
            (f(fs.identifier, obj.*(fs.pointer)), ...);
        }, schema_of<T>.fields);
    }
}
