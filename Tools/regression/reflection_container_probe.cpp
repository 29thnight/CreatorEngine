// 리플렉션 컨테이너 형상 판정 계약 (range 일반화 게이트 · 컴파일타임 축)
//
// ── 이 검사가 메우는 구멍 ──────────────────────────────────────────────
//
// 직렬화기의 컨테이너 디스패치는 전부 `if constexpr` 다. 고르지 않은 갈래는
// **코드가 아예 생성되지 않으므로**, 판정이 틀려도 런타임 게이트는 볼 것이
// 없다. 잘못 고른 갈래만 돌 뿐이고 그쪽은 정상으로 보인다.
//
// 그래서 판정 자체를 단정한다. 특히 range 일반화가 실제로 무너지는 방식:
//
//   ① std::string 이 시퀀스로 판정된다 → 이름 필드가 글자 시퀀스로 저장된다.
//      path 는 string_view 로 변환되지 않으므로 변환 판정 하나로는 못 막는다.
//   ② 채우기 수단을 이름으로 고른다 → set 에 push_back 을 부르거나 vector 를
//      단일 인자 insert 로 채우려 든다(둘 다 없는 함수다).
//   ③ 원소 참조를 항상 참조로 묶는다 → std::vector<bool> 프록시에서 깨진다.
//   ④ 원소의 const 여부를 지운다 → std::set 원소에 비-const 훅을 걸어 놓고도
//      컴파일이 통과하는 것처럼 보이다가 소비 측에서 읽기 어려운 오류로 터진다.
//
// ── 두 계보 교차 대조 ──────────────────────────────────────────────────
//
// 콘솔 세터 가드(TypeTrait.h 의 IsCopyableForProperty)는 <ranges> 를 들이지
// 않으려고 value_type 한 겹만 본다. 직렬화기는 range 로 본다. 두 판정이 갈리면
// vector<unique_ptr<T>> 가 "복사 가능"으로 오판돼 any_cast 인스턴스화에서
// 깨진다 — K2 스테이지 A 에서 실제로 밟은 함정이다. 여기서 맞대 둔다.
//
// ── 어느 바이너리를 재는가 ─────────────────────────────────────────────
//
// 아무것도 재지 않는다. 제품 헤더를 그 자리에서 Debug/Release 로 각각 컴파일
// 하므로 Bin\ 산출물과 무관하다(verify-hashing-string 과 같은 관례).
//
// 값이 실제로 왕복하는지는 이 게이트의 축이 아니다 — 그쪽은
// `verify-reflection-container-roundtrip.ps1` 이 제품 바이너리로 든다.

#include "ReflectionContainer.h"
#include "TypeTrait.h"

#include <array>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace meta::container;

namespace
{
    struct Plain { int value{ 0 }; };
    struct NoCopy { NoCopy(const NoCopy&) = delete; NoCopy() = default; };

    // ── ① 문자열은 컨테이너가 아니다 ──────────────────────────────────
    static_assert(!Range<std::string>);
    static_assert(!Range<std::string_view>);
    static_assert(!Range<std::filesystem::path>);
    static_assert(!Range<const std::string&>);
    static_assert(!SequenceRange<std::string>);
    static_assert(!KeyedRange<std::string>);

    // ── 시퀀스 / 맵 분류 ──────────────────────────────────────────────
    static_assert(SequenceRange<std::vector<int>>);
    static_assert(SequenceRange<std::deque<int>>);
    static_assert(SequenceRange<std::list<int>>);
    static_assert(SequenceRange<std::array<int, 4>>);
    static_assert(SequenceRange<std::vector<bool>>);
    static_assert(SequenceRange<std::vector<Plain>>);

    // set 은 key_type 만 있고 mapped_type 이 없다 — 키가 곧 값이라 시퀀스다.
    static_assert(SequenceRange<std::set<int>>);
    static_assert(SequenceRange<std::unordered_set<std::string>>);
    static_assert(!KeyedRange<std::set<int>>);

    static_assert(KeyedRange<std::map<std::string, int>>);
    static_assert(KeyedRange<std::unordered_map<int, float>>);
    static_assert(!SequenceRange<std::map<std::string, int>>);

    // 두 갈래는 배타여야 한다. 겹치면 if/else 의 **줄 순서**가 결과를 정하고,
    // 누군가 분기를 옮기는 순간 조용히 뒤집힌다.
    static_assert(!(SequenceRange<std::map<std::string, int>>
        && KeyedRange<std::map<std::string, int>>));
    static_assert(!(SequenceRange<std::set<int>> && KeyedRange<std::set<int>>));
    static_assert(!(SequenceRange<std::vector<int>> && KeyedRange<std::vector<int>>));

