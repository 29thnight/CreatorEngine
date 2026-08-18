#pragma once
// 리플렉션 엔진 글루 (PHASE 18 CT9) — 순수 스키마 코어(MetaSchema.h) 위의
// 엔진 결합 계층. 여기에만 엔진 의존(TypeTrait·Meta::Type·Property)이 있다.
//
//   MetaSchema.h  : schema/field/method/attrs/of/reflect/질의 — std 전용(독립 가능)
//   이 파일       : identity(정체성 스탬핑 + 상속 서술자 공개) ·
//                   서술자→Meta::Type 어댑터(adapt) · Meta::TypeOf · selftest
//
// 타입 선언부는 MetaSchema.h 상단 문서 참조 — reflect() 레시피가 로컬 스키마만
// 적고, 부모는 클래스 선언(meta::identity<T, Base>)에서 자동 추론된다.
#include "ReflectionFunction.h"
#include "MetaSchema.h"
#include <tuple>
#include <array>
#include <string_view>
#include <type_traits>

namespace meta
{
    // CRTP 정체성 베이스 (CT7-b → CT9 확장) — 상속 선언 자체가 두 가지를 진다:
    //
    //   ① m_name·m_typeID 스탬핑(생성 순서 계약: 베이스 우선·말단 최종 덮어쓰기)
    //   ② 상속 서술자(meta_identity) 공개 — schema<T>의 부모 자동 추론 원본.
    //      base<> 표기는 소멸했다: 상속 관계의 유일한 원본은 클래스 선언이다.
    //
    // Self는 protected로 공급된다 — reflect() 본문이 &Self::member로 쓴다.
    // identity를 못 쓰는 타입(다중 상속 루트 GameObject·Prefab, 무상속 struct)은
    // 국소 using Self와(부모가 있으면) meta_identity 별칭을 직접 선언한다.
    //
    // 주의: 베이스가 X의 구체 특수화(identity<X, P>)라 의존 베이스 2단계
    // 조회 문제는 없고, 중간 베이스 체인에서도 말단의 Self/meta_identity가
    // 지배 규칙으로 이긴다. 자기 identity 없이 조상 것을 상속하면 schema<T>의
    // static_assert가 즉사시킨다(조용한 부모 소실 방지).
    template<class T, class Base = no_base>
    class identity : public Base
    {
    public:
        using meta_identity = identity_descriptor<T, Base>;

    protected:
        using Self = T;

        identity()
        {
            StampIdentity();
        }

        template<class... Args>
        explicit identity(Args&&... args) : Base(std::forward<Args>(args)...)
        {
            StampIdentity();
        }

    private:
        void StampIdentity()
        {
            this->m_name = detail::type_name_holder<T>::view.data();
            this->m_typeID = TypeTrait::GUIDCreator::GetTypeID<T>();
        }
    };

    // 루트 특수화 — 스탬핑 없이 별칭만 공급한다(스탬핑할 상속 멤버가 없다).
    // 인스턴스 이름 의미론(GameObject류)·다중 상속 루트는 이걸 쓰지 않고
    // 수동 별칭을 유지한다.
    template<class T>
    class identity<T, no_base>
    {
    public:
        using meta_identity = identity_descriptor<T, no_base>;

    protected:
        using Self = T;
    };

    // 필드 속성을 레거시 Property로 투영한다 (CT6-b) — 인스펙터가 hasRange/
    // displayName을 소비한다(직렬화는 안 읽으므로 골든 무영향).
    template<auto P, class... As>
    inline Meta::Property BuildPropertyFrom(const field_info<P, As...>& fi)
    {
        using FI = field_info<P, As...>;
        Meta::Property prop = Meta::MakeProperty(FI::identifier.data(), FI::pointer);

        if constexpr (FI::template has_attribute<range_attr<float>>())
        {
            const auto r = fi.template attribute<range_attr<float>>();
            prop.hasRange = true;
            prop.rangeMin = r.min;
            prop.rangeMax = r.max;
        }
        if constexpr (FI::template has_attribute<display_name_attr>())
        {
            prop.displayName = fi.template attribute<display_name_attr>().value.data();
        }
        return prop;
    }

