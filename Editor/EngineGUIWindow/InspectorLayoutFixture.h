#pragma once
// PHASE 21 W2-I3 — 리플렉션 경로의 **모양** 자극물.
//
// 왜 합성인가. W2-I3 의 남은 몫은 경로가 아니라 중첩 구조체·배열·맵이라는 모양이다.
// 그런데 일반 경로(`ReflectionTypedDraw.h`)로 그려지는 컴포넌트 중 그 모양을 가진 것이
// 사실상 없다 — 컨테이너 필드를 가진 컴포넌트 셋(Animator · Sound · Script)은 전용
// 드로어이거나 숨긴 필드다. 실제 데이터에 기대면 분기 대부분이 한 번도 그려지지 않고
// 검사는 초록이다. 그래서 분기마다 하나씩 닿는 타입을 여기 둔다.
//
// `editor.inspector fixture on` 이 켜면 인스펙터가 엔티티 본문 끝에 이 값을
// `DrawTypedObject` 로 그리고, 본문 이름 `ReflectionFixture` 로 줄 수·넘침을 낸다.
// 값은 창 하나가 소유하는 정적 사본이라 씬·직렬화에 닿지 않는다.
//
// 긴 문자열·긴 맵 키는 일부러 넣었다 — 좁은 폭에서 잘리는지가 판정 대상이다.

#include "Reflection.hpp"
#include <mathematics/vector2.hpp>
#include <mathematics/vector3.hpp>
#include <mathematics/vector4.hpp>
#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace editor::inspector
{
    enum class InspectorFixtureMode
    {
        First,
        SecondModeWithAVeryLongEnumeratorName,
        Third,
    };

    struct InspectorFixtureLeaf
    {
        static consteval auto reflect()
        {
            using Self = InspectorFixtureLeaf;
            return meta::schema<Self>(
                meta::field<&Self::weight>,
                meta::field<&Self::note>,
                meta::field<&Self::offset>);
        }

        float weight{ 0.5f };
        std::string note{ "leaf note that is long enough to be clipped at narrow widths" };
        math::vector3 offset{ 1.f, 2.f, 3.f };
    };

    struct InspectorFixtureBranch
    {
        static consteval auto reflect()
        {
            using Self = InspectorFixtureBranch;
            return meta::schema<Self>(
                meta::field<&Self::depth>,
                meta::field<&Self::title>,
                meta::field<&Self::leaf>,
                meta::field<&Self::samples>);
        }

        int depth{ 2 };
        std::string title{ "branch title" };
        InspectorFixtureLeaf leaf{};
        std::vector<float> samples{ 0.25f, 0.5f, 0.75f };
    };

    struct InspectorLayoutFixture
    {
        static consteval auto reflect()
        {
            using Self = InspectorLayoutFixture;
            return meta::schema<Self>(
                meta::field<&Self::count>,
                meta::field<&Self::ratio>,
                meta::field<&Self::toggle>,
                meta::field<&Self::caption>,
                meta::field<&Self::mode>,
                meta::field<&Self::extent>,
                meta::field<&Self::position>,
                meta::field<&Self::plane>,
                meta::field<&Self::branch>,
                meta::field<&Self::weights>,
                meta::field<&Self::names>,
                meta::field<&Self::points>,
                meta::field<&Self::smallIds>,
                meta::field<&Self::fixed>,
                meta::field<&Self::tags>,
                meta::field<&Self::scores>,
                meta::field<&Self::leaves>,
                meta::field<&Self::branches>);
        }

        int count{ 7 };
        float ratio{ 0.33f };
        bool toggle{ true };
        std::string caption{ "a caption string that keeps going well past the width of a narrow inspector" };
        InspectorFixtureMode mode{ InspectorFixtureMode::SecondModeWithAVeryLongEnumeratorName };
        math::vector2 extent{ 640.f, 480.f };
        math::vector3 position{ -12.5f, 3.25f, 1024.f };
        math::vector4 plane{ 0.f, 1.f, 0.f, -2.5f };
        InspectorFixtureBranch branch{};
        std::vector<float> weights{ 0.1f, 0.2f, 0.3f };                       // 스칼라 시퀀스
        std::vector<std::string> names{ "alpha", "a much longer element name that should clip" };
        std::vector<math::vector3> points{ { 1.f, 2.f, 3.f }, { -100.f, 2000.f, 0.5f } }; // 드로어 원소
        std::vector<std::uint16_t> smallIds{ 3, 65535 };                      // 폭만 다른 정수
        std::array<float, 3> fixed{ 1.f, 2.f, 3.f };                           // 크기 고정
        std::set<int> tags{ 4, 8, 15 };                                        // 읽기 전용 원소
        std::map<std::string, float> scores{
            { "short", 1.f },
            { "a very long map key that must be clipped by the label column", 2.f } };
        std::map<std::string, InspectorFixtureLeaf> leaves{ { "first", {} }, { "second", {} } };
        std::vector<InspectorFixtureBranch> branches{ {}, {} };                // reflect 원소
    };
}
