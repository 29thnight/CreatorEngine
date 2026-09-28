// reflgen 다리 규칙 시험 (reflgen 도입)
//
// 다리(ReflgenBridge.h)가 reflgen 서술을 엔진 스키마(meta::schema_of<T>)로 옮기는 규칙을 카나리아 타입 하나로
// 컴파일 때 단정한다 — 같은 서술을 손으로 쓴 엔진 레시피(legacy)와 **같은 타입이고 같은 값**이어야 한다. 엔진
// 소비자(직렬화·인스펙터·Meta::Type 어댑터)는 스키마 타입에 대한 template 이라 타입과 값이 같으면 동작도 같다.
// 필드 순서·이름·속성 타입은 타입에, 속성 값과 파라미터 이름은 값에 있다.
//
// 엔진 타입 전부를 옮길 때는 옮기기 전 레시피 전부를 같은 식으로 대조했다(전환 커밋의 ReflgenParity.*). 그 표는
// 필드가 바뀔 때마다 깨지므로 전환 뒤에 치웠다 — 여기에는 다리의 규칙(속성·메서드를 옮기는 법)만 남긴다.
#include "ReflgenBridgeCanary.h"
#include <cstddef>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace
{
    namespace legacy
    {
        consteval auto canary()
        {
            using Self = reflgen_bridge_canary::Canary;
            return meta::schema<Self>(
                meta::field<&Self::m_count>,
                meta::field<&Self::m_ranged>.with(meta::range(0.0f, 1.0f), meta::displayName("Ranged")),
                meta::field<&Self::m_distance>.with(meta::units("m"), meta::wide()),
                meta::field<&Self::m_hidden>.with(meta::hidden()),
                meta::field<&Self::m_instanceID>.with(meta::readonly(), meta::debugOnly()),
                meta::method<&Self::Fire>.params("shots").hideInInspector(),
                meta::method<&Self::IsEmpty>.readOnlyInInspector());
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

    // 같은 타입이어야 값을 대조할 수 있다 — 타입이 다르면 is_same 단정이 먼저 붉어진다.
    template<class T, class Legacy>
    constexpr bool same_values(const Legacy& legacy)
    {
        const auto& bridged = meta::schema_of<T>;
        if constexpr (!std::is_same_v<std::remove_cvref_t<decltype(bridged)>, Legacy>)
        {
            return false;
        }
        else
        {
            const bool fields = all_pairs(bridged.fields, legacy.fields, [](const auto& left, const auto& right)
            {
                return all_pairs(left.attributes, right.attributes,
                    [](const auto& a, const auto& b) { return same_attribute(a, b); });
            });
            const bool methods = all_pairs(bridged.methods, legacy.methods, [](const auto& left, const auto& right)
            {
                for (std::size_t i = 0; i < left.paramNames.size(); ++i)
                {
                    if (std::string_view(left.paramNames[i]) != std::string_view(right.paramNames[i]))
                    {
                        return false;
                    }
                }
                return left.inspectorReadOnly == right.inspectorReadOnly
                    && left.inspectorHidden == right.inspectorHidden;
            });
            return fields && methods;
        }
    }

    template<class T>
    using bridged_t = std::remove_cvref_t<decltype(meta::schema_of<T>)>;

    // 레시피가 없다 — 서술은 다리(meta::of<T>)에서만 온다.
    static_assert(!meta::detail::has_local_recipe<reflgen_bridge_canary::Canary>
        && meta::detail::has_external_recipe<reflgen_bridge_canary::Canary>);
    static_assert(Meta::HasReflection<reflgen_bridge_canary::Canary>);

    // 속성·메서드를 옮기는 규칙 전부.
    static_assert(std::is_same_v<bridged_t<reflgen_bridge_canary::Canary>, decltype(legacy::canary())>);
    static_assert(same_values<reflgen_bridge_canary::Canary>(legacy::canary()));
    // 표시 이름은 C 문자열로 쓰인다(Property::displayName) — NUL 종단이어야 한다.
    static_assert(std::get<1>(meta::schema_of<reflgen_bridge_canary::Canary>.fields)
        .attribute<meta::display_name_attr>().value.data()[6] == '\0');
}