    // 어댑터: 로컬 스키마 → 기존 Meta::Type 테이블 — 이름 기반 소비자의 정본
    // 공급원(CT7 재정의). Property/Method는 **로컬** 필드·메서드만 싣고 부모는
    // Type::parent 체인이 잇는다(레거시 파리티). 컴파일타임 서술자는 canonical
    // 물질화(schema_of<T>)만 읽는다.
    template<reflectable T>
    const Meta::Type& adapt()
    {
        using S = std::remove_cvref_t<decltype(schema_of<T>)>;
        static_assert(S::field_count > 0 || S::method_count > 0,
            "빈 서술은 지원하지 않는다 — 필드나 메서드가 하나는 있어야 한다");

        // MethodOnly 타입(UIButton 등)은 필드 0이므로 빈 뷰를 허용한다.
        // 람다는 canonical 객체(schema_of)를 캡처 없이 직접 읽는다.
        const Meta::View<const Meta::Property> propView = []() -> Meta::View<const Meta::Property>
        {
            if constexpr (S::field_count > 0)
            {
                static const auto arr = std::apply([](const auto&... fs)
                {
                    return std::to_array({ BuildPropertyFrom(fs)... });
                }, schema_of<T>.fields);
                return { arr };
            }
            else
            {
                return {};
            }
        }();

        const Meta::View<const Meta::Method> methodView = []() -> Meta::View<const Meta::Method>
        {
            if constexpr (S::method_count > 0)
            {
                static const auto arr = std::apply([](const auto&... fs)
                {
                    return std::to_array({ Meta::MakeMethod(fs.identifier.data(), fs.pointer,
                        std::vector<std::string>(fs.paramNames.begin(), fs.paramNames.end()))... });
                }, schema_of<T>.methods);
                return { arr };
            }
            else
            {
                return {};
            }
        }();

        const Meta::Type* parent = nullptr;
        if constexpr (S::has_base)
        {
            // TypeOf 경유 — 부모 런타임 Type도 단일 창구로 얻는다.
            parent = &Meta::TypeOf<typename S::base_type>();
        }

        // CT11: 팩토리를 Type이 직접 든다(무상태 람다 → 함수 포인터).
        // meta::polymorphic 파생만 shared 경로 — 구 FactoryRegistry 규약 그대로.
        Meta::CreateFn createFn = []() -> void* { return new T(); };
        Meta::CreateSharedFn createSharedFn = nullptr;
        Meta::CreateUniqueFn createUniqueFn = nullptr;
        if constexpr (std::is_base_of_v<meta::polymorphic, T>)
        {
            createSharedFn = []() -> std::shared_ptr<void> { return std::make_shared<T>(); };

            // K2 스테이지 A: std::make_unique<T>()로 만든 뒤 release()해 소유권을
            // void* 삭제자 쌍으로 옮긴다 — 삭제자가 T를 캡처하므로 소비 측이
            // static_cast<Component*>로 내린 뒤에도(가상 소멸자 체인, meta::polymorphic
            // 규약) 올바른 파생 타입으로 delete된다. createShared가 shared_ptr<T>
            // → shared_ptr<void> 암묵 변환으로 하는 일과 같은 원리다.
            createUniqueFn = []() -> std::unique_ptr<void, void(*)(void*)>
            {
                void* raw = std::make_unique<T>().release();
                return std::unique_ptr<void, void(*)(void*)>(raw, [](void* p) { delete static_cast<T*>(p); });
            };
        }

        static const Meta::Type type{ std::string(S::identifier), propView, methodView, parent,
            TypeTrait::GUIDCreator::GetTypeID<T>(), createFn, createSharedFn, createUniqueFn };
        return type;
    }
}

namespace Meta
{
    // 런타임 Type 테이블의 단일 창구 (선언은 ReflectionFunction.h — Register가
    // 쓴다). 타입 쪽 보일러플레이트는 reflect() 레시피 하나다. 레거시
    // T::Reflect() 폴백은 정의 잔존 0건 증명 후 제거됨(CT8) — meta::adapt의
    // 별명이다.
    template<class T>
    const Type& TypeOf()
    {
        return ::meta::adapt<T>();
    }
}

