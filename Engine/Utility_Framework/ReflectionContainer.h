#pragma once
// 컨테이너 형상 판정 (range 일반화) — std 전용 계층.
//
// ── 무엇을 고치는가 ────────────────────────────────────────────────────
//
// 직렬화기는 컨테이너를 `is_vector_v` 하나로 알아봤다. 그래서 컨테이너 가운데
// std::vector 만 필드로 실을 수 있었고, 나머지는 EmitMember 의 마지막
// else(static_assert)에 걸려 **빌드가 막혔다**. 막힌 것 자체는 옳다 — 레거시가
// "[not support type]" 을 적고 넘어가 값을 조용히 잃던 자리를 C1 이 컴파일
// 오류로 바꿔 둔 결과다. 틀린 것은 "컨테이너를 하나만 안다" 는 쪽이다.
//
// 여기서는 형상을 **타입 이름이 아니라 능력**으로 판정한다: 순회할 수 있는가,
// 키를 갖는가, 비울 수 있는가, 뒤에 붙일 수 있는가, 넣을 수 있는가. std::vector
// 는 그 판정의 한 사례로 내려앉고 deque·list·set·map·array 가 같은 경로로
// 들어온다. 새 컨테이너를 지원하려고 여기에 이름을 더할 일은 없어야 한다 —
// 이름을 더하고 있다면 그 컨테이너가 능력으로 설명되지 않는다는 뜻이다.
//
// ── 문자열은 컨테이너가 아니다 ─────────────────────────────────────────
//
// std::string 도 std::filesystem::path 도 range 다. 판정에서 먼저 빼지 않으면
// 이름 필드 하나가 글자 시퀀스로 저장된다 — 골든이 통째로 뒤집히는 사고이고,
// range 일반화가 가장 흔하게 무너지는 자리다. 소비 측(ReflectionTypedYml)은
// 여기에 더해 **스칼라 계약이 range 판정보다 앞선다**는 순서를 따로 세운다.
// 두 겹을 두는 이유는 하나가 뚫려도 다른 하나가 남게 하기 위해서다.
//
// ── 왜 std 만 아는가 ───────────────────────────────────────────────────
//
// MetaSchema.h 와 같은 이유다. 이 판정은 저작 backend(ryml)·로깅·Meta 레지스트리
// 와 무관해야 하고, 무관해야 독립적으로 컴파일해 재어 볼 수 있다. <ranges> 를
// 여기 가두는 것도 같은 값이다 — TypeTrait.h 는 이 헤더를 물지 않는다(그쪽은
// Core.Minimal 을 타고 수백 TU 로 퍼진다).
#include <concepts>
#include <cstddef>
#include <filesystem>
#include <ranges>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace meta::container
{
    // ── 제외 목록 — 이것만은 "능력"으로 판정하면 안 되는 것들 ─────────────
    //
    // 글자를 담았을 뿐 컨테이너로 다룰 뜻이 없는 타입. std::string_view 와
    // std::string 은 변환으로, path 는 이름으로 잡는다(path 는 string_view 로
    // 변환되지 않지만 range 다 — 변환 하나로는 못 막는다는 실측이다).
    template<class T>
    concept StringLike =
        std::is_convertible_v<const std::remove_cvref_t<T>&, std::string_view>
        || std::is_same_v<std::remove_cvref_t<T>, std::filesystem::path>;

    /// 직렬화기가 컨테이너로 다룰 range.
    template<class T>
    concept Range =
        std::ranges::range<std::remove_cvref_t<T>> && !StringLike<T>;

    /// 키로 값을 찾는 range — map·unordered_map 계열.
    ///
    /// std::set 은 key_type 만 있고 mapped_type 이 없어 여기 걸리지 않는다.
    /// 의도한 결과다: 집합은 키가 곧 값이라 시퀀스로 적는 편이 자연스럽다.
    template<class T>
    concept KeyedRange = Range<T> && requires {
        typename std::remove_cvref_t<T>::key_type;
        typename std::remove_cvref_t<T>::mapped_type;
    };

    /// 키가 없는 range — vector·deque·list·set·array.
    template<class T>
    concept SequenceRange = Range<T> && !KeyedRange<T>;

    template<class R>
    using ValueT = std::remove_cvref_t<
        std::ranges::range_value_t<std::remove_cvref_t<R>>>;

    template<class R>
    using KeyT = std::remove_cvref_t<typename std::remove_cvref_t<R>::key_type>;

    template<class R>
    using MappedT =
        std::remove_cvref_t<typename std::remove_cvref_t<R>::mapped_type>;

    // ── 채우기 능력 — 역직렬화가 묻는 것 ──────────────────────────────────

    template<class T>
    concept Clearable = requires(std::remove_cvref_t<T>& c) { c.clear(); };

    template<class T>
    concept Reservable =
        requires(std::remove_cvref_t<T>& c) { c.reserve(std::size_t{ 0 }); };

    template<class T>
    concept BackInsertable = Range<T>
        && requires(std::remove_cvref_t<T>& c, ValueT<T>&& v)
        { c.push_back(std::move(v)); };

    /// 인자 하나짜리 insert — set·unordered_set·multiset.
    ///
    /// std::vector 는 여기 걸리지 않는다: vector::insert 는 위치를 먼저 받아서
    /// 인자 하나로는 부르지 못한다. 판정이 능력으로 갈리는 예다.
    template<class T>
    concept SetInsertable = Range<T>
        && requires(std::remove_cvref_t<T>& c, ValueT<T>&& v)
        { c.insert(std::move(v)); };

    /// 크기가 타입에 박힌 range — std::array.
    ///
    /// 비울 수 없고 붙일 수도 없으니 자리마다 덮어쓰는 수밖에 없다.
    template<class T>
    concept FixedSizeRange = Range<T> && !Clearable<T>
        && requires(std::remove_cvref_t<T>& c, std::size_t index) { c[index]; };

    /// 역직렬화가 채울 수 있는 시퀀스인가. 셋 중 어느 수단도 없으면 읽기 경로를
    /// 세울 수 없다 — 소비 측이 이 판정으로 컴파일 오류를 낸다.
    template<class T>
    concept FillableSequence = SequenceRange<T>
        && (BackInsertable<T> || SetInsertable<T> || FixedSizeRange<T>);

    /// 키로 덮어쓸 수 있는 맵인가.
    template<class T>
    concept FillableKeyed = KeyedRange<T>
        && requires(std::remove_cvref_t<T>& c, KeyT<T>&& k, MappedT<T>&& v)
        { c.insert_or_assign(std::move(k), std::move(v)); };

    // ── 원소를 어떻게 받을 것인가 ─────────────────────────────────────────
    //
    // std::vector<bool> 의 원소 참조는 프록시 **prvalue** 라 참조로 묶지 못한다.
    // 묶을 수 있으면 참조로 받고(문자열 원소를 복사하지 않는다), 못 묶으면 값으로
    // 받는다. 이 구분이 없으면 vector<bool> 하나가 직렬화기 전체를 컴파일 불가로
    // 만든다.
    //
    // ★ const 여부를 **지우지 않는다.** std::set 은 원소를 const 로만 내주고,
    //   그 사실이 소비 측 판단을 가른다(직렬화 훅은 비-const 참조를 요구한다).
    //   여기서 const 를 벗기면 그 갈림이 보이지 않게 되고, 벗기지 못하는 자리는
    //   결국 템플릿 안쪽의 읽기 힘든 오류로 터진다.
    template<class R>
    using ElementRefT = std::conditional_t<
        std::is_reference_v<
            std::ranges::range_reference_t<std::remove_cvref_t<R>>>,
        std::ranges::range_reference_t<std::remove_cvref_t<R>>,
        ValueT<R>>;

    /// 원소를 const 로만 내주는 range 인가(std::set 계열).
    template<class R>
    concept ConstElementRange = Range<R>
        && std::is_const_v<std::remove_reference_t<ElementRefT<R>>>;

    // ── 카나리아 — 판정이 흔들리면 여기서 먼저 깨진다 ─────────────────────
    //
    // 아래 단정들은 range 일반화가 실제로 무너지는 방식을 그대로 적은 것이다.
    // 컴파일타임 디스패치는 런타임 게이트가 못 보므로(분기가 아예 생성되지
    // 않는다) 계약을 이 자리에서 붙든다.
    namespace canary
    {
        static_assert(!Range<std::string>,
            "std::string 이 컨테이너로 판정됐다 - 이름 필드가 글자 시퀀스로 저장된다");
        static_assert(!Range<std::string_view>,
            "std::string_view 가 컨테이너로 판정됐다");
        static_assert(!Range<std::filesystem::path>,
            "path 가 컨테이너로 판정됐다 - path 는 string_view 로 변환되지 않으므로 "
            "변환 판정만으로는 못 막는다");
        static_assert(!Range<int>, "스칼라가 컨테이너로 판정됐다");
    }
}
