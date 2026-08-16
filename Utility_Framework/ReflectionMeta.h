#pragma once
// 컴파일타임 리플렉션 코어 (PHASE 18 CT4) — P2996(C++26 표준 리플렉션) 유사 표면.
//
// 매크로 없이, 표준에 도입 예정인 문법의 라이브러리 근사로 표기한다
// (사용자 결정 2026-08-17). 대응표:
//
//   P2996                     여기 (C++20 라이브러리 근사)
//   ─────────────────────     ─────────────────────────────────────
//   ^^T                       meta::reflect<T>()          (타입 서술자)
//   ^^T::m                    &T::m                       (멤버 포인터가 리플렉션 값)
//   obj.[:m:]                 meta::get<m>(obj)           (스플라이스)
//   members_of(^^T)           meta::members_of<T>()
//   identifier_of(m)          meta::identifier_of(m)
//   template for              meta::for_each_member(obj, f)
//
// 타입 선언부는 매크로 없이 두 함수만 쓴다 (파일럿 4타입 참조):
//
//   static consteval auto reflect()
//   {
//       return meta::describe<MeshRenderer, Component,   // 부모 없으면 void
//           &MeshRenderer::m_Material,
//           &MeshRenderer::m_Mesh>{};
//   }
//   static const Meta::Type& Reflect() { return meta::adapt<MeshRenderer>(); }
//
// 설계 제약 (ReflectionRedesignPlan CT4):
//   - reflect()는 static 데이터가 아니라 **함수** — 클래스 상단(멤버 선언 앞)
//     배치를 허용하려면 멤버 포인터 형성이 complete-class 문맥이어야 한다.
//   - 멤버 이름은 __FUNCSIG__의 멤버 포인터 NTTP 표기에서 추출한다. VS 18
//     실측: "void __cdecl f<&Probe::m_value>(const char *)" — 아래 selftest의
//     static_assert가 이 표기의 카나리아다(툴체인 업그레이드 시 최우선 확인).
//   - adapt()는 기존 Meta::Type 테이블로의 과도기 다리(소비자 무변경)이며
//     CT7에서 소멸한다. MakeProperty를 재사용하므로 산출은 generated.h 경로와
//     바이트 동등 — 골든 diff 0이 그 증명.
//   - P2996이 MSVC에 오면 reflect()/describe 선언부가 컴파일러 제공으로 접히고
//     소비 표면(get/members_of/for_each_member)은 유지된다.
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

    // 멤버 서술자 — 멤버 포인터 NTTP가 곧 정체성이다. 구조적 타입(빈 클래스)
    // 이므로 constexpr 값으로 들고 다니며 meta::get<m>(obj)에 넘길 수 있다.
    template<auto Ptr>
    struct member_info
    {
        static constexpr auto pointer = Ptr;
        static constexpr std::string_view identifier = detail::member_name_holder<Ptr>::view;
    };

    // 타입 서술자 — meta::reflect<T>()의 산출물. Parent는 없으면 void.
    template<class T, class Parent, auto... Ptrs>
    struct type_desc
    {
        using type = T;
        using parent = Parent;
        static constexpr std::string_view identifier = detail::type_name_holder<T>::view;
        static constexpr std::tuple<member_info<Ptrs>...> members{};
        static constexpr size_t member_count = sizeof...(Ptrs);
    };

    template<class T, class Parent, auto... Ptrs>
    using describe = type_desc<T, Parent, Ptrs...>;

    template<class T>
    concept reflectable = requires { T::reflect(); };

    // ^^T 대응
    template<reflectable T>
    consteval auto reflect()
    {
        return T::reflect();
    }

    // members_of(^^T) 대응
    template<reflectable T>
    consteval auto members_of()
    {
        return decltype(T::reflect())::members;
    }

    // identifier_of(m) 대응
    template<auto Ptr>
    consteval std::string_view identifier_of(member_info<Ptr>)
    {
        return member_info<Ptr>::identifier;
    }

    // obj.[:m:] 스플라이스 대응 — m은 member_info 값이든 생 멤버 포인터든 좋다.
    template<auto M, class Obj>
    constexpr decltype(auto) get(Obj&& obj)
    {
        if constexpr (std::is_member_object_pointer_v<decltype(M)>)
        {
            return std::forward<Obj>(obj).*M;
        }
        else
        {
            return std::forward<Obj>(obj).*(M.pointer);
        }
    }

    // template for 대응 — 부모 서술 보유 시 부모 먼저(직렬화의 부모-우선 순회와
    // 동일 의미론). 레거시 부모(reflect 없음)의 몫은 어댑터 Type::parent 체인이
    // 담당하므로 여기서는 건너뛴다.
    template<reflectable T, class F>
    constexpr void for_each_member(T& obj, F&& f)
    {
        constexpr auto desc = T::reflect();
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
        static constexpr auto desc = T::reflect();
        using Desc = std::remove_cv_t<decltype(desc)>;
        static_assert(Desc::member_count > 0, "빈 멤버 서술은 아직 지원하지 않는다");

        static const auto properties = std::apply([](const auto&... ms)
        {
            return std::to_array({ Meta::MakeProperty(ms.identifier.data(), ms.pointer)... });
        }, desc.members);

        const Meta::Type* parent = nullptr;
        if constexpr (!std::is_void_v<typename Desc::parent>)
        {
            parent = &Desc::parent::Reflect();
        }

        static const Meta::Type type{ Desc::identifier.data(), properties, {}, parent,
            TypeTrait::GUIDCreator::GetTypeID<T>() };
        return type;
    }

    namespace detail::selftest
    {
        struct Canary { int m_value; };
        // __FUNCSIG__ 표기 카나리아 — 여기가 깨지면 member_name_raw의 파서를
        // 새 표기에 맞춰 갱신해야 한다 (계획 CT4 함정 4).
        static_assert(member_name_raw<&Canary::m_value>() == "m_value",
            "MSVC 멤버 포인터 NTTP __FUNCSIG__ 표기가 변했다 — member_name_raw 갱신 필요");
        static_assert(member_info<&Canary::m_value>::identifier == "m_value");
        // 타입 이름은 **한정 이름**이다(네임스페이스 포함) — Canary는 selftest
        // 네임스페이스 안이라 이렇게 나온다. 전역 타입(MeshRenderer 등)은 비한정.
        static_assert(type_desc<Canary, void, &Canary::m_value>::identifier
            == "meta::detail::selftest::Canary");
    }
}