namespace meta
{
    namespace detail::selftest
    {
        // CT9 카나리아 — 코퍼스의 대표 유형을 지킨다:
        //   Canary            루트 identity(별칭 전용 특수화) + 속성 + 파라미터 메서드
        //   CanaryChild       수동 meta_identity 별칭(비-identity 경로) + private 필드
        //   CanaryMethodsOnly 메서드 전용(UIButton류)
        //   ExternalCanary    외부 서술(meta::of — 침투 0)
        struct Canary : meta::identity<Canary>
        {
            int m_value;
            float m_ranged;

            void Fire(int shots) { m_value = shots; }
            bool IsEmpty() { return m_value == 0; }

            static consteval auto reflect()
            {
                return meta::schema<Self>(
                    meta::field<&Self::m_value>,
                    meta::field<&Self::m_ranged>
                        .with(meta::range(0.0f, 1.0f),
                              meta::displayName("Ranged")),
                    meta::method<&Self::Fire>.params("shots"),
                    meta::method<&Self::IsEmpty>);
            }
        };

        class CanaryChild : public Canary
        {
        private:
            int m_secret = 0;

        public:
            using Self = CanaryChild;
            using meta_identity = meta::identity_descriptor<CanaryChild, Canary>;

            void Poke() { m_secret = 1; }

            static consteval auto reflect()
            {
                return meta::schema<Self>(
                    meta::field<&Self::m_secret>
                        .with(meta::range(0.0f, 1.0f)),
                    meta::method<&Self::Poke>);
            }
        };

        struct CanaryMethodsOnly
        {
            using Self = CanaryMethodsOnly;

            void Ping() {}

            static consteval auto reflect()
            {
                return meta::schema<Self>(meta::method<&Self::Ping>);
            }
        };

        struct ExternalCanary
        {
            int hp;
            float speed;
        };
    }

    // 외부 서술 — 클래스 침투 0. 특수화는 meta 스코프에 둔다.
    template<>
    struct of<detail::selftest::ExternalCanary>
    {
        static constexpr auto value = []
        {
            using Self = detail::selftest::ExternalCanary;
            return schema<Self>(field<&Self::hp>, field<&Self::speed>);
        }();
    };

    namespace detail::selftest
    {
        // __FUNCSIG__ 표기 카나리아 — 여기가 깨지면 파서를 새 표기에 맞춰
        // 갱신해야 한다 (계획 CT4 함정 4 · 툴체인 업그레이드 시 최우선 확인).
        static_assert(member_name_raw<&Canary::m_value>() == "m_value",
            "MSVC 멤버 포인터 NTTP __FUNCSIG__ 표기가 변했다 — member_name_raw 갱신 필요");
        static_assert(method_name_raw<&Canary::Fire>() == "Fire",
            "MSVC 멤버 함수 NTTP __FUNCSIG__ 표기가 변했다 — method_name_raw 갱신 필요");
        static_assert(method_name_raw<&Canary::IsEmpty>() == "IsEmpty");

        // 타입 이름은 **한정 이름**(네임스페이스 포함)이고, 라이브러리 복제본과
        // 엔진 TypeTrait::type_name의 출력 동일성은 여기서 교차 검증한다 —
        // 이것이 깨지면 typeID(FNV-1a)·YAML 헤더가 갈라진다.
        static_assert(decltype(schema_of<Canary>)::identifier
            == "meta::detail::selftest::Canary");
        static_assert(meta::type_name_of<Canary>() == TypeTrait::type_name<Canary>());
        static_assert(meta::type_name_of<CanaryChild>() == TypeTrait::type_name<CanaryChild>());