    // ── ② 채우기 수단은 이름이 아니라 능력으로 ────────────────────────
    static_assert(BackInsertable<std::vector<int>>);
    static_assert(BackInsertable<std::deque<int>>);
    static_assert(BackInsertable<std::list<int>>);
    static_assert(!BackInsertable<std::set<int>>);
    static_assert(!BackInsertable<std::array<int, 4>>);

    static_assert(SetInsertable<std::set<int>>);
    static_assert(SetInsertable<std::unordered_set<std::string>>);
    // vector::insert 는 위치를 먼저 받는다 — 인자 하나로는 못 부른다.
    static_assert(!SetInsertable<std::vector<int>>);

    static_assert(FixedSizeRange<std::array<int, 4>>);
    static_assert(!FixedSizeRange<std::vector<int>>);
    static_assert(!FixedSizeRange<std::list<int>>);

    static_assert(Reservable<std::vector<int>>);
    static_assert(!Reservable<std::deque<int>>);
    static_assert(!Reservable<std::list<int>>);

    static_assert(Clearable<std::vector<int>>);
    static_assert(!Clearable<std::array<int, 4>>);

    // 세 수단 중 하나는 있어야 역직렬화 경로가 선다.
    static_assert(FillableSequence<std::vector<int>>);
    static_assert(FillableSequence<std::set<int>>);
    static_assert(FillableSequence<std::array<int, 4>>);
    static_assert(FillableKeyed<std::map<std::string, int>>);
    static_assert(FillableKeyed<std::unordered_map<int, float>>);

    // ── ③ 프록시 원소 / ④ const 원소 ─────────────────────────────────
    static_assert(std::is_same_v<ElementRefT<std::vector<int>>, int&>);
    static_assert(std::is_same_v<ElementRefT<std::vector<std::string>>, std::string&>);
    // vector<bool> 의 원소 참조는 프록시 prvalue — 참조로 묶으면 깨진다.
    static_assert(std::is_same_v<ElementRefT<std::vector<bool>>, bool>);
    static_assert(std::is_same_v<ElementRefT<std::set<int>>, const int&>);

    static_assert(ConstElementRange<std::set<int>>);
    static_assert(ConstElementRange<std::unordered_set<std::string>>);
    static_assert(!ConstElementRange<std::vector<int>>);
    static_assert(!ConstElementRange<std::vector<bool>>);

    // ── 원소·키·값 타입 ───────────────────────────────────────────────
    static_assert(std::is_same_v<ValueT<std::vector<std::string>>, std::string>);
    static_assert(std::is_same_v<ValueT<std::set<int>>, int>);
    static_assert(std::is_same_v<KeyT<std::map<std::string, int>>, std::string>);
    static_assert(std::is_same_v<MappedT<std::map<std::string, int>>, int>);

    // ── 두 계보 교차 대조 ─────────────────────────────────────────────
    //
    // is_copy_constructible_v 는 vector<unique_ptr<T>> 에 **true** 를 돌려준다
    // (복사 생성자 선언이 원소와 무관하게 늘 있다). 원소까지 내려가야 참이 나온다.
    static_assert(std::is_copy_constructible_v<std::vector<std::unique_ptr<int>>>,
        "전제가 바뀌었다 - 이 트레이트가 더 이상 거짓말하지 않는다면 가드의 "
        "존재 이유를 다시 적어라");
    static_assert(!IsCopyableForProperty<std::vector<std::unique_ptr<int>>>());
    static_assert(!IsCopyableForProperty<std::vector<NoCopy>>());
    static_assert(!IsCopyableForProperty<std::deque<std::unique_ptr<int>>>());
    static_assert(!IsCopyableForProperty<std::list<NoCopy>>());
    static_assert(IsCopyableForProperty<std::vector<int>>());
    static_assert(IsCopyableForProperty<std::vector<std::string>>());
    static_assert(IsCopyableForProperty<std::map<std::string, int>>());
    static_assert(IsCopyableForProperty<std::set<int>>());
    static_assert(IsCopyableForProperty<std::array<int, 4>>());

    // 문자열은 value_type 이 char 라 한 겹 내려가지만 답은 같아야 한다 —
    // 두 계보가 "무관하다"고 적어 둔 근거가 이것이다.
    static_assert(IsCopyableForProperty<std::string>());
    static_assert(IsCopyableForProperty<std::filesystem::path>());
    static_assert(IsCopyableForProperty<int>());
    static_assert(!IsCopyableForProperty<std::unique_ptr<int>>());
}

int main()
{
    // 단정은 전부 컴파일타임이다. 여기 도달했다는 것은 그 전부가 성립했다는 뜻이고,
    // 게이트는 이 줄 하나를 확인한다.
    std::puts("[REFLECTION CONTAINER] classification contract ok");
    return 0;
}