        // 로컬 스키마 / 상속 합성 질의의 분리 계약.
        static_assert(decltype(schema_of<Canary>)::field_count == 2);
        static_assert(decltype(schema_of<Canary>)::method_count == 2);
        static_assert(std::tuple_size_v<decltype(localFields<CanaryChild>())> == 1);
        static_assert(std::tuple_size_v<decltype(fields<CanaryChild>())> == 3);
        static_assert(std::get<0>(fields<CanaryChild>()).identifier == "m_value");
        static_assert(std::get<2>(fields<CanaryChild>()).identifier == "m_secret");
        static_assert(std::tuple_size_v<decltype(methods<CanaryChild>())> == 3);

        // 부모 자동 추론 — 서술식 어디에도 base<>가 없다.
        static_assert(!decltype(schema_of<Canary>)::has_base);
        static_assert(std::is_same_v<direct_base_t<CanaryChild>, Canary>);
        static_assert(std::is_same_v<decltype(bases<CanaryChild>()), type_list<Canary>>);

        // 속성·파라미터·선언 주체.
        static_assert(std::get<1>(schema_of<Canary>.fields)
            .has_attribute<range_attr<float>>());
        static_assert(std::get<1>(schema_of<Canary>.fields)
            .attribute<range_attr<float>>().max == 1.0f);
        static_assert(std::get<1>(schema_of<Canary>.fields)
            .attribute<display_name_attr>().value == "Ranged");
        static_assert(!std::get<0>(schema_of<Canary>.fields)
            .has_attribute<display_name_attr>());
        static_assert(std::get<0>(schema_of<Canary>.methods).paramNames[0]
            == std::string_view{ "shots" });
        static_assert(declaringType(std::get<2>(fields<CanaryChild>()))
            == "meta::detail::selftest::CanaryChild");
        static_assert(declaringType(std::get<0>(fields<CanaryChild>()))
            == "meta::detail::selftest::Canary");

        // 외부 서술 경로 + 메서드 전용.
        static_assert(reflectable<ExternalCanary>);
        static_assert(decltype(schema_of<ExternalCanary>)::field_count == 2);
        static_assert(!decltype(schema_of<ExternalCanary>)::has_base);
        static_assert(decltype(schema_of<CanaryMethodsOnly>)::field_count == 0);
        static_assert(decltype(schema_of<CanaryMethodsOnly>)::method_count == 1);

        // 인스턴스 크기 불변(정적/타입 별칭은 인스턴스에 안 실린다) + 상수
        // 초기화 증명(constexpr 변수는 정의상 constant-initialized).
        static_assert(sizeof(Canary) == 8);

        // ── 열거형 카나리아 (CT9-b: magic_enum 대체 자급 표) ────────────────
        // 코퍼스 대표 유형: 스코프드 순차 / 비스코프드 / uint8_t 기저 /
        // 값 간극·음수. __FUNCSIG__의 enum NTTP 표기가 변하면 여기가 먼저 깨진다.
        enum class ScopedCanary { Off, On, Auto };
        enum UnscopedCanary { UC_Zero, UC_One, UC_Two };
        enum class ByteCanary : uint8_t { A, B };
        enum class GapCanary { Neg = -2, Zero = 0, Four = 4 };

        static_assert(enum_count<ScopedCanary>() == 3);
        static_assert(enum_entries<ScopedCanary>[0].name == "Off");
        static_assert(enum_entries<ScopedCanary>[2].name == "Auto");
        static_assert(enum_entries<ScopedCanary>[2].value == ScopedCanary::Auto);
        static_assert(enum_count<UnscopedCanary>() == 3);
        static_assert(enum_entries<UnscopedCanary>[1].name == "UC_One");
        static_assert(enum_count<ByteCanary>() == 2);
        static_assert(enum_entries<ByteCanary>[1].name == "B");
        static_assert(enum_count<GapCanary>() == 3);
        static_assert(enum_entries<GapCanary>[0].name == "Neg");   // 값 오름차순
        static_assert(enum_entries<GapCanary>[2].name == "Four");
        static_assert(enum_name(ScopedCanary::On) == "On");
        static_assert(enum_name(static_cast<ScopedCanary>(99)) == "");
        // NUL 종단(콘솔 문자열 비교·ImGui 아이템 전제).
        static_assert(enum_entries<ScopedCanary>[0].name.data()[3] == '\0');
    }
}
